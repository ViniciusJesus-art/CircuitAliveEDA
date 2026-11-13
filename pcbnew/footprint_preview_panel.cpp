/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * Copyright (C) 2017 Chris Pavlina <pavlina.chris@gmail.com>
 * Copyright (C) 2016 Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <memory>
#include <optional>
#include <utility>

#include "pcbnew_settings.h"
#include <advanced_config.h>
#include <base_units.h>
#include <board.h>
#include <footprint.h>
#include <pad.h>
#include <pcb_dimension.h>
#include <dpi_scaling_common.h>
#include <eda_draw_frame.h>
#include <footprint_preview_panel.h>
#include <footprint_library_adapter.h>
#include <gal/graphics_abstraction_layer.h>
#include <kiway.h>
#include <math/box2.h>
#include <pcb_painter.h>
#include <pcb_draw_panel_gal.h>
#include <pcb_edit_frame.h>
#include <pgm_base.h>
#include <settings/settings_manager.h>
#include <view/view.h>
#include <wx/stattext.h>
#include <dialog_shim.h>
#include <project_pcb.h>
#include <3d_canvas/eda_3d_canvas.h>
#include <3d_rendering/track_ball.h>
#include <3d_viewer/eda_3d_viewer_settings.h>
#include <common_ogl/ogl_attr_list.h>
#include <wx/weakref.h>
#include <board_design_settings.h>
#include <board_stackup_manager/board_stackup.h>
#include <pcb_field.h>
#include <pcb_text.h>

class FOOTPRINT_PREVIEW_PANEL::PREVIEW_3D
{
public:
    EDA_3D_VIEWER_SETTINGS settings;
    BOARD_ADAPTER adapter;
    TRACK_BALL camera{ 2 * RANGE_SCALE_3D };
    wxWeakRef<wxPanel> panel;
    wxWeakRef<EDA_3D_CANVAS> canvas;
    wxWeakRef<wxStaticText> status;
    S3D_CACHE* cache = nullptr;
    std::map<wxString, BBOX_3D> modelBounds;
    std::optional<std::pair<BBOX_3D, bool>> pendingFit;
    unsigned generation = 0;
    bool stopped = false;

    BBOX_3D Bounds( const FOOTPRINT& footprint, const wxString& basePath )
    {
        BBOX_3D result;
        glm::mat4 placement( 1.f );
        const auto pos = footprint.GetPosition();
        placement = glm::translate( placement, SFVEC3F( pcbIUScale.IUTomm( pos.x ),
                                                        -pcbIUScale.IUTomm( pos.y ), 0 ) );
        placement = glm::rotate( placement, float( footprint.GetOrientation().AsRadians() ), SFVEC3F( 0, 0, 1 ) );
        if( footprint.IsFlipped() )
        {
            placement = glm::rotate( placement, glm::pi<float>(), SFVEC3F( 0, 1, 0 ) );
            placement = glm::rotate( placement, glm::pi<float>(), SFVEC3F( 0, 0, 1 ) );
        }
        for( const auto& model : footprint.Models() )
        {
            if( !model.m_Show || model.m_Filename.empty() ) continue;
            auto it = modelBounds.find( model.m_Filename );
            if( it == modelBounds.end() )
            {
                const auto* data = cache->GetModel( model.m_Filename, basePath,
                        { footprint.GetEmbeddedFiles(), footprint.GetBoard()->GetEmbeddedFiles() } );
                if( !data || !data->m_Meshes ) continue;
                BBOX_3D bounds;
                for( unsigned mesh = 0; mesh < data->m_MeshesSize; ++mesh )
                {
                    const auto& vertices = data->m_Meshes[mesh];
                    if( !vertices.m_Positions ) continue;
                    for( unsigned vertex = 0; vertex < vertices.m_VertexSize; ++vertex )
                        bounds.Union( vertices.m_Positions[vertex] );
                }
                if( !bounds.IsInitialized() ) continue;
                it = modelBounds.emplace( model.m_Filename, bounds ).first;
            }
            auto transform = glm::translate( placement, SFVEC3F( model.m_Offset.x, model.m_Offset.y, model.m_Offset.z ) );
            transform = glm::rotate( transform, glm::radians( float( -model.m_Rotation.z ) ), SFVEC3F( 0, 0, 1 ) );
            transform = glm::rotate( transform, glm::radians( float( -model.m_Rotation.y ) ), SFVEC3F( 0, 1, 0 ) );
            transform = glm::rotate( transform, glm::radians( float( -model.m_Rotation.x ) ), SFVEC3F( 1, 0, 0 ) );
            transform = glm::scale( transform, SFVEC3F( model.m_Scale.x, model.m_Scale.y, model.m_Scale.z ) );
            for( unsigned corner = 0; corner < 8; ++corner )
            {
                const auto& lo = it->second.Min();
                const auto& hi = it->second.Max();
                result.Union( SFVEC3F( transform * glm::vec4( corner & 1 ? hi.x : lo.x,
                        corner & 2 ? hi.y : lo.y, corner & 4 ? hi.z : lo.z, 1.f ) ) );
            }
        }
        return result;
    }

