/* Copyright (C) 2026 CircuitAliveEDA contributors. SPDX-License-Identifier: GPL-3.0-or-later */
#include "control_references.h"
#include "circuit_snapshot.h"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace CIRCUITALIVE
{
using JSON = nlohmann::json;
namespace
{
std::string Number( double value )
{
    std::ostringstream s; s.imbue( std::locale::classic() ); s << std::setprecision(17) << value; return s.str();
}
std::string Name( std::string s )
{
    const auto start = s.find_first_not_of(" \t\r\n");
    if( start == std::string::npos ) return {};
    s = s.substr( start, s.find_last_not_of(" \t\r\n") - start + 1 );
    for( char& c : s ) { if( c == ' ' ) c = '_'; else if( c >= 'A' && c <= 'Z' ) c += 'a'-'A'; }
    return s;
}
std::string Expand( const std::string& text, const std::map<std::string,std::string>& tokens )
{
    std::string result; std::size_t at = 0;
    while( at < text.size() )
    {
        const auto start = text.find("${",at);
        if( start == std::string::npos ) { result += text.substr(at); break; }
        result += text.substr(at,start-at); const auto end = text.find('}',start);
        if( end == std::string::npos ) throw std::runtime_error("Unclosed live parameter placeholder");
        const auto key = text.substr(start+2,end-start-2);
        result += tokens.contains(key) ? tokens.at(key) : text.substr(start,end-start+1);
        at = end+1;
    }
    return result;
}
std::vector<std::string> Words( const std::string& text )
{
    std::vector<std::string> result; std::size_t start = 0; int depth = 0;
    for( std::size_t i=0;i<=text.size();++i )
    {
        const char c = i==text.size() ? ' ' : text[i];
        if( (c==' '||c=='\t'||c==',') && depth==0 )
        { if(i>start)result.push_back(text.substr(start,i-start)); start=i+1; }
        else if(c=='('||c=='{')++depth;
        else if(c==')'||c=='}')--depth;
    }
    return result;
}
std::string Expression( std::string value )
{
    if( value.size()>1 && value.front()=='{' && value.back()=='}' ) value=value.substr(1,value.size()-2);
    auto checked=value;std::size_t at=0;
    while((at=checked.find("${param:",at))!=std::string::npos)
    {
        const auto end=checked.find('}',at);if(end==std::string::npos)break;
        checked.replace(at,end-at+1,"1");++at;
    }
    if( checked.empty() || checked.find_first_not_of(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.+-*/^(), #" ) != std::string::npos )
        throw std::runtime_error("This field cannot change during simulation; use a numeric electrical parameter");
    return value;
}
bool Live( const std::string& s ) { return s.find('#')!=std::string::npos; }
}
std::string ControlReferenceName( const DESIGN_INTERACTION& interaction )
{
    return Name( interaction.reference.empty() ? interaction.label : interaction.reference );
}
CONTROL_REFERENCE_COMPILER::CONTROL_REFERENCE_COMPILER( const MODEL_DESIGN& design,
                                                        const std::vector<SIMULATION_MODEL>& models ) : m_design(design)
{
    std::set<std::string> explicitNames;
    for( const auto& i : design.interactions )
    {
        const auto b=std::find_if(design.blocks.begin(),design.blocks.end(),[&](const auto& block){return block.id==i.block;});
        if(b==design.blocks.end())throw std::runtime_error("Missing interaction block");
        const auto& model=models.at(b-design.blocks.begin());
        REFERENCE ref; ref.block=i.block;ref.control=i.control;ref.slider=i.control.starts_with("adjust:");
        if(ref.slider)
        {
            const auto a=std::find_if(model.adjustments.begin(),model.adjustments.end(),[&](const auto& item){return "adjust:"+item.id==i.control;});
            if(a==model.adjustments.end())throw std::runtime_error("Unknown slider reference");
            ref.minimum=a->minimum;ref.maximum=a->maximum;
        }
        else
        {
            try { ref.minimum=ParseComponentValue(i.offValue);ref.maximum=ParseComponentValue(i.onValue); }
            catch(const std::exception&) { ref.minimum=ref.maximum=0; } // Text is rejected if used in a numeric field.
            if(ref.minimum>ref.maximum)std::swap(ref.minimum,ref.maximum);
        }
        const auto key=ControlReferenceName(i);
        if(!i.reference.empty() && (!explicitNames.insert(key).second || key.empty()
            || key.find_first_of("#@\t\r\n")!=std::string::npos))
            throw std::runtime_error("Control reference names must be unique");
        std::set<std::string> aliases{key,Name(i.label)};
        for(const auto& alias:aliases)if(!alias.empty())m_names[alias].push_back(m_refs.size());
        m_refs.push_back(std::move(ref));
    }
}
std::string CONTROL_REFERENCE_COMPILER::Placeholder( const std::string& value, const MODEL_PARAMETER& parameter )
{
    const auto key=Name(value.substr(1)); const auto found=m_names.find(key);
    if(found==m_names.end())throw std::runtime_error("Unknown control reference: "+value);
    std::size_t index=found->second.front();
    if(found->second.size()>1)
    {
        bool explicitMatch=false;
        for(const auto candidate:found->second)
            if(!m_design.interactions[candidate].reference.empty() && ControlReferenceName(m_design.interactions[candidate])==key)
            {index=candidate;explicitMatch=true;break;}
        if(!explicitMatch)throw std::runtime_error("Ambiguous control label; use its reference name: "+value);
    }
    const auto& ref=m_refs[index];
    if(!ref.slider)
    {
        try{ParseComponentValue(m_design.interactions[index].onValue);ParseComponentValue(m_design.interactions[index].offValue);}
        catch(const std::exception&){throw std::runtime_error(value+": text cannot be assigned to numeric parameter "+parameter.label);}
    }
    if(ref.minimum<parameter.minimum || ref.maximum>parameter.maximum)
        throw std::runtime_error(value+": control range must fit parameter limits: "+parameter.label);
    const std::string token="CA_CONTROL_REFERENCE_"+std::to_string(index);
    m_pending[token]=index;return token;
}
void CONTROL_REFERENCE_COMPILER::Resolve( std::vector<std::map<std::string,std::string>>& tokens, JSON& json )
{
    for(std::size_t n=0;n<m_refs.size();++n)
    {
        auto& ref=m_refs[n];const auto& interaction=m_design.interactions[n];
        const std::string prefix="b_"+ref.block+"_";
        if(ref.slider)
        {
            const std::string id=prefix+ref.control.substr(7);
            for(const auto& a:json.value("adjustments",JSON::array()))if(a.at("id")==id)
            {ref.source=a.at("source");ref.initial="${param:"+a.at("parameter").get<std::string>()+"}";}
            ref.rawInitial=ref.initial;ref.expression="#"+std::to_string(n)+"#";
        }
        else
        {
            const std::string id=prefix+(ref.control=="switch"?"switch":ref.control.substr(7));
            for(const auto& c:json.value("controls",JSON::array()))if(c.at("id")==id)
            {
                ref.source=c.at("source");
                const double high=c.at("on"),low=c.at("off");
                ref.rawInitial=Number(c.value("initially_closed",false)?high:low);
                // Text-only controls need no numerical conversion until a numeric field references them.
                try
                {
                    const double on=ParseComponentValue(interaction.onValue),off=ParseComponentValue(interaction.offValue);
                    if(high==low)throw std::runtime_error("Button source levels must differ");
                    ref.initial=Number(c.value("initially_closed",false)?on:off);
                    ref.expression="("+Number(off)+"+("+Number(on-off)+")*(#"+std::to_string(n)+"#-("+Number(low)+"))/("+Number(high-low)+"))";
                }catch(const std::exception&){ref.initial.clear();}
            }
        }
        if(ref.source.empty())throw std::runtime_error("Interaction must be exposed before using a control reference");
    }
    m_expressions.resize(tokens.size());
    for(std::size_t n=0;n<tokens.size();++n)for(auto& [key,value]:tokens[n])
        if(m_pending.contains(value))
        {
            const auto& ref=m_refs.at(m_pending.at(value));m_expressions[n][key]=ref.expression;value=ref.initial;
        }
    // Text destinations retain their type. They never enter SPICE expressions.
    for(const char* group:{"controls","adjustments"})if(json.contains(group))for(auto& control:json[group])
    for(const char* field:{"label","unit"})if(control.contains(field))
    {
        const std::string text=control[field];if(!text.starts_with('#'))continue;
        const auto key=Name(text.substr(1));const auto found=m_names.find(key);
        if(found==m_names.end())throw std::runtime_error("Unknown text control reference: "+text);
        std::size_t index=found->second.front();
        if(found->second.size()>1)
        {
            bool exact=false;for(const auto n:found->second)if(!m_design.interactions[n].reference.empty()&&ControlReferenceName(m_design.interactions[n])==key)
            {index=n;exact=true;break;}
            if(!exact)throw std::runtime_error("Ambiguous text control reference: "+text);
        }
        const auto& ref=m_refs[index];const auto& i=m_design.interactions[index];
        double on=1,off=0;
        if(!ref.slider)for(const auto& c:json["controls"])if(c.at("source")==ref.source){on=c.at("on");off=c.at("off");break;}
        json["live_texts"].push_back({{"target",control.at("id")},{"field",field},{"source",ref.source},{"initial",ref.rawInitial},
            {"on",on},{"off",off},{"on_text",i.onValue},{"off_text",i.offValue},{"slider",ref.slider}});
        control[field]=ref.slider?ref.rawInitial:((ref.rawInitial==Number(on))?i.onValue:i.offValue);
    }
}
std::map<std::string,std::string> CONTROL_REFERENCE_COMPILER::Expressions( const std::map<std::string,std::string>& tokens, std::size_t n ) const
{
    auto result=tokens;
    for(const auto& [key,value]:m_expressions.at(n))result[key]=value;
    return result;
}
void CONTROL_REFERENCE_COMPILER::CopyNestedUpdates( const SIMULATION_MODEL& model,
        const std::map<std::string,std::string>& tokens, JSON& json ) const
{
    for(const auto& u:model.liveUpdates)
    {
        JSON sources=JSON::array();for(const auto& source:u.sources)sources.push_back(Expand(source,tokens));
        JSON expressions=JSON::array();for(const auto& e:u.change.expressions)expressions.push_back(Expand(e,tokens));
        JSON initials=JSON::array();for(const auto& value:u.initials)initials.push_back(Expand(value,tokens));
        json["live_updates"].push_back({{"reference",Expand(u.change.reference,tokens)}, {"parameter",u.change.parameter},
            {"model",u.change.model}, {"expressions",expressions}, {"sources",sources}, {"initials",initials}});
    }
    for(const auto& t:model.liveTexts)
    {
        std::string targetSource;
        for(const auto& c:model.controls)if(c.id==t.target)targetSource=Expand(c.source,tokens);
        for(const auto& c:model.adjustments)if(c.id==t.target)targetSource=Expand(c.source,tokens);
        for(const char* type:{"controls","adjustments"})if(json.contains(type))for(auto& c:json[type])
            if(c.at("source")==targetSource && c.contains(t.field))
            {
                json["live_texts"].push_back({{"target",c.at("id")},{"field",t.field},{"source",Expand(t.source,tokens)},
                    {"initial",Expand(t.initial,tokens)},{"on",t.on},{"off",t.off},{"on_text",t.onText},{"off_text",t.offText},{"slider",t.slider}});
            }
    }
}
std::string CONTROL_REFERENCE_COMPILER::CompileLine( const std::string& initial, const std::string& dynamic,
                                                    bool definition, JSON& json )
{
    if(!Live(dynamic))return initial;
    const auto fields=Words(dynamic);if(fields.empty())return initial;
    std::vector<std::string> sources;for(const auto& ref:m_refs)sources.push_back(ref.source);
    std::vector<std::string> initials;for(const auto& ref:m_refs)initials.push_back(ref.rawInitial);
    auto update=[&](const std::string& reference,const std::string& parameter,std::vector<std::string> expressions,bool model=false)
    {
        if(std::none_of(expressions.begin(),expressions.end(),Live))return;
        for(auto& e:expressions)e=Expression(e);
        json["live_updates"].push_back({{"reference",reference},{"parameter",parameter},{"expressions",expressions},{"model",model},{"sources",sources},{"initials",initials}});
    };
    const auto type=char(std::toupper(static_cast<unsigned char>(fields[0][0])));
    if(type=='B' && !definition)
    {
        std::string result=dynamic;
        for(std::size_t n=0;n<m_refs.size();++n)
        {
            const auto marker="#"+std::to_string(n)+"#";
            if(result.find(marker)==std::string::npos)continue;
            const auto node="ca_live_"+std::to_string(n)+"_${id}";
            const auto source="V"+node;
            if(m_proxies.insert(n).second)
            {
                json["spice"]["instance"].push_back(source+" "+node+" 0 "+m_refs[n].rawInitial);
                update(source,"dc",{marker});
                json["spice"]["definitions"].push_back(".global "+node);
            }
            std::size_t at=0;while((at=result.find(marker,at))!=std::string::npos){result.replace(at,marker.size(),"v("+node+")");at+=node.size()+3;}
        }
        return result;
    }
    if(definition && fields[0]==".model")
    {
        const auto start=dynamic.find('(');const auto end=dynamic.rfind(')');
        if(fields.size()<3 || start==std::string::npos || end==std::string::npos)throw std::runtime_error("Unsupported live model parameter");
        for(const auto& field:Words(dynamic.substr(start+1,end-start-1)))
        {const auto eq=field.find('=');if(eq!=std::string::npos)update(fields[1],field.substr(0,eq),{field.substr(eq+1)},true);}
        return initial;
    }
    if(definition)throw std::runtime_error("Live references in subcircuits or global solver settings are not supported");
    if(fields.size()<4)throw std::runtime_error("Unsupported live parameter field");
    if(type=='R'||type=='C'||type=='L')
    {
        const std::string property=type=='R'?"resistance":type=='C'?"capacitance":"inductance";
        if(fields[3].find('=')==std::string::npos)update(fields[0],property,{fields[3]});
    }
    else if(type=='V'||type=='I')
    {
        auto value=fields[3];std::string lower=value;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return std::tolower(c);});
        if(lower=="dc" && fields.size()>4)update(fields[0],"dc",{fields[4]});
        else if(const auto open=value.find('(');open!=std::string::npos)
        {update(fields[0],lower.substr(0,open),Words(value.substr(open+1,value.size()-open-2)));}
        else update(fields[0],"dc",{value});
    }
    else if(type=='X'||type=='A')throw std::runtime_error("This block does not support live parameter references");
    bool named=false;
    for(std::size_t i=3;i<fields.size();++i)
    {
        const auto eq=fields[i].find('=');
        if(eq!=std::string::npos && Live(fields[i])){update(fields[0],fields[i].substr(0,eq),{fields[i].substr(eq+1)});named=true;}
    }
    if(type!='R'&&type!='C'&&type!='L'&&type!='V'&&type!='I'&&!named)
        throw std::runtime_error("This field cannot change during simulation; use a numeric electrical parameter");
    return initial;
}
std::vector<SIM_PARAMETER_CHANGE> LiveParameterChanges( const CIRCUIT_SNAPSHOT& circuit,
        const std::map<std::string,double>& values, const std::string& source )
{
    std::vector<SIM_PARAMETER_CHANGE> result;
    for(const auto& u:circuit.liveUpdates)
    {
        const auto changed=std::find(u.sources.begin(),u.sources.end(),source);
        if(changed==u.sources.end())continue;
        const std::string marker="#"+std::to_string(changed-u.sources.begin())+"#";
        if(std::none_of(u.change.expressions.begin(),u.change.expressions.end(),[&](const auto& e){return e.find(marker)!=std::string::npos;}))continue;
        auto change=u.change;
        for(auto& expression:change.expressions)
            for(std::size_t n=0;n<u.sources.size();++n)
            {
                const auto key="#"+std::to_string(n)+"#";if(expression.find(key)==std::string::npos)continue;
                const auto found=values.find(u.sources[n]);if(found==values.end())throw std::runtime_error("Missing live control value");
                const auto number=Number(found->second);std::size_t at=0;
                while((at=expression.find(key,at))!=std::string::npos){expression.replace(at,key.size(),"("+number+")");at+=number.size()+2;}
            }
        result.push_back(std::move(change));
    }
    return result;
}
std::string LiveControlText( const CIRCUIT_SNAPSHOT& circuit, const std::vector<bool>& switches,
        const std::vector<double>& adjustments, std::size_t component, const std::string& id,
        const std::string& field, const std::string& fallback )
{
    for(const auto& t:circuit.liveTexts)if(t.component==component&&t.target==id&&t.field==field)
    {
        double value=t.initial;
        for(std::size_t n=0;n<circuit.switches.size();++n)if(circuit.switches[n].controlSource==t.source)
            value=(n<switches.size()?switches[n]:circuit.switches[n].initiallyClosed)?circuit.switches[n].on:circuit.switches[n].off;
        for(std::size_t n=0;n<circuit.adjustments.size();++n)if(circuit.adjustments[n].source==t.source)
            value=n<adjustments.size()?adjustments[n]:circuit.adjustments[n].initial;
        if(t.slider)return Number(value);
        return std::abs(value-t.on)<std::abs(value-t.off)?t.onText:t.offText;
    }
    return fallback;
}
}
