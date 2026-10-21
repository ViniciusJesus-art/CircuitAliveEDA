/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <sim/compiled_expression.h>
#include <sim/interactive_sim_session.h>
#include <cmath>

using namespace CIRCUITALIVE;

BOOST_AUTO_TEST_CASE( CompiledExpressionsValuesAndJacobianMatchFiniteDifferences )
{
    for( const std::string expression : {
        "v(a,b)*tanh(v(c)/2k)+abs(v(a))+max(v(b),1u)",
        "v(a)>v(b) && v(c)!=0 ? min(v(a)*v(b),v(c)) : v(c)/max(v(b),1u)",
        "-(v(a)+v(b))*1meg+time*v(c)", "v(a)/max(abs(v(b)),1u)+0*v(c)" } )
    {
        COMPILED_EXPRESSIONS bank;
        const auto deck = bank.Configure( "Test\nB1 out 0 V={" + expression + "}\n.end\n", {} );
        BOOST_REQUIRE( !bank.Empty() );
        BOOST_CHECK( deck.find( "ca_expression" ) != std::string::npos );
        double inputs[] = { 2, .7, 1.3 }, value;
        const double* partials = nullptr;
        BOOST_REQUIRE( bank.Evaluate( 0, inputs, 3, .4, value, partials ) );
        const std::vector<double> analytic( partials, partials + 3 );
        for( unsigned i = 0; i < 3; ++i )
        {
            inputs[i] += 1e-5;
            double plus, minus;
            BOOST_REQUIRE( bank.Evaluate( 0, inputs, 3, .4, plus, partials ) );
            inputs[i] -= 2e-5;
            BOOST_REQUIRE( bank.Evaluate( 0, inputs, 3, .4, minus, partials ) );
            inputs[i] += 1e-5;
            BOOST_CHECK_SMALL( analytic[i] - ( plus - minus ) / 2e-5,
                               1e-4 * std::max( 1., std::abs( analytic[i] ) ) );
        }
    }
}

BOOST_AUTO_TEST_CASE( CompiledExpressionsPreserveUnsupportedMutableAndReferencedSources )
{
    for( const std::string expression : std::vector<std::string>{ "v(in)/v(ref)", "v(in)/1p", "sin(v(in))",
                                        "v(in)+gain", std::string( 200, '-' ) + "v(in)" } )
    {
        COMPILED_EXPRESSIONS bank;
        const auto original = "Test\nB1 out 0 V={" + expression + "}\n.end\n";
        BOOST_CHECK_EQUAL( bank.Configure( original, {} ), original );
        BOOST_CHECK( bank.Empty() );
    }
    for( const auto& signals : std::vector<std::vector<std::string>>{
            { "i(B1)" }, { "@b1[i]" }, { "b1#branch" } } )
    {
        COMPILED_EXPRESSIONS bank;
        const std::string deck = "Test\nB1 out 0 V={v(in)*2}\n.end\n";
        BOOST_CHECK_EQUAL( bank.Configure( deck, signals ), deck );
    }
    COMPILED_EXPRESSIONS bank;
    const std::string mutableDeck = "Test\nB1 out 0 V={v(in)*2}\n.end\n";
    BOOST_CHECK_EQUAL( bank.Configure( mutableDeck, {}, { "b1" } ), mutableDeck );
    const std::string controlled = "Test\nB1 out 0 V={v(in)*2}\nF1 copy 0 B1 1\n.end\n";
    BOOST_CHECK_EQUAL( bank.Configure( controlled, {} ), controlled );
    const std::string continued = "Test\nB1 out 0 V={v(in)*2}\n+ tc1=0\n.end\n";
    BOOST_CHECK_EQUAL( bank.Configure( continued, {} ), continued );
    const std::string subckt = "Test\n.subckt amp in out\nB1 out 0 V={v(in)*2}\n.ends\n.end\n";
    BOOST_CHECK_EQUAL( bank.Configure( subckt, {} ), subckt );
}

