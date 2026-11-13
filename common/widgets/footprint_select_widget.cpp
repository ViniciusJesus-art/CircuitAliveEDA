/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
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

#include <eda_draw_frame.h>
#include <kiway.h>
#include <kiface_ids.h>
#include <widgets/footprint_choice.h>
#include <widgets/footprint_select_widget.h>
#include <json_common.h>
#include <wx/wupdlock.h>


wxDEFINE_EVENT( EVT_FOOTPRINT_SELECTED, wxCommandEvent );

FOOTPRINT_SELECT_WIDGET::FOOTPRINT_SELECT_WIDGET( EDA_DRAW_FRAME* aFrame, wxWindow* aParent,
                                                  int aMaxItems ) :
        wxPanel( aParent ),
        m_max_items( aMaxItems ),
        m_pin_count( 0 ),
        m_zero_filter( true ),
        m_kiway( nullptr )
{
    m_sizer = new wxBoxSizer( wxVERTICAL );
    m_fp_sel_ctrl = new FOOTPRINT_CHOICE( this, wxID_ANY );
    m_fp_sel_ctrl->SetToolTip( _( "Select empty to leave this component without a footprint. Use Make default for this symbol to save a preference." ) );
    m_sizer->Add( m_fp_sel_ctrl, 1, wxEXPAND, 5 );

    SetSizer( m_sizer );
    Layout();
    m_sizer->Fit( this );

    m_fp_sel_ctrl->Bind( wxEVT_COMBOBOX, &FOOTPRINT_SELECT_WIDGET::OnComboBox, this );
}


void FOOTPRINT_SELECT_WIDGET::Load( KIWAY& aKiway, PROJECT& aProject )
{
    m_kiway = &aKiway;
}


void FOOTPRINT_SELECT_WIDGET::OnComboBox( wxCommandEvent& aEvent )
{
    wxCommandEvent evt( EVT_FOOTPRINT_SELECTED );
    int            sel = m_fp_sel_ctrl->GetSelection();

    if( sel == wxNOT_FOUND )
        return;

    wxStringClientData* clientdata =
            static_cast<wxStringClientData*>( m_fp_sel_ctrl->GetClientObject( sel ) );
    wxASSERT( clientdata );

    evt.SetString( clientdata->GetData() );
    evt.SetEventObject( this );
    // Complete the selection before OK or Make default can use its value.
    ProcessWindowEvent( evt );
}


void FOOTPRINT_SELECT_WIDGET::ClearFilters()
{
    m_pin_count = 0;
    m_filters.Clear();
    m_default_footprint.Clear();
    m_defaultMatchesFilters = false;
    m_zero_filter = false;
    m_mounting.clear();
    m_physicalText.clear();
    m_package.clear();
    m_maxWidth = m_maxHeight = 0;
}


void FOOTPRINT_SELECT_WIDGET::FilterByPinCount( int aPinCount )
{
    m_pin_count = aPinCount;
}


void FOOTPRINT_SELECT_WIDGET::FilterByFootprintFilters( wxArrayString const& aFilters,
                                                        bool aZeroFilters )
{
    m_zero_filter = ( aZeroFilters && aFilters.size() == 0 );
    m_filters = aFilters;
}


void FOOTPRINT_SELECT_WIDGET::SetDefaultFootprint( wxString const& aFp )
{
    m_default_footprint = aFp;
}