    void Fit( BBOX_3D bounds, bool flipped )
    {
        const auto version = generation;
        canvas->CallAfter( [this, bounds, flipped, version]()
        {
            if( stopped || !canvas || generation != version ) return;
            const float scale = pcbIUScale.mmToIU( 1 ) * adapter.BiuTo3dUnits();
            if( !std::isfinite( scale ) || scale <= 0 ) return;
            auto center = bounds.GetCenter() * scale;
            center.z += adapter.GetFootprintZPos( flipped );
            const float radius = glm::length( bounds.Max() - bounds.Min() ) * .5f * scale;
            const auto size = canvas->GetClientSize();
            const float aspect = size.y > 0 ? float( size.x ) / size.y : 1.f;
            const float tangent = camera.GetFrustum().tang * std::min( aspect, 1.f );
            if( !std::isfinite( radius ) || !std::isfinite( tangent ) || radius <= 0 || tangent <= 0 ) return;
            const float distance = radius / std::sin( std::atan( tangent ) ) * 1.15f;
            camera.SetBoardLookAtPos( center );
            camera.ZoomReset();
            camera.Zoom( glm::length( camera.GetCameraInitPos() ) / distance );
            canvas->Request_refresh();
        } );
    }

    ~PREVIEW_3D()
    {
        Stop();
        if( canvas ) delete canvas.get();
    }

    void Stop()
    {
        if( stopped ) return;
        stopped = true;
        if( canvas )
        {
            canvas->Show();
            wxCloseEvent event;
            canvas->OnCloseWindow( event );
            canvas->SetEvtHandlerEnabled( false );
        }
    }
};

wxWindow* FOOTPRINT_PREVIEW_PANEL::Create3DPreview( wxWindow* aParent )
{
    if( m_preview3D ) return m_preview3D->panel.get();
    m_preview3D = std::make_unique<PREVIEW_3D>();
    auto& preview = *m_preview3D;
    preview.panel = new wxPanel( aParent );
    auto* sizer = new wxBoxSizer( wxVERTICAL );
    preview.status = new wxStaticText( preview.panel.get(), wxID_ANY, _( "Select a footprint" ) );
    preview.status->Wrap( FromDIP( 260 ) );
    sizer->Add( preview.status.get(), 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 5 );
    preview.adapter.SetBoard( m_dummyBoard.get() );
    preview.adapter.m_IsBoardView = false;
    preview.adapter.m_IsPreviewer = true;
    const auto* viewerSettings = GetAppSettings<EDA_3D_VIEWER_SETTINGS>( "3d_viewer" );
    preview.settings.m_Render = viewerSettings->m_Render;
    preview.settings.m_Camera = viewerSettings->m_Camera;
    preview.settings.m_UseStackupColors = viewerSettings->m_UseStackupColors;
    preview.settings.m_Render.preview_show_board_body = false;
    preview.settings.m_Render.engine = RENDER_ENGINE::OPENGL;
    preview.adapter.m_Cfg = &preview.settings;
    preview.cache = PROJECT_PCB::Get3DCacheManager( &Prj() );
    auto& settings = m_dummyBoard->GetDesignSettings();
    settings.SetBoardThickness( pcbIUScale.mmToIU( 1.6 ) );
    settings.SetEnabledLayers( LSET::FrontMask() | LSET::BackMask() );
    auto& stackup = settings.GetStackupDescriptor();
    stackup.RemoveAll();
    stackup.BuildDefaultStackupList( &settings, 2 );
    // Keep the blue Z axis pointing up in the initial perspective.
    preview.camera.RotateX( glm::radians( -60.f ) );
    preview.camera.RotateZ( glm::radians( -35.f ) );
    const auto& attrs = OGL_ATT_LIST::GetAttributesList( ANTIALIASING_MODE::AA_NONE );
    if( wxGLCanvas::IsDisplaySupported( attrs ) )
    {
        preview.canvas = new EDA_3D_CANVAS( preview.panel.get(), attrs, preview.adapter,
                                            preview.camera, PROJECT_PCB::Get3DCacheManager( &Prj() ) );
        preview.canvas->Bind( wxEVT_PAINT, [&preview]( wxPaintEvent& event )
        {
            // The native paint reloads the board scale and camera target. Fit only
            // after that paint, including the first paint of a newly shown canvas.
            event.Skip();
            if( preview.pendingFit )
            {
                const auto [bounds, flipped] = *preview.pendingFit;
                preview.pendingFit.reset();
                preview.Fit( bounds, flipped );
            }
        } );
        sizer->Add( preview.canvas.get(), 1, wxEXPAND );
        preview.canvas->Hide();
    }
    else
        preview.status->SetLabel( _( "3D preview is unavailable on this display" ) );
    preview.panel->SetSizer( sizer );
    return preview.panel.get();
}

