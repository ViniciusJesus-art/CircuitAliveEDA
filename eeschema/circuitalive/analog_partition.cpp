/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "analog_partition.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <future>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace CIRCUITALIVE
{
namespace
{
std::string lower( std::string text )
{
    for( auto& c : text ) c = std::tolower( static_cast<unsigned char>( c ) );
    return text;
}
struct UNION
{
    std::vector<std::size_t> parents;
    explicit UNION( std::size_t size ) : parents( size ) { std::iota( parents.begin(), parents.end(), 0 ); }
    std::size_t Root( std::size_t i ) { return parents[i] == i ? i : parents[i] = Root( parents[i] ); }
    void Join( std::size_t a, std::size_t b ) { parents[Root( b )] = Root( a ); }
};
struct ELEMENT
{
    std::string line, name, model;
    std::size_t offset;
    std::vector<std::string> nodes, branches;
    bool fixed = false;
};
struct OBSERVATION { bool current = false; std::vector<std::string> nodes, branches; };
OBSERVATION observation( const std::string& value )
{
    const auto text = lower( value );
    OBSERVATION result;
    if( text.starts_with( "v(" ) && text.back() == ')' )
    {
        auto ports = text.substr( 2, text.size() - 3 );
        std::replace( ports.begin(), ports.end(), ',', ' ' );
        std::istringstream in( ports ); std::string node;
        while( in >> node ) result.nodes.push_back( node );
    }
    else if( text.starts_with( "i(" ) && text.back() == ')' )
    { result.current = true; result.branches.push_back( text.substr( 2, text.size() - 3 ) ); }
    else if( text.starts_with( '@' ) && text.find( '[' ) != std::string::npos )
        result.branches.push_back( text.substr( 1, text.find( '[' ) - 1 ) );
    else throw std::runtime_error( "Unsupported partition observation" );
    return result;
}
}

std::vector<ANALOG_PARTITION> BuildAnalogPartitions( const std::string& netlist,
        const SIM_SESSION_CONFIG& config, unsigned maximum )
{
    auto original = [&]
    {
        ANALOG_PARTITION part; part.netlist = netlist; part.config = config;
        part.signals.resize( config.signals.size() ); std::iota( part.signals.begin(), part.signals.end(), 0 );
        part.activities.resize( config.activityMonitors.size() ); std::iota( part.activities.begin(), part.activities.end(), 0 );
        for( const auto& memory : config.memories ) part.memories.insert( memory.id );
        return std::vector<ANALOG_PARTITION>{ std::move( part ) };
    };
    if( !config.partitionable || !config.mutableElements.empty() ) return original();
    const bool automatic = !maximum;
    if( !maximum ) maximum = std::min( 4u, std::max( 1u, std::thread::hardware_concurrency() > 2
            ? std::thread::hardware_concurrency() - 2 : 1 ) );
    if( maximum < 2 ) return original();
    try
    {
        std::vector<ELEMENT> elements;
        std::vector<std::string> declarations;
        std::map<std::string, std::string> models;
        std::map<std::string, std::size_t> memorySlots;
        std::string title, line;
        std::istringstream input( netlist ); std::getline( input, title );
        std::size_t offset = title.size() + 1;
        const std::regex reads( R"(\b([vi])\s*\(\s*([^()]+)\))", std::regex::icase );
        const std::regex slot( R"(\bslot\s*=\s*([0-9]+))", std::regex::icase );
        while( std::getline( input, line ) )
        {
            const auto here = offset; offset += line.size() + 1;
            std::istringstream words( lower( line ) ); std::vector<std::string> fields; std::string field;
            while( words >> field ) fields.push_back( field );
            if( fields.empty() || fields[0][0] == '*' ) continue;
            if( line.find( '@' ) != std::string::npos ) return original();
            if( fields[0][0] == '.' )
            {
                if( fields[0] == ".end" ) continue;
                if( fields[0] != ".model" && fields[0] != ".param" && fields[0] != ".options" ) return original();
                if( std::regex_search( line, reads ) ) return original();
                declarations.push_back( line );
                if( fields[0] == ".model" && fields.size() >= 3 )
                {
                    models[fields[1]] = fields[2];
                    std::smatch match;
                    if( fields[2].starts_with( "ca_memory" ) )
                    {
                        if( !std::regex_search( line, match, slot ) ) return original();
                        memorySlots[fields[1]] = std::stoul( match[1] );
                    }
                }
                continue;
            }
            elements.push_back( { line, fields[0], {}, here, {}, {}, false } );
        }
        // First collect every declaration: a BJT's optional fourth terminal is
        // distinguished from its model name, independent of declaration order.
        for( auto& element : elements )
        {
            std::istringstream words( lower( element.line ) );
            std::vector<std::string> f; std::string word; while( words >> word ) f.push_back( word );
            const char type = element.name[0]; std::size_t count = 0;
            switch( type )
            {
            case 'r': case 'l': case 'c': case 'v': case 'i': case 'b': case 'd': count = 2; break;
            case 'q': count = f.size() > 4 && models.contains( f[4] ) ? 3 : 4; break;
            case 'j': count = 3; break;
            case 'm': case 's': case 't': count = 4; break;
            case 'e': case 'g': count = 4; break;
            case 'f': case 'h': case 'w':
                count = 2; if( f.size() < 4 ) return original(); element.branches.push_back( f[3] ); break;
            case 'k':
                if( f.size() != 4 ) return original();
                element.branches = { f[1], f[2] }; break;
            case 'a':
            {
                if( f.size() < 3 || !models.contains( f.back() ) ) return original();
                element.model = f.back();
                auto ports = lower( element.line.substr( element.line.find_first_of( " \t" ) ) );
                ports.resize( ports.rfind( element.model ) );
                ports = std::regex_replace( ports, std::regex( "%[a-z]+" ), " " );
                for( auto& c : ports ) if( c == '[' || c == ']' || c == '(' || c == ')' || c == ',' || c == '~' ) c = ' ';
                std::istringstream pins( ports );
                while( pins >> word ) if( word != "null" ) element.nodes.push_back( word );
                break;
            }
            default: return original();
            }
            if( count )
            {
                if( f.size() <= count ) return original();
                element.nodes.assign( f.begin() + 1, f.begin() + 1 + count );
                // Special VALUE/POLY forms require a richer grammar. Keep their solver whole.
                for( const auto& node : element.nodes )
                    if( node.find_first_of( "={}()" ) != std::string::npos ) return original();
            }
            for( std::sregex_iterator it( element.line.begin(), element.line.end(), reads ), end; it != end; ++it )
            {
                auto read = observation( lower( ( *it )[1].str() ) + "(" + ( *it )[2].str() + ")" );
                element.nodes.insert( element.nodes.end(), read.nodes.begin(), read.nodes.end() );
                element.branches.insert( element.branches.end(), read.branches.begin(), read.branches.end() );
            }
            if( type == 'v' && ( f.size() == 4 || ( f.size() == 5 && f[3] == "dc" ) )
                && ( f[1] == "0" ) != ( f[2] == "0" ) )
            {
                char* end = nullptr; double value = std::strtod( f.back().c_str(), &end );
                element.fixed = end && !*end && std::isfinite( value );
            }
        }
        std::map<std::string, std::size_t> byName;
        for( std::size_t i = 0; i < elements.size(); ++i ) if( !byName.emplace( elements[i].name, i ).second ) return original();
        // A dependent source observing the total rail current couples every
        // load. It must never receive the current of only one replicated source.
        for( const auto& element : elements ) for( const auto& branch : element.branches )
        {
            if( !byName.contains( branch ) ) return original();
            elements[byName.at( branch )].fixed = false;
        }
        for( const auto& ref : config.mutableElements )
            if( byName.contains( lower( ref ) ) ) elements[byName.at( lower( ref ) )].fixed = false;
        std::set<std::string> fixedNodes{ "0" };
        std::map<std::string, std::size_t> fixedSources;
        for( std::size_t i = 0; i < elements.size(); ++i ) if( elements[i].fixed )
        { fixedSources[elements[i].name] = i; fixedNodes.insert( elements[i].nodes.begin(), elements[i].nodes.end() ); }
        UNION groups( elements.size() );
        std::map<std::string, std::size_t> nodes;
        auto joinNode = [&]( const std::string& node, std::size_t i )
        {
            if( fixedNodes.contains( node ) ) return;
            auto [it, inserted] = nodes.emplace( node, i ); if( !inserted ) groups.Join( i, it->second );
        };
        std::map<std::size_t, std::size_t> memoryOwners;
        for( std::size_t i = 0; i < elements.size(); ++i ) if( !elements[i].fixed )
        {
            for( const auto& node : elements[i].nodes ) joinNode( node, i );
            for( const auto& branch : elements[i].branches ) groups.Join( i, byName.at( branch ) );
            if( memorySlots.contains( elements[i].model ) )
            {
                auto [it, inserted] = memoryOwners.emplace( memorySlots.at( elements[i].model ), i );
                if( !inserted ) groups.Join( i, it->second );
            }
        }
        for( const auto& [begin, end] : config.analogCouplings )
        {
            if( begin > end || end > netlist.size() ) return original();
            std::optional<std::size_t> first;
            for( std::size_t i = 0; i < elements.size(); ++i )
                if( !elements[i].fixed && elements[i].offset >= begin && elements[i].offset < end )
                { if( first ) groups.Join( *first, i ); else first = i; }
        }
        std::vector<OBSERVATION> observations;
        auto owners = [&]( const OBSERVATION& read )
        {
            std::vector<std::size_t> result;
            for( const auto& node : read.nodes )
            {
                if( nodes.contains( node ) ) result.push_back( nodes.at( node ) );
                else if( !fixedNodes.contains( node ) ) throw std::runtime_error( "Unbound partition node" );
            }
            for( const auto& branch : read.branches )
            {
                if( !byName.contains( branch ) ) throw std::runtime_error( "Unbound partition branch" );
                if( !fixedSources.contains( branch ) ) result.push_back( byName.at( branch ) );
            }
            return result;
        };
        auto unite = [&]( const std::vector<std::size_t>& values )
        { for( std::size_t n = 1; n < values.size(); ++n ) groups.Join( values[0], values[n] ); };
        for( const auto& signal : config.signals )
        { observations.push_back( observation( signal ) ); unite( owners( observations.back() ) ); }
        for( const auto& monitor : config.activityMonitors )
        {
            std::vector<std::size_t> related;
            for( auto index : { monitor.signal, monitor.reference, monitor.supply } ) if( index != ACTIVITY_NO_SIGNAL )
            {
                const auto own = owners( observations.at( index ) ); related.insert( related.end(), own.begin(), own.end() );
                // Total-current activity needs one solver's original step grid.
                if( observations[index].current && own.empty() ) return original();
            }
            unite( related );
        }
        std::vector<std::vector<std::size_t>> runtimeOwners;
        for( const auto& device : config.programmableDevices )
        {
            std::vector<std::size_t> related;
            for( const auto& pin : device.bindings )
            {
                if( !pin.signal.empty() )
                { const auto own = owners( observation( pin.signal ) ); related.insert( related.end(), own.begin(), own.end() ); }
                for( const auto& source : { pin.highSource, pin.lowSource, pin.pullupSource } ) if( !source.empty() )
                { if( !byName.contains( lower( source ) ) ) return original(); related.push_back( byName.at( lower( source ) ) ); }
            }
            if( related.empty() ) return original();
            unite( related ); runtimeOwners.push_back( std::move( related ) );
        }
        std::map<std::size_t, std::vector<std::size_t>> islands;
        for( std::size_t i = 0; i < elements.size(); ++i ) if( !elements[i].fixed ) islands[groups.Root( i )].push_back( i );
        if( islands.size() < 2 ) return original();
        std::vector<std::vector<std::size_t>> ordered;
        for( auto& [root, members] : islands ) ordered.push_back( std::move( members ) );
        std::stable_sort( ordered.begin(), ordered.end(), []( const auto& a, const auto& b ) { return a.size() > b.size(); } );
        // A process for an isolated bypass capacitor costs more IPC than its
        // solver work. Pack small islands into the existing substantial jobs.
        const auto substantial = std::count_if( ordered.begin(), ordered.end(),
                []( const auto& island ) { return island.size() >= 16; } );
        const auto count = std::min<std::size_t>( maximum, automatic
                ? std::max<std::size_t>( 1, substantial ) : ordered.size() );
        if( count < 2 ) return original();
        std::vector<ANALOG_PARTITION> parts( count );
        std::vector<std::size_t> loads( count ), assignment( elements.size(), 0 );
        for( const auto& island : ordered )
        {
            const auto bin = std::min_element( loads.begin(), loads.end() ) - loads.begin();
            loads[bin] += island.size(); for( auto index : island ) assignment[index] = bin;
        }
        auto destinations = [&]( const OBSERVATION& read )
        {
            const auto own = owners( read );
            if( !own.empty() ) return std::vector<std::size_t>{ assignment[own[0]] };
            std::vector<std::size_t> all( count ); std::iota( all.begin(), all.end(), 0 ); return all;
        };
        for( std::size_t p = 0; p < count; ++p )
        {
            auto& part = parts[p]; part.config = config;
            part.config.signals.clear(); part.config.activityMonitors.clear(); part.config.programmableDevices.clear();
            part.config.partitionable = false; part.config.analogCouplings.clear();
            part.config.synchronizeAdvances = true;
            if( config.historySignals ) part.config.historySignals = std::vector<std::size_t>{};
            part.netlist = title + " (partition " + std::to_string( p + 1 ) + ")\n";
            for( std::size_t i = 0; i < elements.size(); ++i ) if( elements[i].fixed || assignment[i] == p )
            {
                part.netlist += elements[i].line + '\n'; part.elements.insert( elements[i].name );
                if( memorySlots.contains( elements[i].model ) )
                    part.memories.insert( config.memories.at( memorySlots.at( elements[i].model ) ).id );
            }
            for( const auto& declaration : declarations ) part.netlist += declaration + '\n';
            for( const auto& [model, type] : models ) part.models.insert( model );
            part.netlist += ".end\n";
        }
        for( std::size_t n = 0; n < observations.size(); ++n ) for( auto p : destinations( observations[n] ) )
        {
            auto& part = parts[p];
            if( config.historySignals && std::find( config.historySignals->begin(), config.historySignals->end(), n ) != config.historySignals->end() )
                part.config.historySignals->push_back( part.signals.size() );
            part.signals.push_back( n ); part.config.signals.push_back( config.signals[n] );
        }
        for( std::size_t n = 0; n < config.activityMonitors.size(); ++n )
        {
            const auto& monitor = config.activityMonitors[n];
            std::size_t p = monitor.signal == ACTIVITY_NO_SIGNAL ? 0
                    : destinations( observations.at( monitor.signal ) )[0];
            for( auto index : { monitor.signal, monitor.reference, monitor.supply } ) if( index != ACTIVITY_NO_SIGNAL )
            {
                const auto own = owners( observations.at( index ) );
                if( !own.empty() ) { p = assignment[own[0]]; break; }
            }
            auto local = monitor;
            for( auto* index : { &local.signal, &local.reference, &local.supply } ) if( *index != ACTIVITY_NO_SIGNAL )
            {
                const auto it = std::find( parts[p].signals.begin(), parts[p].signals.end(), *index );
                if( it == parts[p].signals.end() ) return original();
                *index = it - parts[p].signals.begin();
            }
            parts[p].config.activityMonitors.push_back( local ); parts[p].activities.push_back( n );
        }
        for( std::size_t n = 0; n < runtimeOwners.size(); ++n )
            parts[assignment[runtimeOwners[n][0]]].config.programmableDevices.push_back( config.programmableDevices[n] );
        // Every worker needs at least one observed vector for the protocol.
        for( auto& part : parts ) if( part.config.signals.empty() ) return original();
        return parts;
    }
    catch( const std::exception& ) { return original(); }
}

struct ANALOG_SIM_SESSION::IMPL
{
    struct WORKER
    {
        ANALOG_PARTITION part;
        INTERACTIVE_SIM_SESSION session;
        SIM_SESSION_SNAPSHOT snapshot;
        SIM_SAMPLE anchor;
        std::vector<std::size_t> historyColumns;
        std::thread thread;
        std::mutex mutex;
        std::condition_variable wake;
        std::function<void()> task;
        bool quit = false;
        WORKER( ANALOG_PARTITION p, const std::string& path, std::chrono::milliseconds timeout ) :
                part( std::move( p ) ), session( path, timeout ), thread( [this]
                {
                    std::unique_lock lock( mutex );
                    for( ;; )
                    {
                        wake.wait( lock, [&] { return quit || bool( task ); } );
                        if( quit ) return;
                        auto work = std::move( task ); lock.unlock(); work(); lock.lock();
                    }
                } ) {}
        ~WORKER()
        { { std::lock_guard lock( mutex ); quit = true; } wake.notify_one(); thread.join(); }
        std::future<void> Submit( std::function<void()> work )
        {
            auto job = std::make_shared<std::packaged_task<void()>>( std::move( work ) ); auto result = job->get_future();
            { std::lock_guard lock( mutex ); task = [job] { ( *job )(); }; } wake.notify_one(); return result;
        }
    };
    std::string path;
    std::chrono::milliseconds timeout;
    SIM_SESSION_CONFIG config;
    std::vector<bool> additive;
    std::uint64_t sourceCopies = 0;
    std::vector<std::unique_ptr<WORKER>> workers;
    bool failed = false;
    void Each( const std::function<void( WORKER& )>& operation )
    {
        if( workers.empty() ) throw std::runtime_error( "Simulation is not configured" );
        if( failed ) throw std::runtime_error( "Reconfigure the partitioned simulation after a failure" );
        if( workers.size() == 1 )
        {
            try { operation( *workers[0] ); } catch( ... ) { failed = true; throw; }
            return;
        }
        std::vector<std::future<void>> pending;
        for( auto& worker : workers ) pending.push_back( worker->Submit( [&, w = worker.get()] { operation( *w ); } ) );
        std::exception_ptr error;
        for( auto& future : pending ) try { future.get(); } catch( ... ) { if( !error ) error = std::current_exception(); }
        if( error ) { failed = true; std::rethrow_exception( error ); }
    }
};

ANALOG_SIM_SESSION::ANALOG_SIM_SESSION( std::string worker, std::chrono::milliseconds timeout ) : m_impl( std::make_unique<IMPL>() )
{ m_impl->path = std::move( worker ); m_impl->timeout = timeout; }
ANALOG_SIM_SESSION::~ANALOG_SIM_SESSION() = default;
std::size_t ANALOG_SIM_SESSION::PartitionCount() const { return m_impl->workers.size(); }
void ANALOG_SIM_SESSION::Configure( const std::string& netlist, const SIM_SESSION_CONFIG& config, bool partition, unsigned maximum )
{
    auto& self = *m_impl; self.workers.clear(); self.config = config; self.failed = false;
    self.sourceCopies = 0;
    self.additive.clear();
    auto cfg = config; if( !partition ) cfg.partitionable = false;
    for( auto& part : BuildAnalogPartitions( netlist, cfg, maximum ) )
    {
        auto worker = std::make_unique<IMPL::WORKER>( std::move( part ), self.path, self.timeout );
        for( std::size_t n = 0; n < worker->part.signals.size(); ++n )
            if( !config.historySignals || std::find( config.historySignals->begin(), config.historySignals->end(),
                    worker->part.signals[n] ) != config.historySignals->end() ) worker->historyColumns.push_back( n );
        self.workers.push_back( std::move( worker ) );
    }
    if( self.workers.size() > 1 )
        for( const auto& signal : config.signals ) self.additive.push_back( observation( signal ).current );
    self.Each( []( auto& w ) { w.session.Configure( w.part.netlist, w.part.config ); } );
}
void ANALOG_SIM_SESSION::AdvanceTo( double time )
{ m_impl->Each( [time]( auto& w ) { w.session.AdvanceTo( time ); } ); }
void ANALOG_SIM_SESSION::SetSourceValue( const std::string& reference, double value,
        const std::vector<SIM_PARAMETER_CHANGE>& changes )
{
    auto& self = *m_impl;
    if( self.workers.size() == 1 )
    { self.Each( [&]( auto& w ) { w.session.SetSourceValue( reference, value, changes ); } ); return; }
    std::size_t found = 0;
    for( const auto& w : self.workers ) found += w->part.elements.contains( lower( reference ) );
    if( !found ) throw std::runtime_error( "Unknown simulation source: " + reference );
    // Apply every affected change at the same paused frontier before any solver resumes.
    self.Each( [&]( auto& w )
    {
        std::vector<SIM_PARAMETER_CHANGE> local;
        for( const auto& change : changes ) if( change.model ? w.part.models.contains( lower( change.reference ) )
                                                           : w.part.elements.contains( lower( change.reference ) ) ) local.push_back( change );
        if( w.part.elements.contains( lower( reference ) ) ) w.session.SetSourceValue( reference, value, local );
        else if( !local.empty() ) throw std::runtime_error( "Cross-partition live parameter dependency" );
    } );
    self.sourceCopies += found - 1;
}
MEMORY_SNAPSHOT ANALOG_SIM_SESSION::ReadMemory( const std::string& id, std::size_t offset, std::size_t length )
{
    if( m_impl->failed ) throw std::runtime_error( "Reconfigure the partitioned simulation after a failure" );
    for( const auto& w : m_impl->workers ) if( w->part.memories.contains( id ) ) return w->session.ReadMemory( id, offset, length );
    throw std::runtime_error( "Unknown simulation memory: " + id );
}
SIM_SESSION_SNAPSHOT ANALOG_SIM_SESSION::Snapshot( bool drain )
{
    auto& self = *m_impl;
    self.Each( [drain]( auto& w ) { w.snapshot = w.session.Snapshot( drain ); } );
    if( self.workers.size() == 1 ) return self.workers[0]->snapshot;
    SIM_SESSION_SNAPSHOT result = self.workers[0]->snapshot; result.history.clear();
    if( result.latest.values.empty() ) return result;
    result.latest.values.assign( self.config.signals.size(), 0 ); result.activity.assign( self.config.activityMonitors.size(), {} );
    result.samplesReceived = result.samplesOverwritten = result.eventsApplied = result.residentBytes = result.engineRetainedPoints = 0;
    const auto& additive = self.additive;
    std::vector<double> times;
    for( const auto& w : self.workers )
    {
        const auto& s = w->snapshot;
        if( s.state == SIM_SESSION_STATE::FAILED ) { self.failed = true; throw std::runtime_error( s.error ); }
        const double tolerance = std::max( 2e-11, std::abs( s.latest.time ) * 32 * std::numeric_limits<double>::epsilon() );
        if( std::abs( result.latest.time - s.latest.time ) > tolerance )
        { self.failed = true; throw std::runtime_error( "Analog partitions did not reach a common time" ); }
        for( std::size_t n = 0; n < w->part.signals.size(); ++n )
        {
            auto index = w->part.signals[n];
            if( additive[index] ) result.latest.values[index] += s.latest.values.at( n );
            else result.latest.values[index] = s.latest.values.at( n );
        }
        for( std::size_t n = 0; n < w->part.activities.size(); ++n ) result.activity[w->part.activities[n]] = s.activity.at( n );
        result.samplesReceived += s.samplesReceived; result.samplesOverwritten += s.samplesOverwritten;
        result.eventsApplied += s.eventsApplied; result.lastEventTime = std::max( result.lastEventTime, s.lastEventTime );
        result.engineRetainedPoints += s.engineRetainedPoints; result.residentBytes += s.residentBytes;
        for( const auto& sample : s.history ) times.push_back( sample.time );
    }
    result.eventsApplied -= self.sourceCopies;
    std::sort( times.begin(), times.end() ); times.erase( std::unique( times.begin(), times.end() ), times.end() );
    // Only observations are resampled, never solver inputs. A channel retains
    // every original local timestamp; other islands are interpolated there.
    // This also reconstructs the total current of replicated ideal supplies.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for( double time : times )
    {
        SIM_SAMPLE sample{ time, std::vector<double>( self.config.signals.size(), nan ) };
        for( const auto& w : self.workers )
        {
            const auto& history = w->snapshot.history;
            auto next = std::lower_bound( history.begin(), history.end(), time,
                    []( const auto& point, double t ) { return point.time < t; } );
            const SIM_SAMPLE* a = nullptr; const SIM_SAMPLE* b = nullptr;
            if( next != history.end() ) b = &*next;
            else b = &w->snapshot.latest;
            if( next != history.begin() ) a = &*std::prev( next );
            for( auto n : w->historyColumns )
            {
                const auto index = w->part.signals[n];
                double value = b->values.at( n );
                if( a && b->time > a->time ) value = std::lerp( a->values.at( n ), value,
                        std::clamp( ( time - a->time ) / ( b->time - a->time ), 0., 1. ) );
                else if( !a && !w->anchor.values.empty() && time >= w->anchor.time && b->time > w->anchor.time )
                    value = std::lerp( w->anchor.values.at( n ), value,
                            ( time - w->anchor.time ) / ( b->time - w->anchor.time ) );
                if( additive[index] && std::isfinite( sample.values[index] ) ) sample.values[index] += value;
                else sample.values[index] = value;
            }
        }
        result.history.push_back( std::move( sample ) );
    }
    // The union is bounded by workers * local ring capacity (at most four
    // workers by default). Preserve the local points rather than discarding
    // edges from earlier in the batch to force a single worker's sample count.
    if( drain ) for( auto& w : self.workers ) w->anchor = w->snapshot.latest;
    return result;
}
}
