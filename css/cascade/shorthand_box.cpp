#include "shorthand_box.hpp"
#include <cstdio>
#include <vector>
#include <string>

namespace browser::css {

[[maybe_unused]] static std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for(char c: s){
        if(c==' '||c=='\t'||c=='\n'||c=='\r'){
            if(!cur.empty()){ out.push_back(cur); cur.clear(); }
        } else cur.push_back(c);
    }
    if(!cur.empty()) out.push_back(cur);
    return out;
}

void expand_four_sides(ComputedStyle& style, const std::string& base, const std::string& value) {
    auto parts = split_ws(value);
    if(parts.empty()) return;
    auto set_side = [&](const std::string& side, const std::string& pv){
        CSSValue cv;
        // Try to preserve original parsing: length vs keyword - store as string for now
        // The engine previously did string->CSSValue via string reconstruction; keep same.
        // Minimal: store as STRING so later resolve can parse.
        if(pv=="auto"||pv=="inherit"||pv=="initial"||pv=="unset"){
            cv.type = CSSValue::Type::KEYWORD; cv.keyword = pv;
        } else {
            cv.type = CSSValue::Type::STRING; cv.string_value = pv;
        }
        style.properties[side] = cv;
    };
    if(parts.size()==1){
        set_side(base+"-top", parts[0]); set_side(base+"-right", parts[0]);
        set_side(base+"-bottom", parts[0]); set_side(base+"-left", parts[0]);
    } else if(parts.size()==2){
        set_side(base+"-top", parts[0]); set_side(base+"-right", parts[1]);
        set_side(base+"-bottom", parts[0]); set_side(base+"-left", parts[1]);
    } else if(parts.size()==3){
        set_side(base+"-top", parts[0]); set_side(base+"-right", parts[1]);
        set_side(base+"-bottom", parts[2]); set_side(base+"-left", parts[1]);
    } else if(parts.size()>=4){
        set_side(base+"-top", parts[0]); set_side(base+"-right", parts[1]);
        set_side(base+"-bottom", parts[2]); set_side(base+"-left", parts[3]);
    }
}

void expand_four_sides(ComputedStyle& style, const std::string& base, const CSSValue& val) {
    if(val.type==CSSValue::Type::STRING){
        expand_four_sides(style, base, val.string_value);
        return;
    }
    std::string val_str;
    if(val.type==CSSValue::Type::KEYWORD) val_str = val.keyword;
    else if(val.type==CSSValue::Type::LENGTH){
        char buf[64]; snprintf(buf,sizeof(buf),"%.0f", val.length.value); val_str=buf;
        if(val.length.unit==Length::Unit::PX) val_str+="px";
        else if(val.length.unit==Length::Unit::EM) val_str+="em";
        else if(val.length.unit==Length::Unit::REM) val_str+="rem";
        else if(val.length.unit==Length::Unit::PERCENT) val_str+="%";
    } else if(val.type==CSSValue::Type::NUMBER) val_str = std::to_string(val.number);
    if(!val_str.empty()) expand_four_sides(style, base, val_str);
}

} // namespace browser::css
