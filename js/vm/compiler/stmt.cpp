#include "compiler.hpp"

namespace browser::js {

    void Compiler::compile_stmt(Stmt &stmt) {
        if (auto *expr = std::get_if<ExpressionStmt>(&stmt)) {
            compile_expr(*expr->expr);
            if (!at_top_level_) {
                current_->emit(Opcode::POP);
            }
        } else if (auto *block = std::get_if<BlockStmt>(&stmt)) {
            enter_scope();
            bool saved = at_top_level_;
            at_top_level_ = false;
            for (auto &s : block->body) {
                compile_stmt(*s);
            }
            at_top_level_ = saved;
            exit_scope();
        } else if (auto *var = std::get_if<VarDeclStmt>(&stmt)) {
            for (auto &decl : var->declarations) {
                if (decl.init) {
                    compile_expr(*decl.init);
                } else {
                    current_->emit(Opcode::PUSH_UNDEFINED);
                }
                if (auto *pat = std::get_if<IdentPattern>(decl.id.get())) {
                    if (at_top_level_) {
                        // Spec: a top-level `var` creates a property on the
                        // global object so separate scripts share state.
                        current_->emit(Opcode::STORE_GLOBAL, pat->name);
                        current_->emit(Opcode::POP);
                    } else {
                        u32 slot = allocate_local(pat->name);
                        current_->emit(Opcode::STORE_LOCAL, slot);
                        current_->emit(Opcode::POP);
                    }
                    current_->emit(Opcode::PUSH_UNDEFINED);
                } else {
                    compile_destructuring(decl.id);
                    current_->emit(Opcode::POP);
                    current_->emit(Opcode::PUSH_UNDEFINED);
                }
            }
        } else if (auto *func = std::get_if<FuncDeclStmt>(&stmt)) {
            compile_function(*func);
        } else if (auto *ret = std::get_if<ReturnStmt>(&stmt)) {
            if (ret->argument) {
                compile_expr(*ret->argument);
            } else {
                current_->emit(Opcode::PUSH_UNDEFINED);
            }
            current_->emit(Opcode::RETURN);
        } else if (auto *brk = std::get_if<BreakStmt>(&stmt)) {
            (void)brk;
            if (break_jumps_.empty()) {
                current_->emit(Opcode::NOP);
                return;
            }
            u32 jmp = emit_jump(Opcode::JMP);
            break_jumps_.back().push_back(jmp);
        } else if (auto *thr = std::get_if<ThrowStmt>(&stmt)) {
            compile_expr(*thr->argument);
            current_->emit(Opcode::THROW);
        } else if (auto *ifs = std::get_if<IfStmt>(&stmt)) {
            compile_if(*ifs);
        } else if (auto *whl = std::get_if<WhileStmt>(&stmt)) {
            compile_while(*whl);
        } else if (auto *fr = std::get_if<ForStmt>(&stmt)) {
            compile_for(*fr);
        } else if (auto *trys = std::get_if<TryStmt>(&stmt)) {
            compile_try(*trys);
        } else if (auto *empty = std::get_if<EmptyStmt>(&stmt)) {
            (void)empty;
        }
    }

