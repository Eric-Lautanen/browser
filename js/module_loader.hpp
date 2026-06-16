#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include "../tests/utility.hpp"
#include "../html/dom.hpp"

namespace browser::js {

class VM;
class BytecodeFunction;

struct ModuleEntry {
    std::string url;
    std::string source;
    std::unique_ptr<BytecodeFunction> compiled;
    bool executed = false;
    std::vector<std::string> dependencies;
};

class ModuleLoader {
public:
    ModuleLoader(VM* vm);
    void add_module(const std::string& url, const std::string& source);
    Result<void> load_module_tree(const std::string& entry_url);
    std::vector<std::string> errors() const { return errors_; }
private:
    VM* vm_;
    std::unordered_map<std::string, std::unique_ptr<ModuleEntry>> modules_;
    std::vector<std::string> errors_;
    ModuleEntry* parse_module(const std::string& url, const std::string& source);
    void execute_module(ModuleEntry* entry);
};

} // namespace browser::js