void FOOTPRINT_PREVIEW_PANEL::Update3DPreview( bool aHasFootprint )
{
    if( !m_preview3D || m_preview3D->stopped || !m_preview3D->canvas ) return;
    auto& preview = *m_preview3D;
    ++preview.generation;
    preview.pendingFit.reset();
    const bool selected = aHasFootprint && m_currentFootprint;
    const auto bounds = selected ? preview.Bounds( *m_currentFootprint, Prj().GetProjectPath() ) : BBOX_3D{};
    const bool model = bounds.IsInitialized();
    preview.status->SetLabel( !selected ? _( "Select a footprint" )
                                        : _( "This footprint has no 3D model" ) );
    preview.status->Show( !model );
    preview.canvas->Show( model );
    preview.panel->Layout();
    if( model )
    {
        preview.pendingFit.emplace( bounds, m_currentFootprint->IsFlipped() );
        preview.canvas->ReloadRequest();
        preview.canvas->Request_refresh();
    }
}

void FOOTPRINT_PREVIEW_PANEL::Shutdown3DPreview()
{
    if( m_preview3D ) m_preview3D->Stop();
}


FOOTPRINT_PREVIEW_PANEL::FOOTPRINT_PREVIEW_PANEL( KIWAY* aKiway, wxWindow* aParent,
                                                  UNITS_PROVIDER* aUnitsProvider,
                                                  std::unique_ptr<KIGFX::GAL_DISPLAY_OPTIONS> aOpts,
                                                  GAL_TYPE aGalType ) :
        PCB_DRAW_PANEL_GAL( aParent, -1, wxPoint( 0, 0 ), wxSize( 200, 200 ), *aOpts, aGalType ),
        KIWAY_HOLDER( aKiway, KIWAY_HOLDER::PANEL ),
        m_displayOptions( std::move( aOpts ) )
{
    SetStealsFocus( false );
    ShowScrollbars( wxSHOW_SB_NEVER, wxSHOW_SB_NEVER );
    EnableScrolling( false, false );    // otherwise Zoom Auto disables GAL canvas

    m_userUnits = aUnitsProvider->GetUserUnits();

    m_dummyBoard = std::make_unique<BOARD>();
    m_dummyBoard->SetUserUnits( m_userUnits );
    m_dummyBoard->SetBoardUse( BOARD_USE::FPHOLDER );
    UpdateColors();
    SyncLayersVisibility( m_dummyBoard.get() );

    Raise();
    Show( true );
    StartDrawing();

    Bind( wxEVT_SIZE, &FOOTPRINT_PREVIEW_PANEL::onSize, this );
}


FOOTPRINT_PREVIEW_PANEL::~FOOTPRINT_PREVIEW_PANEL( )
{
    m_preview3D.reset();
    ClearViewAndData();
}


