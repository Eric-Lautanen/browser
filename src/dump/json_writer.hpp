#pragma once
#include "../../html/utf8.hpp"
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstdio>

namespace json {

std::string esc(const std::string &s);
std::string q(const std::string &s);
std::string num(float n);
std::string bool_str(bool b);

struct Obj {
    std::string data;
    bool first = true;
    int indent = -1;
    Obj(int indent = -1) : indent(indent) { data = "{"; }
    void nl(bool inner = true);
    void kv(const std::string &k, const std::string &v);
    void kv_raw(const std::string &k, const std::string &v);
    void kv_num(const std::string &k, float v);
    void kv_bool(const std::string &k, bool v);
    std::string done();
};

struct Arr {
    std::string data;
    bool first = true;
    int indent = -1;
    Arr(int indent = -1) : indent(indent) { data = "["; }
    void nl(bool inner = true);
    void push(const std::string &v);
    std::string done();
};

} // namespace json
