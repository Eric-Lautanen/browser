#include "script_runner.hpp"
#include "vm.hpp"
#include "parser.hpp"
#include "compiler.hpp"

namespace browser::js {

ScriptRunner::ScriptRunner(VM* vm) : vm_(vm) {}

void ScriptRunner::add_script(ScriptEntry entry) {
    scripts_.push_back(std::move(entry));
}

void ScriptRunner::compile_script(ScriptEntry& entry) {
    if (entry.executed) return;
    if (entry.compiled) return;
    Parser parser(entry.source);
    auto program = parser.parse_program();
    if (!parser.errors().empty()) {
        for (auto& err : parser.errors()) errors_.push_back(err);
        return;
    }
    Compiler compiler;
    entry.compiled = compiler.compile(*program);
}

void ScriptRunner::execute_script(ScriptEntry& entry) {
    if (entry.executed) return;
    compile_script(entry);
    if (!entry.compiled) return;
    vm_->execute(entry.compiled.get());
    entry.executed = true;
}

void ScriptRunner::execute_immediate() {
    for (auto& entry : scripts_) {
        if (entry.kind == ScriptKind::INLINE && entry.phase == ScriptPhase::IMMEDIATE) {
            execute_script(entry);
        }
    }
}

void ScriptRunner::execute_deferred() {
    for (auto& entry : scripts_) {
        if (entry.phase == ScriptPhase::DEFER) {
            execute_script(entry);
        }
    }
}

void ScriptRunner::execute_async(const std::string& url) {
    for (auto& entry : scripts_) {
        if (entry.phase == ScriptPhase::ASYNC && entry.url == url) {
            execute_script(entry);
            return;
        }
    }
}

bool ScriptRunner::all_executed() const {
    for (auto& entry : scripts_) {
        if (!entry.executed) return false;
    }
    return true;
}

} // namespace browser::js
