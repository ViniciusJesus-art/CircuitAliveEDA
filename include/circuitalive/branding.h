/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <bitmaps.h>
#include <wx/iconbndl.h>
#include <wx/intl.h>

namespace CIRCUITALIVE
{
inline wxString ProductName() { return wxS( "CircuitAliveEDA" ); }
inline wxString ProjectUrl() { return wxS( "https://github.com/ViniciusJesus-art/CircuitAliveEDA" ); }
inline wxString DocumentationUrl() { return ProjectUrl() + wxS( "/tree/main/docs/circuitalive" ); }
inline wxString DesktopId() { return wxS( "org.circuitaliveeda.CircuitAliveEDA" ); }
// Library type tokens remain unchanged on disk; only their UI labels use our brand.
inline wxString LibraryTypeLabel( const wxString& type )
{
    return type == wxS( "KiCad" ) ? ProductName() : type;
}
inline wxString LibraryTypeId( const wxString& label )
{
    return label == ProductName() ? wxS( "KiCad" ) : label;
}
inline wxString AppTitle( const wxString& role ) { return ProductName() + " " + wxGetTranslation( role ); }
inline wxString WindowTitle( const wxString& title )
{
    return title.Contains( ProductName() ) ? title : title + wxS( " — " ) + ProductName();
}

// Window identity is shared; functional toolbar icons remain distinct for each editor.
inline wxIconBundle WindowIcons()
{
    wxIconBundle icons;
    for( int size : { 48, 64, 128, 256 } )
    {
        wxIcon icon;
        icon.CopyFromBitmap( KiBitmap( BITMAPS::icon_kicad, size ) );
        icons.AddIcon( icon );
    }
    for( auto bitmap : { BITMAPS::icon_kicad_16, BITMAPS::icon_kicad_32 } )
    {
        wxIcon icon;
        icon.CopyFromBitmap( KiBitmap( bitmap ) );
        icons.AddIcon( icon );
    }
    return icons;
}
}
