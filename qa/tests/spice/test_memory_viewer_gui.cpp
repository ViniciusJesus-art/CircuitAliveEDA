/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <boost/test/unit_test.hpp>
#include <circuitalive/memory_viewer.h>
#include <circuitalive/session_runner.h>
#include <circuitalive/source_probe.h>
#include <circuitalive/simulation_model.h>
#include <wx/app.h>
#include <wx/button.h>
#include <wx/frame.h>
#include <wx/grid.h>
#include <chrono>
#include <thread>
using namespace CIRCUITALIVE;

class MEMORY_TEST_APP : public wxApp
{
public:
    bool OnInit() override { SetExitOnFrameDelete( false ); return true; }
};
wxIMPLEMENT_APP_NO_MAIN( MEMORY_TEST_APP );

namespace
{
CIRCUIT_SNAPSHOT RamCircuit()
{
    CIRCUIT_COMPONENT ram{ "U1", "Memory_RAM:CY62256-70PC", "CY62256-70PC", {} };
    for( const auto& pin : MODEL_REPOSITORY().SelectModel( ram ).pins )
        ram.pins[pin.defaultPin] = pin.id == "vcc" ? "vcc" : pin.id == "we" ? "write"
            : pin.id == "oe" ? "vcc" : pin.role == "bidirectional" ? "data" : "0";
    SOURCE_PROBE_SETTINGS write; write.initialHigh = true;
    return BuildCircuitSnapshot( { { "V1", "Simulation_SPICE:VDC", "5", { { "1", "vcc" }, { "2", "0" } } },
        SourceProbeComponent( PROBE_KIND::LOGIC, {}, "#P1", "data" ),
        SourceProbeComponent( PROBE_KIND::LOGIC, write, "#P2", "write" ), ram } );
}
template<typename Predicate> void Await( Predicate predicate )
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 5 );
    for( ;; )
    {
        wxTheApp->Yield( true );
        if( predicate() ) return;
        BOOST_REQUIRE( std::chrono::steady_clock::now() < deadline );
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
}
wxButton* FindButton( wxWindow* parent, const wxString& label )
{
    for( auto* child : parent->GetChildren() )
    {
        if( auto* button = dynamic_cast<wxButton*>( child ); button && button->GetLabel() == label ) return button;
        if( auto* button = FindButton( child, label ) ) return button;
    }
    return nullptr;
}
struct MEMORY_GUI
{
    wxFrame* parent = new wxFrame( nullptr, wxID_ANY, "Memory viewer GUI regression" );
    std::shared_ptr<SESSION_RUNNER> runner = std::make_shared<SESSION_RUNNER>( CIRCUITALIVE_TEST_WORKER );
    MEMORY_GUI() { parent->Show(); }
    ~MEMORY_GUI()
    {
        parent->Destroy();
        wxTheApp->Yield( true );
        runner.reset();
    }
    void State( EDITOR_SESSION_STATE state )
    {
        Await( [&]
        {
            const auto view = runner->View();
            BOOST_REQUIRE_MESSAGE( view.state != EDITOR_SESSION_STATE::FAILED, view.error );
            return view.state == state;
        } );
    }
};
}

BOOST_FIXTURE_TEST_CASE( ModelessMemoryViewerTracksCircuitWritesAndClearsCachedHighlights, MEMORY_GUI )
{
    const auto circuit = RamCircuit();
    auto initial = circuit.config.memories;
    initial[0].id = "main"; // Open before Play, just like the editor's initial-image path.
    auto* viewer = ShowMemoryViewer( parent, "U1", initial, runner );
    auto* grid = dynamic_cast<wxGrid*>( wxWindow::FindWindowByName( "memory_grid", viewer ) );
    BOOST_REQUIRE( grid );
    BOOST_CHECK( parent->IsEnabled() );
    BOOST_CHECK_EQUAL( grid->GetCellValue( 0, 0 ), "00" );
    BOOST_CHECK( ShowMemoryViewer( parent, "U1", initial, runner ) == viewer );
    runner->Start( circuit ); State( EDITOR_SESSION_STATE::RUNNING );
    Await( [&] { return !grid->IsShown(); } );
    runner->ToggleSwitch( 0 ); // Write before the very first pause.
    Await( [&] { return runner->View().switches[0]; } );
    runner->ToggleSwitch( 1 );
    Await( [&] { return !runner->View().switches[1]; } );
    const auto writeTime = runner->View().sample.latest.time;
    Await( [&] { return runner->View().sample.latest.time > writeTime; } );
    runner->ToggleSwitch( 1 );
    Await( [&] { return runner->View().switches[1]; } );
    runner->Pause(); State( EDITOR_SESSION_STATE::PAUSED );
    Await( [&] { return grid->IsShown() && grid->GetCellValue( 0, 0 ) == "FF"; } );
    const wxColour yellow( 255, 214, 100 );
    BOOST_CHECK( grid->GetCellBackgroundColour( 0, 0 ) == yellow );
    BOOST_CHECK( grid->GetCellBackgroundColour( 0, 1 ) != yellow );
    auto* refresh = FindButton( viewer, "Re-Capture" );
    BOOST_REQUIRE( refresh );
    auto* exportButton = FindButton( viewer, "Export…" );
    BOOST_REQUIRE( exportButton );
    BOOST_CHECK_GT( refresh->GetScreenPosition().x, exportButton->GetScreenPosition().x );
    const auto before = runner->View().memorySequence;
    wxCommandEvent capture( wxEVT_BUTTON, refresh->GetId() );
    capture.SetEventObject( refresh ); refresh->GetEventHandler()->ProcessEvent( capture );
    Await( [&] { return runner->View().memorySequence > before && refresh->IsEnabled(); } );
    BOOST_CHECK( grid->GetCellBackgroundColour( 0, 0 ) == yellow );
    grid->SetGridCursor( 256, 0 ); grid->MakeCellVisible( 256, 0 );
    runner->Resume(); State( EDITOR_SESSION_STATE::RUNNING );
    Await( [&] { return !grid->IsShown(); } );
    runner->Pause(); State( EDITOR_SESSION_STATE::PAUSED );
    Await( [&] { return grid->IsShown() && refresh->IsEnabled(); } );
    BOOST_CHECK( grid->GetCellBackgroundColour( 0, 0 ) != yellow );
    BOOST_CHECK_EQUAL( grid->GetGridCursorRow(), 256 );
    runner->Stop(); State( EDITOR_SESSION_STATE::STOPPED );
    runner->Start( circuit ); State( EDITOR_SESSION_STATE::RUNNING );
    Await( [&] { return !grid->IsShown(); } );
    runner->Pause(); State( EDITOR_SESSION_STATE::PAUSED );
    Await( [&] { return grid->IsShown() && refresh->IsEnabled(); } );
    BOOST_CHECK_EQUAL( grid->GetCellValue( 0, 0 ), "00" );
    BOOST_CHECK( grid->GetCellBackgroundColour( 0, 0 ) != yellow );
    runner.reset();
    Await( [&] { return !grid->IsShown(); } ); // Closing/replacing the editor's runner cannot leave a dangling pointer.
}

bool InitTests() { return true; }
int main( int argc, char** argv )
{
    if( !wxEntryStart( argc, argv ) ) return 1;
    if( !wxTheApp->CallOnInit() ) { wxEntryCleanup(); return 1; }
    const int result = boost::unit_test::unit_test_main( &InitTests, argc, argv );
    wxTheApp->OnExit(); wxEntryCleanup();
    return result;
}
