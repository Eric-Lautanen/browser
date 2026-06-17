#include "compiler.hpp"

#include <cstddef>

namespace browser::js {

    void Compiler::compile_expr(Expr &expr) {
        if (auto *lit = std::get_if<LiteralExpr>(&expr)) {
            compile_literal(*lit);
        } else if (auto *id = std::get_if<IdentExpr>(&expr)) {
            compile_ident(*id);
        } else if (auto *bin = std::get_if<BinaryExpr>(&expr)) {
            compile_binary(*bin);
        } else if (auto *un = std::get_if<UnaryExpr>(&expr)) {
            compile_unary(*un);
        } else if (auto *call = std::get_if<CallExpr>(&expr)) {
            compile_call(*call);
        } else if (auto *mem = std::get_if<MemberExpr>(&expr)) {
            compile_member(*mem);
        } else if (auto *assign = std::get_if<AssignExpr>(&expr)) {
            compile_assign(*assign);
        } else if (auto *arr = std::get_if<ArrLiteralExpr>(&expr)) {
            compile_array(*arr);
        } else if (auto *obj = std::get_if<ObjLiteralExpr>(&expr)) {
            compile_object(*obj);
        } else if (auto *arrow = std::get_if<ArrowFuncExpr>(&expr)) {
            compile_arrow(*arrow);
        } else if (auto *tpl = std::get_if<TemplateExpr>(&expr)) {
            compile_template(*tpl);
        }
    }

    void Compiler::compile_literal(LiteralExpr &lit) {
        switch (lit.type) {
            case LiteralExpr::Type::NUMBER: {
                BytecodeFunction::Constant nc;
                nc.type = BytecodeFunction::Constant::Type::NUMBER;
                nc.number = lit.number;
                u32 idx = add_constant(nc);
                current_->emit(Opcode::PUSH_NUMBER, idx);
                break;
            }
            case LiteralExpr::Type::STRING: {
                u32 idx = add_constant({BytecodeFunction::Constant::Type::STRING, 0, lit.string_val});
                current_->emit(Opcode::PUSH_STRING, idx);
                break;
            }
            case LiteralExpr::Type::BOOLEAN: {
                u32 idx = add_constant({BytecodeFunction::Constant::Type::BOOL, 0, "", lit.bool_val});
                current_->emit(Opcode::PUSH_BOOL, idx);
                break;
            }
            case LiteralExpr::Type::NULL_VAL:
                current_->emit(Opcode::PUSH_NULL);
                break;
            case LiteralExpr::Type::UNDEFINED:
                current_->emit(Opcode::PUSH_UNDEFINED);
                break;
            case LiteralExpr::Type::BIGINT: {
                u32 idx = add_constant({BytecodeFunction::Constant::Type::STRING, 0, lit.string_val});
                current_->emit(Opcode::PUSH_STRING, idx);
                break;
            }
            case LiteralExpr::Type::REGEXP: {
                u32 idx = add_constant({BytecodeFunction::Constant::Type::STRING, 0, lit.string_val});
                current_->emit(Opcode::PUSH_STRING, idx);
                break;
            }
        }
    }

    void Compiler::compile_ident(IdentExpr &id) {
        if (id.name == "this") {
            current_->emit(Opcode::PUSH_THIS);
            return;
        }
        u32 slot = resolve_local(id.name);
        if (slot != UINT32_MAX) {
            current_->emit(Opcode::LOAD_LOCAL, slot);
        } else {
            current_->emit(Opcode::LOAD_GLOBAL, id.name);
        }
    }

