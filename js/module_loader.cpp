#include "module_loader.hpp"
#include "vm.hpp"
#include "parser.hpp"
#include "compiler.hpp"

namespace browser::js {

ModuleLoader::ModuleLoader(VM* vm) : vm_(vm) {}

void ModuleLoader::add_module(const std::string& url, const std::string& source) {
    auto entry = std::make_unique<ModuleEntry>();
    entry->url = url;
    entry->source = source;
    modules_[url] = std::move(entry);
}

ModuleEntry* ModuleLoader::parse_module(const std::string& url, const std::string& source) {
    auto it = modules_.find(url);
    if (it == modules_.end()) return nullptr;
    auto* entry = it->second.get();
    if (entry->compiled) return entry;
    Parser parser(source);
    auto program = parser.parse_program();
    if (!parser.errors().empty()) {
        for (auto& err : parser.errors()) errors_.push_back(err);
        return nullptr;
    }
    Compiler compiler;
    entry->compiled = compiler.compile(*program);
    return entry;
}

void ModuleLoader::execute_module(ModuleEntry* entry) {
    if (!entry || entry->executed) return;
    if (!entry->compiled) {
        parse_module(entry->url, entry->source);
    }
    if (entry->compiled) {
        vm_->execute(entry->compiled.get());
        entry->executed = true;
    }
}

Result<void> ModuleLoader::load_module_tree(const std::string& entry_url) {
    auto* entry = parse_module(entry_url, "");
    if (!entry) return Result<void>("Module not found: " + entry_url);
    
    // Topological sort and execute
    std::vector<ModuleEntry*> sorted;
    std::unordered_map<std::string, bool> visited;
    std::function<bool(const std::string&)> visit = [&](const std::string& url) -> bool {
        if (visited[url]) return true;
        visited[url] = true;
        auto it = modules_.find(url);
        if (it == modules_.end()) return false;
        for (auto& dep : it->second->dependencies) {
            if (!visit(dep)) return false;
        }
        sorted.push_back(it->second.get());
        return true;
    };
    
    if (!visit(entry_url)) return Result<void>("Module dependency error");
    
    for (auto* m : sorted) {
        execute_module(m);
    }
    
    return Result<void>();
}

} // namespace browser::js
