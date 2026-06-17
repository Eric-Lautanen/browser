#include "compiler.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace browser::js {

    // ============================================================================
    // BytecodeFunction implementation
    // ============================================================================

    u32 BytecodeFunction::add_constant(const Constant &c) {
        for (u32 i = 0; i < constants.size(); i++) {
            if (constants[i].type != c.type)
                continue;
            switch (c.type) {
                case Constant::Type::NUMBER: {
                    if (std::memcmp(&constants[i].number, &c.number, sizeof(f64)) == 0)
                        return i;
                    break;
                }
                case Constant::Type::STRING:
                    if (constants[i].str == c.str)
                        return i;
                    break;
                case Constant::Type::BOOL:
                    if (constants[i].boolean == c.boolean)
                        return i;
                    break;
            }
        }
        constants.push_back(c);
        return (u32)constants.size() - 1;
    }

    void BytecodeFunction::emit(Opcode op) {
        instructions.push_back({op, u32{0}});
    }

    void BytecodeFunction::emit(Opcode op, u32 operand) {
        Instruction inst;
        inst.op = op;
        inst.operand = operand;
        instructions.push_back(std::move(inst));
    }

    void BytecodeFunction::emit(Opcode op, f64 operand) {
        Instruction inst;
        inst.op = op;
        inst.operand = operand;
        instructions.push_back(std::move(inst));
    }

    void BytecodeFunction::emit(Opcode op, const std::string &operand) {
        Instruction inst;
        inst.op = op;
        inst.operand = operand;
        instructions.push_back(std::move(inst));
    }

    void BytecodeFunction::emit(Opcode op, const Instruction::CallMethodInfo &info) {
        Instruction inst;
        inst.op = op;
        inst.operand = info;
        instructions.push_back(std::move(inst));
    }

    // ============================================================================
    // Compiler
    // ============================================================================

    Compiler::Compiler() {
        enter_scope();
    }

    std::unique_ptr<BytecodeFunction> Compiler::compile(Program &program) {
        current_ = std::make_unique<BytecodeFunction>();
        current_->name = "top-level";
        at_top_level_ = true;
        for (auto &stmt : program.body) {
            compile_stmt(*stmt);
        }
        at_top_level_ = false;
        current_->emit(Opcode::RETURN);
        return std::move(current_);
    }

    // ============================================================================
    // Function compilation
    // ============================================================================

    void Compiler::compile_function(FuncDeclStmt &func) {
        auto child = std::make_unique<BytecodeFunction>();
        child->name = func.name;
        child->num_params = (u32)func.params.size();

        auto parent = std::move(current_);
        u32 saved_next_local = next_local_slot_;
        auto saved_scope_stack = std::move(scope_stack_);

        current_ = std::move(child);
        next_local_slot_ = 0;
        bool saved_tl = at_top_level_;
        at_top_level_ = false;
        enter_scope();

        for (auto &param : func.params) {
            if (auto *ip = std::get_if<IdentPattern>(param.get())) {
                allocate_local(ip->name);
            }
        }

        Stmt body_stub(std::move(*func.body));
        compile_stmt(body_stub);

        current_->emit(Opcode::PUSH_UNDEFINED);
        current_->emit(Opcode::RETURN);
        exit_scope();
        at_top_level_ = saved_tl;

        child = std::move(current_);
        current_ = std::move(parent);
        next_local_slot_ = saved_next_local;
        scope_stack_ = std::move(saved_scope_stack);
        u32 func_idx = (u32)current_->child_functions.size();
        current_->child_functions.push_back(std::move(child));

        current_->emit(Opcode::PUSH_FUNCTION, func_idx);
        current_->emit(Opcode::STORE_GLOBAL, func.name);
        u32 slot = resolve_local(func.name);
        if (slot != UINT32_MAX) {
            current_->emit(Opcode::STORE_LOCAL, slot);
        } else {
            u32 new_slot = allocate_local(func.name);
            current_->emit(Opcode::STORE_LOCAL, new_slot);
        }
        current_->emit(Opcode::POP);
    }

    // ============================================================================
    // Scope / local helpers
    // ============================================================================

    void Compiler::enter_scope() {
        scope_stack_.push_back({});
    }

    void Compiler::exit_scope() {
        scope_stack_.pop_back();
    }

    u32 Compiler::resolve_local(const std::string &name) {
        for (auto it = scope_stack_.rbegin(); it != scope_stack_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                return found->second;
            }
        }
        return UINT32_MAX;
    }

    u32 Compiler::allocate_local(const std::string &name) {
        u32 slot = next_local_slot_++;
        scope_stack_.back()[name] = slot;
        if (current_->num_locals <= slot) {
            current_->num_locals = slot + 1;
        }
        return slot;
    }

    // ============================================================================
    // Constant helpers
    // ============================================================================

    u32 Compiler::add_constant(const BytecodeFunction::Constant &c) {
        return current_->add_constant(c);
    }

    // ============================================================================
    // Jump helpers
    // ============================================================================

    u32 Compiler::emit_jump(Opcode op) {
        current_->emit(op, (u32)0);
        return (u32)current_->instructions.size() - 1;
    }

    void Compiler::patch_jump(u32 index) {
        current_->instructions[index].operand = (u32)current_->instructions.size();
    }

    // ============================================================================
    // Destructuring compilation
    // ============================================================================

    void Compiler::compile_destructuring(std::unique_ptr<Pattern> &pat) {
        if (auto *obj = std::get_if<ObjPattern>(pat.get())) {
            for (auto &prop : obj->properties) {
                current_->emit(Opcode::DUP);
                current_->emit(Opcode::GET_PROP, prop.key);
                if (auto *ip = std::get_if<IdentPattern>(prop.value.get())) {
                    u32 slot = allocate_local(ip->name);
                    current_->emit(Opcode::STORE_LOCAL, slot);
                    current_->emit(Opcode::POP);
                }
            }
        } else if (auto *arr = std::get_if<ArrPattern>(pat.get())) {
            for (size_t i = 0; i < arr->elements.size(); i++) {
                if (!arr->elements[i])
                    continue;
                current_->emit(Opcode::DUP);
                BytecodeFunction::Constant ic;
                ic.type = BytecodeFunction::Constant::Type::NUMBER;
                ic.number = (f64)i;
                u32 idx_c = add_constant(ic);
                current_->emit(Opcode::PUSH_NUMBER, idx_c);
                current_->emit(Opcode::GET_PROP_COMPUTED);
                if (auto *ip = std::get_if<IdentPattern>(arr->elements[i].get())) {
                    u32 slot = allocate_local(ip->name);
                    current_->emit(Opcode::STORE_LOCAL, slot);
                    current_->emit(Opcode::POP);
                }
            }
        }
    }

}  // namespace browser::js
