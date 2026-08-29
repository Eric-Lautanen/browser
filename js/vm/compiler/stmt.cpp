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
            // J-C4: a return inside try/finally runs the finalizer bodies
            // (innermost first) before actually returning. Each level's value
            // slot carries the pending result across the inlined finalizer.
            while (!active_finalizers_.empty()) {
                ActiveFinalizer fin = active_finalizers_.back();
                active_finalizers_.pop_back();
                current_->emit(Opcode::STORE_LOCAL, fin.value_slot);
                current_->emit(Opcode::POP);
                compile_stmt(*fin.stmt);
                current_->emit(Opcode::LOAD_LOCAL, fin.value_slot);
            }
            current_->emit(Opcode::RETURN);
        } else if (auto *brk = std::get_if<BreakStmt>(&stmt)) {
            (void)brk;
            if (break_jumps_.empty()) {
                current_->emit(Opcode::NOP);
                return;
            }
            // J-C4: run finalizers enclosed by the loop being exited.
            size_t keep = break_fin_depth_.back();
            while (active_finalizers_.size() > keep) {
                ActiveFinalizer fin = active_finalizers_.back();
                active_finalizers_.pop_back();
                compile_stmt(*fin.stmt);
            }
            u32 jmp = emit_jump(Opcode::JMP);
            break_jumps_.back().push_back(jmp);
        } else if (auto *cont = std::get_if<ContinueStmt>(&stmt)) {
            // J-C6: continue was previously unparseable and silently fell
            // through the rest of the loop body.
            (void)cont;
            if (continue_jumps_.empty()) {
                current_->emit(Opcode::NOP);
                return;
            }
            size_t keep = continue_fin_depth_.back();
            while (active_finalizers_.size() > keep) {
                ActiveFinalizer fin = active_finalizers_.back();
                active_finalizers_.pop_back();
                compile_stmt(*fin.stmt);
            }
            u32 jmp = emit_jump(Opcode::JMP);
            continue_jumps_.back().push_back(jmp);
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
        // J-C3/J-C4 layout (all jumps absolute):
        //
        //   [A] TRY Lcatch                 ; body handler
        //   [B] <try body>
        //   [C] END_TRY                    ; pops the body handler
        //   [D] JMP Lnormal                ; normal completion skips the rest
        //   [E] Lcatch: <bind param>
        //   [G] TRY Lexc                   ; protects the catch body (fin only)
        //   [H] <catch body>
        //   [I] END_TRY
        //   [J] JMP Lnormal                ; catch completed normally
        //   [K] Lexc: STORE slot; POP      ; stash the exception
        //   [L] <finalizer copy 2>         ; exceptional-path finalizer
        //   [N] LOAD slot; THROW           ; rethrow after the finalizer ran
        //   [M] Lnormal: <finalizer copy 1>; normal-path finalizer
        //
        // Without a finalizer this reduces to the classic try/catch shape.
        const bool has_handler = trys.handler != nullptr;
        const bool has_fin = trys.finalizer != nullptr;

        u32 try_body_idx = current_->instructions.size();
        current_->emit(Opcode::TRY, (u32)0);

        // A return inside the body tunnels through this finalizer and drains
        // the stack itself; restoring by depth keeps bookkeeping consistent.
        const size_t fin_depth_at_entry = active_finalizers_.size();
        if (has_fin)
            active_finalizers_.push_back({trys.finalizer.get(), allocate_local("!fin_ret")});
        {
            bool saved = at_top_level_;
            at_top_level_ = false;
            compile_stmt(*trys.block);
            at_top_level_ = saved;
        }
        active_finalizers_.resize(fin_depth_at_entry);

        current_->emit(Opcode::END_TRY);
        u32 jmp_normal = emit_jump(Opcode::JMP);

        u32 catch_start = (u32)current_->instructions.size();
        current_->instructions[try_body_idx].operand = catch_start;

        if (has_handler) {
            // Bind the exception value (pushed by op_throw) to the parameter.
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
        }

        u32 catch_protect_idx = UINT32_MAX;
        u32 jmp_after_catch = UINT32_MAX;
        if (has_fin && has_handler)
            catch_protect_idx = current_->instructions.size();
        {
            bool saved = at_top_level_;
            at_top_level_ = false;
            if (has_handler) {
                if (has_fin)
                    active_finalizers_.push_back({trys.finalizer.get(), allocate_local("!fin_ret")});
                compile_stmt(*trys.handler->body);
                active_finalizers_.resize(fin_depth_at_entry);
            }
            at_top_level_ = saved;
        }
        if (has_fin && has_handler)
            current_->emit(Opcode::END_TRY);

        if (!has_fin) {
            // The normal-completion jump must be patched past the catch body
            // before returning, or it would restart execution at instruction
            // 0 (crash) for any try whose body completes normally.
            patch_jump(jmp_normal);
            if (!has_handler) {
                // Bare try{} swallows the pending exception value.
                current_->emit(Opcode::POP);
            }
            return;
        }

        // ---- exceptional path ----
        if (has_handler) {
            jmp_after_catch = emit_jump(Opcode::JMP);
            // Exceptions from the catch body enter the stash sequence below.
            current_->instructions[catch_protect_idx].operand = (u32)current_->instructions.size();
        } else {
            // Body exceptions land directly in the stash sequence.
            current_->instructions[try_body_idx].operand = (u32)current_->instructions.size();
        }
        u32 fin_val_slot = allocate_local("!fin_exc");
        current_->emit(Opcode::STORE_LOCAL, fin_val_slot);
        current_->emit(Opcode::POP);
        {
            bool saved = at_top_level_;
            at_top_level_ = false;
            compile_stmt(*trys.finalizer);
            at_top_level_ = saved;
        }
        current_->emit(Opcode::LOAD_LOCAL, fin_val_slot);
        current_->emit(Opcode::THROW);

        // ---- normal path ----
        patch_jump(jmp_normal);
        if (jmp_after_catch != UINT32_MAX)
            patch_jump(jmp_after_catch);
        {
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
        continue_jumps_.push_back({});
        break_jumps_.push_back({});
        continue_fin_depth_.push_back(active_finalizers_.size());
        break_fin_depth_.push_back(active_finalizers_.size());
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
        break_fin_depth_.pop_back();
        // Continue restarts at the test.
        for (u32 jmp_idx : continue_jumps_.back()) {
            current_->instructions[jmp_idx].operand = loop_start;
        }
        continue_jumps_.pop_back();
        continue_fin_depth_.pop_back();
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
        continue_jumps_.push_back({});
        break_fin_depth_.push_back(active_finalizers_.size());
        continue_fin_depth_.push_back(active_finalizers_.size());
        {
            bool saved = at_top_level_;
            at_top_level_ = false;
            compile_stmt(*for_stmt.body);
            at_top_level_ = saved;
        }
        // J-C6 fix: continue jumps land here, running the update expression
        // before re-testing. The jump list must exist while the body is being
        // compiled, so it is registered before the body above.
        u32 continue_target = (u32)current_->instructions.size();
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
        break_fin_depth_.pop_back();
        for (u32 jmp_idx : continue_jumps_.back()) {
            current_->instructions[jmp_idx].operand = continue_target;
        }
        continue_jumps_.pop_back();
        continue_fin_depth_.pop_back();
        exit_scope();
    }

}  // namespace browser::js
