/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "panel_led_appearance.h"
#include <wx/clrpicker.h>
#include <wx/dcbuffer.h>
#include <wx/spinctrl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <algorithm>
#include <charconv>
#include <cmath>
namespace CIRCUITALIVE
{
namespace
{
std::string Number(double value)
{
    char buffer[64];const auto r=std::to_chars(buffer,buffer+sizeof(buffer),value,std::chars_format::general,10);
    return {buffer,r.ptr};
}
}
PANEL_LED_APPEARANCE::PANEL_LED_APPEARANCE(wxWindow* parent):
    wxScrolledWindow(parent,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxVSCROLL|wxTAB_TRAVERSAL)
{
    SetScrollRate(0,10);
    auto* layout=new wxBoxSizer(wxHORIZONTAL);auto* left=new wxBoxSizer(wxVERTICAL);
    auto* grid=new wxFlexGridSizer(2,FromDIP(7),FromDIP(12));grid->AddGrowableCol(1);
    grid->Add(new wxStaticText(this,wxID_ANY,_("LED color:")),0,wxALIGN_CENTER_VERTICAL);
    m_color=new wxColourPickerCtrl(this,wxID_ANY,wxColour(255,66,5));grid->Add(m_color,1,wxEXPAND);
    const wxString labels[]={_("Intensity (%):"),_("Circle diameter (mm):"),_("Halo diameter (mm):"),_("Opacity (%):")};
    constexpr double maximum[]={100,50,100,100};
    for(std::size_t i=0;i<m_numbers.size();++i)
    {
        grid->Add(new wxStaticText(this,wxID_ANY,labels[i]),0,wxALIGN_CENTER_VERTICAL);
        auto* field=new wxSpinCtrlDouble(this,wxID_ANY);m_numbers[i]=field;
        field->SetRange(0,maximum[i]);field->SetIncrement(i==1||i==2?.1:1);field->SetDigits(i==1||i==2?2:0);
        grid->Add(field,1,wxEXPAND);
        auto update=[this,i,field](auto& event)
        {
            if(!m_loading&&m_set)
            {m_set(m_binding.ids[i+3],Number(field->GetValue()));preview();}
            event.Skip();
        };
        field->Bind(wxEVT_SPINCTRLDOUBLE,update);field->Bind(wxEVT_TEXT,update);
    }
    left->Add(grid,0,wxEXPAND);auto* note=new wxStaticText(this,wxID_ANY,
        _("Appearance only; current still comes from the circuit.\nZero diameter hides that circle. Stop and Play to apply changes to a running simulation."));
    note->Wrap(430);left->Add(note,0,wxTOP|wxEXPAND,FromDIP(10));layout->Add(left,1,wxALL|wxEXPAND,FromDIP(12));
    m_preview=new wxPanel(this);m_preview->SetMinSize(FromDIP(wxSize(160,160)));m_preview->SetBackgroundStyle(wxBG_STYLE_PAINT);
    layout->Add(m_preview,0,wxALL|wxEXPAND,FromDIP(12));SetSizer(layout);FitInside();
    m_color->Bind(wxEVT_COLOURPICKER_CHANGED,[this](wxColourPickerEvent&)
    {
        if(m_loading||!m_set)return;
        const auto color=m_color->GetColour();
        m_set(m_binding.ids[0],std::to_string(color.Red()));m_set(m_binding.ids[1],std::to_string(color.Green()));
        m_set(m_binding.ids[2],std::to_string(color.Blue()));preview();
    });
    m_preview->Bind(wxEVT_PAINT,[this](wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(m_preview);dc.SetBackground(wxBrush(wxColour(28,32,37)));dc.Clear();
        const auto size=m_preview->GetClientSize();const auto display=LedDisplay(1,m_value);
        const double scale=std::min(18.,(std::min(size.x,size.y)-24)/std::max({1.,m_value.diameterMm,m_value.haloDiameterMm}));
        auto blend=[&](double r,double g,double b,double alpha)
        {return wxColour(int(28+(255*r-28)*alpha),int(32+(255*g-32)*alpha),int(37+(255*b-37)*alpha));};
        const wxPoint center(size.x/2,size.y/2);
        if(m_value.diameterMm>0)
        {
            dc.SetPen(wxPen(blend(m_value.red,m_value.green,m_value.blue,display.strokeOpacity),2));
            dc.SetBrush(wxBrush(blend(display.red,display.green,display.blue,display.fillOpacity)));
            dc.DrawCircle(center,int(m_value.diameterMm*scale/2));
        }
        if(m_value.haloDiameterMm>0)
        {
            dc.SetPen(wxPen(blend(m_value.red,m_value.green,m_value.blue,display.haloOpacity),2));dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawCircle(center,int(m_value.haloDiameterMm*scale/2));
        }
        dc.SetTextForeground(wxColour(210,220,230));dc.DrawText(_("On-state preview"),8,8);
    });
}
void PANEL_LED_APPEARANCE::Configure(const SIMULATION_MODEL& model,std::function<double(const std::string&)> get,
                                   std::function<void(const std::string&,const std::string&)> set)
{
    m_loading=true;m_binding=model.ledAppearance;m_set=std::move(set);
    std::map<std::string,double> values;
    for(std::size_t i=0;i<m_binding.ids.size();++i)if(!m_binding.ids[i].empty())
        values[m_binding.ids[i]]=std::clamp(get(m_binding.ids[i]),0.,LED_APPEARANCE_LIMITS[i]);
    m_value=ResolveLedAppearance(m_binding,values);
    m_color->SetColour(wxColour(std::lround(m_value.red*255),std::lround(m_value.green*255),std::lround(m_value.blue*255)));
    m_numbers[0]->SetValue(m_value.intensity*100);m_numbers[1]->SetValue(m_value.diameterMm);
    m_numbers[2]->SetValue(m_value.haloDiameterMm);m_numbers[3]->SetValue(m_value.opacity*100);
    m_loading=false;m_preview->Refresh();
}
void PANEL_LED_APPEARANCE::preview()
{
    const auto color=m_color->GetColour();m_value.red=color.Red()/255.;m_value.green=color.Green()/255.;m_value.blue=color.Blue()/255.;
    m_value.intensity=m_numbers[0]->GetValue()/100;m_value.diameterMm=m_numbers[1]->GetValue();
    m_value.haloDiameterMm=m_numbers[2]->GetValue();m_value.opacity=m_numbers[3]->GetValue()/100;m_preview->Refresh();
}
}