bool FOOTPRINT_SELECT_WIDGET::UpdateList()
{
    if( !m_kiway )
        return false;

    wxWindowUpdateLocker lock( m_fp_sel_ctrl );
    m_fp_sel_ctrl->Clear();

    m_defaultMatchesFilters = false;
    // Empty is always available, independently of search mode or physical filters.
    m_fp_sel_ctrl->Append( "empty", new wxStringClientData( wxEmptyString ) );
    if( !m_default_footprint.IsEmpty() )
        m_fp_sel_ctrl->Append( m_default_footprint, new wxStringClientData( m_default_footprint ) );

    // If zero_filter is set and we have no filters, show no footprints
    if( m_zero_filter && m_filters.IsEmpty() && m_pin_count == 0 )
    {
        SelectDefault();
        return true;
    }

    // Build JSON request for pcbnew
    using json = nlohmann::json;
    json request;
    request["pin_count"] = m_pin_count;
    request["zero_filters"] = m_zero_filter;
    request["max_results"] = m_max_items;
    request["mounting"] = m_mounting.ToStdString( wxConvUTF8 );
    request["physical_text"] = m_physicalText.ToStdString( wxConvUTF8 );
    request["package"] = m_package.ToStdString( wxConvUTF8 );
    request["max_width_mm"] = m_maxWidth;
    request["max_height_mm"] = m_maxHeight;

    json filtersArray = json::array();

    for( const wxString& filter : m_filters )
        filtersArray.push_back( filter.ToStdString( wxConvUTF8 ) );

    request["filters"] = filtersArray;

    // Get the filter function from pcbnew via KIWAY
    try
    {
        KIFACE* kiface = m_kiway->KiFACE( KIWAY::FACE_PCB );

        if( !kiface )
        {
            SelectDefault();
            return true;
        }

        void* funcPtr = kiface->IfaceOrAddress( KIFACE_FILTER_FOOTPRINTS );

        if( !funcPtr )
        {
            SelectDefault();
            return true;
        }

        // Call the filter function
        using FilterFunc = wxString ( * )( const wxString& );
        FilterFunc filterFootprints = reinterpret_cast<FilterFunc>( funcPtr );

        wxString requestStr = wxString::FromUTF8( request.dump() );
        wxString responseStr = filterFootprints( requestStr );

        // Parse the response
        json response = json::parse( responseStr.ToStdString( wxConvUTF8 ) );

        if( response.is_array() )
        {
            for( const auto& item : response )
            {
                if( item.is_string() )
                {
                    wxString fpName = wxString::FromUTF8( item.get<std::string>() );
                    if( fpName == m_default_footprint )
                    {
                        m_defaultMatchesFilters = true;
                        continue;
                    }
                    m_fp_sel_ctrl->Append( fpName, new wxStringClientData( fpName ) );
                }
            }
        }
    }
    catch( const std::exception& e )
    {
        // JSON parsing or other error - just show default
    }

    SelectDefault();
    return true;
}


void FOOTPRINT_SELECT_WIDGET::SelectDefault()
{
    m_fp_sel_ctrl->SetSelection( m_default_footprint.IsEmpty() ? 0 : 1 );
}

void FOOTPRINT_SELECT_WIDGET::SelectPhysicalMatch()
{
    if( m_defaultMatchesFilters ) return;
    if( !m_default_footprint.IsEmpty() )
        m_fp_sel_ctrl->Delete( 1 );
    if( m_fp_sel_ctrl->GetCount() == 1 )
    {
        m_fp_sel_ctrl->Append( _( "No footprints match these filters" ), new wxStringClientData( wxEmptyString ) );
    }
    m_fp_sel_ctrl->SetSelection( 1 );
    auto* data = static_cast<wxStringClientData*>( m_fp_sel_ctrl->GetClientObject( 1 ) );
    wxCommandEvent event( EVT_FOOTPRINT_SELECTED );
    event.SetString( data->GetData() );
    event.SetInt( 1 ); // Automatic matching; this is not an explicit empty choice.
    event.SetEventObject( this );
    ProcessWindowEvent( event );
}

void FOOTPRINT_SELECT_WIDGET::FilterByPhysicalProperties( const wxString& aMounting,
        const wxString& aText, const wxString& aPackage, double aWidth, double aHeight )
{
    m_mounting = aMounting;
    m_physicalText = aText;
    m_package = aPackage;
    m_maxWidth = aWidth;
    m_maxHeight = aHeight;
}


bool FOOTPRINT_SELECT_WIDGET::Enable( bool aEnable )
{
    return m_fp_sel_ctrl->Enable( aEnable );
}