    void Compiler::compile_try(TryStmt &trys) {
        // Layout (all jumps absolute):
        //   TRY -> catch_start
        //   [try body]  END_TRY  JMP -> after_catch
        //   catch_start: [bind param] [catch body]
        //   after_catch: [finalizer]
        // op_throw pushes the thrown value before jumping to catch_start.
        u32 try_idx = current_->instructions.size();
        current_->emit(Opcode::TRY, (u32)0);

        {
            bool saved = at_top_level_;
            at_top_level_ = false;
            compile_stmt(*trys.block);
            at_top_level_ = saved;
        }
        current_->emit(Opcode::END_TRY);
        u32 skip_catch = emit_jump(Opcode::JMP);

        u32 catch_start = (u32)current_->instructions.size();
        current_->instructions[try_idx].operand = catch_start;

        if (trys.handler) {
            // Bind the exception value (pushed by op_throw) to the parameter.
            // Binding scope follows the enclosing context: locals inside a
            // function, a global property at top level.
            bool bind_global = at_top_level_;
            if (trys.handler->param) {
                if (auto *ip = std::get_if<IdentPattern>(trys.handler->param.get())) {
                    if (bind_global) {
                        current_->emit(Opcode::STORE_GLOBAL, ip->name);
                        current_->emit(Opcode::POP);
                    } else {
                        u32 slot = allocate_local(ip->name);
                        current_->emit(Opcode::STORE_LOCAL, slot);
                        current_->emit(Opcode::POP);
                    }
                } else {
                    // Destructuring catch params are not supported yet: drop.
                    current_->emit(Opcode::POP);
                }
            } else {
                current_->emit(Opcode::POP);
            }
            bool saved = at_top_level_;
            at_top_level_ = false;
            compile_stmt(*trys.handler->body);
            at_top_level_ = saved;
        } else {
            // No handler: behave as empty catch.
            current_->emit(Opcode::POP);
        }
        patch_jump(skip_catch);

        if (trys.finalizer) {
            bool saved = at_top_level_;
            at_top_level_ = false;
            compile_stmt(*trys.finalizer);
            at_top_level_ = saved;
        }
    }

    void Compiler::compile_if(IfStmt &if_stmt) {
        compile_expr(*if_stmt.test);
        u32 else_jump = emit_jump(Opcode::JMP_IF_FALSE);
        {
            bool saved = at_top_level_;
            at_top_level_ = false;
            compile_stmt(*if_stmt.consequent);
            at_top_level_ = saved;
        }
        u32 end_jump = emit_jump(Opcode::JMP);
        patch_jump(else_jump);
        if (if_stmt.alternate) {
            bool saved = at_top_level_;
            at_top_level_ = false;
            compile_stmt(*if_stmt.alternate);
            at_top_level_ = saved;
        }
        patch_jump(end_jump);
    }

    void Compiler::compile_while(WhileStmt &while_stmt) {
        u32 loop_start = (u32)current_->instructions.size();
        compile_expr(*while_stmt.test);
        u32 exit_jump = emit_jump(Opcode::JMP_IF_FALSE);
        continue_targets_.push_back(loop_start);
        break_jumps_.push_back({});
        {
            bool saved = at_top_level_;
            at_top_level_ = false;
            compile_stmt(*while_stmt.body);
            at_top_level_ = saved;
        }
        current_->emit(Opcode::JMP, loop_start);
        patch_jump(exit_jump);
        u32 break_target = (u32)current_->instructions.size();
        for (u32 jmp_idx : break_jumps_.back()) {
            current_->instructions[jmp_idx].operand = (u32)break_target;
        }
        break_jumps_.pop_back();
        continue_targets_.pop_back();
    }

    void Compiler::compile_for(ForStmt &for_stmt) {
        enter_scope();
        if (for_stmt.init_var_decl) {
            Stmt vs(std::move(*for_stmt.init_var_decl));
            compile_stmt(vs);
        } else if (for_stmt.init_expr) {
            compile_expr(*for_stmt.init_expr);
            current_->emit(Opcode::POP);
        }
        u32 loop_test = (u32)current_->instructions.size();
        u32 exit_jump = UINT32_MAX;
        if (for_stmt.test) {
            compile_expr(*for_stmt.test);
            exit_jump = emit_jump(Opcode::JMP_IF_FALSE);
        }
        break_jumps_.push_back({});
        {
            bool saved = at_top_level_;
            at_top_level_ = false;
            compile_stmt(*for_stmt.body);
            at_top_level_ = saved;
        }
        u32 continue_target = (u32)current_->instructions.size();
        continue_targets_.push_back(continue_target);
        if (for_stmt.update) {
            compile_expr(*for_stmt.update);
            current_->emit(Opcode::POP);
        }
        current_->emit(Opcode::JMP, loop_test);
        if (exit_jump != UINT32_MAX) {
            patch_jump(exit_jump);
        }
        u32 break_target = (u32)current_->instructions.size();
        for (u32 jmp_idx : break_jumps_.back()) {
            current_->instructions[jmp_idx].operand = (u32)break_target;
        }
        break_jumps_.pop_back();
        continue_targets_.pop_back();
        exit_scope();
    }

}  // namespace browser::js
