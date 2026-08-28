#include "shorthand_animation.hpp"
#include <cstdlib>
#include <vector>

namespace browser::css {

void expand_animation(ComputedStyle& style, const CSSValue& val) {
    if (val.type != CSSValue::Type::STRING) return;
    std::string s = val.string_value;
    size_t sp = 0; std::vector<std::string> parts;
    while (sp < s.size()) {
        while (sp < s.size() && s[sp]==' ') sp++;
        if (sp>=s.size()) break;
        size_t end = s.find(' ', sp);
        if (end==std::string::npos) end=s.size();
        parts.push_back(s.substr(sp,end-sp));
        sp=end+1;
    }
    auto set_anim = [&](const std::string &subprop, const std::string &v){ CSSValue cv; cv.type=CSSValue::Type::KEYWORD; cv.keyword=v; style.properties[subprop]=cv; };
    int num_seen=0;
    for(auto &p: parts){
        if(!p.empty() && (std::isdigit((unsigned char)p[0])||p[0]=='.')){
            char *end=nullptr; f32 num=std::strtof(p.c_str(),&end);
            if(end && *end!='\0'){ if(num_seen==0){ if(std::string(end)=="ms") set_anim("animation-duration", std::to_string(num/1000.0f)+"s"); else set_anim("animation-duration",p); num_seen++; }
            } else num_seen++;
        }
    }
    if(!parts.empty() && parts[0]!="infinite"){
        std::string first=parts[0];
        if(first!="ease"&&first!="linear"&&first!="ease-in"&&first!="ease-out"&&first!="ease-in-out"&&first.substr(0,6)!="cubic-"&&first.substr(0,6)!="steps("&&first.find("ms")==std::string::npos&&first.find('s')==std::string::npos){
            set_anim("animation-name",first);
        }
    }
}

} // namespace browser::css
