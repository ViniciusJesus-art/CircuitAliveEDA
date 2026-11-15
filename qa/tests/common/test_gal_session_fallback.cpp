/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <wx/app.h>
#include <wx/frame.h>
#include <wx/modalhook.h>
#include <qa_utils/wx_utils/unit_test_utils.h>
#include <class_draw_panel_gal.h>
#include <gal/gal_display_options.h>
#include <gal/opengl/opengl_gal.h>
#include <gal/opengl/gpu_oom_error.h>
#include <pgm_base.h>
#include <settings/kicad_settings.h>
#include <settings/settings_manager.h>
#include <iostream>
#include <stdexcept>

class SMOKE_APP : public wxApp
{
public:
    bool OnInit() override { return true; }
};
wxIMPLEMENT_APP_NO_MAIN( SMOKE_APP );

class SMOKE_PGM : public PGM_BASE
{
public:
    void MacOpenFile( const wxString& ) override {}
};

class TEST_CANVAS : public EDA_DRAW_PANEL_GAL
{
public:
    using EDA_DRAW_PANEL_GAL::EDA_DRAW_PANEL_GAL;

    bool SimulateOpenGlFailure()
    {
        return recoverFromGalError( KIGFX::GPU_OOM_ERROR( "Simulated OpenGL failure" ) );
    }
};

static void require( bool condition, const char* error )
{
    if( !condition ) throw std::runtime_error( error );
}

class WARNING_HOOK : public wxModalDialogHook
{
public:
    wxFrame* parent;
    KIGFX::GAL_DISPLAY_OPTIONS& options;
    int warnings = 0;

    WARNING_HOOK( wxFrame* frame, KIGFX::GAL_DISPLAY_OPTIONS& opts ) : parent( frame ), options( opts ) {}

    int Enter( wxDialog* ) override
    {
        ++warnings;
        require( warnings == 1, "More than one graphics warning in the same process" );
        // A modal warning may create another preview through a nested event loop.
        auto* nested = new EDA_DRAW_PANEL_GAL( parent, wxID_ANY, wxDefaultPosition,
                wxSize( 80, 80 ), options, EDA_DRAW_PANEL_GAL::GAL_TYPE_OPENGL );
        require( nested->GetBackend() == EDA_DRAW_PANEL_GAL::GAL_FALLBACK,
                 "Reentrant preview retried OpenGL" );
        delete nested;
        return wxID_OK;
    }
};

int main( int argc, char** argv )
{
    try
    {
        if( !EDA_DRAW_PANEL_GAL::GAL_FALLBACK_AVAILABLE || !wxEntryStart( argc, argv ) )
            return 77;

        require( wxTheApp->CallOnInit(), "Cannot initialize GUI application" );
        KI_TEST::SetMockConfigDir();
        SetPgm( new SMOKE_PGM );
        // Unit-test initialization intentionally stops before loading application settings.
        Pgm().InitPgm( true, true, true );
        Pgm().GetSettingsManager().RegisterSettings( new KICAD_SETTINGS, false );
        Pgm().GetSettingsManager().Load();
        Pgm().GetSettingsManager().LoadProject( "" );
        require( !KIGFX::OPENGL_GAL::IsDisabledForSession(), "A new process retained old fallback state" );

        KIGFX::GAL_DISPLAY_OPTIONS options;
        auto* first = new wxFrame( nullptr, wxID_ANY, "First editor" );
        wxTheApp->SetTopWindow( first );
        WARNING_HOOK hook( first, options );
        hook.Register();

        auto* canvas = new TEST_CANVAS( first, wxID_ANY, wxDefaultPosition,
                wxSize( 80, 80 ), options, EDA_DRAW_PANEL_GAL::GAL_TYPE_OPENGL );
        std::cout << "Initial backend: " << ( canvas->GetBackend() == EDA_DRAW_PANEL_GAL::GAL_TYPE_OPENGL
                ? "OpenGL" : "software" ) << '\n';
        if( canvas->GetBackend() == EDA_DRAW_PANEL_GAL::GAL_TYPE_OPENGL )
            require( canvas->SimulateOpenGlFailure(), "Runtime OpenGL failure did not fall back" );
        require( canvas->GetBackend() == EDA_DRAW_PANEL_GAL::GAL_FALLBACK, "Unsupported OpenGL did not fall back" );
        require( hook.warnings == 1, "The first OpenGL failure did not warn once" );
        delete canvas;
        first->Destroy();
        wxTheApp->ProcessPendingEvents();

        for( int i = 0; i < 4; ++i )
        {
            auto* frame = new wxFrame( nullptr, wxID_ANY, "Another editor or preview" );
            wxTheApp->SetTopWindow( frame );
            auto* next = new EDA_DRAW_PANEL_GAL( frame, wxID_ANY, wxDefaultPosition,
                    wxSize( 80, 80 ), options, EDA_DRAW_PANEL_GAL::GAL_TYPE_OPENGL );
            require( next->GetBackend() == EDA_DRAW_PANEL_GAL::GAL_FALLBACK, "Another window retried OpenGL" );
            require( next->SwitchBackend( EDA_DRAW_PANEL_GAL::GAL_TYPE_OPENGL ), "Backend switch failed" );
            require( next->GetBackend() == EDA_DRAW_PANEL_GAL::GAL_FALLBACK, "Explicit switch retried OpenGL" );
            require( hook.warnings == 1, "Another window repeated the warning" );
            delete next;
            frame->Destroy();
            wxTheApp->ProcessPendingEvents();
        }

        std::cout << "PASS: one warning, nested preview and four recreated windows use software; restart starts fresh\n";
        return 0;
    }
    catch( const std::exception& error )
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