    void Compiler::compile_binary(BinaryExpr &bin) {
        if (bin.op == TokenType::NULLISH_COALESCING) {
            compile_expr(*bin.left);
            current_->emit(Opcode::DUP);
            u32 right_jump = emit_jump(Opcode::JMP_IF_NULLISH);
            u32 end_jump = emit_jump(Opcode::JMP);
            patch_jump(right_jump);
            current_->emit(Opcode::POP);
            compile_expr(*bin.right);
            patch_jump(end_jump);
            return;
        }
        if (bin.op == TokenType::AND_AND) {
            compile_expr(*bin.left);
            current_->emit(Opcode::DUP);
            u32 end_jump = emit_jump(Opcode::JMP_IF_FALSE);
            current_->emit(Opcode::POP);
            compile_expr(*bin.right);
            patch_jump(end_jump);
            return;
        }
        if (bin.op == TokenType::OR_OR) {
            compile_expr(*bin.left);
            current_->emit(Opcode::DUP);
            u32 end_jump = emit_jump(Opcode::JMP_IF_TRUE);
            current_->emit(Opcode::POP);
            compile_expr(*bin.right);
            patch_jump(end_jump);
            return;
        }
        compile_expr(*bin.left);
        compile_expr(*bin.right);
        switch (bin.op) {
            case TokenType::PLUS:
                current_->emit(Opcode::ADD);
                break;
            case TokenType::MINUS:
                current_->emit(Opcode::SUB);
                break;
            case TokenType::STAR:
                current_->emit(Opcode::MUL);
                break;
            case TokenType::SLASH:
                current_->emit(Opcode::DIV);
                break;
            case TokenType::PERCENT:
                current_->emit(Opcode::MOD);
                break;
            case TokenType::EQ_EQ:
                current_->emit(Opcode::EQ);
                break;
            case TokenType::NOT_EQ:
                current_->emit(Opcode::NEQ);
                break;
            case TokenType::EQ_EQ_EQ:
                current_->emit(Opcode::STRICT_EQ);
                break;
            case TokenType::NOT_EQ_EQ:
                current_->emit(Opcode::STRICT_NEQ);
                break;
            case TokenType::LT:
                current_->emit(Opcode::LT);
                break;
            case TokenType::GT:
                current_->emit(Opcode::GT);
                break;
            case TokenType::LT_EQ:
                current_->emit(Opcode::LTE);
                break;
            case TokenType::GT_EQ:
                current_->emit(Opcode::GTE);
                break;
            case TokenType::INSTANCEOF:
                current_->emit(Opcode::INSTANCEOF);
                break;
            case TokenType::AMPERSAND:
                current_->emit(Opcode::BITWISE_AND);
                break;
            case TokenType::PIPE:
                current_->emit(Opcode::BITWISE_OR);
                break;
            case TokenType::CARET:
                current_->emit(Opcode::BITWISE_XOR);
                break;
            default:
                break;
        }
    }

    void Compiler::compile_unary(UnaryExpr &un) {
        if (un.op == TokenType::PLUS_PLUS || un.op == TokenType::MINUS_MINUS) {
            compile_inc_dec(un);
            return;
        }
        compile_expr(*un.argument);
        switch (un.op) {
            case TokenType::MINUS:
                current_->emit(Opcode::NEGATE);
                break;
            case TokenType::NOT:
                current_->emit(Opcode::NOT);
                break;
            case TokenType::PLUS:
                current_->emit(Opcode::NOP);
                break;
            case TokenType::IDENTIFIER:
                current_->emit(Opcode::TYPEOF);
                break;
            case TokenType::TILDE:
                current_->emit(Opcode::BITWISE_NOT);
                break;
            default:
                break;
        }
    }

    void Compiler::compile_inc_dec(UnaryExpr &un) {
        auto *id = std::get_if<IdentExpr>(un.argument.get());
        if (!id)
            return;
        u32 slot = resolve_local(id->name);
        bool is_global = (slot == UINT32_MAX);
        BytecodeFunction::Constant one_c;
        one_c.type = BytecodeFunction::Constant::Type::NUMBER;
        one_c.number = 1.0;
        u32 one_idx = add_constant(one_c);

        if (un.prefix) {
            if (is_global) {
                current_->emit(Opcode::LOAD_GLOBAL, id->name);
            } else {
                current_->emit(Opcode::LOAD_LOCAL, slot);
            }
            current_->emit(Opcode::PUSH_NUMBER, one_idx);
            if (un.op == TokenType::PLUS_PLUS)
                current_->emit(Opcode::ADD);
            else
                current_->emit(Opcode::SUB);
            if (is_global) {
                current_->emit(Opcode::STORE_GLOBAL, id->name);
                current_->emit(Opcode::LOAD_GLOBAL, id->name);
            } else {
                current_->emit(Opcode::STORE_LOCAL, slot);
                current_->emit(Opcode::LOAD_LOCAL, slot);
            }
        } else {
            if (is_global) {
                current_->emit(Opcode::LOAD_GLOBAL, id->name);
                current_->emit(Opcode::DUP);
                current_->emit(Opcode::PUSH_NUMBER, one_idx);
                if (un.op == TokenType::PLUS_PLUS)
                    current_->emit(Opcode::ADD);
                else
                    current_->emit(Opcode::SUB);
                current_->emit(Opcode::STORE_GLOBAL, id->name);
                current_->emit(Opcode::POP);
            } else {
                current_->emit(Opcode::LOAD_LOCAL, slot);
                current_->emit(Opcode::DUP);
                current_->emit(Opcode::PUSH_NUMBER, one_idx);
                if (un.op == TokenType::PLUS_PLUS)
                    current_->emit(Opcode::ADD);
                else
                    current_->emit(Opcode::SUB);
                current_->emit(Opcode::STORE_LOCAL, slot);
                current_->emit(Opcode::POP);
            }
        }
    }

