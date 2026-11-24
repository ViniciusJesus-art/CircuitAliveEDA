/*
 * CircuitAliveEDA stage-1 worker: ngspice globals never enter the editor process.
 * Copyright (C) 2026 CircuitAliveEDA contributors.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "interactive_sim_protocol.h"
#include "runtime_memory.h"
#include "programmable_pipeline.h"
#include "native_peripherals.h"
#include "audio_sampler.h"
#include "programmable_pin_backend.h"
#include "compiled_expression.h"
#include "expression_backend.h"
#include <filesystem>
#include "platform_runtime.h"
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define read _read
#define dup _dup
#define dup2 _dup2
#define fdopen _fdopen
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#else
#include <dlfcn.h>
#include <unistd.h>
#endif
#include <fcntl.h>
#include <ngspice/sharedspice.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <map>
#include <mutex>
#include <set>
#include <regex>
#include <signal.h>
#include <sstream>

namespace CIRCUITALIVE
{
using namespace PROTOCOL;

static std::string Lower( std::string aText )
{
    std::transform( aText.begin(), aText.end(), aText.begin(),
                    []( unsigned char c ) { return std::tolower( c ); } );
    return aText;
}

static std::string Number( double aValue )
{
    std::ostringstream stream;
    stream.imbue( std::locale::classic() );
    stream << std::setprecision( 17 ) << aValue;
    return stream.str();
}

static double StopThreshold( double time, double bias = 0 )
{
    // ngspice parses stop expressions separately from circuit values. At long
    // times a fixed 10 fs guard becomes smaller than a few representable doubles
    // (notably at 32 s), so the condition can miss the accepted boundary.
    const double roundoff = 16 * std::numeric_limits<double>::epsilon() * time;
    return time - std::max( bias, roundoff );
}

static bool Token( const std::string& aText )
{
    return !aText.empty() && aText.size() <= 256
           && aText.find_first_not_of( "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                      "0123456789_/.$#+@()[]{}:-" ) == std::string::npos;
}

class ENGINE
{
public:
    ENGINE()
    {
        ngSpice_Init( &OnMessage, &OnStatus, &OnExit, &OnData, &OnInit, &OnBackground, this );
        ngSpice_Init_Sync( &OnVoltage, nullptr, &OnSync, &m_engineId, this );
        // The solver's preflight assumes every time point is retained. Our
        // .save none streaming policy retains one solver point plus a bounded
        // ring, whose limits are checked in Configure and Snapshot instead.
        Command( "set no_mem_check" );
    }

    JSON Handle( const JSON& aRequest )
    {
        const std::string op = aRequest.at( "op" );

        if( op == "hello" )
            return { { "version", VERSION } };

        Refresh();

        if( op == "configure" )
            Configure( aRequest );
        else if( op == "play" )
            Play();
        else if( op == "pause" )
            Pause();
        else if( op == "stop" )
        {
            Stop();
            return { { "generation", m_generation } };
        }
        else if( op == "advance" )
            Advance( aRequest.at( "time" ) );
        else if( op == "source" )
            Source( aRequest.at( "reference" ), aRequest.at( "value" ), aRequest.value( "changes", JSON::array() ) );
        else if( op == "memory" )
            return Memory( aRequest );
        else if( op == "snapshot" )
            return Snapshot( aRequest.value( "drain", false ),aRequest.contains("history_signals")
                ?std::optional<std::vector<std::size_t>>(aRequest.at("history_signals").get<std::vector<std::size_t>>())
                :std::nullopt );
        else if(op=="history")
        {
            std::lock_guard lock(m_mutex);
            if(!m_retainedHistoryValid||aRequest.at("token").get<std::uint64_t>()!=m_historyToken)
                throw std::runtime_error("The requested history snapshot has expired");
            JSON result={{"history",HistoryRows(m_retainedHead,m_retainedSize)}};
            if(m_config.historySignals)result["history_signals"]=*m_config.historySignals;
            return result;
        }
        else
            throw std::runtime_error( "Unknown simulation operation" );

        return JSON::object();
    }

    void Stop()
    {
        m_programPlaying = false;
        if( !Finished() )
            Halt();
        m_programmable.Cancel();

        Command( "delete all" );
        Command( "destroy all" );
        Command( "remcirc" );
        ngSpice_Command( nullptr );
        m_commands = 0;
        m_state = SIM_SESSION_STATE::STOPPED;
        std::lock_guard lock( m_mutex );
        Clear();
        ++m_generation; // Invalidate all snapshots and pending visual state.
    }

private:
    static bool StatelessNetwork( const std::string& deck )
    {
        // Unknown models, subcircuits and dynamic expressions keep the full
        // integration policy. In particular, never relax RC/RL/transistor
        // circuits or diode models with junction/diffusion capacitance.
        const auto lowered = Lower( deck );
        std::set<std::string> sourceNodes{ "0" }, constantNodes{ "0" };
        std::istringstream sources( lowered ); std::string source;
        while( std::getline( sources, source ) )
        {
            std::istringstream words( source ); std::string name, p, n; words >> name >> p >> n;
            if( name.empty() || name[0] != 'v' ) continue;
            if( p != "0" && n != "0" ) continue;
            const auto node = p == "0" ? n : p;
            sourceNodes.insert( node );
            if( source.find( '(' ) == std::string::npos ) constantNodes.insert( node );
        }
        std::istringstream lines( lowered );
        std::string line;
        std::set<std::string> pinModels;
        std::vector<std::string> analogModels;
        bool title = true;
        const std::regex function( R"(([a-z_][a-z_0-9]*)\s*\()" );
        const std::regex temporal( R"(\btime\b)" );
        const std::regex parameter( R"(\b([a-z][a-z0-9_]*)\s*=)" );
        const std::set<std::string> staticDiodeParameters{ "is", "n", "rs", "tnom" };
        const std::set<std::string> functions{ "v", "min", "max", "abs", "tanh" };
        const std::regex voltageRead( R"(\bv\s*\(\s*([^()]+)\))" );
        while( std::getline( lines, line ) )
        {
            if( title ) { title = false; continue; }
            std::istringstream words( line ); std::string name; words >> name;
            if( name.empty() || name[0] == '*' ) continue;
            if( std::regex_search( line, temporal ) ) return false;
            if( name == ".model" )
            {
                std::string id, type; words >> id >> type;
                type = type.substr( 0, type.find( '(' ) );
                if( type == "ca_programmable_pin" || type == "ca_expression" ) pinModels.insert( id );
                else if( type != "d" && type != "sw" ) return false;
                if( type == "d" && ( line.find( "cjo" ) != std::string::npos
                    || line.find( "cj0" ) != std::string::npos || line.find( "tt" ) != std::string::npos
                    || line.find( "level" ) != std::string::npos ) ) return false;
                if( type == "d" )
                    for( std::sregex_iterator i( line.begin(), line.end(), parameter ), end; i != end; ++i )
                        if( !staticDiodeParameters.contains( ( *i )[1].str() ) ) return false;
            }
            else if( name[0] == '.' )
            {
                if( name != ".param" && name != ".options" && name != ".end"
                    && name != ".save" && name != ".tran" ) return false;
            }
            else if( name[0] == 'a' )
            {
                std::string token, model;
                while( words >> token ) model = token;
                analogModels.push_back( model );
            }
            else if( name[0] == 'b' )
            {
                std::string positive,negative;words>>positive>>negative;
                std::string compact=line;
                compact.erase(std::remove_if(compact.begin(),compact.end(),[](unsigned char c){return std::isspace(c);}),compact.end());
                // The model compiler's potentiometer is a positive
                // conductance controlled by an independent voltage source.
                // Its one own-terminal voltage read is linear, without state
                // or feedback in the coefficient. Other behavioral feedback
                // remains outside this proof.
                const auto conductance=compact.find("i={v("+positive+","+negative+")/max(1u,");
                bool ownConductance=false;
                if(conductance!=std::string::npos)
                {
                    const auto start=compact.find("max(",conductance)+4;unsigned depth=1;
                    for(auto i=start;i<compact.size();++i)
                    {
                        if(compact[i]=='(')++depth;
                        else if(compact[i]==')'&&!--depth){ownConductance=i+2==compact.size()&&compact.back()=='}';break;}
                    }
                }
                for( std::sregex_iterator i( line.begin(), line.end(), function ), end; i != end; ++i )
                    if( !functions.contains( ( *i )[1].str() ) ) return false;
                for( std::sregex_iterator i( line.begin(), line.end(), voltageRead ), end; i != end; ++i )
                {
                    auto ports = ( *i )[1].str(); std::replace( ports.begin(), ports.end(), ',', ' ' );
                    std::istringstream pair(ports);std::string a,b,extra;pair>>a>>b>>extra;
                    if(ownConductance&&a==positive&&b==negative&&extra.empty()){ownConductance=false;continue;}
                    std::istringstream inputs( ports ); std::string node;
                    while( inputs >> node ) if( !sourceNodes.contains( node ) ) return false;
                }
            }
            else if( name[0] == 's' )
            {
                std::string p, n, control, reference; words >> p >> n >> control >> reference;
                if( !constantNodes.contains( control ) || !constantNodes.contains( reference ) ) return false;
            }
            else if( name[0] == 'r' && std::regex_search( line, function ) ) return false;
            else if( name[0] != 'r' && name[0] != 'v' && name[0] != 'i'
                     && name[0] != 'd' && name[0] != 's' ) return false;
        }
        return std::all_of( analogModels.begin(), analogModels.end(),
                [&]( const auto& model ) { return pinModels.contains( model ); } );
    }

    void RefreshStatelessSources()
    {
        m_staticPulses.clear(); m_staticSourceNames.clear(); m_staticSine.reset(); m_staticSinePhases.clear(); m_staticSolutions.clear();
        if( !m_statelessNetwork ) return;
        std::istringstream lines( Lower( m_deck ) ); std::string line;
        const std::regex pulse( R"(\bpulse\s*\()" );
        const std::regex sine( R"(\bsin\s*\()" );
        while( std::getline( lines, line ) )
        {
            std::istringstream words( line ); std::string name; words >> name;
            if( name.empty() || ( name[0] != 'v' && name[0] != 'i' ) ) continue;
            if( line.find( "pwl" ) != std::string::npos || line.find( "external" ) != std::string::npos )
            { m_statelessNetwork = false; return; }
            if(std::regex_search(line,sine))
            {
                // A single periodic source in a memoryless native-peripheral
                // island can reuse the same electrical phase every cycle.
                // Firmware, ramps/damping and mixed waveforms keep ngspice's
                // transient solver and discontinuity scheduling.
                if(m_hasClockedRuntime||m_staticSine||!m_staticPulses.empty())
                {m_statelessNetwork=false;return;}
                auto query="@"+name+"[sin]";const auto* data=ngGet_Vec_Info(query.data());
                if(!data||!data->v_realdata||data->v_length<3||data->v_length>6)
                {m_statelessNetwork=false;return;}
                std::array<double,6> parameters{};std::copy_n(data->v_realdata,data->v_length,parameters.begin());
                if(!std::all_of(parameters.begin(),parameters.end(),[](double x){return std::isfinite(x);})
                    ||parameters[2]<=0||parameters[3]!=0||parameters[4]!=0)
                {m_statelessNetwork=false;return;}
                const double samples=4*std::ceil(std::max(24.,1/(parameters[2]*m_config.maxStep))/4);
                const double ticks=m_config.horizon*parameters[2]*samples;
                if(!std::isfinite(samples)||samples>512||!std::isfinite(ticks)||ticks>4503599627370496.)
                {m_statelessNetwork=false;return;}
                parameters[5]=std::remainder(parameters[5],360.);
                m_staticSine=parameters;m_staticSineSamples=samples;m_staticSourceNames.push_back(name);
                continue;
            }
            if( line.find( "sffm" ) != std::string::npos || line.find( "exp(" ) != std::string::npos
                || line.find( "trnoise" ) != std::string::npos || line.find( "trrandom" ) != std::string::npos )
            { m_statelessNetwork = false; return; }
            if( !std::regex_search( line, pulse ) )
            {
                if( line.find( '(' ) != std::string::npos ) { m_statelessNetwork = false; return; }
                continue;
            }
            std::string query = "@" + name + "[pulse]";
            if(m_staticSine){m_statelessNetwork=false;return;}
            const auto* data = ngGet_Vec_Info( query.data() );
            if( !data || !data->v_realdata || data->v_length < 7 )
            { m_statelessNetwork = false; return; }
            std::array<double, 7> parameters;
            std::copy_n( data->v_realdata, 7, parameters.begin() );
            if( !std::all_of( parameters.begin(), parameters.end(), [](double x){return std::isfinite(x);} )
                || parameters[3] < 0 || parameters[4] < 0 || parameters[6] <= 0
                || ( data->v_length > 7 && data->v_realdata[7] > 0 ) )
            { m_statelessNetwork = false; return; }
            m_staticPulses.push_back( parameters );
            m_staticSourceNames.push_back( name );
        }
    }

    double StatelessSourceDeadline( double time ) const
    {
        double next = std::numeric_limits<double>::infinity();
        const double roundoff = std::max( 1e-15, std::abs( time ) * 16 * std::numeric_limits<double>::epsilon() );
        if(m_staticSine)
        {
            const double ticks=time*(*m_staticSine)[2]*m_staticSineSamples;
            const double error=std::max(1e-12,std::abs(ticks)*32*std::numeric_limits<double>::epsilon());
            next=(std::floor(ticks+error)+1)/((*m_staticSine)[2]*m_staticSineSamples);
        }
        for( const auto& p : m_staticPulses )
        {
            const double cycle = std::max( 0., std::floor( ( time - p[2] ) / p[6] ) );
            for( int offset = 0; offset < 2; ++offset )
            {
                const double base = p[2] + ( cycle + offset ) * p[6];
                for( const double edge : { 0., p[3], p[3] + p[5], p[3] + p[5] + p[4] } )
                {
                    const double at = base + edge;
                    if( at > time + roundoff ) next = std::min( next, at );
                }
            }
        }
        return next;
    }

    void StatelessPoint( double time )
    {
        std::vector<double> sources;
        std::string key;
        sources.reserve( m_staticSourceNames.size() );
        key.reserve( m_staticSourceNames.size() * sizeof( double ) + (m_programDrivers.size()+3)/4 );
        if(m_staticSine)
        {
            const auto& p=*m_staticSine;const double ticks=time*p[2]*m_staticSineSamples;
            const double nearest=std::round(ticks),error=std::max(1e-12,std::abs(ticks)*32*std::numeric_limits<double>::epsilon());
            // Recover an exact grid phase from floating point timestamp
            // roundoff, rather than assigning a different key each period.
            double phase=std::abs(ticks-nearest)<=error?std::fmod(nearest,m_staticSineSamples)/m_staticSineSamples:std::fmod(time*p[2],1.);
            // Native deadlines can recur between grid points. Their computed
            // phase carries the timestamp's floating point roundoff too.
            const double phaseError=std::max(1e-12,std::abs(time*p[2])*32*std::numeric_limits<double>::epsilon());
            const auto next=m_staticSinePhases.lower_bound(phase);
            if(next!=m_staticSinePhases.end()&&*next-phase<=phaseError)phase=*next;
            else if(next!=m_staticSinePhases.begin()&&phase-*std::prev(next)<=phaseError)phase=*std::prev(next);
            else if(m_staticSinePhases.size()<2048)m_staticSinePhases.insert(phase);
            const double value=p[0]+p[1]*std::sin(6.2831853071795864769*std::remainder(phase+p[5]/360.,1.));
            sources.push_back(value);key.append(reinterpret_cast<const char*>(&value),sizeof(value));
        }
        for( const auto& p : m_staticPulses )
        {
            double value = p[0];
            if( time >= p[2] )
            {
                double phase = std::fmod( time - p[2], p[6] );
                const double error = std::max( 1e-15, std::abs( time ) * 32 * std::numeric_limits<double>::epsilon() );
                if( p[6] - phase <= error ) phase = 0;
                for( const double edge : { 0., p[3], p[3] + p[5], p[3] + p[5] + p[4] } )
                    if( std::abs( phase - edge ) <= error ) phase = edge;
                if( phase < p[3] ) value = p[0] + ( p[1] - p[0] ) * phase / p[3];
                else if( phase < p[3] + p[5] ) value = p[1];
                else if( phase < p[3] + p[5] + p[4] )
                    value = p[1] + ( p[0] - p[1] ) * ( phase - p[3] - p[5] ) / p[4];
            }
            sources.push_back( value );
            key.append( reinterpret_cast<const char*>( &value ), sizeof( value ) );
        }
        unsigned packed=0,shift=0;
        for( const auto& driver : m_programDrivers )
        {
            const auto next=std::upper_bound(driver.begin(),driver.end(),time,
                [](double t,const EXTERNAL_VALUE& entry){return t<entry.time;});
            const int mode=next==driver.begin()?0:int(std::prev(next)->value);
            packed|=static_cast<unsigned>(mode)<<shift;shift+=2;
            if(shift==8){key.push_back(static_cast<char>(packed));packed=shift=0;}
        }
        if(shift)key.push_back(static_cast<char>(packed));
        ++m_statelessAttempts;
        const auto found = m_staticSolutions.find( key );
        if( found != m_staticSolutions.end() )
        {
            m_latest.values = found->second;
            ++m_statelessExpansions;
        }
        else
        {
            for( std::size_t i = 0; i < sources.size(); ++i )
                Command( "alter " + m_staticSourceNames[i] + " dc = " + Number( sources[i] ) );
            m_indices.clear();
            m_dcPointTime = time;
            m_dcSolving = true;
            try { Command( "op" ); CheckError(); }
            catch( ... ) { m_dcSolving = false; throw; }
            m_dcSolving = false;
            if( m_staticSolutions.size() >= 2048 ) m_staticSolutions.clear();
            m_staticSolutions.emplace( std::move( key ), m_latest.values );
            Command( "destroy all" );
        }
        m_latest.time = time;
        const auto& events = m_nativePeripherals.Observe( time, m_latest.values );
        if( !events.empty() )
        {
            PruneProgrammableHistory( time );
            ProgrammableEvents( events, time, time );
            m_nativeSettleDeadline = time + 1e-9;
        }
        else if( time + 1e-12 >= m_nativeSettleDeadline )
            m_nativeSettleDeadline = std::numeric_limits<double>::infinity();
        while(!m_programTransitions.empty()&&*m_programTransitions.begin()<=time)m_programTransitions.erase(m_programTransitions.begin());
        if(m_hasClockedRuntime&&(!m_programReady||time>=m_programBoundary))ScheduleProgrammable();
        RecordAcceptedPoint();
    }

    void AdvanceStateless( double target )
    {
        if( !m_configured || !std::isfinite( target ) || target <= m_latest.time || target > m_config.horizon )
            throw std::runtime_error( "Target must advance within the configured horizon" );
        if( m_state != SIM_SESSION_STATE::STOPPED && m_state != SIM_SESSION_STATE::PAUSED )
            throw std::runtime_error( "Cannot advance a completed/failed session" );
        m_programTarget=target;
        if( m_state == SIM_SESSION_STATE::STOPPED )
        {
            Load();
            if( !m_statelessNetwork ) { AdvanceAnalog( target ); return; }
            StatelessPoint( 0 );
        }
        else if(m_hasClockedRuntime)ScheduleProgrammable();
        while( m_latest.time < target )
        {
            double edge=m_programTransitions.empty()?std::numeric_limits<double>::infinity():*m_programTransitions.begin();
            // Preserve both sides of an ideal GPIO edge for the scope,
            // activity monitors and integrated audio, without a recovery ramp.
            double left=std::numeric_limits<double>::infinity();
            if(std::isfinite(edge))
            {
                left=std::nextafter(edge,-std::numeric_limits<double>::infinity());
                if(left>m_latest.time)edge=left;
            }
            // With only DC sources, voltages are constant between GPIO and
            // peripheral events. A periodic analog grid adds identical cached
            // samples; all real transitions (including both edge sides) and
            // runtime input/deadline boundaries remain explicit below.
            const double grid=m_staticPulses.empty()?target:m_latest.time+m_config.maxStep;
            const double next = std::min( { target, grid, StatelessSourceDeadline( m_latest.time ),
                    m_nativePeripherals.Deadline(), m_nativeSettleDeadline,edge,
                    m_hasClockedRuntime&&m_programBoundary>m_latest.time?m_programBoundary:target } );
            if( next <= m_latest.time ) throw std::runtime_error( "Stateless peripheral deadline did not advance: "
                +Number(m_latest.time)+" next="+Number(next)+" firmware="+Number(m_programBoundary)+" edge="+Number(edge));
            if(m_staticPulses.empty()&&!m_staticSine&&next==left&&next<target
                &&next<m_nativePeripherals.Deadline()&&next<m_nativeSettleDeadline
                &&(!m_hasClockedRuntime||next<m_programBoundary))
            {
                // The left side is the already solved constant interval.
                // Preserve its timestamp for scopes/audio without repeating
                // the driver lookup or observing an unchanged peripheral bus.
                m_latest.time=next;RecordAcceptedPoint();
            }
            else StatelessPoint( next );
        }
        m_state = target >= m_config.horizon ? SIM_SESSION_STATE::COMPLETED : SIM_SESSION_STATE::PAUSED;
    }

    void Configure( const JSON& aRequest )
    {
        if( m_state != SIM_SESSION_STATE::STOPPED )
            throw std::runtime_error( "Stop the session before changing the circuit" );

        SIM_SESSION_CONFIG cfg;
        cfg.step = aRequest.at( "step" );
        cfg.maxStep = aRequest.at( "max_step" );
        cfg.horizon = aRequest.at( "horizon" );
        cfg.useInitialConditions = aRequest.at( "uic" );
        cfg.historyCapacity = aRequest.at( "capacity" );
        cfg.signals = aRequest.at( "signals" ).get<std::vector<std::string>>();
        cfg.accelerateExpressions = aRequest.value( "accelerate_expressions", false );
        cfg.synchronizeAdvances = aRequest.value( "synchronize_advances", false );
        cfg.mutableElements = aRequest.value( "mutable_elements", std::vector<std::string>{} );
        if( cfg.mutableElements.size() > 4096 )
            throw std::runtime_error( "Too many mutable elements" );
        for( const auto& element : cfg.mutableElements )
            if( !Token( element ) )
                throw std::runtime_error( "Invalid mutable element token" );
        for(const auto& c:aRequest.value("audio_channels",JSON::array()))
        {
            SIM_AUDIO_CHANNEL channel{c.at("signal"),c.at("full_scale"),c.at("volume")};
            if(cfg.audioChannels.size()>=32||channel.signal>=cfg.signals.size()||!std::isfinite(channel.fullScale)||channel.fullScale<=0
                ||!std::isfinite(channel.volume)||channel.volume<0||channel.volume>1)throw std::runtime_error("Invalid audio channel");
            cfg.audioChannels.push_back(channel);
        }
        const auto& devices = aRequest.value("programmable_devices",JSON::array());
        if( !devices.is_array() || devices.size()>32 )
            throw std::runtime_error("Too many programmable devices");
        for( const auto& device : devices )
            cfg.programmableDevices.push_back(DeserializeProgrammableDevice(device));

        if( !std::isfinite( cfg.step ) || !std::isfinite( cfg.maxStep )
                || !std::isfinite( cfg.horizon ) || cfg.step <= 0 || cfg.maxStep <= 0
                || cfg.horizon <= 0 || cfg.step > cfg.horizon || cfg.maxStep > cfg.horizon
                || cfg.horizon / std::min( cfg.step, cfg.maxStep ) > 1e8
                || cfg.historyCapacity == 0 || cfg.historyCapacity > 4096
                || cfg.signals.empty() || cfg.signals.size() > MAX_SESSION_SIGNALS
                || cfg.signals.size() * cfg.historyCapacity > MAX_SESSION_HISTORY_VALUES )
            throw std::runtime_error( "Invalid simulation timing, signals or history capacity" );

        if( aRequest.contains( "history_signals" ) )
        {
            const auto& selection = aRequest.at( "history_signals" );
            if( !selection.is_array() || selection.size() > cfg.signals.size() )
                throw std::runtime_error( "Invalid history signal selection" );
            std::set<std::size_t> selected;
            cfg.historySignals.emplace();
            for( const auto& item : selection )
            {
                if( !item.is_number_integer() || item < 0 || item >= cfg.signals.size() )
                    throw std::runtime_error( "Invalid history signal selection" );
                const auto index = item.get<std::size_t>();
                if( !selected.insert( index ).second )
                    throw std::runtime_error( "Invalid history signal selection" );
                cfg.historySignals->push_back( index );
            }
        }

        if( aRequest.contains( "activity_monitors" ) )
        {
            if( aRequest.at( "activity_monitors" ).size() > 4096 )
                throw std::runtime_error( "Too many signal activity monitors" );
            for( const auto& item : aRequest.at( "activity_monitors" ) )
            {
                SIM_ACTIVITY_MONITOR monitor{ item.at( "signal" ), item.at( "reference" ),
                    item.at( "supply" ), item.at( "low" ), item.at( "high" ),
                    item.at( "default_supply" ), item.at( "min_supply" ), item.at( "max_supply" ),
                    item.value( "threshold_reference", 0.0 ) };
                auto valid = [&]( std::size_t index )
                    { return index == ACTIVITY_NO_SIGNAL || index < cfg.signals.size(); };
                if( !valid( monitor.signal ) || !valid( monitor.reference )
                    || !valid( monitor.supply ) || !std::isfinite( monitor.low )
                    || !std::isfinite( monitor.high ) || monitor.low >= monitor.high
                    || !std::isfinite( monitor.defaultSupply ) || monitor.defaultSupply <= 0
                    || !std::isfinite( monitor.minSupply ) || monitor.minSupply <= 0
                    || !std::isfinite( monitor.maxSupply ) || monitor.maxSupply < monitor.minSupply
                    || !std::isfinite( monitor.thresholdReference ) || monitor.thresholdReference < 0 )
                    throw std::runtime_error( "Invalid signal activity monitor" );
                cfg.activityMonitors.push_back( monitor );
            }
        }

        std::vector<std::string> names;
        std::set<std::string> unique;
        for( const auto& signal : cfg.signals )
        {
            if( !Token( signal ) )
                throw std::runtime_error( "Invalid signal token" );
            std::string name = Lower( signal );
            if( name.starts_with( "v(" ) && name.back() == ')' )
                name = name.substr( 2, name.size() - 3 );
            else if( name.starts_with( "i(" ) && name.back() == ')' )
                name = name.substr( 2, name.size() - 3 ) + "#branch";
            if( !unique.insert( name ).second )
                throw std::runtime_error( "Duplicate signal" );
            names.push_back( name );
        }

        std::string netlist = aRequest.at( "netlist" );
        if( netlist.size() > 1024 * 1024 || netlist.find( '\0' ) != std::string::npos )
            throw std::runtime_error( "Invalid or oversized netlist" );

        std::istringstream input( netlist );
        std::string line, deck;
        bool title = true, end = false, mixedSignal = false;
        const std::set<std::string> allowed = { ".model", ".subckt", ".ends", ".param",
                                                ".func", ".ic", ".options", ".option",
                                                ".global", ".temp" };
        while( std::getline( input, line ) )
        {
            if( title )
            {
                deck += line + '\n';
                title = false;
                continue;
            }
            std::istringstream words( Lower( line ) );
            std::string first;
            words >> first;
            if( first.empty() || first[0] == '*' )
                continue;
            if( first[0] == 'a' ) mixedSignal = true;
            if( end )
                throw std::runtime_error( "Unexpected data after .end" );
            if( first == ".end" )
            {
                end = true;
                continue;
            }
            // Stage 1 accepts inline models only. Analysis and interpreter commands
            // (including commands hidden inside include files) cannot own this worker.
            if( first[0] == '.' && !allowed.contains( first ) )
                throw std::runtime_error( "Unsupported deck directive: " + first );
            deck += line + '\n';
        }
        if( title || !end )
            throw std::runtime_error( "A component deck with a title and .end is required" );

        COMPILED_EXPRESSIONS expressions;
        const bool stateless = StatelessNetwork( deck );
        if( cfg.accelerateExpressions )
            deck = expressions.Configure( deck, cfg.signals, cfg.mutableElements );

        deck += ".save none";
        for( const auto& signal : cfg.signals )
            deck += " " + signal;
        // XSPICE freezes time while converging its first point. A large first
        // point can strand a hard limiter at its startup kink, even with the
        // stock adc_bridge. Start conservatively, then retain the configured
        // adaptive maximum; firmware sampling and display timing are unchanged.
        const double initialStep = mixedSignal || !expressions.Empty()
                ? std::min( cfg.step, 1e-6 ) : cfg.step;
        // The public horizon is a controller stop, not ngspice's final-time
        // subtraction. ngspice 46 can produce a zero delta at that subtraction
        // on Windows. Leave one step of internal room and stop on the last
        // accepted public point instead; no sample past the horizon is taken.
        const double solverHorizon = cfg.horizon + std::min( cfg.step, cfg.maxStep );
        if( !std::isfinite( solverHorizon ) )
            throw std::runtime_error( "Simulation horizon is too large" );
        deck += "\n.tran " + Number( initialStep ) + " " + Number( solverHorizon ) + " 0 "
                + Number( cfg.maxStep ) + ( cfg.useInitialConditions ? " uic\n" : "\n" ) + ".end\n";

        if( aRequest.contains( "memories" ) )
            for( const auto& region : aRequest.at( "memories" ) )
                cfg.memories.push_back(
                        { region.at( "id" ),
                          region.at( "label" ),
                          region.at( "word_bits" ),
                          region.at( "writable" ),
                          { MemoryUnhex( region.at( "data" ) ), MemoryUnhex( region.at( "known" ) ) } } );
        m_memory.Reset( cfg.memories );
        ConfigureProgrammable(cfg);
        // Resistive circuits have no integration history to rebuild after a
        // GPIO edge. Keep ngspice's nonlinear solve, but choose accepted points
        // directly at firmware edges instead of a breakpoint recovery ramp.
        // Other source waveforms keep ngspice's own discontinuity scheduler.
        m_memorylessSteps=m_hasClockedRuntime&&stateless&&cfg.memories.empty()
            &&aRequest.value("accelerate_stateless",true);
        if(m_memorylessSteps)
        {
            std::istringstream lines(Lower(deck));std::string line;
            while(std::getline(lines,line))
            {
                std::istringstream fields(line);std::string name;fields>>name;
                if(name.empty()||(name[0]!='v'&&name[0]!='i'))continue;
                if(line.find("external")!=std::string::npos||line.find('{')!=std::string::npos)m_memorylessSteps=false;
                if(line.find('(')!=std::string::npos)
                {
                    std::smatch match;
                    if(!std::regex_search(line,match,std::regex(R"(\bsin\s*\(([^()]*)\))")))m_memorylessSteps=false;
                    else
                    {
                        std::istringstream values(match[1].str());std::string value;unsigned index=0;
                        while(values>>value)
                        {
                            char* end=nullptr;const auto number=std::strtod(value.c_str(),&end);
                            if(!end||*end||!std::isfinite(number)||index>5||(index==3&&number!=0))m_memorylessSteps=false;
                            ++index;
                        }
                    }
                }
            }
        }
        m_statelessNetwork = ( !cfg.programmableDevices.empty() || aRequest.value("display_network",false) )
                && cfg.memories.empty() && std::none_of(names.begin(),names.end(),[](const auto& name){return name=="time";})
                && aRequest.value( "accelerate_stateless", true ) && stateless;
        m_expressions = std::move( expressions );
        m_config = std::move( cfg );
        m_names = std::move( names );
        m_deck = std::move( deck );
        m_configured = true;
        std::lock_guard lock( m_mutex );
        m_ring.assign( m_config.historyCapacity, SIM_SAMPLE{} );
        for( auto& sample : m_ring )
            sample.values.resize( m_config.historySignals ? m_config.historySignals->size() : m_names.size() );
        Clear();
    }

    void Clear() // m_mutex held; never called while the solver is active.
    {
        m_latest = {};
        m_historyMinimum.assign(m_config.signals.size(),0);m_historyMaximum=m_historyMinimum;
        m_haveHistoryExtrema=false;m_retainedHistoryValid=false;++m_historyToken;
        m_nativeSettleDeadline = std::numeric_limits<double>::infinity();
        m_programTransitions.clear();
        m_statelessAttempts = m_statelessExpansions = 0;
        m_staticSolutions.clear();
        m_staticSinePhases.clear();
        m_samples = m_overwritten = m_events = 0;
        m_eventTime = 0;
        m_head = m_size = 0;
        m_indices.clear();
        m_activity.assign( m_config.activityMonitors.size(), SIGNAL_ACTIVITY_TRACKER{} );
        m_audio.Reset();
        m_error.clear();
        m_finished = true;
    }

    bool Finished()
    {
        std::lock_guard lock( m_mutex );
        return m_finished;
    }

    void Command( const std::string& aCommand )
    {
        if( ngSpice_Command( const_cast<char*>( aCommand.c_str() ) ) != 0 )
            throw std::runtime_error( "ngspice rejected: " + aCommand );

        // ngspice also retains interpreter command structures independently of plots.
        if( ++m_commands >= 32 && Finished() )
        {
            ngSpice_Command( nullptr );
            m_commands = 0;
        }
    }

    void CheckError()
    {
        std::lock_guard lock( m_mutex );
        if( m_error.empty() ) m_error = m_memory.Error();
        if( !m_error.empty() )
        {
            m_state = SIM_SESSION_STATE::FAILED;
            throw std::runtime_error( m_error );
        }
    }

    void Load()
    {
        if( !m_configured )
            throw std::runtime_error( "Configure a circuit first" );

        {
            std::lock_guard lock( m_mutex );
            Clear();
            ++m_generation;
        }
        m_memory.Reset( m_config.memories );
        m_programmable.Reset();
        m_nativePeripherals.Reset();
        ResetProgrammableSources();
        if( (!m_config.memories.empty() || !m_programDrivers.empty() || !m_expressions.Empty()) && !m_memoryModule )
        {
            const auto file = ExecutablePath().parent_path() / "circuitalive-memory.cm";
            const auto path = PathUtf8(file);
#ifdef _WIN32
            m_memoryModule = reinterpret_cast<void*>(LoadLibraryW(file.c_str()));
            if(!m_memoryModule) throw std::runtime_error("Cannot load memory engine: " + path);
            auto bind = reinterpret_cast<void (*)(const CA_MEMORY_BACKEND*)>(
                GetProcAddress(reinterpret_cast<HMODULE>(m_memoryModule),"CircuitAliveMemorySetBackend"));
            if(!bind) throw std::runtime_error("Invalid memory engine");
            auto backend=m_memory.Backend();bind(&backend);
            // ngspice resolves relative Windows modules beside the executable.
            // This also avoids its ANSI loader interpreting the installation path.
            Command("codemodel circuitalive-memory.cm");
#else
            const int fd = open(path.c_str(),O_RDONLY|O_CLOEXEC);
            if(fd<0)throw std::runtime_error("Memory engine file is missing: "+path);
            struct CLOSE_FD{int fd;~CLOSE_FD(){close(fd);}} closeFd{fd};
            const auto modulePath="/proc/self/fd/"+std::to_string(fd);
            m_memoryModule=dlopen(modulePath.c_str(),RTLD_NOW|RTLD_LOCAL);
            if(!m_memoryModule)throw std::runtime_error(std::string("Cannot load memory engine: ")+dlerror());
            auto bind=reinterpret_cast<void (*)(const CA_MEMORY_BACKEND*)>(dlsym(m_memoryModule,"CircuitAliveMemorySetBackend"));
            if(!bind)throw std::runtime_error("Invalid memory engine");
            auto backend=m_memory.Backend();bind(&backend);Command("codemodel "+modulePath);
#endif
            auto bindPins = reinterpret_cast<void (*)(const CA_PROGRAMMABLE_PIN_BACKEND*)>(
#ifdef _WIN32
                GetProcAddress(reinterpret_cast<HMODULE>(m_memoryModule),"CircuitAliveProgrammablePinSetBackend")
#else
                dlsym(m_memoryModule,"CircuitAliveProgrammablePinSetBackend")
#endif
            );
            if(!bindPins)throw std::runtime_error("Invalid programmable pin engine");
            const CA_PROGRAMMABLE_PIN_BACKEND pinBackend{1,this,[](void* user,unsigned slot,double time)->int {
                const auto* engine=static_cast<ENGINE*>(user);
                const auto& drivers=engine->m_programDrivers;
                if(slot>=drivers.size()||drivers[slot].empty())return 0;
                const auto& history=drivers[slot];
                if(engine->m_dcSolving)time=engine->m_dcPointTime;
                if(time>=history.back().time)return int(history.back().value);
                const auto next=std::upper_bound(history.begin(),history.end(),time,
                    [](double t,const EXTERNAL_VALUE& value){return t<value.time;});
                return next==history.begin()?0:int(std::prev(next)->value);
            }};
            bindPins(&pinBackend);
            auto bindExpressions = reinterpret_cast<void (*)(const CA_EXPRESSION_BACKEND*)>(
#ifdef _WIN32
                GetProcAddress(reinterpret_cast<HMODULE>(m_memoryModule),"CircuitAliveExpressionSetBackend")
#else
                dlsym(m_memoryModule,"CircuitAliveExpressionSetBackend")
#endif
            );
            if(!bindExpressions)throw std::runtime_error("Invalid expression engine");
            const CA_EXPRESSION_BACKEND expressionBackend{1,this,
                [](void* user,unsigned slot,const double* inputs,unsigned count,double time,double* value,const double** partials)->int {
                    return static_cast<ENGINE*>(user)->m_expressions.Evaluate(slot,inputs,count,time,*value,*partials);
                }};
            bindExpressions(&expressionBackend);
            CheckError();
        }
        std::istringstream stream( m_deck );
        std::vector<std::string> lines;
        for( std::string line; std::getline( stream, line ); )
            lines.push_back( std::move( line ) );
        std::vector<char*> pointers;
        for( auto& line : lines )
            pointers.push_back( line.data() );
        pointers.push_back( nullptr );
        if( ngSpice_Circ( pointers.data() ) != 0 )
        {
            m_state = SIM_SESSION_STATE::FAILED;
            throw std::runtime_error( "ngspice could not load the circuit" );
        }
        CheckError();
        Command( "esave none" ); // Event history is outside the analog stage-1 contract.
        RefreshStatelessSources();
    }

    void Run( bool aFresh, bool aBackground = true )
    {
        {
            std::lock_guard lock( m_mutex );
            m_finished = false; // Set before dispatch; even very short runs can finish immediately.
        }
        m_state = SIM_SESSION_STATE::RUNNING;
        try
        {
            if( aBackground )
                Command( aFresh ? "bg_run" : "bg_resume" );
            else
            {
                // Advance already waits for this boundary. Running on the
                // worker's control thread avoids creating a new ngspice thread
                // for every display frame; Play still uses background execution.
                Command( aFresh ? "run" : "resume" );
                std::lock_guard lock( m_mutex );
                m_finished = true;
            }
        }
        catch( ... )
        {
            std::lock_guard lock( m_mutex );
            m_finished = true;
            m_state = SIM_SESSION_STATE::FAILED;
            throw;
        }
    }

    void Play()
    {
        if( m_hasClockedRuntime || m_statelessNetwork )
        {
            if( m_programPlaying ) return;
            if( m_state!=SIM_SESSION_STATE::STOPPED && m_state!=SIM_SESSION_STATE::PAUSED )
                throw std::runtime_error("Stop the completed/failed session before restarting");
            if( m_statelessNetwork ) AdvanceStateless( ProgrammablePollDeadline() );
            else AdvanceProgrammable(ProgrammablePollDeadline());
            m_programPlaying=m_state!=SIM_SESSION_STATE::COMPLETED;
            if(m_programPlaying)m_state=SIM_SESSION_STATE::RUNNING;
            return;
        }
        if( m_state == SIM_SESSION_STATE::RUNNING )
            return;
        const bool fresh = m_state == SIM_SESSION_STATE::STOPPED;
        if( !fresh && m_state != SIM_SESSION_STATE::PAUSED )
            throw std::runtime_error( "Stop the completed/failed session before restarting" );
        if( fresh )
            Load();
        else
            Command( "delete all" );
        Command( "stop when time >= " + Number( StopThreshold( m_config.horizon ) ) );
        Run( fresh );
    }

    void Halt()
    {
        Command( "bg_halt" );
        Wait(); // A pause is acknowledged only after the final callback.
    }

    void Wait()
    {
        std::unique_lock lock( m_mutex );
        if( !m_condition.wait_for( lock, std::chrono::seconds( 4 ), [&] { return m_finished; } ) )
            throw std::runtime_error( "ngspice did not acknowledge stopping" );
    }

    void Refresh()
    {
        if(m_programPlaying)return; // Cooperative runtime Play advances on snapshot requests.
        bool finished, failed;
        {
            std::lock_guard lock( m_mutex );
            finished = m_finished;
            failed = !m_error.empty();
        }
        if( m_state != SIM_SESSION_STATE::RUNNING )
            return;
        if( failed && !finished )
        {
            Halt();
            finished = true;
        }
        if( finished )
        {
            std::lock_guard lock( m_mutex );
            if( failed || m_samples == 0 )
                m_state = SIM_SESSION_STATE::FAILED;
            else if( m_latest.time >= m_config.horizon * ( 1 - 1e-12 ) )
                m_state = SIM_SESSION_STATE::COMPLETED;
            else
                m_state = SIM_SESSION_STATE::PAUSED;
        }
    }

    void Pause()
    {
        if(m_programPlaying)
        {
            m_programPlaying=false;m_state=SIM_SESSION_STATE::PAUSED;CheckError();return;
        }
        if( m_state == SIM_SESSION_STATE::STOPPED )
            throw std::runtime_error( "No active simulation to pause" );
        if( m_state == SIM_SESSION_STATE::RUNNING )
            Halt();
        Refresh();
        CheckError();
    }

    void Advance( double aTime )
    {
        if( m_statelessNetwork )
        {
            if( m_programPlaying ) Pause();
            AdvanceStateless( aTime ); return;
        }
        if( m_hasClockedRuntime )
        {
            if(m_programPlaying)Pause();
            AdvanceProgrammable(aTime);return;
        }
        AdvanceAnalog(aTime);
    }

    void AdvanceAnalog( double aTime, bool exact = false )
    {
        m_analogTarget = aTime;
        if( !m_configured || !std::isfinite( aTime ) || aTime <= 0 || aTime > m_config.horizon )
            throw std::runtime_error( "Target must be positive and within the configured horizon" );
        if( m_state == SIM_SESSION_STATE::RUNNING )
            Pause();
        if( m_state != SIM_SESSION_STATE::STOPPED && m_state != SIM_SESSION_STATE::PAUSED )
            throw std::runtime_error( "Cannot advance a completed/failed session" );
        {
            std::lock_guard lock( m_mutex );
            if( aTime <= m_latest.time )
                throw std::runtime_error( "Simulation time must advance monotonically" );
        }

        const bool fresh = m_state == SIM_SESSION_STATE::STOPPED;
        if( fresh )
            Load();
        else
            Command( "delete all" );
        // Before the first run, "delete all" would also remove the .save policy.
        // On resume the current plot already owns its storage and selected vectors.
        if( exact && !fresh )
        {
            // Resume from the last accepted point before the solver takes its
            // next step. There is no second data callback at this same time.
            std::lock_guard lock( m_mutex );
            ScheduleProgrammable();
        }
        // OnSync already bounds the next step at the requested runtime time.
        // Do not add a second permanent breakpoint here: a rounded stop just
        // before it would leave a femtosecond remainder to solve on resume.
        // Only actual pin transitions need permanent breakpoints.
        const double bias = exact ? ProgrammableStopMargin( aTime )
                : m_config.synchronizeAdvances ? std::min( aTime * 1e-9, 1e-11 ) : 0;
        const double threshold = StopThreshold( aTime, bias );
        Command( "stop when time >= " + Number( threshold ) );
        Run( fresh, false );
        Wait();
        Refresh();
        CheckError();
        if( m_state == SIM_SESSION_STATE::FAILED )
            throw std::runtime_error( "Simulation produced no data" );
    }

    // The runtime advances on confirmed electrical boundaries. It predicts output
    // transitions while inputs are held for one configurable sampling window.
    // The solver only reads that immutable event history; rejected analog steps
    // never execute firmware twice or mutate CPU/peripheral state.
    void ConfigureProgrammable( const SIM_SESSION_CONFIG& config )
    {
        m_programInputs.clear();m_programOutputs.clear();m_external.clear();m_externalCache.clear();m_programDrivers.clear();
        m_programPlaying=false;m_programReady=false;m_programInputRefresh=false;m_programBoundary=0;m_programQuantum=1e-3;
        std::vector<PROGRAMMABLE_DEVICE_CONFIG> clocked;
        std::set<std::string> deviceIds;
        m_nativePeripherals.Configure( config.programmableDevices, config.signals );
        for( const auto& device : config.programmableDevices )
        {
            if( device.id.empty() || device.id.size() > 256 || !deviceIds.insert( device.id ).second )
                throw std::runtime_error( "Invalid or duplicate programmable device ID: " + device.id );
            const bool native = device.engine == "native";
            if( !native ) clocked.push_back( device );
            if(!std::isfinite(device.quantumSeconds)||device.quantumSeconds<1e-9||device.quantumSeconds>1e-3)
                throw std::runtime_error("Programmable input sampling period must be between 1 ns and 1 ms");
            if( !native ) m_programQuantum=std::min(m_programQuantum,device.quantumSeconds);
            if(device.bindings.size()>512)throw std::runtime_error("Too many programmable pin bindings");
            std::set<std::string> ids;
            for( const auto& binding : device.bindings )
            {
                if(!ids.insert(binding.id).second)
                    throw std::runtime_error("Duplicate programmable pin binding");
                const auto signal=std::find(config.signals.begin(),config.signals.end(),binding.signal);
                if(!binding.signal.empty()&&signal==config.signals.end())throw std::runtime_error("Programmable pin input signal is missing");
                if( !native ) m_programInputs.push_back({device.id,binding.id,binding.signal.empty()
                    ? std::numeric_limits<std::size_t>::max() : std::size_t(signal-config.signals.begin())});
                if(binding.driverSlot < -1 || binding.driverSlot >= 16384)throw std::runtime_error("Invalid programmable driver slot");
                if(binding.driverSlot>=0) {
                    if(m_programDrivers.size()<=std::size_t(binding.driverSlot))m_programDrivers.resize(binding.driverSlot+1);
                    auto& history=m_programDrivers[binding.driverSlot];
                    if(!history.empty())throw std::runtime_error("Duplicate programmable driver slot");
                    history={{0,0}};
                    m_programOutputs[{device.id,binding.id}]={"","","",binding.driverSlot};
                    continue;
                }
                if(binding.highSource.empty()&&binding.lowSource.empty()&&binding.pullupSource.empty())continue;
                OUTPUT_BINDING output{Lower(binding.highSource),Lower(binding.lowSource),Lower(binding.pullupSource),-1};
                for(const auto& source:{output.high,output.low,output.pullup})
                {
                    if(source.empty()||source.front()!='v'||source.size()>240||source.find_first_not_of(
                        "abcdefghijklmnopqrstuvwxyz0123456789_")!=std::string::npos||m_external.contains(source))
                        throw std::runtime_error("Invalid or duplicate programmable driver source");
                    m_external[source]={{0,0}};
                }
                m_programOutputs[{device.id,binding.id}]=std::move(output);
            }
        }
        m_hasClockedRuntime = !clocked.empty();
        m_programmable.Configure( clocked );
    }

    void ResetProgrammableSources()
    {
        m_programReady=false;m_programBoundary=0;m_programInputRefresh=false;
        m_externalCache.clear();
        for(auto& [source,history]:m_external)history={{0,0}};
        for(auto& history:m_programDrivers)if(!history.empty())history={{0,0}};
    }

    void ProgrammableEvents( const std::vector<PROGRAMMABLE_PIN_STATE>& events, double from, double until )
    {
        if(events.size()>100000)throw std::runtime_error("Too many programmable events in one sampling window");
        std::set<double> breaks;
        bool immediate=false;
        for(const auto& event:events)
        {
            const auto output=m_programOutputs.find({event.device,event.pin});
            if(output==m_programOutputs.end())continue; // Input-only/backend-internal state.
            if(!std::isfinite(event.time)||event.time>until+1e-12)
                throw std::runtime_error("Programmable event lies beyond the sampling boundary");
            const auto time=std::max(event.time,from);
            const auto& pin=output->second;
            if(pin.slot>=0) {
                auto& history=m_programDrivers.at(pin.slot);
                if(history.back().value!=int(event.drive)) {
                    if(time<history.back().time)throw std::runtime_error("Programmable events are not chronological");
                    history.push_back({time,double(int(event.drive))});
                    if(time>from)breaks.insert(time);
                    else immediate=true;
                }
                continue;
            }
            const std::pair<std::string,double> values[]={{pin.high,event.drive==PROGRAMMABLE_DRIVE::HIGH?1.:0.},
                {pin.low,event.drive==PROGRAMMABLE_DRIVE::LOW?1.:0.},
                {pin.pullup,event.drive==PROGRAMMABLE_DRIVE::PULL_UP?1.:0.}};
            for(const auto& [source,value]:values)
            {
                auto& history=m_external.at(source);
                if(history.back().value!=value)
                {
                    if(time<history.back().time)throw std::runtime_error("Programmable events are not chronological");
                    history.push_back({time,value});
                    if(time>from)breaks.insert(time);
                    else immediate=true;
                }
            }
        }
        for(double time:breaks)
            if(m_memorylessSteps||m_statelessNetwork)m_programTransitions.insert(time);
            else if(!ngSpice_SetBkpt(time))throw std::runtime_error("Cannot schedule programmable pin transition");
        if(immediate)
        {
            // A write at the already accepted frontier still changes the
            // electrical state. Resolve its right side before a later GPIO
            // event can replace its data/enable level (notably LCD strobes).
            const double settle=m_statelessNetwork?std::nextafter(from,std::numeric_limits<double>::infinity()):from+1e-9;
            m_nativeSettleDeadline=std::min(m_nativeSettleDeadline,settle);
        }
        m_events+=events.size();
    }

    void AdvanceProgrammable( double target )
    {
        if(!m_configured||!std::isfinite(target)||target<=0||target>m_config.horizon)
            throw std::runtime_error("Target must be positive and within the configured horizon");
        if(m_state!=SIM_SESSION_STATE::STOPPED&&m_state!=SIM_SESSION_STATE::PAUSED)
            throw std::runtime_error("Cannot advance a completed/failed session");
        if(target<=m_latest.time)throw std::runtime_error("Simulation time must advance monotonically");
        m_programTarget=target;
        AdvanceAnalog(target,true);
        if(std::abs(m_latest.time-target)>ProgrammableTolerance())
            throw std::runtime_error("Analog solver did not stop on the programmable sampling boundary: "
                + Number(m_latest.time) + " instead of " + Number(target));
    }

    // Called after an accepted analog point, or while paused before resuming,
    // with m_mutex held. Solver retries never recompute a pending runtime window.
    void ScheduleProgrammable()
    {
        const double now=m_latest.time;
        const double tolerance=ProgrammableTolerance();
        if(now>=m_programTarget-tolerance)return;
        if(m_programReady&&now<m_programBoundary-tolerance)return;
        if(m_programReady&&now>m_programBoundary+tolerance)
            throw std::runtime_error("Analog solver skipped a programmable input boundary: "
                + Number(now) + " instead of " + Number(m_programBoundary));
        PruneProgrammableHistory( now );
        std::vector<PROGRAMMABLE_PIN_INPUT> inputs;
        for(const auto& binding:m_programInputs)
            inputs.push_back({binding.device,binding.pin,binding.signal==std::numeric_limits<std::size_t>::max()
                ? 0. : m_latest.values.at(binding.signal)});
        double window=m_programQuantum;
        for(const auto& device:m_config.programmableDevices)
            if( device.engine != "native" ) window=std::max(window,device.autonomousWindowSeconds);
        // alter changes the solver, but m_latest still contains the previous
        // accepted voltages. Resolve one sampling interval before allowing
        // lookahead/prefetch from the new electrical input snapshot.
        const bool refresh=m_programInputRefresh;
        m_programInputRefresh=false;
        if(refresh)window=m_programQuantum;
        double boundary=std::min(m_programTarget,m_programmable.Time()+window);
        // Do not let accumulated floating point additions create a second
        // breakpoint just below the exact stop-condition value.
        if(m_programTarget-boundary<tolerance)boundary=m_programTarget;
        if(boundary<=now+tolerance)
            throw std::runtime_error("Programmable device clock did not advance");
        const auto events=m_programmable.AdvanceTo(boundary,inputs,refresh?boundary:m_programTarget);
        boundary=m_programmable.Time();
        ProgrammableEvents(events,now,boundary);
        m_programBoundary=boundary;m_programReady=true;
    }

    void PruneProgrammableHistory( double now )
    {
        for(auto& [source,history]:m_external)
        {
            const auto next=std::upper_bound(history.begin(),history.end(),now,
                [](double time,const EXTERNAL_VALUE& entry){return time<entry.time;});
            if(next!=history.begin())history.erase(history.begin(),std::prev(next));
        }
        for(auto& history:m_programDrivers) {
            const auto next=std::upper_bound(history.begin(),history.end(),now,
                [](double time,const EXTERNAL_VALUE& entry){return time<entry.time;});
            if(next!=history.begin())history.erase(history.begin(),std::prev(next));
        }
    }

    double ProgrammablePollDeadline() const
    {
        if( m_statelessNetwork ) return std::min( m_config.horizon, m_latest.time + .008 );
        return std::min(m_config.horizon,m_latest.time+128*m_programQuantum);
    }

    double ProgrammableTolerance() const
    {
        // Bound acceptance to 0.1% of the sampling interval, while retaining
        // enough representable doubles at large absolute simulation times.
        const double roundoff = 32 * std::numeric_limits<double>::epsilon()
                * std::max( m_programTarget, m_latest.time );
        return std::max( { 1e-12, m_programQuantum * 1e-3, roundoff } );
    }

    double ProgrammableStopMargin( double target ) const
    {
        // Use one margin for both the stop expression and the last step cap.
        // Otherwise a valid point just before the boundary can be too late to
        // shorten its next step, yet too early to satisfy the stop expression.
        return std::min( { target * 1e-9, ProgrammableTolerance() * .5, 1e-11 } );
    }

    static int LimitProgrammableStep( double time, double* delta, void* user, bool fresh = false )
    {
        auto& self=*static_cast<ENGINE*>(user);
        const bool programmable = self.m_hasClockedRuntime;
        if(fresh&&self.m_memorylessSteps&&self.m_programReady)*delta=self.m_config.maxStep;
        auto limit = [&]( double boundary, double tolerance )
        {
            const double remaining = boundary - time;
            if( remaining > tolerance && *delta > remaining ) *delta = remaining;
        };
        limit( self.m_nativePeripherals.Deadline(), 1e-12 );
        limit( self.m_nativeSettleDeadline, 1e-12 );
        if(!self.m_programTransitions.empty())limit(*self.m_programTransitions.begin(),1e-15);
        // Stop and input boundaries have separate roles. The last CPU window
        // may end within its sampling tolerance before the requested stop;
        // reaching that window must not disable the stop's independent cap.
        if( programmable )
        {
            limit( self.m_programTarget, self.ProgrammableStopMargin( self.m_programTarget ) );
            limit( self.m_programReady ? self.m_programBoundary : self.m_programQuantum,
                   self.ProgrammableTolerance() );
        }
        else
        {
            if( self.m_config.synchronizeAdvances )
                limit( self.m_analogTarget, std::min( self.m_analogTarget * 1e-9, 1e-11 ) );
            limit( self.m_config.horizon,
                   8 * std::numeric_limits<double>::epsilon() * self.m_config.horizon );
        }
        return 0;
    }

    // Both ngspice callback declarations are selected by the installed header:
    // versions with a call-location argument and older versions without it.
    // Limit the next trial step without marking every input sample as a physical
    // discontinuity. Permanent breakpoints still preserve actual output edges.
    static int OnSync( double time, double* delta, double, int redo, int, int location, void* user )
    { return LimitProgrammableStep(time,delta,user,!redo&&location==0); }
    static int OnSync( double time, double* delta, double oldDelta, int redo, int, void* user )
    { return LimitProgrammableStep(time,delta,user,!redo&&oldDelta==0); }

    static int OnVoltage( double* value, double time, char* source, int, void* user )
    {
        auto& self=*static_cast<ENGINE*>(user);
        auto cached=self.m_externalCache.find(source);
        if(cached==self.m_externalCache.end())
        {
            const auto found=self.m_external.find(Lower(source));
            if(found==self.m_external.end())
            {
                *value=0;
                std::lock_guard lock(self.m_mutex);
                if(self.m_error.empty())self.m_error="Unknown external voltage source: "+std::string(source);
                return 1;
            }
            // Ngspice passes the instance's interned VSRCname, stable for the
            // loaded deck. Reset clears the cache before another deck is loaded.
            cached=self.m_externalCache.emplace(source,&found->second).first;
        }
        const auto& history=*cached->second;
        if(time>=history.back().time){*value=history.back().value;return 0;}
        auto next=std::upper_bound(history.begin(),history.end(),time,
            [](double t,const EXTERNAL_VALUE& entry){return t<entry.time;});
        *value=next==history.begin()?0:std::prev(next)->value;
        return 0;
    }

    void Source( const std::string& aReference, double aValue, const JSON& changes )
    {
        if( m_state != SIM_SESSION_STATE::PAUSED )
            throw std::runtime_error( "Source changes require a confirmed pause" );
        if( !std::isfinite( aValue ) || !Token( aReference )
                || aReference.find_first_not_of( "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                                 "0123456789_" ) != std::string::npos
                || ( std::tolower( aReference.front() ) != 'v'
                     && std::tolower( aReference.front() ) != 'i' ) )
            throw std::runtime_error( "A V/I source reference and finite value are required" );

        if( !changes.is_array() || changes.size() > 1024 ) throw std::runtime_error( "Invalid parameter updates" );
        std::vector<std::string> commands;
        for( const auto& change : changes )
        {
            const std::string reference = change.at( "reference" ), parameter = change.at( "parameter" );
            auto identifier = []( const std::string& s )
            { return !s.empty() && s.size() <= 240 && s.find_first_not_of( "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_" ) == std::string::npos; };
            if( !identifier( reference ) || !identifier( parameter ) ) throw std::runtime_error( "Invalid parameter target" );
            const auto values = change.at( "expressions" ).get<std::vector<std::string>>();
            if( values.empty() || values.size() > 256 ) throw std::runtime_error( "Invalid parameter values" );
            std::string expression;
            for( const auto& value : values )
            {
                // Numeric expressions only: no interpreter commands, paths, substitutions or redirection.
                if( value.empty() || value.size() > 4096 || value.find_first_not_of(
                    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.+-*/^(), " ) != std::string::npos )
                    throw std::runtime_error( "Invalid numeric parameter expression" );
                if( !expression.empty() ) expression += " ";
                expression += "(" + value + ")";
            }
            if( values.size() > 1 )
            {
                // The alter vector parser accepts numeric literals, but does not evaluate
                // arithmetic separately for each element. Resolve each scalar first; otherwise
                // a calculated PULSE width can silently turn into a non-periodic source.
                expression.clear();
                for( const auto& value : values )
                {
                    Command( "let ca_parameter_value = (" + value + ")" ); CheckError();
                    char name[] = "ca_parameter_value";
                    const auto* vector = ngGet_Vec_Info( name );
                    if( !vector || vector->v_length != 1 || !vector->v_realdata
                        || !std::isfinite( vector->v_realdata[0] ) )
                        throw std::runtime_error( "Parameter update must evaluate to a finite scalar" );
                    if( !expression.empty() ) expression += " ";
                    expression += Number( vector->v_realdata[0] );
                    Command( "unlet ca_parameter_value" );
                }
                expression = "[ " + expression + " ]";
            }
            commands.push_back( std::string( change.value( "model", false ) ? "altermod " : "alter " )
                    + reference + " " + parameter + " = " + expression );
        }
        Command( "alter " + aReference + " dc = " + Number( aValue ) );
        for( const auto& command : commands ) { Command( command ); CheckError(); }
        CheckError();
        RefreshStatelessSources();
        std::lock_guard lock( m_mutex );
        ++m_events;
        m_eventTime = m_latest.time;
        m_programInputRefresh = true;
        m_nativeSettleDeadline = m_latest.time + 1e-9;
    }

    JSON Memory( const JSON& request )
    {
        if( !Finished() || ( m_state != SIM_SESSION_STATE::PAUSED && m_state != SIM_SESSION_STATE::COMPLETED ) )
            throw std::runtime_error( "Pause the simulation before reading memory" );
        CheckError();
        const std::size_t length = request.at( "length" );
        if( length > 4096 )
            throw std::runtime_error( "Memory page exceeds 4096 bytes" );
        const auto image = m_memory.Read( request.at( "id" ), request.at( "offset" ), length );
        return {
                { "time", m_latest.time }, { "data", MemoryHex( image.data ) }, { "known", MemoryHex( image.known ) } };
    }

    JSON HistoryRows(std::size_t head,std::size_t size,
            const std::optional<std::vector<std::size_t>>& columns={}) const
    {
        JSON rows=JSON::array();
        if(columns&&columns->empty())return rows;
        for(std::size_t i=0;i<size;++i)
        {
            const auto& sample=m_ring[(head+i)%m_ring.size()];
            if(columns)
            {
                JSON values=JSON::array();for(auto column:*columns)values.push_back(sample.values.at(column));
                rows.push_back({{"time",sample.time},{"values",std::move(values)}});
            }
            else rows.push_back({{"time",sample.time},{"values",sample.values}});
        }
        return rows;
    }

    JSON Snapshot( bool aDrain,const std::optional<std::vector<std::size_t>>& historySignals={} )
    {
        if(m_programPlaying)
        {
            m_programPlaying=false;m_state=SIM_SESSION_STATE::PAUSED;
            if( m_statelessNetwork ) AdvanceStateless( ProgrammablePollDeadline() );
            else AdvanceProgrammable(ProgrammablePollDeadline());
            m_programPlaying=m_state!=SIM_SESSION_STATE::COMPLETED;
            if(m_programPlaying)m_state=SIM_SESSION_STATE::RUNNING;
        }
        // Never read a live ngspice vector. vecindex is an accepted-step counter,
        // not a portable measure of retained storage (ngspice versions differ).
        std::size_t retained = 0;
        if( m_statelessNetwork ) retained = m_samples ? 1 : 0;
        else if( Finished() && m_samples )
        {
            char timeName[] = "time";
            if( auto* vector = ngGet_Vec_Info( timeName ) )
                retained = vector->v_length;
            if( retained != 1 )
                throw std::runtime_error( "ngspice does not support single-point streaming storage ("
                                          + std::to_string( retained ) + " retained)" );
        }
        const auto resident=ResidentMemoryBytes();

        std::lock_guard lock( m_mutex );
        std::optional<std::vector<std::size_t>> columns;
        if(historySignals)
        {
            columns.emplace();std::set<std::size_t> seen;
            for(const auto index:*historySignals)
            {
                if(index>=m_names.size()||!seen.insert(index).second)throw std::runtime_error("Invalid snapshot history signal");
                if(m_config.historySignals)
                {
                    const auto found=std::find(m_config.historySignals->begin(),m_config.historySignals->end(),index);
                    if(found==m_config.historySignals->end())throw std::runtime_error("Snapshot signal is not retained");
                    columns->push_back(found-m_config.historySignals->begin());
                }
                else columns->push_back(index);
            }
        }
        JSON history=HistoryRows(m_head,m_size,columns);
        JSON result = { { "state", StateName( m_state ) }, { "generation", m_generation },
                        { "time", m_latest.time }, { "values", m_latest.values },
                        { "history", std::move( history ) }, { "samples", m_samples },
                        { "overwritten", m_overwritten }, { "events", m_events },
                        { "event_time", m_eventTime }, { "retained", retained },
                        { "resident_bytes", resident }, { "error", m_error } };
        if(historySignals)result["history_signals"]=*historySignals;
        else if( m_config.historySignals )
            result["history_signals"] = *m_config.historySignals;
        if(m_config.historySignals&&!m_latest.values.empty())
        {result["history_minimum"]=m_historyMinimum;result["history_maximum"]=m_historyMaximum;}
        m_retainedHead=m_head;m_retainedSize=m_size;m_retainedHistoryValid=true;
        result["history_token"]=++m_historyToken;
        result["activity"] = JSON::array();
        for( const auto& tracker : m_activity )
        {
            const auto activity = tracker.Activity();
            result["activity"].push_back( { { "period", activity.period },
                    { "last_transition", activity.lastTransition }, { "high_value", activity.highValue } } );
        }
        auto presentation=m_programmable.Presentation();
        for(const auto& [id,state]:m_nativePeripherals.Presentation())presentation[id]=state;
        result["presentation"]=presentation;
        result["stateless_network"] = m_statelessNetwork;
        result["memoryless_steps"] = m_memorylessSteps;
        result["stateless_points"] = m_statelessAttempts;
        result["stateless_cache_hits"] = m_statelessExpansions;
        result["stateless_dc_solves"] = m_statelessAttempts - m_statelessExpansions;
        result["audio"]=m_audio.Samples();result["audio_overwritten"]=m_audio.Overwritten();
        if( aDrain )
        {
            m_head = m_size = 0; m_audio.Drain();
            if(m_config.historySignals&&!m_latest.values.empty())
                for(auto index:*m_config.historySignals)m_historyMinimum[index]=m_historyMaximum[index]=m_latest.values[index];
        }
        return result;
    }

    void RecordAcceptedPoint()
    {
        m_retainedHistoryValid=false;
        if(m_config.historySignals)
        {
            for(auto index:*m_config.historySignals)
            {
                const auto value=m_latest.values[index];
                if(!m_haveHistoryExtrema)m_historyMinimum[index]=m_historyMaximum[index]=value;
                else {m_historyMinimum[index]=std::min(m_historyMinimum[index],value);m_historyMaximum[index]=std::max(m_historyMaximum[index],value);}
            }
            m_haveHistoryExtrema=true;
        }
        m_audio.Observe(m_latest.time,m_latest.values,m_config.audioChannels);
        ++m_samples;
        for( std::size_t i = 0; i < m_activity.size(); ++i )
            m_activity[i].Observe( m_config.activityMonitors[i], m_latest.time,
                                       m_latest.values );
        if(m_config.historySignals&&m_size>=2)
        {
            auto& last=m_ring[(m_head+m_size-1)%m_ring.size()];
            const auto& previous=m_ring[(m_head+m_size-2)%m_ring.size()];
            bool flat=last.values==previous.values;
            for(std::size_t i=0;flat&&i<m_config.historySignals->size();++i)
                flat=last.values[i]==m_latest.values[(*m_config.historySignals)[i]];
            // Keep the beginning and end of every constant interval. Changing
            // values, both sides of edges, activity and audio stay untouched.
            if(flat){last.time=m_latest.time;return;}
        }
        const auto position = ( m_head + m_size ) % m_ring.size();
        auto& stored = m_ring[position];
        stored.time = m_latest.time;
        if( m_config.historySignals )
        {
            // Only scope/warning channels need a historical copy at every
            // solver step. Latest pin values and activity remain complete.
            for( std::size_t i = 0; i < m_config.historySignals->size(); ++i )
                stored.values[i] = m_latest.values[( *m_config.historySignals )[i]];
        }
        else stored.values = m_latest.values;
        if( m_size == m_ring.size() )
        {
            m_head = ( m_head + 1 ) % m_ring.size();
            ++m_overwritten;
        }
        else
            ++m_size;
    }

    static int OnMessage( char* aMessage, int, void* aUser )
    {
        auto& self = *static_cast<ENGINE*>( aUser );
        const std::string message = Lower( aMessage );
        if( message.find( "error:" ) != std::string::npos
                || message.find( "fatal" ) != std::string::npos
                || message.find( "timestep too small" ) != std::string::npos )
        {
            std::lock_guard lock( self.m_mutex );
            if( self.m_error.empty() )
                self.m_error = std::string( aMessage ).substr( 0, 1024 );
        }
        return 0;
    }

    static int OnStatus( char*, int, void* ) { return 0; }
    static int OnInit( pvecinfoall, int, void* ) { return 0; }
    static int OnExit( int, NG_BOOL, NG_BOOL, int, void* aUser )
    {
        char message[] = "Error: ngspice requested a controlled exit";
        return OnMessage( message, 0, aUser );
    }

    static int OnBackground( NG_BOOL aFinished, int, void* aUser )
    {
        auto& self = *static_cast<ENGINE*>( aUser );
        {
            std::lock_guard lock( self.m_mutex );
            self.m_finished = aFinished;
        }
        self.m_condition.notify_all();
        return 0;
    }

    static int OnData( pvecvaluesall aData, int, int, void* aUser )
    {
        auto& self = *static_cast<ENGINE*>( aUser );
        std::lock_guard lock( self.m_mutex );
        if( !self.m_error.empty() )
            return 0;
        if( self.m_indices.empty() )
        {
            for( const auto& name : self.m_names )
            {
                int index = -1;
                for( int j = 0; j < aData->veccount; ++j )
                {
                    if( Lower( aData->vecsa[j]->name ) == name )
                        index = j;
                }
                if( index < 0 )
                {
                    self.m_error = "Signal not found: " + name;
                    return 0;
                }
                self.m_indices.push_back( index );
            }
            self.m_latest.values.resize( self.m_names.size() );
        }
        for( int j = 0; j < aData->veccount; ++j )
        {
            if( aData->vecsa[j]->is_scale )
                self.m_latest.time = aData->vecsa[j]->creal;
        }
        for( std::size_t i = 0; i < self.m_indices.size(); ++i )
        {
            auto* value = aData->vecsa[self.m_indices[i]];
            if( value->is_complex || !std::isfinite( value->creal ) )
            {
                self.m_error = "Non-finite/complex data in transient analysis";
                return 0;
            }
            self.m_latest.values[i] = value->creal;
        }
        if( self.m_dcSolving ) { self.m_latest.time = self.m_dcPointTime; return 0; }
        try
        {
            while(!self.m_programTransitions.empty()&&*self.m_programTransitions.begin()<=self.m_latest.time+1e-15)
                self.m_programTransitions.erase(self.m_programTransitions.begin());
            const auto& events = self.m_nativePeripherals.Observe( self.m_latest.time, self.m_latest.values );
            if( !events.empty() )
            {
                self.PruneProgrammableHistory( self.m_latest.time );
                self.ProgrammableEvents( events, self.m_latest.time, self.m_latest.time );
                self.m_nativeSettleDeadline = self.m_latest.time + 1e-9;
            }
            else if( self.m_latest.time + 1e-12 >= self.m_nativeSettleDeadline )
                self.m_nativeSettleDeadline = std::numeric_limits<double>::infinity();
            if(self.m_hasClockedRuntime)self.ScheduleProgrammable();
            if(!self.m_expressions.Error().empty())throw std::runtime_error(self.m_expressions.Error());
            self.m_memory.Commit(self.m_latest.time);
        }
        catch( const std::exception& e ) { self.m_error=e.what(); return 0; }
        if( !self.m_memory.Error().empty() ) { self.m_error=self.m_memory.Error(); return 0; }
        self.RecordAcceptedPoint();
        return 0;
    }

    SIM_SESSION_STATE       m_state = SIM_SESSION_STATE::STOPPED; // Control thread only.
    NATIVE_PERIPHERALS m_nativePeripherals;
    bool m_hasClockedRuntime = false;
    bool m_statelessNetwork = false;
    bool m_memorylessSteps = false;
    std::set<double> m_programTransitions;
    std::uint64_t m_statelessAttempts = 0, m_statelessExpansions = 0;
    std::vector<std::array<double, 7>> m_staticPulses;
    std::optional<std::array<double,6>> m_staticSine;
    std::set<double> m_staticSinePhases;
    double m_staticSineSamples=24;
    std::vector<std::string> m_staticSourceNames;
    std::map<std::string, std::vector<double>> m_staticSolutions;
    std::vector<double> m_historyMinimum,m_historyMaximum;
    bool m_haveHistoryExtrema=false,m_retainedHistoryValid=false;
    std::uint64_t m_historyToken=0;
    std::size_t m_retainedHead=0,m_retainedSize=0;
    bool m_dcSolving = false;
    double m_dcPointTime = 0;
    double m_nativeSettleDeadline = std::numeric_limits<double>::infinity();
    struct INPUT_BINDING { std::string device,pin;std::size_t signal; };
    struct OUTPUT_BINDING { std::string high,low,pullup; int slot=-1; };
    struct EXTERNAL_VALUE { double time,value; };
    std::vector<std::vector<EXTERNAL_VALUE>> m_programDrivers;
    PROGRAMMABLE_PIPELINE m_programmable;
    AUDIO_SAMPLER m_audio;
    COMPILED_EXPRESSIONS m_expressions;
    std::vector<INPUT_BINDING> m_programInputs;
    std::map<std::pair<std::string,std::string>,OUTPUT_BINDING> m_programOutputs;
    std::map<std::string,std::vector<EXTERNAL_VALUE>> m_external;
    std::map<const char*,const std::vector<EXTERNAL_VALUE>*> m_externalCache;
    double m_programQuantum=1e-6,m_programBoundary=0,m_programTarget=0;
    double m_analogTarget = 0;
    bool m_programPlaying=false,m_programReady=false,m_programInputRefresh=false;
    int m_engineId=0;
    RUNTIME_MEMORY m_memory;
    void* m_memoryModule = nullptr; // Kept loaded for the physical worker lifetime.
    SIM_SESSION_CONFIG      m_config;
    std::string             m_deck;
    std::vector<std::string> m_names;
    bool                    m_configured = false;
    unsigned                m_commands = 0;
    std::mutex              m_mutex;
    std::condition_variable m_condition;
    bool                    m_finished = true;
    std::string             m_error;
    SIM_SAMPLE              m_latest;
    std::vector<int>        m_indices;
    std::vector<SIM_SAMPLE> m_ring;
    std::vector<SIGNAL_ACTIVITY_TRACKER> m_activity;
    std::size_t            m_head = 0, m_size = 0;
    std::uint64_t          m_samples = 0, m_overwritten = 0, m_events = 0, m_generation = 0;
    double                 m_eventTime = 0;
};
} // namespace CIRCUITALIVE