BOOST_AUTO_TEST_CASE( CompiledExpressionsCacheTracksInputsAndTimeAndAvoidsNameCollisions )
{
    COMPILED_EXPRESSIONS bank;
    const auto deck = bank.Configure(
            "Test\nRca_compiled_expr_0 in 0 1k\nB1 out 0 V={v(in)*time}\n.end\n", {} );
    BOOST_CHECK( deck.find( ".model ca_compiled_expr__0" ) != std::string::npos );
    double input = 2, value;
    const double* partials = nullptr;
    BOOST_REQUIRE( bank.Evaluate( 0, &input, 1, .5, value, partials ) );
    BOOST_CHECK_EQUAL( value, 1 );
    BOOST_CHECK_EQUAL( partials[0], .5 );
    BOOST_REQUIRE( bank.Evaluate( 0, &input, 1, 1, value, partials ) );
    BOOST_CHECK_EQUAL( value, 2 );
    input = 3;
    BOOST_REQUIRE( bank.Evaluate( 0, &input, 1, 1, value, partials ) );
    BOOST_CHECK_EQUAL( value, 3 );
    BOOST_CHECK( !bank.Evaluate( 1, &input, 1, 1, value, partials ) );
}

BOOST_AUTO_TEST_CASE( CompiledExpressionsElectricalFeedbackMatchesNgspiceBehavioralSources )
{
    for( const bool initialConditions : { false, true } )
    {
        const std::string deck = "Compiled expression comparison\n"
            "Vcc vcc 0 5\nVinput input 0 0 SIN(0 .4 1k)\n"
            "Bfeedback out 0 V={v(vcc)*tanh((v(input)-v(filtered))/2)}\n"
            "Rfeedback out filtered 1k\nCfeedback filtered 0 1u\n"
            "Bload out 0 I={max(v(out),0)/10k}\n"
            "Bmutable copy 0 V={v(out)*2}\nRcopy copy 0 10k\n"
            "Adummy [input] [unused] adc\n.model adc adc_bridge(in_low=1 in_high=2)\n.end\n";
        SIM_SESSION_CONFIG config;
        config.step = config.maxStep = 10e-6;
        config.horizon = .03;
        config.useInitialConditions = initialConditions;
        config.signals = { "v(out)", "v(filtered)", "v(copy)", "i(Vcc)" };
        config.mutableElements = { "Bmutable" };
        INTERACTIVE_SIM_SESSION original( CIRCUITALIVE_TEST_WORKER );
        INTERACTIVE_SIM_SESSION compiled( CIRCUITALIVE_TEST_WORKER );
        original.Configure( deck, config );
        config.accelerateExpressions = true;
        compiled.Configure( deck, config );
        for( int step = 1; step <= 100; ++step )
        {
            const double target = step * .000213;
            original.AdvanceTo( target );
            compiled.AdvanceTo( target );
            const auto a = original.Snapshot( true ), b = compiled.Snapshot( true );
            BOOST_REQUIRE_EQUAL( a.latest.values.size(), b.latest.values.size() );
            for( unsigned i = 0; i < a.latest.values.size(); ++i )
                BOOST_CHECK_SMALL( a.latest.values[i] - b.latest.values[i], 2e-6 );
            if( step == 50 )
            {
                SIM_PARAMETER_CHANGE change{ "Bmutable", "v", { "v(out)*3" }, false };
                original.SetSourceValue( "Vcc", 4, { change } );
                compiled.SetSourceValue( "Vcc", 4, { change } );
            }
        }
    }
}

BOOST_AUTO_TEST_CASE( MixedSignalStartupConvergesAcrossHardCurrentLimitsWithLargeAdaptiveMaximum )
{
    for( const bool compiled : { false, true } )
    {
        SIM_SESSION_CONFIG config;
        config.step = config.maxStep = 100e-6;
        config.signals = { "v(out)" };
        config.accelerateExpressions = compiled;
        INTERACTIVE_SIM_SESSION session( CIRCUITALIVE_TEST_WORKER );
        session.Configure( "Mixed current limiter startup\nV1 in 0 9\n"
            "Btarget target 0 V={max(0,min(8,v(in)-2))}\n"
            "Rresponse target response 5k\nCresponse response 0 1n ic=0\n"
            "Bpass in out I={v(in)>0 ? max(0,min(1.5,(v(response)-v(out))/.05)) : 0}\n"
            "Rload out 0 1\nAdummy [in] [unused] adc\n"
            ".model adc adc_bridge(in_low=1 in_high=2)\n.end\n", config );
        session.AdvanceTo( .001 );
        BOOST_CHECK_CLOSE( session.Snapshot().latest.values[0], 1.5, .001 );
    }
}
