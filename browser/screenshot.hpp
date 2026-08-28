#pragma once
#include <vector>
#include <cstdint>
#include <cstdio>
#include <string>
#include "../core/utility.hpp"

namespace browser {

inline std::vector<u8> encode_bmp_rgba(int sw, int sh, const std::vector<u8>& rgba) {
    auto wu32 = [](std::vector<u8>& d, u32 v){ d.push_back(v & 0xFF); d.push_back((v>>8)&0xFF); d.push_back((v>>16)&0xFF); d.push_back((v>>24)&0xFF); };
    auto wu16 = [](std::vector<u8>& d, u16 v){ d.push_back(v & 0xFF); d.push_back((v>>8)&0xFF); };
    std::vector<u8> bmp;
    bmp.reserve(static_cast<size_t>(14+40 + (((sw*24+31)/32)*4)*sh));
    u32 row = ((static_cast<u32>(sw)*24 +31)/32)*4;
    u32 ds = row * static_cast<u32>(sh);
    bmp.push_back('B'); bmp.push_back('M');
    wu32(bmp, 14+40+ds); wu32(bmp,0); wu32(bmp,14+40);
    wu32(bmp,40); wu32(bmp, static_cast<u32>(sw)); wu32(bmp, static_cast<u32>(sh));
    wu16(bmp,1); wu16(bmp,24); wu32(bmp,0); wu32(bmp,ds);
    wu32(bmp,2835); wu32(bmp,2835); wu32(bmp,0); wu32(bmp,0);
    for(int y=0; y<sh; ++y){
        for(int x=0;x<sw;++x){
            size_t idx=(static_cast<size_t>(y)*static_cast<size_t>(sw)+static_cast<size_t>(x))*4;
            bmp.push_back(rgba[idx+2]); bmp.push_back(rgba[idx+1]); bmp.push_back(rgba[idx+0]);
        }
        for(u32 p=static_cast<u32>(sw)*3;p<row;++p) bmp.push_back(0);
    }
    return bmp;
}

inline bool write_bmp_file(const std::string& path, int sw, int sh, const std::vector<u8>& rgba){
    auto bmp = encode_bmp_rgba(sw, sh, rgba);
    FILE* f = fopen(path.c_str(),"wb");
    if(!f) return false;
    fwrite(bmp.data(),1,bmp.size(),f);
    fclose(f);
    return true;
}

} // namespace browser
