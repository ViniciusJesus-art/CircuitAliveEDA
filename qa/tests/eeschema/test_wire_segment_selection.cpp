/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <qa_utils/wx_utils/unit_test_utils.h>
#include <tools/sch_tool_utils.h>
#include <lib_symbol.h>
#include <sch_bus_entry.h>
#include <sch_junction.h>
#include <sch_label.h>
#include <sch_line.h>
#include <sch_pin.h>
#include <sch_screen.h>
#include <sch_sheet.h>
#include <sch_sheet_pin.h>
#include <sch_symbol.h>

namespace
{
struct WIRES
{
    SCH_SCREEN screen{ nullptr };

    SCH_LINE* Add( VECTOR2I start, VECTOR2I end, int layer = LAYER_WIRE )
    {
        auto* line = new SCH_LINE( start, layer );
        line->SetEndPoint( end );
        screen.Append( line, false );
        return line;
    }

    std::set<SCH_LINE*> Run( SCH_LINE* line ) { return CollectWireSegment( screen, nullptr, *line ); }
};
}

BOOST_FIXTURE_TEST_SUITE( WireSegmentSelection, WIRES )

BOOST_AUTO_TEST_CASE( CornersAndCollinearPiecesBelongToOneRun )
{
    auto* a = Add({0,1000},{0,0});
    auto* b = Add({0,0},{1000,0});
    auto* c = Add({1000,0},{2000,0});
    auto* d = Add({2000,0},{2000,1000});
    const std::set<SCH_LINE*> expected{a,b,c,d};
    for( SCH_LINE* line : expected ) BOOST_CHECK(Run(line) == expected);
}

BOOST_AUTO_TEST_CASE( ForkStopsInEveryDirectionWithOrWithoutADot )
{
    auto* a = Add({0,1000},{0,0});
    auto* b = Add({0,0},{1000,0});
    auto* c = Add({1000,0},{1000,1000});
    auto* d = Add({1000,1000},{2000,1000});
    auto* e = Add({1000,1000},{1000,2000});
    for( bool dot : {false,true} )
    {
        if(dot) screen.Append(new SCH_JUNCTION({1000,1000}),false);
        BOOST_CHECK(Run(b) == (std::set<SCH_LINE*>{a,b,c}));
        BOOST_CHECK(Run(d) == (std::set<SCH_LINE*>{d}));
        BOOST_CHECK(Run(e) == (std::set<SCH_LINE*>{e}));
    }
}

BOOST_AUTO_TEST_CASE( BranchOnAnUnsplitThroughWireIsNotACorner )
{
    Add({0,0},{2000,0});
    auto* branch = Add({1000,0},{1000,1000});
    BOOST_CHECK(Run(branch) == (std::set<SCH_LINE*>{branch}));
}

BOOST_AUTO_TEST_CASE( UnconnectedCrossingsAndRemoteSameNameLabelsDoNotJoin )
{
    auto* a = Add({0,1000},{0,0});
    auto* b = Add({0,0},{2000,0});
    Add({1000,-1000},{1000,1000});
    auto* remote = Add({4000,0},{5000,0});
    screen.Append(new SCH_LABEL({0,0},"DATA"),false);
    screen.Append(new SCH_LABEL({4000,0},"DATA"),false);
    BOOST_CHECK(Run(a) == (std::set<SCH_LINE*>{a,b}));
    BOOST_CHECK(Run(remote) == (std::set<SCH_LINE*>{remote}));
}

BOOST_AUTO_TEST_CASE( ClosedLoopsTerminateAndGraphicsAndBusesRemainSeparate )
{
    auto* a = Add({0,0},{1000,0});
    auto* b = Add({1000,0},{1000,1000});
    auto* c = Add({1000,1000},{0,1000});
    auto* d = Add({0,1000},{0,0});
    auto* bus = Add({0,0},{-1000,0},LAYER_BUS);
    auto* graphic = Add({0,0},{0,-1000},LAYER_NOTES);
    BOOST_CHECK(Run(a) == (std::set<SCH_LINE*>{a,b,c,d}));
    BOOST_CHECK(Run(bus).empty());
    BOOST_CHECK(Run(graphic).empty());
}

BOOST_AUTO_TEST_CASE( SymbolPinStopsTheRunWithoutSelectingTheSymbol )
{
    LIB_SYMBOL lib("Terminal");
    auto* pin = new SCH_PIN(&lib);
    pin->SetNumber("1"); pin->SetUnit(1); pin->SetBodyStyle(1);
    pin->SetPosition({0,0}); pin->SetLength(500);
    lib.AddDrawItem(pin);
    auto* symbol = new SCH_SYMBOL(lib,LIB_ID("Test","Terminal"),nullptr,1,1,{1000,0});
    screen.Append(symbol,false);
    auto* a = Add({0,0},{1000,0});
    auto* b = Add({1000,0},{1000,1000});
    BOOST_CHECK(Run(a) == (std::set<SCH_LINE*>{a}));
    BOOST_CHECK(Run(b) == (std::set<SCH_LINE*>{b}));
    BOOST_CHECK(!symbol->IsSelected());
}

BOOST_AUTO_TEST_CASE( SheetPinsAndBusEntriesStopTraversal )
{
    auto* sheet = new SCH_SHEET(nullptr,{1000,0},{1000,1000});
    auto* pin = new SCH_SHEET_PIN(sheet,{1000,0},"IN");
    sheet->AddPin(pin);
    screen.Append(sheet,false);
    auto* a = Add({0,0},{1000,0});
    Add({1000,0},{1000,-1000});
    BOOST_CHECK(Run(a) == (std::set<SCH_LINE*>{a}));

    auto* entry = new SCH_BUS_WIRE_ENTRY({4000,0});
    screen.Append(entry,false);
    const auto point = entry->GetPosition();
    auto* b = Add(point-VECTOR2I(1000,0),point);
    Add(point,point+VECTOR2I(0,1000));
    BOOST_CHECK(Run(b) == (std::set<SCH_LINE*>{b}));
}

BOOST_AUTO_TEST_SUITE_END()
