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
        std::unique_ptr<BytecodeFunction> current_;
        u32 next_local_slot_ = 0;
        bool at_top_level_ = true;
        std::vector<std::unordered_map<std::string, u32>> scope_stack_;
        std::vector<std::vector<u32>> break_jumps_;
        std::vector<u32> continue_targets_;

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
        u32 resolve_local(const std::string &name);
        u32 allocate_local(const std::string &name);
        u32 add_constant(const BytecodeFunction::Constant &c);
        u32 emit_jump(Opcode op);
        void patch_jump(u32 index);
        void enter_scope();
        void exit_scope();
        void compile_destructuring(std::unique_ptr<Pattern> &pat);
    };

}  // namespace browser::js
