/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "compiled_expression.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace CIRCUITALIVE
{
namespace
{
enum OP { CONSTANT, INPUT, TIME, ADD, SUB, MUL, DIV, NEG, NOT, LT, LE, EQ, NE, GT, GE,
          AND, OR, SELECT, MIN, MAX, ABS, TANH };
struct NODE { OP op; int a=-1,b=-1,c=-1; double constant=0; };
struct RANGE
{
    double low = -std::numeric_limits<double>::infinity();
    double high = std::numeric_limits<double>::infinity();
};
std::string Lower( std::string value )
{
    for(auto& c:value)c=std::tolower(static_cast<unsigned char>(c));
    return value;
}
std::string Trim( std::string value )
{
    const auto first=value.find_first_not_of(" \t\r\n");
    if(first==std::string::npos)return {};
    return value.substr(first,value.find_last_not_of(" \t\r\n")-first+1);
}
struct PROGRAM
{
    std::string name;
    std::vector<NODE> nodes;
    std::vector<std::string> inputs;
    std::vector<double> values,adjoints,last,partials;
    bool usesTime=false, cached=false;
    double lastTime=0, result=0;
    int root=0;

    static double Value(OP op,double a,double b=0,double c=0)
    {
        switch(op) {
        case ADD:return a+b; case SUB:return a-b;case MUL:return a*b;case DIV:return a/b;
        case NEG:return -a;case NOT:return !a;case LT:return a<b;case LE:return a<=b;
        case EQ:return a==b;case NE:return a!=b;case GT:return a>b;case GE:return a>=b;
        case AND:return a!=0 && b!=0;case OR:return a!=0 || b!=0;
        case SELECT:return a!=0?b:c;case MIN:return a<b?a:b;case MAX:return a>b?a:b;
        case ABS:return std::abs(a);case TANH:return std::tanh(a);
        default:return 0;
        }
    }
    bool Evaluate(const double* data,unsigned count,double time)
    {
        if(count!=inputs.size())return false;
        if(cached && (!usesTime || time==lastTime) && std::equal(last.begin(),last.end(),data))return true;
        for(std::size_t i=0;i<nodes.size();++i) {
            const auto& n=nodes[i];
            switch(n.op) {
            case CONSTANT:values[i]=n.constant;break;
            case INPUT:values[i]=data[n.a];break;
            case TIME:values[i]=time;break;
            default:values[i]=Value(n.op,values[n.a],n.b<0?0:values[n.b],n.c<0?0:values[n.c]);
            }
        }
        std::fill(adjoints.begin(),adjoints.end(),0);std::fill(partials.begin(),partials.end(),0);
        adjoints[root]=1;result=values[root];
        for(int i=int(nodes.size())-1;i>=0;--i) {
            const auto& n=nodes[i];const auto d=adjoints[i];
            if(d==0)continue; // Do not propagate inactive branch singularities.
            const double a=n.a<0 || n.op==INPUT?0:values[n.a];
            const double b=n.b<0?0:values[n.b];
            switch(n.op) {
            case INPUT:partials[n.a]+=d;break;
            case ADD:adjoints[n.a]+=d;adjoints[n.b]+=d;break;
            case SUB:adjoints[n.a]+=d;adjoints[n.b]-=d;break;
            case MUL:adjoints[n.a]+=d*b;adjoints[n.b]+=d*a;break;
            case DIV:adjoints[n.a]+=d/b;adjoints[n.b]-=d*a/(b*b);break;
            case NEG:adjoints[n.a]-=d;break;
            case SELECT:adjoints[a!=0?n.b:n.c]+=d;break;
            case MIN:adjoints[a<b?n.a:n.b]+=d;break;
            case MAX:adjoints[a>b?n.a:n.b]+=d;break;
            case ABS:adjoints[n.a]+=d*(a>0?1:a<0?-1:0);break;
            case TANH:adjoints[n.a]+=d*(1-values[i]*values[i]);break;
            default:break; // Comparisons/logical conditions have zero derivative.
            }
        }
        if(!std::isfinite(result) || !std::all_of(partials.begin(),partials.end(),[](double x){return std::isfinite(x);}))return false;
        std::copy_n(data,count,last.begin());lastTime=time;cached=true;return true;
    }
};

class PARSER
{
public:
    explicit PARSER(std::string text):m_text(std::move(text)){}
    PROGRAM Parse()
    {
        m_program.root=Expression(0);Space();if(m_at!=m_text.size())Fail();
        m_program.values.resize(m_program.nodes.size());m_program.adjoints.resize(m_program.nodes.size());
        m_program.last.resize(m_program.inputs.size());m_program.partials.resize(m_program.inputs.size());
        return std::move(m_program);
    }
private:
    std::string m_text;std::size_t m_at=0;unsigned m_depth=0;
    PROGRAM m_program;
    std::vector<RANGE> m_ranges;
    std::map<std::tuple<OP,int,int,int,double>,int> m_common;
    [[noreturn]] void Fail(){throw std::runtime_error("Expression outside compiled subset");}
    void Space(){while(m_at<m_text.size()&&std::isspace(static_cast<unsigned char>(m_text[m_at])))++m_at;}
    bool Take(const std::string& token)
    { Space();if(m_text.compare(m_at,token.size(),token)!=0)return false;m_at+=token.size();return true; }
    int Add(OP op,int a=-1,int b=-1,int c=-1,double constant=0)
    {
        if(m_program.nodes.size()>=2048)Fail();
        auto& nodes=m_program.nodes;
        // ngspice regularizes singular division with its circuit gmin. Leave
        // potentially singular expressions to it instead of changing that rule.
        if( op == DIV && !( m_ranges[b].low >= 1e-9 || m_ranges[b].high <= -1e-9 ) )
            Fail();
        if(op==SELECT && nodes[a].op==CONSTANT)return nodes[a].constant!=0?b:c;
        if(op!=CONSTANT && op!=INPUT && op!=TIME && nodes[a].op==CONSTANT
            && (b<0 || nodes[b].op==CONSTANT) && (c<0 || nodes[c].op==CONSTANT)) {
            constant=PROGRAM::Value(op,nodes[a].constant,b<0?0:nodes[b].constant,c<0?0:nodes[c].constant);
            if(std::isfinite(constant)){op=CONSTANT;a=b=c=-1;}
        }
        const auto key=std::make_tuple(op,a,b,c,constant);
        const auto existing=m_common.find(key);if(existing!=m_common.end())return existing->second;
        const int index=int(nodes.size());
        nodes.push_back({op,a,b,c,constant});
        RANGE range;
        const auto x = a < 0 || op == INPUT ? RANGE{} : m_ranges[a];
        const auto y = b < 0 ? RANGE{} : m_ranges[b];
        switch( op )
        {
        case CONSTANT: range = {constant,constant}; break;
        case MIN: range = {std::min(x.low,y.low),std::min(x.high,y.high)}; break;
        case MAX: range = {std::max(x.low,y.low),std::max(x.high,y.high)}; break;
        case NEG: range = {-x.high,-x.low}; break;
        case ABS: range = {x.low<=0 && x.high>=0 ? 0 : std::min(std::abs(x.low),std::abs(x.high)),
                           std::max(std::abs(x.low),std::abs(x.high))}; break;
        case TANH: range = {std::tanh(x.low),std::tanh(x.high)}; break;
        case SELECT: range = {std::min(y.low,m_ranges[c].low),std::max(y.high,m_ranges[c].high)}; break;
        case LT: case LE: case EQ: case NE: case GT: case GE: case AND: case OR: case NOT:
            range = {0,1}; break;
        default: break;
        }
        m_ranges.push_back(range);
        m_common.emplace(key,index);return index;
    }
    int Input(const std::string& raw)
    {
        const auto name=Lower(Trim(raw));
        if(name=="0")return Add(CONSTANT);
        if(name.empty()||name.size()>200||name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_./:$#+@-[]")!=std::string::npos)Fail();
        const auto found=std::find(m_program.inputs.begin(),m_program.inputs.end(),name);
        int index=int(found-m_program.inputs.begin());
        if(found==m_program.inputs.end()) { if(index>=64)Fail();m_program.inputs.push_back(name); }
        return Add(INPUT,index);
    }
    int Atom()
    {
        if(++m_depth>96)Fail();
        struct DEPTH_GUARD { unsigned& depth; ~DEPTH_GUARD(){--depth;} } guard{m_depth};
        Space();if(m_at>=m_text.size())Fail();
        if(Take("(")){int value=Expression(0);if(!Take(")"))Fail();return value;}
        if(Take("+"))return Atom();
        if(Take("-"))return Add(NEG,Atom());
        if(Take("!"))return Add(NOT,Atom());
        Space();const auto begin=m_at;
        if(std::isdigit(static_cast<unsigned char>(m_text[m_at]))||m_text[m_at]=='.') {
            char* end=nullptr;double value=std::strtod(m_text.c_str()+m_at,&end);
            if(end==m_text.c_str()+m_at||!std::isfinite(value))Fail();
            m_at=std::size_t(end-m_text.c_str());
            std::size_t suffixStart=m_at;while(m_at<m_text.size()&&std::isalpha(static_cast<unsigned char>(m_text[m_at])))++m_at;
            const auto suffix=Lower(m_text.substr(suffixStart,m_at-suffixStart));
            if(!suffix.empty()) {
                const std::array<std::pair<const char*,double>,10> scales{{{"meg",1e6},{"mil",25.4e-6},{"t",1e12},{"g",1e9},{"k",1e3},{"m",1e-3},{"u",1e-6},{"n",1e-9},{"p",1e-12},{"f",1e-15}}};
                bool matched=false;for(const auto& [name,scale]:scales)if(suffix.starts_with(name)){value*=scale;matched=true;break;}
                if(!matched)Fail();
            }
            if(!std::isfinite(value))Fail();
            return Add(CONSTANT,-1,-1,-1,value);
        }
        while(m_at<m_text.size()&&(std::isalnum(static_cast<unsigned char>(m_text[m_at]))||m_text[m_at]=='_'))++m_at;
        const auto name=Lower(m_text.substr(begin,m_at-begin));if(name.empty())Fail();
        if(name=="time"){m_program.usesTime=true;return Add(TIME);}
        if(!Take("("))Fail();
        if(name=="v") {
            const auto first=m_at;while(m_at<m_text.size()&&m_text[m_at]!=')'&&m_text[m_at]!=',')++m_at;
            auto a=Input(m_text.substr(first,m_at-first));
            if(Take(",")){const auto second=m_at;while(m_at<m_text.size()&&m_text[m_at]!=')')++m_at;auto b=Input(m_text.substr(second,m_at-second));a=Add(SUB,a,b);}
            if(!Take(")"))Fail();
            return a;
        }
        int a=Expression(0);
        if(name=="min"||name=="max") {if(!Take(","))Fail();int b=Expression(0);if(!Take(")"))Fail();return Add(name=="min"?MIN:MAX,a,b);}
        if(!Take(")"))Fail();
        if(name=="tanh")return Add(TANH,a);
        if(name=="abs")return Add(ABS,a);
        Fail();
    }
    int Expression(int minimum)
    {
        if(++m_depth>96)Fail();
        int left=Atom();
        const std::array<std::tuple<const char*,OP,int>,14> ops{{
            {"||",OR,1},{"&&",AND,2},{"==",EQ,3},{"!=",NE,3},{"<=",LE,4},{">=",GE,4},
            {"<",LT,4},{">",GT,4},{"+",ADD,5},{"-",SUB,5},{"*",MUL,6},{"/",DIV,6},
            {"",ADD,-1},{"",ADD,-1}}};
        for(;;) {
            bool found=false;for(const auto& [token,op,precedence]:ops) {
                if(precedence<minimum||precedence<0)continue;
                if(Take(token)){int right=Expression(precedence+1);left=Add(op,left,right);found=true;break;}
            }
            if(!found)break;
        }
        if(minimum==0&&Take("?")){int yes=Expression(0);if(!Take(":"))Fail();int no=Expression(0);left=Add(SELECT,left,yes,no);}
        --m_depth;return left;
    }
};
}

struct COMPILED_EXPRESSIONS::IMPL { std::vector<PROGRAM> programs;std::string error; };
COMPILED_EXPRESSIONS::COMPILED_EXPRESSIONS():m_impl(std::make_unique<IMPL>()){}
COMPILED_EXPRESSIONS::~COMPILED_EXPRESSIONS()=default;
COMPILED_EXPRESSIONS::COMPILED_EXPRESSIONS(COMPILED_EXPRESSIONS&&) noexcept=default;
COMPILED_EXPRESSIONS& COMPILED_EXPRESSIONS::operator=(COMPILED_EXPRESSIONS&&) noexcept=default;
bool COMPILED_EXPRESSIONS::Empty()const{return m_impl->programs.empty();}
std::string COMPILED_EXPRESSIONS::Error()const{return m_impl->error;}
bool COMPILED_EXPRESSIONS::Evaluate(unsigned slot,const double* inputs,unsigned count,double time,double& value,const double*& partials)
{
    if(slot>=m_impl->programs.size())return false;
    auto& program=m_impl->programs[slot];
    if(!program.Evaluate(inputs,count,time)){m_impl->error="Non-finite compiled expression: "+program.name;return false;}
    value=program.result;partials=program.partials.data();return true;
}
std::string COMPILED_EXPRESSIONS::Configure(const std::string& deck,const std::vector<std::string>& signals,const std::vector<std::string>& mutableElements)
{
    m_impl=std::make_unique<IMPL>();
    std::set<std::string> preserved;
    for(const auto& name:mutableElements)preserved.insert(Lower(name));
    std::string all=Lower(deck);for(const auto& signal:signals)all+=' '+Lower(signal);
    // Current references need the original SPICE element and its branch vector.
    for(std::size_t at=0;(at=all.find("i(",at))!=std::string::npos;at+=2) {
        auto end=all.find(')',at);if(end==std::string::npos)break;preserved.insert(Trim(all.substr(at+2,end-at-2)));
    }
    // Preserve branch-controlled sources and device-vector references as well.
    std::istringstream references(deck);
    std::string referenceLine;
    while(std::getline(references,referenceLine))
    {
        std::istringstream words(Lower(referenceLine));
        std::string name,positive,negative,control;
        words>>name>>positive>>negative>>control;
        if(!name.empty() && (name[0]=='f'||name[0]=='h'))preserved.insert(control);
    }
    std::string prefix="ca_compiled_expr_";
    while(all.find(prefix)!=std::string::npos)prefix+='_';
    std::istringstream lines(deck);std::string line,result;int scope=0;
    bool title=true;
    while(std::getline(lines,line)) {
        std::istringstream fields(line);std::string name,positive,negative,type;
        fields>>name;
        const auto lower=Lower(name);
        if(lower==".subckt")++scope;
        if(lower==".ends")--scope;
        bool compiled=false;
        const auto next = lines.tellg();
        std::string following;
        if(next>=0) { std::getline(lines,following); lines.clear(); lines.seekg(next); }
        following=Trim(following);
        const bool continued=!following.empty() && following[0]=='+';
        if(!title && scope==0 && !continued && !name.empty() && (name[0]=='B'||name[0]=='b')
            && !preserved.contains(lower) && all.find('@'+lower+'[')==std::string::npos
            && all.find(lower+"#branch")==std::string::npos) {
            fields>>positive>>negative;std::string expression;std::getline(fields,expression);expression=Trim(expression);
            if(expression.size()>4 && (expression[0]=='v'||expression[0]=='V'||expression[0]=='i'||expression[0]=='I')
                && expression[1]=='=' && expression[2]=='{' && expression.back()=='}') {
                try {
                    auto program=PARSER(expression.substr(3,expression.size()-4)).Parse();
                    if(program.inputs.empty() || m_impl->programs.size()>=4096)throw std::runtime_error("Keep source native");
                    program.name=name;const auto slot=m_impl->programs.size();
                    const auto model=prefix+std::to_string(slot);
                    result+=".model "+model+" ca_expression(slot="+std::to_string(slot)+")\n";
                    result+="A"+model+" [";for(const auto& input:program.inputs)result+=' '+input;
                    result+=" ] "+std::string(expression[0]=='v'||expression[0]=='V'?"%vd ":"%id ")+positive+' '+negative+' '+model+'\n';
                    m_impl->programs.push_back(std::move(program));compiled=true;
                }catch(const std::runtime_error&){} // Leave unsupported syntax to ngspice.
            }
        }
        if(!compiled)result+=line+'\n';
        title=false;
    }
    return result;
}
}
