/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "panel_model_outputs.h"
#include "segment_json.h"
#include "output_geometry.h"
#include "numeric_value.h"
#include "model_preview_navigation.h"
#include "circuit_snapshot.h"
#include "model_i18n.h"
#include "choice_controls.h"
#include <widgets/symbol_preview_widget.h>
#include <dialog_shim.h>
#include <lib_symbol.h>
#include <view/view.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/clrpicker.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace CIRCUITALIVE
{
using JSON=nlohmann::json;
namespace
{
wxString W(const std::string& s){return wxString::FromUTF8(s);}
bool EditSegment(wxWindow* parent,MODEL_SEGMENT& s,const MODEL_DESIGN& design)
{
    DIALOG_SHIM dialog(parent,wxID_ANY,_("Luminous segment"),wxDefaultPosition,wxDefaultSize,wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER);
    // These values belong to this segment, not to the previous dialog instance.
    dialog.OptOut(&dialog);
    auto* layout=new wxBoxSizer(wxVERTICAL);
    auto* fields=new wxFlexGridSizer(2,8,10);fields->AddGrowableCol(1);
    auto field=[&](const wxString& label,const wxString& value)
    {
        fields->Add(new wxStaticText(&dialog,wxID_ANY,label),0,wxALIGN_CENTER_VERTICAL);
        auto* t=new wxTextCtrl(&dialog,wxID_ANY,value);fields->Add(t,1,wxEXPAND);return t;
    };
    auto* id=field(_("Segment name"),W(s.id));
    auto* signal=field(_("Signal: v(node), v(p,n) or i(source)"),W(s.signal));
    fields->Add(new wxStaticText(&dialog,wxID_ANY,_("Shape")),0,wxALIGN_CENTER_VERTICAL);
    auto* shape=new CLICK_CHOICE(&dialog);shape->Append(_("Line"));shape->Append(_("Circle"));
    shape->SetSelection(s.shape=="circle"?1:0);fields->Add(shape,1,wxEXPAND);
    auto numeric=[&](const wxString& label,double value){return field(label,W(FormatSimulationNumber(value)));};
    auto* scale=numeric(_("Signal value for full brightness"),s.fullScale);
    auto* scaleParameter=field(_("Scale parameter (optional)"),W(s.fullScaleParameter));
    const auto& g=*s.geometry;
    auto* x1=numeric(_("First endpoint X (% of symbol width)"),g.x1*100);
    auto* y1=numeric(_("First endpoint Y (% of symbol height)"),g.y1*100);
    auto* x2=numeric(_("Second endpoint X (%)"),g.x2*100);
    auto* y2=numeric(_("Second endpoint Y (%)"),g.y2*100);
    auto* thickness=numeric(_("Thickness (% of shorter symbol dimension)"),g.thickness*100);
    fields->Add(new wxStaticText(&dialog,wxID_ANY,_("Color")),0,wxALIGN_CENTER_VERTICAL);
    auto* color=new wxColourPickerCtrl(&dialog,wxID_ANY,wxColour(std::lround(s.red*255),std::lround(s.green*255),std::lround(s.blue*255)));
    fields->Add(color,1,wxEXPAND);
    fields->AddSpacer(1);auto* filled=new wxCheckBox(&dialog,wxID_ANY,_("Filled circle"));filled->SetValue(g.filled);fields->Add(filled);
    auto updateShape=[&]{filled->Enable(shape->GetSelection()==1);};updateShape();
    shape->Bind(wxEVT_COMBOBOX,[&](wxCommandEvent&){updateShape();});
    fields->AddSpacer(1);auto* visible=new wxCheckBox(&dialog,wxID_ANY,_("Visible during simulation"));visible->SetValue(s.visual.visible);fields->Add(visible);
    auto* hint=new wxStaticText(&dialog,wxID_ANY,_("Drag either endpoint freely in the preview. Drag the line or circle to move it.\nThe two endpoints define a circle's diameter. Thickness is independent of direction and length.\nUse model pins as ${pin:port}. Brightness follows the electrical signal."));hint->Wrap(600);
    layout->Add(fields,1,wxEXPAND|wxALL,12);layout->Add(hint,0,wxEXPAND|wxALL,12);
    layout->Add(dialog.CreateSeparatedButtonSizer(wxOK|wxCANCEL),0,wxEXPAND|wxALL,12);
    dialog.SetSizerAndFit(layout);dialog.SetMinSize({650,-1});dialog.Centre();
    dialog.Bind(wxEVT_BUTTON,[&](wxCommandEvent&)
    {
        try
        {
            auto number=[](wxTextCtrl* t){return ParseComponentValue(t->GetValue().ToStdString(wxConvUTF8));};
            auto edited=s;edited.id=id->GetValue().ToStdString(wxConvUTF8);edited.signal=signal->GetValue().ToStdString(wxConvUTF8);
            edited.shape=shape->GetSelection()==1?"circle":"line";
            edited.fullScale=number(scale);edited.fullScaleParameter=scaleParameter->GetValue().ToStdString(wxConvUTF8);
            edited.geometry=SEGMENT_GEOMETRY{number(x1)/100,number(y1)/100,number(x2)/100,number(y2)/100,number(thickness)/100,filled->GetValue()};
            edited.visual.visible=visible->GetValue();
            const auto rgb=color->GetColour();edited.red=rgb.Red()/255.;edited.green=rgb.Green()/255.;edited.blue=rgb.Blue()/255.;
            ReadLuminousSegment(WriteLuminousSegment(edited));
            if(edited.id.empty()||edited.id.size()>80||edited.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")!=std::string::npos||edited.signal.empty()||edited.signal.size()>4096||edited.fullScale<=0)
                throw std::runtime_error("Set a segment name, electrical signal and positive brightness scale");
            if(!edited.fullScaleParameter.empty()&&std::none_of(design.parameters.begin(),design.parameters.end(),[&](const auto& p){return p.id==edited.fullScaleParameter;}))
                throw std::runtime_error("Unknown scale parameter");
            s=std::move(edited);dialog.EndModal(wxID_OK);
        }
        catch(const std::exception& e){wxMessageBox(DiagnosticText(e.what()),_("Luminous segment"),wxOK,&dialog);}
    },wxID_OK);
    return dialog.ShowModal()==wxID_OK;
}
}
class OUTPUT_PREVIEW : public SYMBOL_PREVIEW_WIDGET
{
public:
    OUTPUT_PREVIEW(wxWindow* parent,KIWAY* kiway,MODEL_DESIGN& design,std::vector<MODEL_SEGMENT>& segments,int& selected,std::function<void()> changed):
        SYMBOL_PREVIEW_WIDGET(parent,kiway,true,EDA_DRAW_PANEL_GAL::GAL_TYPE_CAIRO),m_design(design),m_segments(segments),m_selected(selected),m_changed(std::move(changed))
    {
        ConfigureModelPreviewNavigation(GetCanvas());
        m_overlay=std::make_unique<KIGFX::VIEW_OVERLAY>();GetCanvas()->GetView()->Add(m_overlay.get());
        GetCanvas()->Bind(wxEVT_LEFT_DOWN,[this](wxMouseEvent& e)
        {
            if(!m_previewItem){e.Skip();return;}
            const auto at=point(e);const double tolerance=GetCanvas()->GetView()->ToWorld(7.);
            int hit=-1,handle=0;
            // Give the selected endpoints priority at crossings.
            if(m_selected>=0&&m_selected<int(m_segments.size()))
            {
                const auto g=LuminousGeometry(body(),m_segments[m_selected]);
                if((at-g.start).EuclideanNorm()<tolerance){hit=m_selected;handle=1;}
                else if((at-g.end).EuclideanNorm()<tolerance){hit=m_selected;handle=2;}
            }
            for(int i=int(m_segments.size())-1;hit<0&&i>=0;--i)
            {
                const auto g=LuminousGeometry(body(),m_segments[i]);
                if(!HitLuminousSegment(g,at,tolerance))continue;
                hit=i;
                if((at-g.start).EuclideanNorm()<tolerance)handle=1;
                else if((at-g.end).EuclideanNorm()<tolerance)handle=2;
            }
            if(hit<0){e.Skip();return;}
            m_selected=hit;m_start=at;m_original=Editable(m_segments[hit]);m_handle=handle;
            m_dragging=true;GetCanvas()->CaptureMouse();RefreshOutputs();m_changed();
        });
        GetCanvas()->Bind(wxEVT_MOTION,[this](wxMouseEvent& e)
        {
            if(!m_dragging||m_selected<0||m_selected>=int(m_segments.size())){e.Skip();return;}
            const auto delta=point(e)-m_start;
            const double dx=delta.x/std::max<double>(1,body().GetWidth()),dy=delta.y/std::max<double>(1,body().GetHeight());
            auto edited=m_original;auto& g=*edited.geometry;
            if(m_handle!=2){g.x1=std::clamp(g.x1+dx,-10.,10.);g.y1=std::clamp(g.y1+dy,-10.,10.);}
            if(m_handle!=1){g.x2=std::clamp(g.x2+dx,-10.,10.);g.y2=std::clamp(g.y2+dy,-10.,10.);}
            m_segments[m_selected]=std::move(edited);RefreshOutputs();m_changed();
        });
        GetCanvas()->Bind(wxEVT_LEFT_UP,[this](wxMouseEvent& e){if(m_dragging)release();else e.Skip();});
        GetCanvas()->Bind(wxEVT_MOUSE_CAPTURE_LOST,[this](wxMouseCaptureLostEvent& e){m_dragging=false;e.Skip();});
        GetCanvas()->Bind(wxEVT_MOUSEWHEEL,[this](wxMouseEvent& e){e.Skip();CallAfter([this]{RefreshOutputs();});});
    }
    ~OUTPUT_PREVIEW() override {release();GetCanvas()->StopDrawing();GetCanvas()->GetView()->Remove(m_overlay.get());}
    MODEL_SEGMENT Editable(const MODEL_SEGMENT& segment) const
    {return EditableLuminousSegment(segment,m_previewItem?body():BOX2I(VECTOR2I(0,0),VECTOR2I(100,100)));}
    void Load()
    {
        release();if(m_design.libraryId.empty())SetStatusText(_("Choose a component symbol to position luminous segments"));
        else{LIB_ID id;id.Parse(W(m_design.libraryId));DisplaySymbol(id,1);}
        RefreshOutputs();
    }
    void RefreshOutputs()
    {
        m_overlay->Clear();
        if(m_previewItem)for(std::size_t i=0;i<m_segments.size();++i)
        {
            DrawLuminousSegment(*m_overlay,body(),m_segments[i],1);
            if(int(i)==m_selected)
            {
                const auto g=LuminousGeometry(body(),m_segments[i]);
                const double radius=GetCanvas()->GetView()->ToWorld(4.);
                m_overlay->SetIsFill(true);m_overlay->SetFillColor(KIGFX::COLOR4D(1,.55,.1,1));
                m_overlay->SetIsStroke(true);m_overlay->SetStrokeColor(KIGFX::COLOR4D(.15,.15,.15,1));
                m_overlay->SetLineWidth(GetCanvas()->GetView()->ToWorld(1.));
                m_overlay->Circle(g.start,radius);m_overlay->Circle(g.end,radius);
            }
        }
        GetCanvas()->GetView()->Update(m_overlay.get());GetCanvas()->ForceRefresh();
    }
private:
    BOX2I body() const{return m_previewItem->GetBodyBoundingBox();}
    VECTOR2D point(const wxMouseEvent& e) const{return GetCanvas()->GetView()->ToWorld(VECTOR2D(e.GetX(),e.GetY()));}
    void release(){m_dragging=false;if(GetCanvas()->HasCapture())GetCanvas()->ReleaseMouse();}
    MODEL_DESIGN& m_design;std::vector<MODEL_SEGMENT>& m_segments;int& m_selected;std::function<void()> m_changed;
    std::unique_ptr<KIGFX::VIEW_OVERLAY> m_overlay;bool m_dragging=false;int m_handle=0;VECTOR2D m_start;MODEL_SEGMENT m_original;
};
PANEL_MODEL_OUTPUTS::PANEL_MODEL_OUTPUTS(wxWindow* parent,KIWAY* kiway,MODEL_DESIGN& design,std::function<void()> changed):wxPanel(parent),m_design(design),m_changed(std::move(changed))
{
    auto* layout=new wxBoxSizer(wxVERTICAL);
    auto* hint=new wxStaticText(this,wxID_ANY,_("Add a line or circle. Drag either endpoint to change its shape, or drag the body to move it. Edit thickness, color and signal in Properties. Wheel: zoom at pointer. Middle drag: pan."));
    hint->Wrap(1100);layout->Add(hint,0,wxEXPAND|wxALL,10);
    auto* buttons=new wxBoxSizer(wxHORIZONTAL);
    auto button=[&](const wxString& label,std::function<void()> fn)
    {auto* b=new wxButton(this,wxID_ANY,label);buttons->Add(b,0,wxRIGHT,5);b->Bind(wxEVT_BUTTON,[fn](wxCommandEvent&){fn();});};
    button(_("Add line"),[this]{add("line");});button(_("Add circle"),[this]{add("circle");});
    button(_("Properties…"),[this]{edit();});button(_("Remove"),[this]
    {if(m_selected>=0&&m_selected<int(m_segments.size())){m_segments.erase(m_segments.begin()+m_selected);m_selected=-1;store();list();}});
    layout->Add(buttons,0,wxLEFT|wxRIGHT|wxBOTTOM,10);auto* row=new wxBoxSizer(wxHORIZONTAL);
    m_list=new wxListBox(this,wxID_ANY);m_list->SetMinSize({260,240});row->Add(m_list,0,wxEXPAND|wxRIGHT,10);
    m_preview=new OUTPUT_PREVIEW(this,kiway,design,m_segments,m_selected,[this]{m_list->SetSelection(m_selected);store();});
    row->Add(m_preview,1,wxEXPAND);layout->Add(row,1,wxEXPAND|wxALL,10);SetSizer(layout);
    m_list->Bind(wxEVT_LISTBOX,[this](wxCommandEvent& e){m_selected=e.GetSelection();m_preview->RefreshOutputs();});
    m_list->Bind(wxEVT_LISTBOX_DCLICK,[this](wxCommandEvent&){edit();});
}
void PANEL_MODEL_OUTPUTS::Reload()
{
    m_segments.clear();const auto json=m_design.baseJson.empty()?JSON::object():JSON::parse(m_design.baseJson);
    for(const auto& j:json.value("segments",JSON::array()))m_segments.push_back(ReadLuminousSegment(j));
    m_selected=-1;list();m_preview->Load();
}
void PANEL_MODEL_OUTPUTS::store()
{
    auto json=m_design.baseJson.empty()?JSON::object():JSON::parse(m_design.baseJson);json["segments"]=JSON::array();
    for(const auto& s:m_segments)json["segments"].push_back(WriteLuminousSegment(s));
    m_design.baseJson=json.dump();m_changed();
}
void PANEL_MODEL_OUTPUTS::list()
{
    m_list->Clear();for(const auto& s:m_segments)m_list->Append(W(s.id+" — "+s.signal));
    m_list->SetSelection(m_selected);m_preview->RefreshOutputs();
}
void PANEL_MODEL_OUTPUTS::add(const std::string& shape)
{
    if(m_segments.size()>=256)return;
    MODEL_SEGMENT s;s.shape=shape;s.geometry=SEGMENT_GEOMETRY{};
    if(shape=="circle"){s.geometry->x1=.4;s.geometry->x2=.6;}
    int n=1;do{s.id="segment"+std::to_string(n++);}while(std::any_of(m_segments.begin(),m_segments.end(),[&](const auto& v){return v.id==s.id;}));
    if(!m_design.pins.empty()){s.signal="v(${pin:"+m_design.pins.front().id+"})";s.fullScale=5;}
    if(!EditSegment(this,s,m_design))return;
    if(std::any_of(m_segments.begin(),m_segments.end(),[&](const auto& existing){return existing.id==s.id;}))
    {wxMessageBox(_("Segment names must be unique"),_("Luminous segment"),wxOK,this);return;}
    m_segments.push_back(std::move(s));m_selected=int(m_segments.size())-1;store();list();
}
void PANEL_MODEL_OUTPUTS::edit()
{
    if(m_selected<0||m_selected>=int(m_segments.size()))return;
    auto edited=m_preview->Editable(m_segments[m_selected]);if(!EditSegment(this,edited,m_design))return;
    for(std::size_t i=0;i<m_segments.size();++i)if(int(i)!=m_selected&&m_segments[i].id==edited.id)
    {wxMessageBox(_("Segment names must be unique"),_("Luminous segment"),wxOK,this);return;}
    m_segments[m_selected]=std::move(edited);store();list();
}
}
