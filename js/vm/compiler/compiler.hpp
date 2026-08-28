#pragma once
#include "../../ast.hpp"
#include "../../bytecode.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace browser::js {

    class Compiler {
    public:
        Compiler();
        std::unique_ptr<BytecodeFunction> compile(Program &program);

    private:
        struct FuncContext {
            std::vector<std::unordered_map<std::string, u32>> scopes;
            u32 next_local = 0;
            // J-C5: closure mapping for this function.
            std::unordered_map<std::string, u32> capture_indices;
            std::vector<BytecodeFunction::Capture> captures;
        };
        std::unique_ptr<BytecodeFunction> current_;
        u32 next_local_slot_ = 0;
        bool at_top_level_ = true;
        std::vector<std::unordered_map<std::string, u32>> scope_stack_;
        // J-C5: stack of outer function contexts while compiling a nested function.
        std::vector<FuncContext> func_stack_;
        std::unordered_map<std::string, u32> current_captures_;
        std::vector<BytecodeFunction::Capture> current_captures_list_;
        std::vector<std::vector<u32>> break_jumps_;
        // J-C6: pending continue jumps per loop; patched to the loop's update
        // (for) or restart (while) position once the body is compiled.
        std::vector<std::vector<u32>> continue_jumps_;
        std::vector<u32> continue_targets_;
        // J-C4: finalizer depth captured when each loop opens, so break /
        // continue only unwind fins enclosed by that loop.
        std::vector<size_t> break_fin_depth_;
        std::vector<size_t> continue_fin_depth_;

        // Enclosing try/finally blocks; return/break/continue tunnel through
        // their bodies (re-emitted inline) before completing.
        struct ActiveFinalizer {
            Stmt *stmt;
            u32 value_slot;  // temp local carrying a return value across the fin
        };
        std::vector<ActiveFinalizer> active_finalizers_;

        void compile_stmt(Stmt &stmt);
        void compile_expr(Expr &expr);
        void compile_literal(LiteralExpr &lit);
        void compile_ident(IdentExpr &id);
        void compile_binary(BinaryExpr &bin);
        void compile_unary(UnaryExpr &un);
        void compile_call(CallExpr &call);
        void compile_member(MemberExpr &mem);
        void compile_assign(AssignExpr &assign);
        void compile_array(ArrLiteralExpr &arr);
        void compile_object(ObjLiteralExpr &obj);
        void compile_arrow(ArrowFuncExpr &arrow);
        void compile_template(TemplateExpr &templ);
        void compile_function(FuncDeclStmt &func);
        void compile_inc_dec(UnaryExpr &un);
        void compile_if(IfStmt &if_stmt);
        void compile_while(WhileStmt &while_stmt);
        void compile_for(ForStmt &for_stmt);
        void compile_try(TryStmt &trys);
        void compile_conditional(ConditionalExpr &cond);
        u32 resolve_local(const std::string &name);
        u32 allocate_local(const std::string &name);
        // J-C5: closure helpers.
        bool is_captured(const std::string &name) const;
        u32 resolve_capture(const std::string &name);
        int find_def_depth(const std::string &name) const;
        u32 find_local_slot_at_depth(int depth, const std::string &name) const;
        void ensure_captured_through(int def_depth, const std::string &name);
        u32 add_capture(const std::string &name, bool from_closure, u32 idx);
        u32 add_constant(const BytecodeFunction::Constant &c);
        u32 emit_jump(Opcode op);
        void patch_jump(u32 index);
        void enter_scope();
        void exit_scope();
        void compile_destructuring(std::unique_ptr<Pattern> &pat);
    };

}  // namespace browser::js
