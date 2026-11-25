/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/analog_partition.h>
#include <circuitalive/circuit_snapshot.h>
#include <circuitalive/simulation_model.h>
#include <circuitalive/model_design.h>
#include <circuitalive/visual_signals.h>
#include <sim/audio_sampler.h>
#include <sim/platform_runtime.h>
#include <sim/programmable_runtime.h>
#include <sim/native_peripherals.h>
#include <circuitalive/source_probe.h>
#include <circuitalive/control_references.h>
#include <circuitalive/session_runner.h>
#include <circuitalive/segment_json.h>
#include <circuitalive/luminous_geometry.h>
#include <circuitalive/model_viewport.h>
#include <chrono>
#include <thread>
#include <nlohmann/json.hpp>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <functional>
using namespace CIRCUITALIVE;
namespace
{
CIRCUIT_COMPONENT Output(const std::string& id,const std::string& ref)
{
    CIRCUIT_COMPONENT c{ref,"CircuitAlive_Simulation:"+id,id,{}};c.modelFile="builtin:"+id;
    const auto model=MODEL_REPOSITORY().SelectModel(c);
    for(const auto& p:model.pins)c.pins[p.defaultPin]="0";
    return c;
}
CIRCUIT_COMPONENT Supply(const std::string& ref,const std::string& net,double voltage)
{return {ref,"Simulation_SPICE:VDC",std::to_string(voltage),{{"1",net},{"2","0"}}};}
SIM_SESSION_SNAPSHOT RunOutput(const CIRCUIT_SNAPSHOT& c,double time)
{
    INTERACTIVE_SIM_SESSION s(CIRCUITALIVE_TEST_WORKER);s.Configure(c.netlist,c.config);s.AdvanceTo(time);return s.Snapshot();
}
CIRCUIT_SNAPSHOT InfraredClock(unsigned receivers=1,bool capacitor=false,PROBE_KIND kind=PROBE_KIND::SQUARE)
{
    SOURCE_PROBE_SETTINGS clock;clock.frequency=38000;
    auto led=Output("ir_led","D1");led.pins={{"1","0"},{"2","anode"}};led.identity="emitter";
    std::vector<CIRCUIT_COMPONENT> parts={Supply("VCC","vcc",3.3),
        SourceProbeComponent(kind,clock,"#CLK","carrier"),
        {"R1","Device:R","220",{{"1","carrier"},{"2","anode"}}}};
    for(unsigned i=0;i<receivers;++i)led.linkedComponents.push_back("receiver"+std::to_string(i));
    parts.push_back(led);
    for(unsigned i=0;i<receivers;++i)
    {
        auto receiver=Output("ir_receiver_tsop","U"+std::to_string(i+1));
        receiver.modelParameters["sample_period"]="1u"; // Old saved settings remain loadable.
        receiver.pins={{"1","out"+std::to_string(i)},{"2","0"},{"3","vcc"}};
        receiver.identity="receiver"+std::to_string(i);receiver.linkedComponents={"emitter"};parts.push_back(receiver);
    }
    if(capacitor)parts.push_back({"C1","Device:C","1u",{{"1","out0"},{"2","0"}}});
    auto c=BuildCircuitSnapshot(parts);c.config.historyCapacity=128;return c;
}
}
BOOST_AUTO_TEST_SUITE(SimulationOutputs)
BOOST_AUTO_TEST_CASE(ModelViewportKeepsThePointerAnchorAndPansWithoutDocumentBounds)
{
    MODEL_VIEWPORT view;view.offset={-1300,700};
    const VECTOR2D pointer(623,197);const auto world=view.ToWorld(pointer);
    for(double factor:{1.2,1.2,.5,1000.,.00001,2.})
    {
        view.ZoomAt(factor,pointer);
        BOOST_CHECK_SMALL((view.ToWorld(pointer)-world).EuclideanNorm(),1e-8);
    }
    const auto before=view.ToWorld(pointer);view.offset+=VECTOR2D(4000,-9000);
    BOOST_CHECK_SMALL((view.ToWorld(pointer)-before-VECTOR2D(-4000,9000)/view.scale).EuclideanNorm(),1e-8);
}
BOOST_AUTO_TEST_CASE(LuminousEndpointsKeepArbitraryAnglesThicknessAndModelRoundTrips)
{
    const BOX2I body(VECTOR2I(200,300),VECTOR2I(1200,600));
    MODEL_SEGMENT s;s.id="free";s.signal="v(${pin:p})";s.shape="line";
    s.geometry=SEGMENT_GEOMETRY{.12,.81,.91,.27,.025,false};s.red=.2;s.green=.7;s.blue=.4;
    const auto g=LuminousGeometry(body,s);
    BOOST_CHECK_SMALL((g.start-VECTOR2D(344,786)).EuclideanNorm(),1e-9);
    BOOST_CHECK_SMALL((g.end-VECTOR2D(1292,462)).EuclideanNorm(),1e-9);
    BOOST_CHECK_CLOSE(g.thickness,15,1e-9);
    BOOST_CHECK(HitLuminousSegment(g,(g.start+g.end)/2,2));
    BOOST_CHECK(!HitLuminousSegment(g,g.start+VECTOR2D(0,100),2));
    const auto roundTrip=ReadLuminousSegment(WriteLuminousSegment(s));
    BOOST_CHECK_EQUAL(WriteLuminousSegment(s).dump(),WriteLuminousSegment(roundTrip).dump());
    auto r=Output("resistor","R1");std::ifstream in(MODEL_REPOSITORY().SelectModel(r).file);
    auto json=nlohmann::json::parse(in);
    json["segments"]=nlohmann::json::array({WriteLuminousSegment(s)});
    BOOST_REQUIRE(json["segments"][0].is_object());
    const auto exported=MODEL_DESIGN::Open(json.dump()).Export();
    const auto model=MODEL_REPOSITORY::LoadText(exported);
    BOOST_REQUIRE(model.segments[0].geometry);BOOST_CHECK_EQUAL(model.segments[0].shape,"line");
    BOOST_CHECK_CLOSE(model.segments[0].geometry->y2,.27,1e-9);
    auto moved=s;moved.geometry->x1=.9;moved.geometry->y1=.2;moved.geometry->x2=.1;moved.geometry->y2=.5;
    BOOST_CHECK_CLOSE(LuminousGeometry(body,moved).thickness,g.thickness,1e-9);
    for(const auto& axes:std::vector<std::pair<VECTOR2I,VECTOR2I>>{{{0,1},{-1,0}},{{-1,0},{0,1}}})
    {
        const auto transformed=LuminousGeometry(body,s,axes.first,axes.second);
        BOOST_CHECK_CLOSE(transformed.thickness,g.thickness,1e-9);
    }
    s.shape="circle";s.geometry->x1=.3;s.geometry->y1=.4;s.geometry->x2=.5;s.geometry->y2=.4;
    const auto circle=LuminousGeometry(body,s);
    BOOST_CHECK(!HitLuminousSegment(circle,(circle.start+circle.end)/2,1)); // An outlined circle has an empty center.
    BOOST_CHECK(HitLuminousSegment(circle,circle.start,1));
    json["segments"][0]["geometry"]["thickness"]=0;
    BOOST_CHECK_THROW(MODEL_REPOSITORY::LoadText(json.dump()),std::runtime_error);
    json["segments"][0]["geometry"]["thickness"]=.02;json["segments"][0]["geometry"]["start"]={11,0};
    BOOST_CHECK_THROW(MODEL_REPOSITORY::LoadText(json.dump()),std::runtime_error);
}
BOOST_AUTO_TEST_CASE(LegacyLuminousShapesMigrateWithoutMovingOrChangingThickness)
{
    for(const auto& body:std::vector<BOX2I>{{{200,300},{1200,600}},{{-500,-600},{300,1500}}})
    for(const char* shape:{"horizontal","vertical","slash","backslash","dot"})
    {
        MODEL_SEGMENT old;old.shape=shape;old.visual={true,.2,.3,.4,.08};
        const auto before=LuminousGeometry(body,old);
        const auto migrated=EditableLuminousSegment(old,body);const auto after=LuminousGeometry(body,migrated);
        BOOST_REQUIRE(migrated.geometry);
        BOOST_CHECK_SMALL((before.start-after.start).EuclideanNorm(),1e-8);
        BOOST_CHECK_SMALL((before.end-after.end).EuclideanNorm(),1e-8);
        BOOST_CHECK_CLOSE(before.thickness,after.thickness,1e-9);
        BOOST_CHECK_EQUAL(before.circle,after.circle);
    }
}
BOOST_AUTO_TEST_CASE(NativeInfraredClockMatchesTransientSolverAndPreservesCapacitorDynamics)
{
    for(bool capacitor:{false,true})
    {
        auto c=InfraredClock(1,capacitor);INTERACTIVE_SIM_SESSION fast(CIRCUITALIVE_TEST_WORKER),reference(CIRCUITALIVE_TEST_WORKER);
        fast.Configure(c.netlist,c.config);c.config.accelerateStateless=false;reference.Configure(c.netlist,c.config);
        const auto binding=std::find_if(c.config.programmableDevices[0].bindings.begin(),c.config.programmableDevices[0].bindings.end(),[](const auto& p){return p.id=="OUT";});
        const auto out=std::find(c.config.signals.begin(),c.config.signals.end(),binding->signal)-c.config.signals.begin();
        for(int i=0;i<80;++i)
        {
            const double time=(i+.137)*.0005;
            fast.AdvanceTo(time);reference.AdvanceTo(time);
            const auto a=fast.Snapshot().latest,b=reference.Snapshot().latest;
            BOOST_REQUIRE_EQUAL(a.values.size(),b.values.size());
            for(std::size_t j=0;j<a.values.size();++j)
            {
                const bool current=c.config.signals[j].starts_with("i(")||c.config.signals[j].starts_with('@');
                BOOST_CHECK_SMALL(a.values[j]-b.values[j],current?2e-5:.006);
            }
            if(!capacitor&&i>0&&i<38)BOOST_CHECK_LT(a.values[out],.15);
            if(i>42)BOOST_CHECK_GT(a.values[out],3.0);
        }
    }
    auto c=InfraredClock(1,true);INTERACTIVE_SIM_SESSION s(CIRCUITALIVE_TEST_WORKER);s.Configure(c.netlist,c.config);s.AdvanceTo(1e-6);
    const auto binding=std::find_if(c.config.programmableDevices[0].bindings.begin(),c.config.programmableDevices[0].bindings.end(),[](const auto& p){return p.id=="OUT";});
    const auto out=std::find(c.config.signals.begin(),c.config.signals.end(),binding->signal)-c.config.signals.begin();
    BOOST_CHECK_LT(s.Snapshot().latest.values[out],2.0); // A capacitor must not jump to its powered DC solution.
}

