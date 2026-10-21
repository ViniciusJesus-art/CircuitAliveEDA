/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/scope_acquisition.h>
#include <circuitalive/scope_waveform.h>
#include <cmath>
#include <functional>

using namespace CIRCUITALIVE;
namespace
{
MEASUREMENT_CHANNEL channel()
{
    MEASUREMENT_CHANNEL c; c.name = "Test"; c.scope = true; c.positive = 0; return c;
}
std::vector<SIM_SAMPLE> samples( double begin, double end, double step, std::function<double( double )> f )
{
    std::vector<SIM_SAMPLE> result;
    for( int i = 0; begin + i * step <= end + step * .01; ++i )
    { const double t = begin + i * step; result.push_back( { t, { f( t ) } } ); }
    return result;
}
SCOPE_FRAME frame( std::vector<SIM_SAMPLE> data )
{
    SCOPE_FRAME result; result.begin = data.front().time; result.end = data.back().time;
    result.samples = std::move( data ); return result;
}
}

BOOST_AUTO_TEST_SUITE( Oscilloscope )
BOOST_AUTO_TEST_CASE( TriggerWaitsForPostCaptureAndInterpolatesEdge )
{
    SCOPE_ACQUISITION scope; SCOPE_TRIGGER trigger; trigger.mode = SCOPE_TRIGGER_MODE::NORMAL;
    scope.Configure( { .002, .5, 0 }, trigger, channel() );
    auto signal = []( double t ) { return t < .0999 ? 0. : 5.; };
    scope.Feed( samples( 0, .105, .001, signal ) );
    BOOST_CHECK( scope.State() == SCOPE_CAPTURE_STATE::POST_TRIGGER );
    BOOST_CHECK( scope.Frame().samples.empty() );
    scope.Feed( samples( .09, .12, .001, signal ) ); // Overlapping poll history must not duplicate points.
    const auto& capture = scope.Frame(); BOOST_REQUIRE( capture.triggerTime );
    BOOST_CHECK_CLOSE( *capture.triggerTime, .0995, 1e-8 );
    BOOST_CHECK_CLOSE( capture.begin, .0895, 1e-8 ); BOOST_CHECK_CLOSE( capture.end, .1095, 1e-8 );
    BOOST_CHECK( !capture.partial );
    for( std::size_t i = 1; i < capture.samples.size(); ++i ) BOOST_CHECK_GT( capture.samples[i].time, capture.samples[i-1].time );
}
BOOST_AUTO_TEST_CASE( FallingEdgeAndHysteresisRejectNoise )
{
    SCOPE_ACQUISITION scope; SCOPE_TRIGGER trigger; trigger.mode = SCOPE_TRIGGER_MODE::SINGLE;
    trigger.rising = false; trigger.hysteresis = .5;
    scope.Configure( { .001, .2, 0 }, trigger, channel() );
    scope.Feed( { { 0, { 2.6 } }, { .001, { 2.4 } }, { .002, { 2.6 } }, { .003, { 2.4 } } } );
    BOOST_CHECK( scope.Frame().samples.empty() );
    scope.Feed( { { .004, { 5 } }, { .005, { 0 } }, { .02, { 0 } } } );
    BOOST_REQUIRE( scope.Frame().triggerTime ); BOOST_CHECK_CLOSE( *scope.Frame().triggerTime, .0045, 1e-8 );
    BOOST_CHECK( scope.State() == SCOPE_CAPTURE_STATE::SINGLE_DONE );
}
BOOST_AUTO_TEST_CASE( SingleHoldsAndRearmWaitsForANewEdge )
{
    SCOPE_ACQUISITION scope; SCOPE_TRIGGER trigger; trigger.mode = SCOPE_TRIGGER_MODE::SINGLE;
    scope.Configure( { .001, .5, 0 }, trigger, channel() );
    const auto a = samples( 0, .05, .001, []( double t ) { return std::fmod( t + 1e-8, .02 ) >= .01 ? 5. : 0.; } );
    scope.Feed( a ); const auto serial = scope.Frame().serial; const auto time = scope.Frame().triggerTime;
    BOOST_CHECK_GT( serial, 0 );
    scope.Feed( a ); BOOST_CHECK_EQUAL( scope.Frame().serial, serial );
    scope.Arm( a ); scope.Feed( a );
    BOOST_CHECK( scope.State() == SCOPE_CAPTURE_STATE::WAITING );
    BOOST_CHECK( scope.Frame().triggerTime == time );
    scope.Feed( samples( .051, .09, .001, []( double t ) { return t < .07 ? 0. : 5.; } ) );
    BOOST_REQUIRE( scope.Frame().triggerTime ); BOOST_CHECK_GT( *scope.Frame().triggerTime, .06 );
}
BOOST_AUTO_TEST_CASE( AutoRollAndNormalHaveDifferentNoEdgeBehavior )
{
    const auto data = samples( 0, .1, .001, []( double ) { return 3.; } );
    SCOPE_ACQUISITION scope; SCOPE_TRIGGER trigger;
    scope.Configure( { .002, .5, 0 }, trigger, channel() ); scope.Feed( data );
    BOOST_CHECK( scope.State() == SCOPE_CAPTURE_STATE::AUTO ); BOOST_CHECK( !scope.Frame().samples.empty() );
    BOOST_CHECK( !scope.Frame().triggerTime );
    trigger.mode = SCOPE_TRIGGER_MODE::NORMAL; scope.Configure( { .002, .5, 0 }, trigger, channel() ); scope.Feed( data );
    BOOST_CHECK( scope.Frame().samples.empty() ); BOOST_CHECK( scope.State() == SCOPE_CAPTURE_STATE::WAITING );
    trigger.mode = SCOPE_TRIGGER_MODE::ROLL; scope.Configure( { .002, .5, -.03 }, trigger, channel() ); scope.Feed( data );
    BOOST_CHECK_CLOSE( scope.Frame().end, .07, 1e-8 );
    const auto serial = scope.Frame().serial; scope.Feed( data ); BOOST_CHECK_EQUAL( scope.Frame().serial, serial );
    scope.SetHold( true ); scope.Feed( samples( .101, .2, .001, []( double ) { return 9.; } ) );
    BOOST_CHECK_EQUAL( scope.Frame().serial, serial ); BOOST_CHECK( scope.State() == SCOPE_CAPTURE_STATE::HOLD );
}
BOOST_AUTO_TEST_CASE( HoldoffSeparatesCapturesAndPretriggerPositionIsHonored )
{
    SCOPE_ACQUISITION scope; SCOPE_TRIGGER trigger; trigger.mode = SCOPE_TRIGGER_MODE::NORMAL;
    trigger.level = 0; trigger.hysteresis = .1; trigger.holdoff = .025;
    scope.Configure( { .001, .3, .002 }, trigger, channel() );
    std::vector<double> edges; std::uint64_t serial = scope.Frame().serial;
    for( const auto& s : samples( 0, .15, .0001, []( double t ) { return std::sin( 2 * M_PI * 100 * t ); } ) )
    {
        scope.Feed( { s } );
        if( scope.Frame().serial == serial ) continue;
        serial = scope.Frame().serial; BOOST_REQUIRE( scope.Frame().triggerTime ); edges.push_back( *scope.Frame().triggerTime );
        BOOST_CHECK_SMALL( scope.Frame().begin - ( *scope.Frame().triggerTime - .001 ), 1e-10 );
    }
    BOOST_REQUIRE_GT( edges.size(), 2 );
    for( std::size_t i = 1; i < edges.size(); ++i ) BOOST_CHECK_GE( edges[i] - edges[i-1], .034 );
}
BOOST_AUTO_TEST_CASE( LongCapturesStayBoundedAndRetainTheirFullTimeWindow )
{
    SCOPE_ACQUISITION scope; SCOPE_TRIGGER trigger; trigger.mode = SCOPE_TRIGGER_MODE::SINGLE;
    scope.Configure( { .1, .5, 0 }, trigger, channel(), 16 );
    scope.Feed( samples( 0, 1, .001, []( double t ) { return t < .01 ? 0. : 5.; } ) );
    BOOST_CHECK_LE( scope.Frame().samples.size(), 16 ); BOOST_CHECK( !scope.Frame().partial );
    BOOST_CHECK( scope.Frame().decimated );
    BOOST_CHECK( scope.State() == SCOPE_CAPTURE_STATE::SINGLE_DONE );
}
BOOST_AUTO_TEST_CASE( NewRunDoesNotJoinOldHistory )
{
    SCOPE_ACQUISITION scope; SCOPE_TRIGGER trigger; trigger.mode = SCOPE_TRIGGER_MODE::ROLL;
    scope.Configure( { .001, .5, 0 }, trigger, channel() );
    scope.Feed( samples( 5, 5.1, .001, []( double ) { return 7.; } ) );
    scope.Feed( samples( 0, .02, .001, []( double ) { return 2.; } ) );
    BOOST_CHECK_LT( scope.Frame().end, .1 );
    BOOST_CHECK_CLOSE( ScopeStatistics( scope.Frame(), channel() ).mean, 2, 1e-8 );
}
BOOST_AUTO_TEST_CASE( StatisticsUseTimeWeightingAndExactLinearRms )
{
    // Irregular sampling of y=t: arithmetic averaging would give the wrong answer.
    const auto f = frame( { { 0, { 0 } }, { .01, { .01 } }, { .1, { .1 } }, { 1, { 1 } } } );
    const auto stats = ScopeStatistics( f, channel() );
    BOOST_CHECK_CLOSE( stats.mean, .5, 1e-8 ); BOOST_CHECK_CLOSE( stats.rms, std::sqrt( 1. / 3 ), 1e-8 );
    BOOST_CHECK_CLOSE( stats.peakToPeak, 1, 1e-8 ); BOOST_CHECK( std::isnan( stats.frequency ) );
    auto clipped = f; clipped.begin = .2; clipped.end = .8;
    const auto c = ScopeStatistics( clipped, channel() );
    BOOST_CHECK_CLOSE( c.minimum, .2, 1e-8 ); BOOST_CHECK_CLOSE( c.maximum, .8, 1e-8 );
    BOOST_CHECK_CLOSE( c.mean, .5, 1e-8 ); BOOST_CHECK_CLOSE( c.rms, std::sqrt( .28 ), 1e-8 );
}
BOOST_AUTO_TEST_CASE( SineFrequencyPeriodRmsAndDutyAreMeasured )
{
    const auto f = frame( samples( 0, .105, .00001, []( double t ) { return 2 + 3 * std::sin( 2 * M_PI * 100 * t ); } ) );
    const auto s = ScopeStatistics( f, channel() );
    BOOST_CHECK_CLOSE( s.frequency, 100, .001 ); BOOST_CHECK_CLOSE( s.period, .01, .001 );
    BOOST_CHECK_CLOSE( s.duty, 50, .001 ); BOOST_CHECK( !s.undersampled );
    const auto exact = ScopeStatistics( frame( samples( 0, .1, .00001, []( double t ) { return 3 * std::sin( 2 * M_PI * 100 * t ); } ) ), channel() );
    BOOST_CHECK_SMALL( exact.mean, 1e-10 ); BOOST_CHECK_CLOSE( exact.rms, 3 / std::sqrt( 2 ), .001 );
}
BOOST_AUTO_TEST_CASE( PulseDutyAndSparseSamplesAreIdentified )
{
    const auto pulse = ScopeStatistics( frame( samples( 0, .1, .0001, []( double t )
        { return std::fmod( t + 1e-8, .02 ) >= .005 ? 0. : 5.; } ) ), channel() );
    BOOST_CHECK_CLOSE( pulse.duty, 25, .1 ); BOOST_CHECK_CLOSE( pulse.frequency, 50, .01 );
    const auto sparse = ScopeStatistics( frame( samples( 0, .1, .003, []( double t ) { return std::sin( 2 * M_PI * 50 * t ); } ) ), channel() );
    BOOST_CHECK( sparse.undersampled );
}
BOOST_AUTO_TEST_CASE( CursorInterpolationNeverExtrapolatesMissingHistory )
{
    auto f = frame( { { .2, { 2 } }, { .5, { 5 } }, { .8, { 8 } } } ); f.begin = 0; f.end = 1;
    BOOST_CHECK_CLOSE( ScopeValueAt( f, channel(), .35 ), 3.5, 1e-8 );
    BOOST_CHECK( std::isnan( ScopeValueAt( f, channel(), .1 ) ) ); BOOST_CHECK( std::isnan( ScopeValueAt( f, channel(), .9 ) ) );
    auto c = channel(); c.connected = false;
    BOOST_CHECK( std::isnan( ScopeStatistics( f, c ).mean ) ); BOOST_CHECK( std::isnan( ScopeValueAt( f, c, .5 ) ) );
    c = channel(); c.positive = 10; BOOST_CHECK( std::isnan( ScopeStatistics( f, c ).rms ) );
}
BOOST_AUTO_TEST_CASE( HeldCaptureCanBeZoomedWithoutRestartingAcquisition )
{
    SCOPE_ACQUISITION scope; SCOPE_TRIGGER trigger; trigger.mode = SCOPE_TRIGGER_MODE::SINGLE;
    scope.Configure( { .002, .5, 0 }, trigger, channel() );
    scope.Feed( samples( 0, .1, .0001, []( double t ) { return t < .05 ? 0. : 5.; } ) );
    const auto edge = scope.Frame().triggerTime; BOOST_REQUIRE( edge );
    const auto dataSize = scope.Frame().samples.size();
    scope.Reframe( { .001, .3, .001 } );
    BOOST_CHECK( scope.State() == SCOPE_CAPTURE_STATE::SINGLE_DONE );
    BOOST_CHECK( scope.Frame().triggerTime == edge ); BOOST_CHECK_EQUAL( scope.Frame().samples.size(), dataSize );
    BOOST_CHECK_SMALL( scope.Frame().begin - ( *edge - .002 ), 1e-12 );
    BOOST_CHECK( !scope.Frame().partial );
    scope.Reframe( { .1, .5, 0 } ); BOOST_CHECK( scope.Frame().partial );
    scope.SetHold( true ); scope.Reframe( { .001, .5, 0 } );
    BOOST_CHECK( scope.State() == SCOPE_CAPTURE_STATE::HOLD );
}
BOOST_AUTO_TEST_CASE( SiControlsAcceptUnitsAndRejectInvalidNumbers )
{
    BOOST_REQUIRE( ScopeParse( "20 ms", "s" ) ); BOOST_CHECK_CLOSE( *ScopeParse( "20 ms", "s" ), .02, 1e-8 );
    BOOST_CHECK_CLOSE( *ScopeParse( "500 µV", "V" ), .0005, 1e-8 );
    BOOST_CHECK_CLOSE( *ScopeParse( "-2,5 V", "V" ), -2.5, 1e-8 );
    BOOST_CHECK_CLOSE( *ScopeParse( "1e-6", "s" ), 1e-6, 1e-8 );
    for( const auto* s : { "nan", "inf", "1e999", "12 V garbage", "1 Hz", "1.2.3", "", "-" } ) BOOST_CHECK( !ScopeParse( s, "s" ) );
    for( double n : { 1e-9, 2e-6, 5e-3, 1., 20., 500., 1e6 } )
    { const auto parsed = ScopeParse( ScopeFormat( n, "s" ), "s" ); BOOST_REQUIRE( parsed ); BOOST_CHECK_CLOSE( *parsed, n, 1e-8 ); }
    BOOST_CHECK_CLOSE( ScopeNiceScale( .0031 ), .005, 1e-8 ); BOOST_CHECK_EQUAL( ScopeNiceScale( 2 ), 2 );
}
BOOST_AUTO_TEST_CASE( AcquisitionRejectsInvalidConfiguration )
{
    SCOPE_ACQUISITION scope; SCOPE_TRIGGER trigger;
    BOOST_CHECK_THROW( scope.Configure( { 0, .5, 0 }, trigger, channel() ), std::invalid_argument );
    BOOST_CHECK_THROW( scope.Configure( { .01, 1.1, 0 }, trigger, channel() ), std::invalid_argument );
    trigger.level = NAN; BOOST_CHECK_THROW( scope.Configure( {}, trigger, channel() ), std::invalid_argument );
}
BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_CASE( ScopeHistorySharesImmutableBlocksAndBoundsIncrementalReads )
{
    SCOPE_HISTORY builder( 100 );
    builder.Append( samples( 0, .049, .001, []( double t ) { return t; } ) );
    const auto saved = builder;
    BOOST_CHECK( &builder.front().values.front() == &saved.front().values.front() );
    builder.Append( samples( .04, .15, .001, []( double t ) { return t; } ) );
    BOOST_CHECK_EQUAL( builder.size(), 100 ); BOOST_CHECK_EQUAL( saved.size(), 50 );
    BOOST_CHECK_SMALL( saved.front().time, 1e-12 ); BOOST_CHECK_CLOSE( saved.back().time, .049, 1e-8 );
    int visited = 0;
    builder.ForEachAfter( .14501, [&]( const auto& ) { ++visited; return true; } );
    BOOST_CHECK_EQUAL( visited, 5 );
    SCOPE_TRIGGER trigger; trigger.mode = SCOPE_TRIGGER_MODE::ROLL; SCOPE_ACQUISITION scope;
    scope.Configure( { .002, .5, 0 }, trigger, channel() ); scope.Feed( builder );
    const auto serial = scope.Frame().serial; scope.Feed( builder ); BOOST_CHECK_EQUAL( scope.Frame().serial, serial );
}
BOOST_AUTO_TEST_CASE( DenseScopeHistoryRetainsSlowBlinkAcrossCompactionAndReopening )
{
    SCOPE_HISTORY builder( 256, { channel() } );
    std::vector<SIM_SAMPLE> block;
    for( int i = 0; i <= 450000; ++i )
    {
        const double time = i * 1e-5;
        block.push_back( { time, { int( time ) % 2 ? 5. : 0. } } );
        if( block.size() == 500 || i == 450000 )
        {
            builder.Append( std::move( block ) );
            block.clear();
        }
    }
    BOOST_REQUIRE_LE( builder.size(), 256 );
    BOOST_CHECK_SMALL( builder.front().time, 1e-12 );
    BOOST_CHECK_CLOSE( builder.back().time, 4.5, 1e-8 );
    const auto points = builder.Copy();
    for( std::size_t i = 1; i < points.size(); ++i ) BOOST_CHECK_GT( points[i].time, points[i-1].time );
    const auto frozen = builder;
    builder.Append( { { 4.6, { 0. } } } );
    BOOST_CHECK_CLOSE( frozen.back().time, 4.5, 1e-8 );
    SCOPE_TRIGGER trigger; trigger.mode = SCOPE_TRIGGER_MODE::ROLL;
    SCOPE_ACQUISITION scope; scope.Configure( { .4, .5, 0 }, trigger, channel(), 256 );
    scope.Feed( frozen );
    BOOST_CHECK( scope.Frame().decimated );
    const auto stats = ScopeStatistics( scope.Frame(), channel() );
    BOOST_CHECK_EQUAL( stats.minimum, 0 ); BOOST_CHECK_EQUAL( stats.maximum, 5 );
    BOOST_CHECK_CLOSE( stats.period, 2, 5 );
}
BOOST_AUTO_TEST_CASE( DenseTriggerCapturesKeepBothHalvesAndBothChannelPeaks )
{
    auto other = channel(); other.name = "Other"; other.positive = 1;
    SCOPE_TRIGGER trigger; trigger.mode = SCOPE_TRIGGER_MODE::SINGLE;
    SCOPE_ACQUISITION scope; scope.Configure( { .02, .5, 0 }, trigger, channel(), 256, { channel(), other } );
    std::vector<SIM_SAMPLE> block;
    for( int i = 0; i <= 30000; ++i )
    {
        const double t = i * 1e-5;
        const double peak = i == 6001 ? 20 : i == 24001 ? -7 : 1;
        block.push_back( { t, { t < .15 ? 0. : 5., peak } } );
        if( block.size() == 250 || i == 30000 ) { scope.Feed( block ); block.clear(); }
    }
    const auto& f = scope.Frame(); BOOST_REQUIRE( f.triggerTime );
    BOOST_CHECK( !f.partial ); BOOST_CHECK( f.decimated ); BOOST_CHECK_LE( f.samples.size(), 256 );
    BOOST_CHECK_LE( f.samples.front().time, f.begin ); BOOST_CHECK_GE( f.samples.back().time, f.end );
    BOOST_CHECK_CLOSE( f.end - f.begin, .2, 1e-8 );
    BOOST_CHECK_CLOSE( ScopeValueAt( f, channel(), *f.triggerTime ), 2.5, 1e-6 );
    const auto stats = ScopeStatistics( f, other ); BOOST_CHECK_EQUAL( stats.maximum, 20 ); BOOST_CHECK_EQUAL( stats.minimum, -7 );
}
BOOST_AUTO_TEST_CASE( TriggerWarmupDoesNotPublishMissingPretriggerHalf )
{
    SCOPE_TRIGGER trigger; trigger.mode = SCOPE_TRIGGER_MODE::NORMAL; trigger.level = 0;
    SCOPE_ACQUISITION scope; scope.Configure( { .02, .5, 0 }, trigger, channel(), 512 );
    scope.Feed( samples( 1, 1.08, 1e-4, []( double t ) { return std::sin( 2 * M_PI * 50 * t ); } ) );
    BOOST_CHECK( scope.Frame().samples.empty() );
    scope.Feed( samples( 1.07, 1.5, 1e-4, []( double t ) { return std::sin( 2 * M_PI * 50 * t ); } ) );
    BOOST_REQUIRE( !scope.Frame().samples.empty() ); BOOST_CHECK( !scope.Frame().partial );
    BOOST_CHECK_LE( scope.Frame().samples.front().time, scope.Frame().begin );
}
BOOST_AUTO_TEST_CASE( ZeroMarkerMovesWholeSignalWithoutChangingScaleOrMeasuredVoltage )
{
    SCOPE_CHANNEL_STYLE c; c.perDivision = .5;
    for( bool inverted : { false, true } )
    {
        c.inverted = inverted; c.center = ScopeCenterForZero( .8, c );
        const auto before = ScopeVerticalFraction( 1, c );
        c.center = ScopeCenterForZero( .3, c );
        BOOST_CHECK_CLOSE( ScopeVerticalFraction( 0, c ), .3, 1e-8 );
        BOOST_CHECK_SMALL( ScopeVerticalFraction( 1, c ) - before + .5, 1e-12 );
        for( double v : { -2., 0., .25, 4. } ) BOOST_CHECK_SMALL( ScopeVerticalValue( ScopeVerticalFraction( v, c ), c ) - v, 1e-12 );
        BOOST_CHECK_EQUAL( c.perDivision, .5 );
    }
}
BOOST_AUTO_TEST_CASE( AutomaticRangeKeepsPhysicalZeroVisible )
{
    for( bool inverted : { false, true } )
    for( const auto range : { std::pair( 3., 6. ), std::pair( -24., -12. ), std::pair( -5., 10. ) } )
    {
        SCOPE_CHANNEL_STYLE c; c.inverted = inverted; ScopeFitRange( c, range.first, range.second, .65 );
        BOOST_CHECK_CLOSE( ScopeVerticalFraction( 0, c ), .65, 1e-8 );
        BOOST_CHECK_GT( ScopeVerticalFraction( range.first, c ), 0 ); BOOST_CHECK_LT( ScopeVerticalFraction( range.first, c ), 1 );
        BOOST_CHECK_GT( ScopeVerticalFraction( range.second, c ), 0 ); BOOST_CHECK_LT( ScopeVerticalFraction( range.second, c ), 1 );
    }
}
BOOST_AUTO_TEST_CASE( ChangingTriggerLevelKeepsTheHeldCapture )
{
    SCOPE_TRIGGER trigger; trigger.mode = SCOPE_TRIGGER_MODE::ROLL;
    SCOPE_ACQUISITION scope; scope.Configure( { .002, .5, 0 }, trigger, channel() );
    scope.Feed( samples( 0, .1, .0001, []( double t ) { return t; } ) ); scope.SetHold( true );
    const auto serial = scope.Frame().serial;
    scope.SetTriggerLevel( 3 ); BOOST_CHECK_EQUAL( scope.Frame().serial, serial );
    BOOST_CHECK( scope.State() == SCOPE_CAPTURE_STATE::HOLD );
}

BOOST_AUTO_TEST_CASE( ScopeFrameRevisionNeverRepeatsWhenControlsReconfigureAcquisition )
{
    SCOPE_ACQUISITION scope; SCOPE_TRIGGER trigger; trigger.mode = SCOPE_TRIGGER_MODE::ROLL;
    const auto points = samples( 0, .1, .0001, []( double t ) { return t; } );
    scope.Configure( { .002, .5, 0 }, trigger, channel() ); scope.Feed( points );
    const auto serial = scope.Frame().serial;
    scope.Configure( { .001, .5, 0 }, trigger, channel() );
    BOOST_CHECK_GT( scope.Frame().serial, serial );
    scope.Feed( points ); BOOST_CHECK_GT( scope.Frame().serial, serial );
    BOOST_CHECK_CLOSE( scope.Frame().end - scope.Frame().begin, .01, 1e-8 );
}
