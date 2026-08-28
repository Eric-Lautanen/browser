#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "../core/utility.hpp"
#include "value.hpp"
#include "../html/dom.hpp"

namespace browser::js {

class VM;
class BytecodeFunction;

enum class ScriptKind { INLINE, EXTERNAL, MODULE };
enum class ScriptPhase { IMMEDIATE, DEFER, ASYNC };

struct ScriptEntry {
    ScriptKind kind;
    ScriptPhase phase;
    std::string source;       // For inline scripts
    std::string url;          // For external scripts
    std::vector<u8> data;     // For external scripts after fetch
    html::Element* element;   // The <script> element
    std::unique_ptr<BytecodeFunction> compiled; // Compiled bytecode
    bool executed = false;
};

class ScriptRunner {
public:
    ScriptRunner(VM* vm);
    void add_script(ScriptEntry entry);
    // BR-C11: attaches fetched bytes to an external script queued earlier.
    void attach_external_data(const std::string &url, std::vector<u8> data);
    void execute_immediate();
    void execute_deferred();
    void execute_async(const std::string& url);
    void fetch_and_execute_external(const std::string& url, html::Element* element);
    bool all_executed() const;
    bool has_pending_external(const std::string &url) const;
    std::vector<std::string> errors() const { return errors_; }
private:
    VM* vm_;
    std::vector<ScriptEntry> scripts_;
    std::vector<std::string> errors_;
    void execute_script(ScriptEntry& entry);
    void compile_script(ScriptEntry& entry);
};

} // namespace browser::js