BOOST_AUTO_TEST_CASE(NativeInfraredSumsEmittersRejectsDCAndDropsOutputWithoutPower)
{
    PROGRAMMABLE_DEVICE_CONFIG c;c.id="IR";c.engine="native";c.device="ir_demodulator";
    c.bindings={{"VCC","vcc"},{"GND","gnd"},{"IR","first"},{"IR_2","second"},{"OUT","out","","","",0}};
    NATIVE_PERIPHERALS r;r.Configure({c},{"vcc","gnd","first","second","out"});
    std::vector<double> inputs={8.3,5,0,0,0};int drive=-1;
    auto sample=[&](double time){for(const auto& e:r.Observe(time,inputs))drive=int(e.drive);};
    sample(0);BOOST_CHECK_EQUAL(drive,2);BOOST_CHECK(std::isinf(r.Deadline()));
    const double period=1./38000;
    for(int i=0;i<8;++i){inputs[2]=inputs[3]=.0006;sample(.001+i*period);inputs[2]=inputs[3]=0;sample(.001+(i+.4)*period);}
    BOOST_CHECK_EQUAL(drive,1);sample(r.Deadline());BOOST_CHECK_EQUAL(drive,2);
    inputs[2]=.01;sample(.01);sample(r.Deadline());BOOST_CHECK_EQUAL(drive,2); // DC is not carrier.
    inputs[0]=7;sample(.02);BOOST_CHECK_EQUAL(drive,0);
}