void FOOTPRINT_PREVIEW_PANEL::ClearViewAndData()
{
    m_dummyBoard->DetachAllFootprints();

    if( m_currentFootprint )
        GetView()->Remove( m_currentFootprint.get() );

    if( m_otherFootprint )
        GetView()->Remove( m_otherFootprint.get() );

    GetView()->Clear();

    m_currentFootprint = nullptr;
    m_otherFootprint = nullptr;
}


const COLOR4D& FOOTPRINT_PREVIEW_PANEL::GetBackgroundColor() const
{
    KIGFX::PAINTER* painter = GetView()->GetPainter();
    auto settings = static_cast<KIGFX::PCB_RENDER_SETTINGS*>( painter->GetSettings() );

    return settings->GetBackgroundColor();
}


const COLOR4D& FOOTPRINT_PREVIEW_PANEL::GetForegroundColor() const
{
    KIGFX::PAINTER* painter = GetView()->GetPainter();
    auto settings = static_cast<KIGFX::PCB_RENDER_SETTINGS*>( painter->GetSettings() );

    return settings->GetLayerColor( F_Fab );
}


void FOOTPRINT_PREVIEW_PANEL::renderFootprint( std::shared_ptr<FOOTPRINT> aFootprint )
{
    if( m_preview3D )
    {
        aFootprint->Reference().SetVisible( false );
        aFootprint->Value().SetVisible( false );
        for( auto* item : aFootprint->GraphicalItems() )
            if( auto* text = dynamic_cast<PCB_TEXT*>( item ); text
                && ( text->GetText().Contains( "${REFERENCE}" ) || text->GetText().Contains( "${VALUE}" ) ) )
                text->SetText( wxEmptyString );
    }
    m_dummyBoard->Add( aFootprint.get() );

    INSPECTOR_FUNC inspector =
            [&]( EDA_ITEM* descendant, void* aTestData )
            {
                static_cast<PCB_DIMENSION_BASE*>( descendant )->UpdateUnits();
                return INSPECT_RESULT::CONTINUE;
            };

    aFootprint->Visit( inspector, nullptr, { PCB_DIM_LEADER_T,
                                             PCB_DIM_ALIGNED_T,
                                             PCB_DIM_ORTHOGONAL_T,
                                             PCB_DIM_CENTER_T,
                                             PCB_DIM_RADIAL_T } );

    for( PAD* pad : aFootprint->Pads() )
        pad->SetPinFunction( m_pinFunctions[ pad->GetNumber() ] );

    // Ensure we are not using the high contrast mode to display the selected footprint
    KIGFX::PAINTER* painter = GetView()->GetPainter();
    auto settings = static_cast<KIGFX::PCB_RENDER_SETTINGS*>( painter->GetSettings() );
    settings->m_ContrastModeDisplay = HIGH_CONTRAST_MODE::NORMAL;

    GetView()->Add( aFootprint.get() );
    GetView()->SetVisible( aFootprint.get(), true );
    GetView()->Update( aFootprint.get(), KIGFX::ALL );
}


void FOOTPRINT_PREVIEW_PANEL::fitToCurrentFootprint()
{
    bool  includeText = m_currentFootprint->TextOnly();
    BOX2I bbox = m_currentFootprint->GetBoundingBox( includeText );

    if( bbox.GetSize().x > 0 && bbox.GetSize().y > 0 )
    {
        // Autozoom
        GetView()->SetViewport( BOX2D( bbox.GetOrigin(), bbox.GetSize() ) );

        // Add a margin
        GetView()->SetScale( GetView()->GetScale() * 0.7 );

        Refresh();
    }
}


void FOOTPRINT_PREVIEW_PANEL::onSize( wxSizeEvent& aEvent )
{
    aEvent.Skip();

    if( m_pendingFit && m_currentFootprint && IsShownOnScreen() )
    {
        m_pendingFit = false;

        // Defer the fit until after the base class onSize handler has called
        // GAL::ResizeScreen(), so SetViewport sees the correct screen dimensions.
        CallAfter( [this]() { fitToCurrentFootprint(); } );
    }
}