int main()
{
    using namespace CIRCUITALIVE;
    using namespace PROTOCOL;
    std::locale::global( std::locale::classic() );
#ifdef _WIN32
    _setmode(STDIN_FILENO,_O_BINARY);_setmode(STDOUT_FILENO,_O_BINARY);
#else
    signal( SIGPIPE, SIG_IGN ); // Worker-local; never change the editor's signal handlers.

    #endif
    // Keep accidental library/code-model stdout out of the protocol channel.
    FILE* wire = fdopen( dup( STDOUT_FILENO ), "wb" );
    if( !wire || dup2( STDERR_FILENO, STDOUT_FILENO ) < 0 )
        return 2;

    // A running editor may predate a repaired launcher environment. Discover the
    // worker's adjacent runtime too, rather than depending on /usr/lib64/ngspice.
    if( const char* scripts = std::getenv( "SPICE_SCRIPTS" ); !scripts || !*scripts )
    {
        std::error_code error;
        const auto executable = ExecutablePath();

        if( !error )
        {
            const auto directory = executable.parent_path() / "ngspice/scripts";
            if( std::filesystem::is_regular_file( directory / "spinit", error ) )
                SetRuntimeEnvironment( "SPICE_SCRIPTS", PathUtf8(directory) );
        }
    }

    ENGINE engine;
    std::string input;
    char chunk[8192];
    for( ;; )
    {
        const auto count = read( STDIN_FILENO, chunk, sizeof( chunk ) );
        if( count < 0 && errno == EINTR )
            continue;
        if( count <= 0 )
            break;
        input.append( chunk, count );
        if( input.size() > MAX_MESSAGE )
            break;

        for( auto end = input.find( '\n' ); end != std::string::npos; end = input.find( '\n' ) )
        {
            JSON response;
            bool stopRequested = false;
            try
            {
                const auto request = JSON::parse( input.substr( 0, end ) );
                stopRequested = request.value( "op", "" ) == "stop";
                response = engine.Handle( request );
                response["ok"] = true;
            }
            catch( const std::exception& error )
            {
                response = { { "ok", false }, { "error", std::string( error.what() ).substr( 0, 2048 ) } };
            }
            input.erase( 0, end + 1 );
            const std::string message = response.dump() + '\n';
            if( fwrite( message.data(), 1, message.size(), wire ) != message.size()
                    || fflush( wire ) != 0 )
                goto done;
            // Stop ends this physical solver instance. The controller may start
            // a fresh worker, with the original circuit, for the next Play.
            if( stopRequested )
                std::_Exit( 0 );
        }
    }
done:
    try { engine.Stop(); } catch( ... ) {}
    // ngspice owns helper threads; OS teardown avoids C++ callback lifetime races at exit.
    std::_Exit( 0 );
}