    void Compiler::compile_call(CallExpr &call) {
        auto *mem = std::get_if<MemberExpr>(call.callee.get());
        if (mem) {
            compile_expr(*mem->object);
            if (mem->computed) {
                compile_expr(*mem->property);
            }
            for (auto &arg : call.args) {
                compile_expr(*arg);
            }
            if (mem->computed) {
                current_->emit(Opcode::CALL_METHOD, Instruction::CallMethodInfo{"", (u32)call.args.size()});
            } else {
                std::string prop_name = std::get<IdentExpr>(*mem->property).name;
                current_->emit(Opcode::CALL_METHOD, Instruction::CallMethodInfo{prop_name, (u32)call.args.size()});
            }
            return;
        }
        compile_expr(*call.callee);
        for (auto &arg : call.args) {
            compile_expr(*arg);
        }
        if (call.is_new) {
            current_->emit(Opcode::NEW, (u32)call.args.size());
        } else {
            current_->emit(Opcode::CALL, (u32)call.args.size());
        }
    }

    void Compiler::compile_member(MemberExpr &mem) {
        compile_expr(*mem.object);
        if (mem.computed) {
            compile_expr(*mem.property);
            current_->emit(Opcode::GET_PROP_COMPUTED);
        } else {
            std::string prop_name = std::get<IdentExpr>(*mem.property).name;
            current_->emit(Opcode::GET_PROP, prop_name);
        }
    }

    void Compiler::compile_assign(AssignExpr &assign) {
        auto *mem = std::get_if<MemberExpr>(assign.left.get());
        if (mem) {
            compile_expr(*mem->object);
            if (mem->computed) {
                compile_expr(*mem->property);
            }
            compile_expr(*assign.right);
            if (mem->computed) {
                current_->emit(Opcode::SET_PROP_COMPUTED);
            } else {
                current_->emit(Opcode::SET_PROP, std::get<IdentExpr>(*mem->property).name);
            }
            return;
        }
        compile_expr(*assign.right);
        auto *id = std::get_if<IdentExpr>(assign.left.get());
        if (id) {
            u32 slot = resolve_local(id->name);
            if (slot != UINT32_MAX) {
                current_->emit(Opcode::STORE_LOCAL, slot);
            } else {
                current_->emit(Opcode::STORE_GLOBAL, id->name);
            }
        }
    }

    void Compiler::compile_array(ArrLiteralExpr &arr) {
        for (auto &el : arr.elements) {
            if (el) {
                compile_expr(*el);
            } else {
                current_->emit(Opcode::PUSH_UNDEFINED);
            }
        }
        current_->emit(Opcode::NEW_ARRAY, (u32)arr.elements.size());
    }

    void Compiler::compile_object(ObjLiteralExpr &obj) {
        current_->emit(Opcode::NEW_OBJECT);
        for (auto &prop : obj.properties) {
            u32 key_idx = add_constant({BytecodeFunction::Constant::Type::STRING, 0, prop.key});
            current_->emit(Opcode::PUSH_STRING, key_idx);
            compile_expr(*prop.value);
            current_->emit(Opcode::DEFINE_PROP);
        }
    }

    void Compiler::compile_arrow(ArrowFuncExpr &arrow) {
        auto child = std::make_unique<BytecodeFunction>();
        child->name = "";
        child->num_params = (u32)arrow.params.size();

        auto parent = std::move(current_);
        u32 saved_next_local = next_local_slot_;
        auto saved_scope_stack = std::move(scope_stack_);

        current_ = std::move(child);
        next_local_slot_ = 0;
        bool saved_tl = at_top_level_;
        at_top_level_ = false;
        enter_scope();

        for (auto &param : arrow.params) {
            if (auto *ip = std::get_if<IdentPattern>(param.get())) {
                allocate_local(ip->name);
            }
        }

        if (arrow.is_expression_body) {
            compile_expr(*arrow.body_expr);
        } else {
            Stmt block_stub(std::move(*arrow.body_block));
            compile_stmt(block_stub);
            current_->emit(Opcode::PUSH_UNDEFINED);
        }
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
    }

    void Compiler::compile_template(TemplateExpr &templ) {
        if (templ.exprs.empty()) {
            u32 idx = add_constant({BytecodeFunction::Constant::Type::STRING, 0, templ.quasis[0]});
            current_->emit(Opcode::PUSH_STRING, idx);
            return;
        }
        for (size_t i = 0; i < templ.quasis.size(); i++) {
            u32 idx = add_constant({BytecodeFunction::Constant::Type::STRING, 0, templ.quasis[i]});
            current_->emit(Opcode::PUSH_STRING, idx);
            if (i < templ.exprs.size()) {
                compile_expr(*templ.exprs[i]);
            }
        }
        current_->emit(Opcode::TEMPLATE_LITERAL, (u32)templ.quasis.size());
    }

}  // namespace browser::js