bool FOOTPRINT_PREVIEW_PANEL::DisplayFootprint( const LIB_ID& aFPID )
{
    m_dummyBoard->DetachAllFootprints();

    if( m_currentFootprint )
        GetView()->Remove( m_currentFootprint.get() );

    GetView()->Clear();

    FOOTPRINT_LIBRARY_ADAPTER* adapter = PROJECT_PCB::FootprintLibAdapter( &Prj() );

    try
    {
        adapter->AsyncLoad();
        adapter->BlockUntilLoaded();
        m_currentFootprint.reset( adapter->LoadFootprint( aFPID.GetLibNickname(), aFPID.GetLibItemName(), false ) );
    }
    catch( ... )
    {
        m_currentFootprint.reset();
    }

    if( m_currentFootprint )
    {
        renderFootprint( m_currentFootprint );
        m_pendingFit = true;
        fitToCurrentFootprint();
    }

    ForceRefresh();

    return m_currentFootprint != nullptr;
}


void FOOTPRINT_PREVIEW_PANEL::DisplayFootprints( std::shared_ptr<FOOTPRINT> aFootprintA,
                                                 std::shared_ptr<FOOTPRINT> aFootprintB )
{
    m_dummyBoard->DetachAllFootprints();

    if( m_currentFootprint )
        GetView()->Remove( m_currentFootprint.get() );

    if( m_otherFootprint )
        GetView()->Remove( m_otherFootprint.get() );

    GetView()->Clear();

    m_currentFootprint = aFootprintA;
    m_otherFootprint = aFootprintB;

    if( m_currentFootprint )
    {
        wxCHECK( m_otherFootprint, /* void */ );

        renderFootprint( m_currentFootprint );
        renderFootprint( m_otherFootprint );
        m_pendingFit = true;
        fitToCurrentFootprint();
    }

    Layout();
    Show();
}


void FOOTPRINT_PREVIEW_PANEL::RefreshAll()
{
    GetView()->UpdateAllItems( KIGFX::REPAINT );
    ForceRefresh();
}


FOOTPRINT_PREVIEW_PANEL* FOOTPRINT_PREVIEW_PANEL::New( KIWAY* aKiway, wxWindow* aParent,
                                                       UNITS_PROVIDER* aUnitsProvider )
{
    PCBNEW_SETTINGS* cfg = Pgm().GetSettingsManager().GetAppSettings<PCBNEW_SETTINGS>( "pcbnew" );
    COMMON_SETTINGS* commonSettings = Pgm().GetCommonSettings();
    std::unique_ptr<GAL_DISPLAY_OPTIONS_IMPL> gal_opts;

    gal_opts = std::make_unique<GAL_DISPLAY_OPTIONS_IMPL>();
    gal_opts->ReadConfig( *commonSettings, cfg->m_Window, aParent );

    auto galType = static_cast<EDA_DRAW_PANEL_GAL::GAL_TYPE>( commonSettings->m_Graphics.canvas_type );
    FOOTPRINT_PREVIEW_PANEL* panel = new FOOTPRINT_PREVIEW_PANEL( aKiway, aParent, aUnitsProvider,
                                                                  std::move( gal_opts ), galType );

    panel->UpdateColors();

    const GRID_SETTINGS& gridCfg = cfg->m_Window.grid;

    panel->GetGAL()->SetGridVisibility( gridCfg.show );

    //Bounds checking cannot include number of elements as an index!
    int    gridIdx = std::clamp( gridCfg.last_size_idx, 0, (int) gridCfg.grids.size() - 1 );
    double gridSizeX = EDA_UNIT_UTILS::UI::DoubleValueFromString( pcbIUScale, EDA_UNITS::MILS,
                                                                  gridCfg.grids[gridIdx].x );
    double gridSizeY = EDA_UNIT_UTILS::UI::DoubleValueFromString( pcbIUScale, EDA_UNITS::MILS,
                                                                  gridCfg.grids[gridIdx].y );
    panel->GetGAL()->SetGridSize( VECTOR2D( gridSizeX, gridSizeY ) );

    auto painter = static_cast<KIGFX::PCB_PAINTER*>( panel->GetView()->GetPainter() );
    auto settings = static_cast<KIGFX::PCB_RENDER_SETTINGS*>( painter->GetSettings() );
    settings->SetHighlight( false );
    settings->SetNetColorMode( NET_COLOR_MODE::OFF );

    return panel;
}
