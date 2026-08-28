#include "shorthand_font.hpp"
#include <cstdlib>
#include <vector>

namespace browser::css {

void expand_font(ComputedStyle& style, const CSSValue& val) {
    if (val.type != CSSValue::Type::STRING) return;
    std::string s = val.string_value;
    if (s == "caption" || s == "icon" || s == "menu" || s == "message-box" || s == "small-caption" || s == "status-bar" || s == "inherit" || s == "initial") {
        if (!style.has("font-family")) { CSSValue cv; cv.type = CSSValue::Type::KEYWORD; cv.keyword = s; style.properties["font-family"] = cv; }
        return;
    }
    std::vector<std::string> parts; size_t pp=0;
    while (pp < s.size()) {
        while (pp < s.size() && s[pp]==' ') pp++;
        if (pp>=s.size()) break;
        if (s[pp]=='"' || s[pp]=='\'') {
            char q=s[pp]; size_t end=s.find(q,pp+1);
            if(end==std::string::npos){parts.push_back(s.substr(pp)); break;}
            parts.push_back(s.substr(pp,end-pp+1)); pp=end+1;
        } else { size_t end=s.find(' ',pp); if(end==std::string::npos) end=s.size(); parts.push_back(s.substr(pp,end-pp)); pp=end+1; }
    }
    int idx=0; int n=(int)parts.size();
    auto is_font_style=[](const std::string& s){return s=="normal"||s=="italic"||s=="oblique";};
    auto is_font_variant=[](const std::string& s){return s=="normal"||s=="small-caps";};
    auto is_font_weight=[](const std::string& s){return s=="normal"||s=="bold"||s=="bolder"||s=="lighter"||(s.size()>=1&&std::isdigit((unsigned char)s[0])&&std::atoi(s.c_str())>=1&&std::atoi(s.c_str())<=1000);};
    auto is_length_or_percent=[](const std::string& s)->bool{ if(s.empty()) return false; char* end=nullptr; std::strtof(s.c_str(),&end); if(end==s.c_str()) return false; std::string u=end; return u=="px"||u=="em"||u=="rem"||u=="pt"||u=="%"||u.empty(); };
    if(idx<n && is_font_style(parts[idx]) && parts[idx]!="normal"){ if(!style.has("font-style")){CSSValue cv; cv.type=CSSValue::Type::KEYWORD; cv.keyword=parts[idx]; style.properties["font-style"]=cv;} idx++; }
    if(idx<n && is_font_variant(parts[idx]) && parts[idx]!="normal"){ if(!style.has("font-variant")){CSSValue cv; cv.type=CSSValue::Type::KEYWORD; cv.keyword=parts[idx]; style.properties["font-variant"]=cv;} idx++; }
    if(idx<n && is_font_weight(parts[idx]) && parts[idx]!="normal"){ if(!style.has("font-weight")){CSSValue cv; cv.type=CSSValue::Type::KEYWORD; cv.keyword=parts[idx]; style.properties["font-weight"]=cv;} idx++; }
    if(idx<n && (is_length_or_percent(parts[idx])||parts[idx]=="xx-small"||parts[idx]=="x-small"||parts[idx]=="small"||parts[idx]=="medium"||parts[idx]=="large"||parts[idx]=="x-large"||parts[idx]=="xx-large"||parts[idx]=="xxx-large"||parts[idx]=="larger"||parts[idx]=="smaller")){
        if(!style.has("font-size")){
            CSSValue cv; cv.type=CSSValue::Type::STRING; cv.string_value=parts[idx];
            char* end=nullptr; f32 num=std::strtof(parts[idx].c_str(),&end);
            if(end!=parts[idx].c_str()){ cv.type=CSSValue::Type::LENGTH; cv.length.value=num; std::string unit=end; if(unit=="px") cv.length.unit=Length::Unit::PX; else if(unit=="em") cv.length.unit=Length::Unit::EM; else if(unit=="rem") cv.length.unit=Length::Unit::REM; else if(unit=="pt") cv.length.unit=Length::Unit::PT; else if(unit=="%") cv.length.unit=Length::Unit::PERCENT; else cv.type=CSSValue::Type::KEYWORD; } else cv.type=CSSValue::Type::KEYWORD;
            cv.keyword=parts[idx]; style.properties["font-size"]=cv;
        }
        idx++;
        if(idx<n && parts[idx]=="/"){ idx++; if(idx<n && (is_length_or_percent(parts[idx])||parts[idx]=="normal")){ if(!style.has("line-height")){CSSValue lh; lh.type=CSSValue::Type::STRING; lh.string_value=parts[idx]; char* end=nullptr; f32 num=std::strtof(parts[idx].c_str(),&end); if(end!=parts[idx].c_str()){lh.type=CSSValue::Type::NUMBER; lh.number=num;} else if(parts[idx]=="normal"){lh.type=CSSValue::Type::KEYWORD; lh.keyword="normal";} style.properties["line-height"]=lh; } idx++; }}
    }
    std::string family; for(int i=idx;i<n;i++){ if(i>idx) family+=", "; std::string fp=parts[i]; if(fp.size()>=2&&(fp[0]=='"'||fp[0]=='\'')&&fp.back()==fp[0]) fp=fp.substr(1,fp.size()-2); family+=fp; }
    if(!family.empty() && !style.has("font-family")){ CSSValue fv; fv.type=CSSValue::Type::KEYWORD; fv.keyword=family; style.properties["font-family"]=fv; }
}

} // namespace browser::css