BOOST_AUTO_TEST_CASE(NativeInfraredCacheTracksLivePwmIncludingZeroAndFullDuty)
{
    auto c=InfraredClock(1,false,PROBE_KIND::PWM);
    INTERACTIVE_SIM_SESSION fast(CIRCUITALIVE_TEST_WORKER),reference(CIRCUITALIVE_TEST_WORKER);
    fast.Configure(c.netlist,c.config);c.config.accelerateStateless=false;reference.Configure(c.netlist,c.config);
    BOOST_REQUIRE_EQUAL(c.adjustments.size(),1u);
    const auto& control=c.adjustments[0];
    const auto binding=std::find_if(c.config.programmableDevices[0].bindings.begin(),c.config.programmableDevices[0].bindings.end(),[](const auto& p){return p.id=="OUT";});
    const auto out=std::find(c.config.signals.begin(),c.config.signals.end(),binding->signal)-c.config.signals.begin();
    int frame=0;
    for(double duty:{.5,0.,1.,.5})
    BOOST_TEST_CONTEXT("duty="<<duty)
    {
        if(frame)
        {
            const auto changes=LiveParameterChanges(c,{{control.source,duty}},control.source);
            fast.SetSourceValue(control.source,duty,changes);reference.SetSourceValue(control.source,duty,changes);
        }
        const double time=++frame*.0011;
        fast.AdvanceTo(time);reference.AdvanceTo(time);
        const auto a=fast.Snapshot().latest,b=reference.Snapshot().latest;
        BOOST_REQUIRE_EQUAL(a.values.size(),b.values.size());
        for(std::size_t i=0;i<a.values.size();++i)
        {
            const bool current=c.config.signals[i].starts_with("i(")||c.config.signals[i].starts_with('@');
            BOOST_CHECK_SMALL(a.values[i]-b.values[i],current?2e-5:.006);
        }
        if(duty==.5)BOOST_CHECK_LT(a.values[out],.15);
        else BOOST_CHECK_GT(a.values[out],3.0);
    }
}

BOOST_AUTO_TEST_CASE(InfraredNativeThroughput,*boost::unit_test::disabled())
{
    for(unsigned count:{1u,8u})
    {
        auto c=InfraredClock(count);INTERACTIVE_SIM_SESSION s(CIRCUITALIVE_TEST_WORKER);s.Configure(c.netlist,c.config);
        s.AdvanceTo(.001);s.Snapshot(true);const auto start=std::chrono::steady_clock::now();
        for(int i=1;i<=125;++i){s.AdvanceTo(.001+i*.008);s.Snapshot(true);}
        const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        BOOST_TEST_MESSAGE("IR receivers="<<count<<" simulated=1s wall="<<seconds<<" speed="<<1/seconds<<"x");
        BOOST_CHECK_CLOSE(s.Snapshot().latest.time,1.001,1e-6);
    }
}

