/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <component_chooser_search.h>

BOOST_AUTO_TEST_SUITE( ComponentChooserSearch )

BOOST_AUTO_TEST_CASE( NaturalQueriesSeparateSymbolAndPhysicalRequirements )
{
    auto resistor = CIRCUITALIVE::ParseComponentQuery( "resistor 1/4W" );
    BOOST_CHECK_EQUAL( resistor.symbols, "resistor" );
    BOOST_CHECK_EQUAL( resistor.footprint, "0.25w" );
    auto dip = CIRCUITALIVE::ParseComponentQuery( "DIP 14" );
    BOOST_CHECK( dip.symbols.empty() );
    BOOST_CHECK_EQUAL( dip.package, "dip" );
    BOOST_CHECK_EQUAL( dip.pins, 14 );
    auto led = CIRCUITALIVE::ParseComponentQuery( "LED 5mm" );
    BOOST_CHECK_EQUAL( led.symbols, "led" );
    BOOST_CHECK_EQUAL( led.footprint, "5mm" );
    BOOST_CHECK_EQUAL( CIRCUITALIVE::ParseComponentQuery( "74HC00" ).symbols, "74hc00" );
}

BOOST_AUTO_TEST_CASE( PhysicalQueriesKeepExactPinCountsAndDecimalDimensions )
{
    auto query = CIRCUITALIVE::ParseComponentQuery( "74HC00 SMD SOIC-14 10,5x8mm" );
    BOOST_CHECK_EQUAL( query.symbols, "74hc00" );
    BOOST_CHECK_EQUAL( query.mounting, "smd" );
    BOOST_CHECK_EQUAL( query.package, "soic" );
    BOOST_CHECK_EQUAL( query.pins, 14 );
    BOOST_CHECK_EQUAL( query.width, 10.5 );
    BOOST_CHECK_EQUAL( query.height, 8 );
    BOOST_CHECK_EQUAL( CIRCUITALIVE::ParseComponentQuery( "DIP 140" ).pins, 140 );
    BOOST_CHECK_EQUAL( CIRCUITALIVE::ParseComponentQuery( "DIP 99999999999999999999999999" ).pins, 0 );
}

BOOST_AUTO_TEST_CASE( EquivalentPhysicalTermsMatchLibraryMetadata )
{
    BOOST_CHECK( CIRCUITALIVE::MatchesPhysicalText( "Axial resistor 0.25 W", "1/4W" ) );
    BOOST_CHECK( CIRCUITALIVE::MatchesPhysicalText( "LED_THT:LED_D5.0mm", "5mm" ) );
    BOOST_CHECK( CIRCUITALIVE::MatchesPhysicalText( "resistor 250mW", "0.25W" ) );
    BOOST_CHECK( !CIRCUITALIVE::MatchesPhysicalText( "resistor 0.125W", "1/4W" ) );
    BOOST_CHECK( !CIRCUITALIVE::MatchesPhysicalText( "LED_D3.0mm", "5mm" ) );
    BOOST_CHECK( !CIRCUITALIVE::MatchesPhysicalText( "LED_D15.0mm", "5mm" ) );
}

BOOST_AUTO_TEST_CASE( PersonalDefaultsOnlyReplaceLibraryAssignmentsWhenExplicit )
{
    using CIRCUITALIVE::PreferredComponentFootprint;
    BOOST_CHECK_EQUAL( PreferredComponentFootprint( "", "Package_DIP:DIP-14", false ), "Package_DIP:DIP-14" );
    BOOST_CHECK_EQUAL( PreferredComponentFootprint( "SOIC-14", "DIP-14", false ), "SOIC-14" );
    BOOST_CHECK_EQUAL( PreferredComponentFootprint( "SOIC-14", "DIP-14", true ), "DIP-14" );
    BOOST_CHECK_EQUAL( PreferredComponentFootprint( "SOIC-14", "", true ), "" );
    BOOST_CHECK_EQUAL( PreferredComponentFootprint( "SOIC-14", "", false ), "SOIC-14" );
}

BOOST_AUTO_TEST_CASE( AutomaticDefaultsOnlyFillAnUnassignedSymbolOnce )
{
    using CIRCUITALIVE::RememberFirstComponentFootprint;
    BOOST_CHECK( RememberFirstComponentFootprint( "", false, "DIP-14" ) );
    BOOST_CHECK( !RememberFirstComponentFootprint( "", false, "" ) );
    BOOST_CHECK( !RememberFirstComponentFootprint( "", true, "DIP-14" ) );
    BOOST_CHECK( !RememberFirstComponentFootprint( "SOIC-14", false, "DIP-14" ) );
    BOOST_CHECK( !RememberFirstComponentFootprint( "SOIC-14", true, "" ) );
}

BOOST_AUTO_TEST_CASE( LegacyPackagePatternsKeepModernCompatiblePackagesDiscoverable )
{
    using CIRCUITALIVE::AlternatePackagePattern;
    BOOST_CHECK_EQUAL( AlternatePackagePattern( "SO14*" ), "soic-14_*" );
    BOOST_CHECK_EQUAL( AlternatePackagePattern( "SO14" ), "soic-14" );
    BOOST_CHECK_EQUAL( AlternatePackagePattern( "TSSOP14*" ), "tssop-14_*" );
    BOOST_CHECK_EQUAL( AlternatePackagePattern( "DIP*W7.62mm*" ), "dip*w7.62mm*" );
    BOOST_CHECK_EQUAL( AlternatePackagePattern( "Package_SO:SOIC-14*" ), "package_so:soic-14*" );
}

BOOST_AUTO_TEST_SUITE_END()
