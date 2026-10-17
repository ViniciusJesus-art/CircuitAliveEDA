/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include <circuitalive/desktop_integration.h>
#include <circuitalive/branding.h>

#include <wx/app.h>

#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

void CIRCUITALIVE::InitializeDesktopIdentity()
{
    wxTheApp->SetClassName( DesktopId() );
    wxTheApp->SetAppDisplayName( ProductName() );

#ifdef __WXGTK__
    // GTK 3 uses the GLib program name for Wayland's app_id and X11's WM_CLASS
    // instance. wxWidgets 3.2 ignores SetClassName() for these identities. Keep
    // wxApp::GetAppName() unchanged: it still identifies technical settings/files.
    const wxString desktopId = DesktopId();
    g_set_prgname( desktopId.utf8_str() );

    // Frames set their own icon bundles, but native dialogs and floating windows
    // otherwise fall back to GTK's icon for "kicad"/"eeschema" from the system.
    const wxIconBundle icons = WindowIcons();
    GList* nativeIcons = nullptr;

    for( size_t i = 0; i < icons.GetIconCount(); ++i )
        nativeIcons = g_list_append( nativeIcons, icons.GetIconByIndex( i ).GetPixbuf() );

    gtk_window_set_default_icon_list( nativeIcons );
    g_list_free( nativeIcons ); // GTK retains a reference to each pixbuf.
#endif
}