BOOST_AUTO_TEST_CASE(NativeReceiversDoNotThrottleEditorBatchesAsFirmware)
{
    auto c=InfraredClock(8);
    const auto pulse=c.netlist.find(" PULSE(");BOOST_REQUIRE(pulse!=std::string::npos);
    c.netlist.replace(pulse,c.netlist.find('\n',pulse)-pulse," DC 0");
    SESSION_RUNNER runner(CIRCUITALIVE_TEST_WORKER);runner.Start(c);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    for(;;)
    {
        const auto view=runner.View();
        BOOST_REQUIRE_MESSAGE(view.state!=EDITOR_SESSION_STATE::FAILED,view.error);
        if(view.state==EDITOR_SESSION_STATE::RUNNING&&view.sample.latest.time>c.config.step+1e-9)
        {
            BOOST_CHECK_GE(view.sample.latest.time,.004); // One normal editor frame, not 64 us of firmware.
            break;
        }
        BOOST_REQUIRE(std::chrono::steady_clock::now()<deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
BOOST_AUTO_TEST_CASE(SegmentDiodesFollowActualForwardCurrentInBothPolarities)
{
    for(bool anode:{false,true})
    {
        auto d=Output(anode?"display_7seg_ca":"display_7seg_cc","DS1");
        for(auto& [pin,net]:d.pins)net=anode?"vcc":"0";
        d.pins["10"]="seg_a";
        std::vector<CIRCUIT_COMPONENT> parts={Supply("V1","vcc",5),d,
            {"R1","Device:R","330",{{"1",anode?"seg_a":"vcc"},{"2",anode?"0":"seg_a"}}}};
        auto c=BuildCircuitSnapshot(parts);ConfigureVisualSignals(c);const auto sample=RunOutput(c,.001);
        BOOST_REQUIRE_EQUAL(c.segments.size(),8u);EDITOR_SESSION_VIEW view;view.sample=sample;view.state=EDITOR_SESSION_STATE::RUNNING;UpdateVisualSignals(c,view,1);
        for(std::size_t i=0;i<c.segments.size();++i)
        {
            const double current=sample.latest.values.at(c.segments[i].signal);
            if(c.segments[i].appearance.id=="A"){BOOST_CHECK_GT(current,.007);BOOST_CHECK_LT(current,.011);BOOST_CHECK_GT(view.segmentLevels[i],.7);}
            else {BOOST_CHECK_SMALL(current,1e-8);BOOST_CHECK_SMALL(view.segmentLevels[i],1e-5);}
        }
    }
}
BOOST_AUTO_TEST_CASE(SegmentCacheTracksSourceChangesAndKeepsCapacitorState)
{
    for(bool capacitor:{false,true})
    {
        auto display=Output("display_7seg_ca","DS1");display.pins["3"]=display.pins["8"]="vcc";
        std::vector<CIRCUIT_COMPONENT> parts={Supply("VCC","vcc",5),display};
        for(auto& [pin,net]:display.pins)if(pin!="3"&&pin!="8")
        {
            net="segment_"+pin;parts.push_back({"Rseg"+pin,"Device:R","330",{{"1",net},{"2","0"}}});
        }
        parts[1]=display;const auto memoryResistor=parts.size();
        if(capacitor){parts.push_back({"R1","Device:R","1k",{{"1","vcc"},{"2","memory"}}});parts.push_back({"C1","Device:C","1u",{{"1","memory"},{"2","0"}}});}
        auto circuit=BuildCircuitSnapshot(parts);BOOST_CHECK(circuit.config.accelerateDisplayNetwork);
        std::size_t memory=0;if(capacitor){memory=circuit.config.signals.size();circuit.config.signals.push_back("v("+circuit.pinNodes[memoryResistor].at("2")+")");}
        INTERACTIVE_SIM_SESSION fast(CIRCUITALIVE_TEST_WORKER),reference(CIRCUITALIVE_TEST_WORKER);fast.Configure(circuit.netlist,circuit.config);
        circuit.config.accelerateStateless=false;reference.Configure(circuit.netlist,circuit.config);
        for(unsigned step=1;step<=3;++step)
        {
            fast.AdvanceTo(step*.001);reference.AdvanceTo(step*.001);const auto a=fast.Snapshot(),b=reference.Snapshot();
            BOOST_REQUIRE_EQUAL(a.latest.values.size(),b.latest.values.size());
            for(std::size_t i=0;i<a.latest.values.size();++i)BOOST_CHECK_SMALL(a.latest.values[i]-b.latest.values[i],2e-6);
            if(capacitor&&step==1){BOOST_CHECK_GT(a.latest.values[memory],3.1);BOOST_CHECK_LT(a.latest.values[memory],3.3);}
            if(step==1){fast.SetSourceValue("Vca1",3.3);reference.SetSourceValue("Vca1",3.3);}
        }
    }
}
BOOST_AUTO_TEST_CASE(EditableOutputSignalsObserveTheirOwnDifferentialReturn)
{
    auto resistor=Output("resistor","DS1");auto file=MODEL_REPOSITORY().SelectModel(resistor).file;
    std::ifstream original(file);auto model=nlohmann::json::parse(original);model["id"]="test_differential_output";
    model["segments"]={{{"id","line"},{"signal","v(${pin:p},${pin:n})"},{"full_scale",5}}};
    model["audio"]={{"signal","v(${pin:p},${pin:n})"},{"full_scale",5},{"volume",.1}};
    const auto path=std::filesystem::temp_directory_path()/("ca-output-voltage-"+std::to_string(ProcessId())+".casim");
    struct REMOVE {std::filesystem::path path;~REMOVE(){std::error_code error;std::filesystem::remove(path,error);}} cleanup{path};
    std::ofstream(path)<<model.dump();resistor.modelFile=path.string();resistor.value="1k";resistor.pins={{"1","positive"},{"2","return"}};
    const auto c=BuildCircuitSnapshot({Supply("V1","positive",5),Supply("V2","return",2),resistor});const auto sample=RunOutput(c,.001);
    BOOST_CHECK_CLOSE(sample.latest.values.at(c.segments[0].signal),3,1e-6);
    BOOST_CHECK_CLOSE(sample.latest.values.at(c.config.audioChannels[0].signal),3,1e-6);
}
BOOST_AUTO_TEST_CASE(SegmentModelsRemainEditableAndNamespacedAfterComposition)
{
    const auto component=Output("display_11seg_cc","DS1");const auto model=MODEL_REPOSITORY().SelectModel(component);
    std::ifstream input(model.file);std::string text(std::istreambuf_iterator<char>(input),{});
    auto design=MODEL_DESIGN::Open(text);design.MakeCoreVisible();auto saved=design.Export();auto loaded=MODEL_REPOSITORY::LoadText(saved);
    BOOST_REQUIRE_EQUAL(loaded.segments.size(),11u);
    BOOST_CHECK(loaded.segments[0].signal.find("b_Core_")!=std::string::npos);
    auto seven=Output("display_7seg_cc","DS2");auto sevenModel=MODEL_REPOSITORY().SelectModel(seven);
    std::ifstream sevenFile(sevenModel.file);auto sevenDesign=MODEL_DESIGN::Open(std::string(std::istreambuf_iterator<char>(sevenFile),{}));
    sevenDesign.MakeCoreVisible();sevenDesign.blocks[0].values["full_current"]="5m";
    const auto rebound=MODEL_REPOSITORY::LoadText(sevenDesign.Export());
    BOOST_CHECK(rebound.segments[0].fullScaleParameter.empty());BOOST_CHECK_CLOSE(rebound.segments[0].fullScale,.005,1e-6);
    auto json=nlohmann::json::parse(saved);json["segments"][0]["visual"]["width"]=-.1;
    BOOST_CHECK_THROW(MODEL_REPOSITORY::LoadText(json.dump()),std::runtime_error);
}
BOOST_AUTO_TEST_CASE(LinkedInfraredReceiverDemodulatesCircuitCurrentAndUnlinkDisconnectsIt)
{
    auto led=Output("ir_led","D1");led.pins={{"1","0"},{"2","anode"}};led.identity="emitter";led.linkedComponents={"receiver"};
    auto receiver=Output("ir_receiver_tsop","U1");receiver.pins={{"1","out"},{"2","0"},{"3","vcc"}};receiver.identity="receiver";receiver.linkedComponents={"emitter"};
    auto source=Supply("Vcarrier","carrier",0);
    std::vector<CIRCUIT_COMPONENT> parts={Supply("VCC","vcc",3.3),source,led,receiver,{"R1","Device:R","220",{{"1","carrier"},{"2","anode"}}}};
    auto c=BuildCircuitSnapshot(parts);BOOST_REQUIRE_EQUAL(c.links.size(),1u);
    // Drive a real LED with 38 kHz carrier bursts in the solver, including a dark gap.
    const auto start=c.netlist.find("Vca2 "); // Instance IDs follow component order.
    BOOST_REQUIRE(start!=std::string::npos);
    const auto end=c.netlist.find('\n',start);
    std::ostringstream waveform;waveform<<std::setprecision(15)<<"Vca2 "<<c.pinNodes[1].at("1")<<" 0 PWL(0 0 ";
    const double period=1./38000;
    for(int cycle=0;cycle<24;++cycle)
    {
        const double t=10e-6+cycle*period;
        waveform<<t<<" 0 "<<t+1e-8<<" 5 "<<t+period*.33<<" 5 "<<t+period*.33+1e-8<<" 0 ";
    }
    waveform<<")";c.netlist.replace(start,end-start,waveform.str());const auto outName="v("+c.pinNodes[3].at("1")+")";const auto output=std::find(c.config.signals.begin(),c.config.signals.end(),outName)-c.config.signals.begin();
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(.0004);
    BOOST_CHECK_LT(session.Snapshot().latest.values.at(output),.15);session.AdvanceTo(.0009);BOOST_CHECK_GT(session.Snapshot().latest.values.at(output),3.0);
    parts[2].linkedComponents.clear();c=BuildCircuitSnapshot(parts);BOOST_CHECK(c.links.empty());
    auto result=RunOutput(c,.0004);
    const auto binding=std::find_if(c.config.programmableDevices[0].bindings.begin(),c.config.programmableDevices[0].bindings.end(),[](const auto& p){return p.id=="OUT";});
    BOOST_REQUIRE(binding!=c.config.programmableDevices[0].bindings.end());
    const auto signal=std::find(c.config.signals.begin(),c.config.signals.end(),binding->signal)-c.config.signals.begin();
    BOOST_CHECK_GT(result.latest.values.at(signal),3.0); // Receiver remains powered with no optical input.
}
BOOST_AUTO_TEST_CASE(LinkedInfraredKeepsOpticalPeersTogetherWithoutJoiningIndependentLoads)
{
    auto led=Output("ir_led","D1");led.pins={{"1","0"},{"2","anode"}};led.identity="emitter";led.linkedComponents={"receiver"};
    auto receiver=Output("ir_receiver_tsop","U1");receiver.pins={{"1","out"},{"2","0"},{"3","vcc"}};receiver.identity="receiver";receiver.linkedComponents={"emitter"};
    auto c=BuildCircuitSnapshot({Supply("VCC","vcc",3.3),Supply("VC","carrier",0),led,receiver,
        {"R1","Device:R","220",{{"1","carrier"},{"2","anode"}}},
        {"RB","Device:R","1k",{{"1","vcc"},{"2","independent"}}},
        {"CB","Device:C","1u",{{"1","independent"},{"2","0"}}}});
    BOOST_REQUIRE_EQUAL(c.links.size(),1);BOOST_CHECK(c.config.partitionable);const auto begin=c.netlist.find("Vca2 "),end=c.netlist.find('\n',begin);
    c.netlist.replace(begin,end-begin,"Vca2 "+c.pinNodes[1].at("1")+" 0 PULSE(0 5 0 1n 1n 13.157u 26.315u)");
    const auto plan=BuildAnalogPartitions(c.netlist,c.config,2);BOOST_REQUIRE_EQUAL(plan.size(),2);
    for(const auto& part:plan)if(!part.config.programmableDevices.empty())
    {BOOST_CHECK(part.netlist.find("Vsoptical_")!=std::string::npos);BOOST_CHECK(part.netlist.find("Cca7 ")==std::string::npos);}
    ANALOG_SIM_SESSION parallel(CIRCUITALIVE_TEST_WORKER),serial(CIRCUITALIVE_TEST_WORKER);
    parallel.Configure(c.netlist,c.config,true,2);serial.Configure(c.netlist,c.config,false);
    const auto out=std::find(c.config.signals.begin(),c.config.signals.end(),"v("+c.pinNodes[3].at("1")+")")-c.config.signals.begin();
    for(double time:{.0004,.001,.025})
    {
        parallel.AdvanceTo(time);serial.AdvanceTo(time);const auto a=parallel.Snapshot(),b=serial.Snapshot();
        for(std::size_t i=0;i<a.latest.values.size();++i)BOOST_CHECK_SMALL(a.latest.values[i]-b.latest.values[i],.006);
        if(time<.002)BOOST_CHECK_LT(a.latest.values.at(out),.15);else BOOST_CHECK_GT(a.latest.values.at(out),3.0);
    }
}
BOOST_AUTO_TEST_CASE(SmallNativeInfraredIslandGetsItsOwnAutomaticWorker)
{
    auto circuit=InfraredClock();std::string chain;
    const auto supply=circuit.pinNodes[0].at("1");
    for(unsigned i=0;i<20;++i)
        chain+="Risland"+std::to_string(i)+" "+(i?"island"+std::to_string(i-1):supply)
            +" "+(i==19?"0":"island"+std::to_string(i))+" 1k\n";
    circuit.netlist.insert(circuit.netlist.find(".end"),chain);circuit.config.signals.push_back("v(island0)");
    const auto automatic=BuildAnalogPartitions(circuit.netlist,circuit.config);
    // On a machine with one available worker the original solver is expected.
    if(std::thread::hardware_concurrency()>3)
    {
        BOOST_REQUIRE_EQUAL(automatic.size(),2u);
        for(const auto& part:automatic)if(!part.config.programmableDevices.empty())
            BOOST_CHECK(!part.elements.contains("risland0"));
    }
    else BOOST_CHECK_EQUAL(BuildAnalogPartitions(circuit.netlist,circuit.config,2).size(),2u);
}
BOOST_AUTO_TEST_CASE(NativeInfraredSineCacheMatchesTransientEdgesAndCurrents)
{
    for(int phase:{0,37})
    {
        auto circuit=InfraredClock(1,false,PROBE_KIND::SINE);circuit.config.synchronizeAdvances=true;
        // Compare the electrical solutions independently of the transient
        // solver's default diode convergence tolerance between time steps.
        circuit.netlist.insert(circuit.netlist.find(".end"),".options reltol=1e-6 vntol=1e-9 abstol=1e-12\n");
        const auto sine=circuit.netlist.find("SIN(");BOOST_REQUIRE(sine!=std::string::npos);
        circuit.netlist.replace(sine,circuit.netlist.find(')',sine)-sine+1,"SIN(2.5 2.5 38000 0 0 "+std::to_string(phase)+")");
        INTERACTIVE_SIM_SESSION fast(CIRCUITALIVE_TEST_WORKER),reference(CIRCUITALIVE_TEST_WORKER);
        fast.Configure(circuit.netlist,circuit.config);
        circuit.config.accelerateStateless=false;reference.Configure(circuit.netlist,circuit.config);
        const auto binding=std::find_if(circuit.config.programmableDevices[0].bindings.begin(),
            circuit.config.programmableDevices[0].bindings.end(),[](const auto& pin){return pin.id=="OUT";});
        BOOST_REQUIRE(binding!=circuit.config.programmableDevices[0].bindings.end());
        const auto out=std::find(circuit.config.signals.begin(),circuit.config.signals.end(),binding->signal)-circuit.config.signals.begin();
        for(const double time:{.000413,.000428,.001123,.025017})
        {
            fast.AdvanceTo(time);reference.AdvanceTo(time);
            const auto a=fast.Snapshot(true),b=reference.Snapshot(true);
            BOOST_CHECK_SMALL(a.latest.time-b.latest.time,2e-11);
            for(std::size_t i=0;i<a.latest.values.size();++i)
            {
                const auto& signal=circuit.config.signals[i];
                BOOST_TEST_CONTEXT("phase="<<phase<<" time="<<time<<" signal="<<signal)
                    BOOST_CHECK_SMALL(a.latest.values[i]-b.latest.values[i],signal.starts_with("v(")?.006:1e-5);
                if(time==.000428&&signal.starts_with("@d"))BOOST_CHECK_GT(a.latest.values[i],.005);
            }
            if(time<.002)BOOST_CHECK_LT(a.latest.values[out],.15);else BOOST_CHECK_GT(a.latest.values[out],3.0);
        }
    }
}
BOOST_AUTO_TEST_CASE(SelectedHistoryCompactsConstantIntervalsWithoutChangingWaveforms)
{
    auto circuit=InfraredClock();circuit.config.historyCapacity=4096;
    const auto signal="v("+circuit.pinNodes[1].at("1")+")";
    const auto index=std::find(circuit.config.signals.begin(),circuit.config.signals.end(),signal)-circuit.config.signals.begin();
    if(index==circuit.config.signals.size())circuit.config.signals.push_back(signal);
    BOOST_REQUIRE_LT(index,circuit.config.signals.size());
    INTERACTIVE_SIM_SESSION compact(CIRCUITALIVE_TEST_WORKER),reference(CIRCUITALIVE_TEST_WORKER);
    reference.Configure(circuit.netlist,circuit.config);circuit.config.historySignals=std::vector<std::size_t>{std::size_t(index)};
    compact.Configure(circuit.netlist,circuit.config);
    for(const double time:{.001,.002})
    {
        compact.AdvanceTo(time);reference.AdvanceTo(time);const auto a=compact.Snapshot(true),b=reference.Snapshot(true);
        BOOST_REQUIRE_GT(a.history.size(),2u);BOOST_CHECK_LT(a.history.size(),b.history.size());
        BOOST_CHECK_EQUAL(a.samplesReceived,b.samplesReceived);BOOST_CHECK_EQUAL(a.eventsApplied,b.eventsApplied);
        for(const auto& point:b.history)
        {
            const auto next=std::lower_bound(a.history.begin(),a.history.end(),point.time,
                [](const auto& sample,double target){return sample.time<target;});
            BOOST_REQUIRE(next!=a.history.end());double value=next->values[index];
            if(next!=a.history.begin()&&next->time>point.time)
            {
                const auto& previous=*std::prev(next);
                value=std::lerp(previous.values[index],value,(point.time-previous.time)/(next->time-previous.time));
            }
            BOOST_CHECK_SMALL(value-point.values[index],1e-10);
        }
    }
}
BOOST_AUTO_TEST_CASE(LcdIsPoweredWithoutFirmwareAndPublishesStateAcrossWorkerBoundary)
{
    auto lcd=Output("lcd1602_parallel","DS1");lcd.pins["2"]="vcc";
    auto c=BuildCircuitSnapshot({Supply("VCC","vcc",5),lcd});
    BOOST_REQUIRE_EQUAL(c.displays.size(),1u);BOOST_REQUIRE_EQUAL(c.config.programmableDevices.size(),1u);BOOST_CHECK(c.config.programmableDevices[0].firmware.empty());
    auto sample=RunOutput(c,.0002);BOOST_REQUIRE(sample.presentation.contains("DS1"));const auto state=nlohmann::json::parse(sample.presentation.at("DS1"));
    BOOST_CHECK(state.at("powered").get<bool>());BOOST_CHECK(!state.at("display").get<bool>());BOOST_CHECK_EQUAL(state.at("cells").size(),32u);
    auto runtime=c.config.programmableDevices[0];NATIVE_PERIPHERALS r;r.Configure({runtime},c.config.signals);
    // Direct runtime readback checks the optional presentation API before and after reset.
    r.Observe(.0001,sample.latest.values);BOOST_CHECK(nlohmann::json::parse(r.Presentation().at("DS1")).at("powered").get<bool>());
    r.Reset();BOOST_CHECK(!nlohmann::json::parse(r.Presentation().at("DS1")).at("powered").get<bool>());
}
BOOST_AUTO_TEST_CASE(LcdParallelCommandsSurviveActualElectricalSampling)
{
    for(bool hy1602:{false,true})for(bool fourBit:{false,true})BOOST_TEST_CONTEXT("HY1602="<<hy1602<<" four-bit="<<fourBit)
    {
        auto lcd=Output("lcd1602_parallel","DS1");
        lcd.libraryId=hy1602?"Display_Character:HY1602E":"Display_Character:WC1602A";
        lcd.modelFile.clear(); // Exercise automatic association and the real symbol's numbering.
        const auto model=MODEL_REPOSITORY().SelectModel(lcd);lcd.pins.clear();
        for(const auto& pin:model.pins)lcd.pins[pin.defaultPin]="0";
        lcd.pins[hy1602?"4":"2"]="vcc";lcd.pins[hy1602?"8":"6"]="enable";
        std::vector<CIRCUIT_COMPONENT> parts={Supply("VCC","vcc",5),lcd};
        for(int bit=0;bit<8;++bit){lcd.pins[std::to_string((hy1602?9:7)+bit)]="data"+std::to_string(bit);parts.push_back(Supply("VD"+std::to_string(bit),"data"+std::to_string(bit),0));}
        lcd.pins[hy1602?"6":"4"]="rs";parts[1]=lcd;parts.push_back(Supply("VE","enable",0));parts.push_back(Supply("VRS","rs",0));
        auto c=BuildCircuitSnapshot(parts);
        // Replace DC sources with PWL initialization and character writes at >15 ms.
        const std::vector<std::pair<int,int>> bytes={{0,fourBit?0x28:0x38},{0,0x0c},{0,0x80},{1,0x48},{1,0x69},{0,0xc0},{1,0x21}};
        std::vector<std::pair<int,int>> commands;
        if(fourBit)commands={{0,0x30},{0,0x30},{0,0x30},{0,0x20}};
        for(const auto& [rs,byte]:bytes)
            if(fourBit){commands.push_back({rs,byte&240});commands.push_back({rs,(byte<<4)&240});}
            else commands.push_back({rs,byte});
        auto pwl=[&](std::size_t part,const std::function<int(int,int)>& value){
            auto stem="Vca"+std::to_string(part+1)+" ";auto start=c.netlist.find(stem);BOOST_REQUIRE(start!=std::string::npos);auto end=c.netlist.find('\n',start);
            std::ostringstream line;line<<std::setprecision(12)<<stem<<c.pinNodes[part].at("1")<<" 0 PWL(0 0 ";
            for(std::size_t i=0;i<commands.size();++i)for(int phase=0;phase<4;++phase)line<<.02+i*.0001+phase*10e-6<<' '<<value(i,phase)<<' ';
            line<<")";c.netlist.replace(start,end-start,line.str());};
        for(int bit=0;bit<8;++bit)pwl(2+bit,[&](int i,int){return commands[i].second&(1<<bit)?5:0;});
        pwl(10,[&](int,int phase){return phase==1||phase==2?5:0;});pwl(11,[&](int i,int){return commands[i].first?5:0;});
        auto state=nlohmann::json::parse(RunOutput(c,.02+(commands.size()+1)*.0001).presentation.at("DS1"));BOOST_CHECK(state.at("display").get<bool>());
        BOOST_CHECK_EQUAL(state.at("cells")[0].get<int>(),0x48);BOOST_CHECK_EQUAL(state.at("cells")[1].get<int>(),0x69);BOOST_CHECK_EQUAL(state.at("cells")[16].get<int>(),0x21);
    }
}
BOOST_AUTO_TEST_CASE(NativeLcdKeepsBusyReadbackCgramAndCursorWithoutPolling)
{
    for(bool fourBit:{false,true})
    {
        NATIVE_HD44780 lcd;lcd.Reset();double time=.02;
        auto pulse=[&](bool rs,bool rw,unsigned value){time+=.0001;const auto out=lcd.Bus(rs,rw,true,value,time);lcd.Bus(rs,rw,false,value,time+1e-6);return out;};
        auto write=[&](bool rs,unsigned value){if(fourBit){pulse(rs,false,value&240);pulse(rs,false,(value<<4)&240);}else pulse(rs,false,value);};
        if(fourBit){pulse(false,false,0x30);pulse(false,false,0x20);}
        write(false,fourBit?0x28:0x38);write(false,0x0f);write(false,0x80);write(true,'A');write(true,'B');
        auto state=nlohmann::json::parse(lcd.Presentation(true,true,time));BOOST_CHECK(state.at("display").get<bool>());BOOST_CHECK(state.at("cursor").get<bool>());
        BOOST_CHECK_EQUAL(state.at("cells")[0].get<int>(),'A');BOOST_CHECK_EQUAL(state.at("cells")[1].get<int>(),'B');
        const auto busy=lcd.Bus(false,true,true,0,time+2e-6);BOOST_CHECK(busy&128);lcd.Bus(false,true,false,0,time+3e-6);
        if(fourBit){lcd.Bus(false,true,true,0,time+4e-6);lcd.Bus(false,true,false,0,time+5e-6);}
        write(false,0x80);unsigned read=pulse(true,true,0);
        if(fourBit)read=(read&240)|(pulse(true,true,0)>>4);
        BOOST_CHECK_EQUAL(read,'A');
        write(false,0x40);for(unsigned row=0;row<8;++row)write(true,row);
        state=nlohmann::json::parse(lcd.Presentation(true,false,time));for(unsigned row=0;row<8;++row)BOOST_CHECK_EQUAL(state.at("cgram")[row].get<int>(),row);
        write(false,0x01);state=nlohmann::json::parse(lcd.Presentation(true,false,time));BOOST_CHECK_EQUAL(state.at("cells")[0].get<int>(),32);
    }
}
BOOST_AUTO_TEST_CASE(I2cBackpackReceivesCharactersOverElectricalOpenDrainBus)
{
    auto lcd=Output("lcd1602_i2c","DS1");lcd.pins={{"1","0"},{"2","vcc"},{"3","sda"},{"4","scl"}};
    auto c=BuildCircuitSnapshot({Supply("VCC","vcc",5),lcd});
    struct LEVEL {double time;int scl,sda;};std::vector<LEVEL> levels={{0,5,0}};double time=.02;int clock=5,data=0;
    auto level=[&](int nextClock,int nextData){levels.push_back({time,clock,data});time+=1e-8;clock=nextClock;data=nextData;levels.push_back({time,clock,data});time+=5e-6;};
    auto send=[&](unsigned byte){for(int bit=7;bit>=0;--bit){const int low=byte&(1<<bit)?0:5;level(0,low);level(5,low);level(0,low);}level(0,0);level(5,0);level(0,0);};
    auto port=[&](unsigned byte){level(5,0);level(5,5);level(0,5);send(0x4e);send(byte);level(0,5);level(5,5);level(5,0);};
    auto nibble=[&](unsigned value,bool rs=false){const unsigned p=(value<<4)|8|(rs?1:0);port(p|4);port(p);};
    auto byte=[&](unsigned value,bool rs=false){nibble(value>>4,rs);nibble(value&15,rs);};
    nibble(3);time+=.005;nibble(3);nibble(3);nibble(2);byte(0x28);byte(0x0c);byte(0x80);byte('O',true);byte('K',true);
    std::ostringstream clockWave,dataWave;clockWave<<std::setprecision(12);dataWave<<std::setprecision(12);
    for(const auto& v:levels){clockWave<<v.time<<' '<<v.scl<<' ';dataWave<<v.time<<' '<<v.sda<<' ';}
    std::ostringstream circuit;circuit<<"Vclock "<<c.pinNodes[1].at("4")<<" 0 PWL("<<clockWave.str()<<")\n";
    circuit<<"Vdata ca_i2c_control 0 PWL("<<dataWave.str()<<")\n";
    circuit<<"Rpullup "<<c.pinNodes[1].at("3")<<' '<<c.pinNodes[1].at("2")<<" 4700\n";
    circuit<<"Sdata "<<c.pinNodes[1].at("3")<<" 0 ca_i2c_control 0 ca_i2c_sink\n.model ca_i2c_sink SW(Ron=1 Roff=1e12 Vt=2.5 Vh=.1)\n";
    c.netlist.insert(c.netlist.find(".end"),circuit.str());
    const auto state=nlohmann::json::parse(RunOutput(c,time+.001).presentation.at("DS1"));
    BOOST_CHECK(state.at("display").get<bool>());BOOST_CHECK_EQUAL(state.at("cells")[0].get<int>(),'O');BOOST_CHECK_EQUAL(state.at("cells")[1].get<int>(),'K');
}
BOOST_AUTO_TEST_CASE(ActiveBuzzerRequiresSupplyAndGeneratesAnAudibleTone)
{
    auto buzzer=Output("buzzer_active","BZ1");buzzer.pins["1"]="vcc";
    for(double voltage:{0.,5.})
    {
        const auto c=BuildCircuitSnapshot({Supply("VCC","vcc",voltage),buzzer});const auto sample=RunOutput(c,.01);
        BOOST_REQUIRE(!sample.audio.empty());int peak=0;for(auto value:sample.audio)peak=std::max(peak,std::abs(int(value)));
        if(voltage==0)BOOST_CHECK_EQUAL(peak,0);else BOOST_CHECK_GT(peak,5000);
    }
}
BOOST_AUTO_TEST_CASE(AudioResamplerPreservesPulseAreaAndRejectsDC)
{
    AUDIO_SAMPLER sampler;std::vector<SIM_AUDIO_CHANNEL> channels={{0,1,1}};
    // 96 kHz input PWM at 25% duty: each audio bin must preserve its area.
    sampler.Observe(0,{1},channels);
    for(int i=1;i<9600;++i)sampler.Observe(i/96000.,{i%4==0?1.:0.},channels);
    auto samples=sampler.Samples();BOOST_REQUIRE_GT(samples.size(),4000u);BOOST_CHECK_LT(std::abs(samples.back()),10000);
    sampler.Reset();sampler.Observe(0,{1},channels);sampler.Observe(.2,{1},channels);samples=sampler.Samples();BOOST_REQUIRE(!samples.empty());BOOST_CHECK_LE(std::abs(int(samples.back())),1);
    sampler.Observe(1,{0},channels);BOOST_CHECK_EQUAL(sampler.Samples().size(),16384u);BOOST_CHECK_GT(sampler.Overwritten(),0u);sampler.Drain();BOOST_CHECK(sampler.Samples().empty());
}
BOOST_AUTO_TEST_CASE(SpeakerAudioStreamsIndependentlyOfScopeHistory)
{
    auto speaker=Output("speaker","SP1");speaker.pins["1"]="audio";speaker.modelParameters["volume"]=".1";
    CIRCUIT_COMPONENT source{"V1","Simulation_SPICE:VSIN","VSIN",{{"1","audio"},{"2","0"}}};source.modelParameters={{"amplitude","1"},{"frequency","1k"}};
    auto c=BuildCircuitSnapshot({source,speaker});c.config.historySignals=std::vector<std::size_t>{};
    BOOST_REQUIRE_EQUAL(c.config.audioChannels.size(),1u);BOOST_CHECK_CLOSE(c.config.audioChannels[0].volume,.1,1e-6);
    INTERACTIVE_SIM_SESSION session(CIRCUITALIVE_TEST_WORKER);session.Configure(c.netlist,c.config);session.AdvanceTo(.01);const auto sample=session.Snapshot(true);
    BOOST_REQUIRE_GE(sample.audio.size(),479u);BOOST_CHECK_LE(sample.audio.size(),481u);
    int peak=0,crossings=0;for(std::size_t i=1;i<sample.audio.size();++i){peak=std::max(peak,std::abs(int(sample.audio[i])));if(sample.audio[i-1]<0&&sample.audio[i]>=0)crossings++;}
    BOOST_CHECK_GT(peak,500);BOOST_CHECK_LT(peak,800);BOOST_CHECK_GE(crossings,9);BOOST_CHECK_LE(crossings,10);BOOST_CHECK(session.Snapshot(true).audio.empty());
}
BOOST_AUTO_TEST_CASE(SpeakerKeepsStreamingThroughEditorFramesAndPauseResume)
{
    SOURCE_PROBE_SETTINGS clock;clock.frequency=200;
    auto speaker=Output("speaker","LS1");speaker.pins["1"]="carrier";
    auto c=BuildCircuitSnapshot({SourceProbeComponent(PROBE_KIND::SQUARE,clock,"#CLK","carrier"),speaker});
    // The PCM clock belongs to the resampler, not the electrical solver.
    BOOST_CHECK_CLOSE(c.config.maxStep,100e-6,1e-6);
    SESSION_RUNNER runner(CIRCUITALIVE_TEST_WORKER);runner.Start(c);
    auto collect=[&](double until)
    {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        std::size_t samples=0;int latePeak=0;
        for(;;)
        {
            const auto view=runner.View();
            BOOST_REQUIRE_MESSAGE(view.state!=EDITOR_SESSION_STATE::FAILED,view.error);
            const auto audio=runner.TakeAudio();samples+=audio.size();
            if(view.sample.latest.time>until-.2)
                for(auto value:audio)latePeak=std::max(latePeak,std::abs(int(value)));
            if(view.sample.latest.time>=until)break;
            BOOST_REQUIRE(std::chrono::steady_clock::now()<deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        BOOST_CHECK_GT(latePeak,2000);
        return samples;
    };
    BOOST_CHECK_GT(collect(1.2),54000u);
    runner.Pause();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(runner.View().state!=EDITOR_SESSION_STATE::PAUSED)
    {
        BOOST_REQUIRE(std::chrono::steady_clock::now()<deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    BOOST_CHECK(runner.TakeAudio().empty());
    runner.Resume();BOOST_CHECK_GT(collect(1.6),16000u);
}
BOOST_AUTO_TEST_SUITE_END()
