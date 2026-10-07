// ============================================================
//  ETC Lang 语义分析器实现
// ============================================================
#include "semantic.h"
#include <algorithm>

namespace etc {

Semantic::Semantic(std::vector<std::string> caps) : granted(std::move(caps)) {}

void Semantic::error(int code, Node& at, const std::string& msg) {
    Diag d;
    d.isErr = true; d.code = code; d.file = at.file; d.line = at.line; d.col = at.col;
    d.msg = msg;
    diags_.push_back(d);
    hasErr_ = true;
}

void Semantic::warn(int code, Node& at, const std::string& msg) {
    Diag d;
    d.isErr = false; d.code = code; d.file = at.file; d.line = at.line; d.col = at.col;
    d.msg = msg;
    diags_.push_back(d);
}

Sym* Semantic::find(const std::string& name) {
    for (size_t s = scopes.size(); s-- > 0;) {
        auto it = scopes[s].find(name);
        if (it != scopes[s].end()) return &it->second;
    }
    return nullptr;
}

void Semantic::declare(const std::string& name, Sym s, Node& at) {
    if (scopes.empty()) pushScope();
    auto& sc = scopes.back();
    if (sc.count(name)) { error(1006, at, "重复声明 '" + name + "'"); return; }
    sc[name] = std::move(s);
}

bool Semantic::isPrimitive(const std::string& t) const {
    static const std::vector<std::string> p = {"i32","u32","i64","u64","f32","f64","str","bool",
        "char","byte","void","i8","u8","i16","u16"};
    for (auto& x : p) if (t == x) return true;
    return false;
}

bool Semantic::isNumeric(const std::string& t) const {
    return t == "i32" || t == "u32" || t == "i64" || t == "u64" || t == "f32" ||
           t == "f64" || t == "i8" || t == "u8" || t == "i16" || t == "u16" || t == "byte";
}

bool Semantic::isKnownTypeName(const std::string& n) const {
    if (isPrimitive(n)) return true;
    if (n == "ptr" || n == "ref" || n == "own" || n == "share" || n == "weak" ||
        n == "vec2" || n == "vec4" || n == "vec8" || n == "vec16" || n == "array" ||
        n == "fn" || n == "Result" || n == "result")
        return true;
    if (classes.count(n)) return true;
    for (auto& g : currentGparams) if (g == n) return true;   // 泛型参数
    return false;
}

std::string Semantic::typeOfType(Node& t) {
    std::string s = t.text;
    for (auto& a : t.kids) s += "<" + typeOfType(*a) + ">";
    return s;
}

// 类型串 → 展开后的内部类型名（去掉 ptr/own 壳时用于元素类型）
static std::string unwrapOnce(const std::string& t) {
    size_t lt = t.find('<'), gt = t.rfind('>');
    if (lt != std::string::npos && gt != std::string::npos && gt > lt)
        return t.substr(lt + 1, gt - lt - 1);
    return t;
}

std::string Semantic::typeOf(Node& e) {
    switch (e.kind) {
        case K::IntLit: return "i32";
        case K::FloatLit: return "f64";
        case K::StrLit: return "str";
        case K::CharLit: return "char";
        case K::BoolLit: return "bool";
        case K::NullLit: return "nil";
        case K::ArrayLit: {
            std::string et = e.kids.empty() ? "i32" : typeOf(*e.kids[0]);
            return "array<" + et + ">";
        }
        case K::Ident: {
            Sym* s = find(e.text);
            if (!s) { error(1001, e, "未定义标识符 '" + e.text + "'"); return "err"; }
            if (s->moved) { error(1004, e, "使用已 move 的变量 '" + e.text + "'"); return "err"; }
            s->used = true;
            return s->type;
        }
        case K::This: {
            Sym* s = find("this");
            if (s) { s->used = true; return s->type; }
            error(1001, e, "'this' 只能在类方法中使用"); return "err";
        }
        case K::Call: {
            if (e.kids.empty()) { error(1001, e, "调用目标缺失"); return "err"; }
            Node& callee = *e.kids[0];
            // 方法调用：obj.method(args)
            if (callee.kind == K::Member) {
                std::string base = typeOf(*callee.kids[0]);
                if (base == "err") return "err";
                std::string b2 = base;
                if (b2.rfind("own<", 0) == 0 || b2.rfind("share<", 0) == 0 ||
                    b2.rfind("ptr<", 0) == 0 || b2.rfind("ref<", 0) == 0 ||
                    b2.rfind("weak<", 0) == 0)
                    b2 = unwrapOnce(b2);
                if (b2 == "str" || b2.rfind("array<", 0) == 0 || b2.rfind("vec", 0) == 0) {
                    for (size_t k = 1; k < e.kids.size(); k++) checkExpr(*e.kids[k]);
                    if (callee.text == "len") return "u64";
                    if (callee.text == "push" || callee.text == "append") return "void";
                    if (callee.text == "pop") return unwrapOnce(b2);
                    error(1001, e, "类型 '" + b2 + "' 没有方法 '" + callee.text + "'");
                    return "err";
                }
                auto it = classes.find(b2);
                if (it == classes.end()) { error(1001, e, "类型 '" + b2 + "' 没有方法 '" + callee.text + "'"); return "err"; }
                bool found = false;
                for (auto& m : it->second.methods) if (m == callee.text) found = true;
                if (!found) { error(1001, e, "类型 '" + b2 + "' 没有方法 '" + callee.text + "'"); return "err"; }
                for (size_t k = 1; k < e.kids.size(); k++) checkExpr(*e.kids[k]);
                return "void";
            }
            std::string fnName;
            if (callee.kind == K::Ident) fnName = callee.text;
            if (fnName.empty()) { error(1001, e, "无法解析调用目标"); return "err"; }
            // 内建函数
            static const std::unordered_map<std::string, std::string> builtins = {
                {"print","void"},{"println","void"},{"printl","void"},
                {"panic","void"},{"assert","void"},{"sleep","void"},
                {"strlen","u64"},{"memset","void"},{"memcpy","void"},
                {"sqrt","f64"},{"abs","f64"},{"min","f64"},{"max","f64"},
                {"clock","f64"},{"read_file","str"},{"write_file","void"},
                {"append","void"},{"len","u64"},{"push","void"},{"pop","i32"},
                {"to_str","str"},
                {"etc_vec4f","vec4<f32>"},{"etc_vec4i","vec4<i32>"},
                {"etc_vec2d","vec2<f64>"},{"etc_vec8i","vec8<i32>"},
                {"etc_vec8f","vec8<f32>"}
            };
            auto bi = builtins.find(fnName);
            if (bi != builtins.end()) {
                for (size_t k = 1; k < e.kids.size(); k++) checkExpr(*e.kids[k]);
                return bi->second;
            }
            Sym* f = find(fnName);
            if (!f || !f->isFn) { error(1001, e, "未定义的函数 '" + fnName + "'"); return "err"; }
            f->used = true;
            // 权限检查：@require 能力必须已 @grant
            for (auto& cap : f->caps) {
                bool ok = false;
                for (auto& g : granted)
                    if (g == cap || g == "root" || g == "system" || g == "*" || g == "all" ||
                        (g.rfind("capability:", 0) == 0 && g.substr(11) == cap)) ok = true;
                if (!ok) error(3001, e, "权限不足，需要 " + cap);
            }
            size_t expect = f->params.size();
            size_t got = e.kids.size() - 1;
            if (expect != got)
                error(1007, e, "函数 '" + fnName + "' 参数数量不匹配: 期望 " +
                      std::to_string(expect) + " 个，得到 " + std::to_string(got) + " 个");
            for (size_t k = 0; k < got && k < expect; k++) {
                std::string at = typeOf(*e.kids[k + 1]);
                if (at != f->params[k] && at != "err" && f->params[k] != "err" &&
                    !f->params[k].empty() && f->params[k] != "T" && f->params[k] != "auto")
                    error(1002, *e.kids[k + 1], "类型不匹配: 期望 " + f->params[k] + "，得到 " + at);
            }
            return f->ret;
        }
        case K::Member: {
            std::string base = e.kids.empty() ? "err" : typeOf(*e.kids[0]);
            if (e.text == "::") return "err";
            if (base == "err") return "err";
            if (base.rfind("own<", 0) == 0 || base.rfind("share<", 0) == 0 ||
                base.rfind("ptr<", 0) == 0 || base.rfind("ref<", 0) == 0)
                base = unwrapOnce(base);
            auto it = classes.find(base);
            if (it == classes.end()) { error(1001, e, "类型 '" + base + "' 没有成员 '" + e.text + "'"); return "err"; }
            auto f = it->second.fields.find(e.text);
            if (f != it->second.fields.end()) return f->second;
            for (auto& m : it->second.methods) if (m == e.text) return "fn";
            error(1001, e, "类型 '" + base + "' 没有成员 '" + e.text + "'");
            return "err";
        }
        case K::Index: {
            if (e.kids.size() < 2) return "err";
            std::string base = typeOf(*e.kids[0]);
            checkExpr(*e.kids[1]);
            if (base == "str") return "char";
            if (base.rfind("array<", 0) == 0) return unwrapOnce(base);
            if (base.rfind("vec", 0) == 0) return unwrapOnce(base);
            if (base.rfind("ptr<", 0) == 0) return unwrapOnce(base);
            error(1002, e, "类型不匹配: 期望可索引类型，得到 " + base);
            return "err";
        }
        case K::BinOp: {
            if (e.kids.size() < 2) return "err";
            std::string l = typeOf(*e.kids[0]);
            std::string r = typeOf(*e.kids[1]);
            const std::string& op = e.text;
            if (op == "as") return l == "err" ? "err" : (e.kids[1]->kind == K::Ident ? e.kids[1]->text : "err");
            if (op == "is" || op == "in") return "bool";
            if (op == "==" || op == "!=" || op == "===" || op == "!==" ||
                op == "<" || op == ">" || op == "<=" || op == ">=") return "bool";
            if (op == "&&" || op == "||" || op == "then" || op == "??") {
                if (op == "&&" || op == "||") {
                    if (l != "bool" && l != "err") error(1002, e, "类型不匹配: && / || 需要 bool，得到 " + l);
                }
                return (op == "&&" || op == "||") ? "bool" : (l == "err" ? r : l);
            }
            if (l == "err" || r == "err") return "err";
            if (op == "+" && l == "str" && r == "str") return "str";
            {
                bool lGen = std::find(currentGparams.begin(), currentGparams.end(), l) != currentGparams.end();
                bool rGen = std::find(currentGparams.begin(), currentGparams.end(), r) != currentGparams.end();
                if (lGen || rGen) return lGen ? l : r;   // 泛型参数：跳过具体数值检查
            }
            if (!isNumeric(l) || !isNumeric(r)) {
                if ((op == "+" || op == "-" || op == "*" || op == "/") &&
                    l.rfind("vec", 0) == 0 && r.rfind("vec", 0) == 0 && l == r) {
                    return l;   // SIMD 向量运算
                }
                error(1002, e, "类型不匹配: 运算符 '" + op + "' 需要数值，得到 " + l + " 和 " + r);
                return "err";
            }
            if (l == "f64" || r == "f64" || l == "f32" || r == "f32")
                return (l == "f64" || r == "f64") ? "f64" : "f32";
            if (l == "i64" || r == "i64") return "i64";
            return "i32";
        }
        case K::UnaryOp: {
            if (e.kids.empty()) return "err";
            std::string t = typeOf(*e.kids[0]);
            if (e.text == "!") return "bool";
            if (e.text == "&") return "ptr<" + t + ">";
            if (e.text == "*") return unwrapOnce(t);
            return t;
        }
        case K::Assign: return e.kids.size() > 1 ? typeOf(*e.kids[0]) : "err";
        case K::Cast: return e.ty ? typeOfType(*e.ty) : "err";
        case K::Alloc: {
            std::string T = e.ty ? typeOfType(*e.ty) : "i32";
            return "ptr<" + T + ">";
        }
        case K::Free: return "void";
        case K::Load: {
            if (e.kids.empty()) return "err";
            std::string t = typeOf(*e.kids[0]);
            return t == "err" ? "err" : unwrapOnce(t);
        }
        case K::Store: return "void";
        case K::New: {
            if (e.ty) return "own<" + typeOfType(*e.ty) + ">";
            if (!e.kids.empty()) {
                Node& c = *e.kids[0];
                if (c.kind == K::Ident) return "own<" + c.text + ">";
            }
            return "own<unknown>";
        }
        case K::Del: return "void";
        case K::Sizeof: return "u64";
        case K::Typeof: return "type_info";
        case K::Lambda: return "fn";
        default: return "void";
    }
}

void Semantic::checkExpr(Node& n) {
    // 校验子树并推断类型（副作用：记录错误 / used 标记）
    switch (n.kind) {
        case K::Assign: {
            std::string lhs = typeOf(*n.kids[0]);
            std::string rhs = typeOf(*n.kids[1]);
            if (n.kids[0]->kind == K::Ident) {
                Sym* s = find(n.kids[0]->text);
                if (s && s->isConst) error(1008, n, "不能给常量 '" + n.kids[0]->text + "' 赋值");
            }
            if (lhs != "err" && rhs != "err" && lhs != rhs && lhs != "auto" && rhs != "auto") {
                if (!(lhs == "f64" && (rhs == "i32" || rhs == "i64"))) {
                    bool ownL = lhs.rfind("own<", 0) == 0;
                    if (ownL && rhs.rfind("own<", 0) == 0) {
                        error(1003, n, "所有权冲突: 不能隐式复制 own 值 '" +
                              (n.kids[0]->kind == K::Ident ? n.kids[0]->text : "?") +
                              "'（请用 move 显式转移）");
                    } else {
                        error(1002, n, "类型不匹配: 期望 " + lhs + "，得到 " + rhs);
                    }
                }
            }
            // own 被 move 时更新标记
            if (n.kids[1]->kind == K::UnaryOp && n.kids[1]->text == "move" &&
                !n.kids[1]->kids.empty() && n.kids[1]->kids[0]->kind == K::Ident) {
                Sym* s = find(n.kids[1]->kids[0]->text);
                if (s) s->moved = true;
            }
            break;
        }
        case K::Call:
        case K::Member:
        case K::Index:
        case K::BinOp:
        case K::UnaryOp:
        case K::Ident:
        case K::This:
            typeOf(n);
            break;
        case K::Alloc: {
            for (auto& k : n.kids) checkExpr(*k);
            break;
        }
        case K::Store: {
            if (n.kids.size() >= 2) { typeOf(*n.kids[0]); typeOf(*n.kids[1]); }
            break;
        }
        default:
            break;
    }
    if (n.ty) for (auto& a : n.ty->kids) typeOfType(*a);
}

void Semantic::checkStmt(Node& n) {
    switch (n.kind) {
        case K::VarDecl: {
            std::string ty = n.ty ? typeOfType(*n.ty) : "auto";
            if (n.isOwn) ty = (ty == "auto") ? "own<auto>" : ty;
            if (!n.kids.empty()) {
                std::string init = typeOf(*n.kids[0]);
                if (n.ty && init != "err" && ty != "auto" && init != ty && ty != "own<auto>") {
                    if (!(ty == "f64" && (init == "i32" || init == "i64")) &&
                        !(ty.rfind("own<", 0) == 0 && init.rfind("own<", 0) == 0))
                        error(1002, n, "类型不匹配: 期望 " + ty + "，得到 " + init);
                }
                if (ty == "auto") ty = init;
            }
            if (n.isOwn && ty == "auto") ty = "own<auto>";
            Sym s;
            s.type = ty;
            s.isConst = n.isConst;
            s.isOwn = n.isOwn || ty.rfind("own<", 0) == 0;
            s.line = n.line; s.col = n.col; s.file = n.file;
            declare(n.text, s, n);
            break;
        }
        case K::Block:
            pushScope();
            for (auto& k : n.kids) checkStmt(*k);
            popScope();
            break;
        case K::IfStmt: {
            if (!n.kids.empty()) {
                std::string c = typeOf(*n.kids[0]);
                if (c != "bool" && c != "err") error(1002, n, "类型不匹配: if 条件需要 bool，得到 " + c);
            }
            for (size_t k = 1; k < n.kids.size(); k++) checkStmt(*n.kids[k]);
            break;
        }
        case K::WhileStmt: {
            std::string c = typeOf(*n.kids[0]);
            if (c != "bool" && c != "err") error(1002, n, "类型不匹配: while 条件需要 bool，得到 " + c);
            for (size_t k = 1; k < n.kids.size(); k++) checkStmt(*n.kids[k]);
            break;
        }
        case K::ForStmt: {
            if (n.kids.size() >= 4) {
                pushScope();
                if (n.kids[0]) {
                    if (n.kids[0]->kind == K::VarDecl) checkStmt(*n.kids[0]);
                    else checkExpr(*n.kids[0]);
                }
                if (n.kids[1]) { std::string c = typeOf(*n.kids[1]); if (c != "bool" && c != "err") error(1002, n, "类型不匹配: for 条件需要 bool"); }
                if (n.kids[2]) checkExpr(*n.kids[2]);
                if (!n.text.empty()) {   // 迭代变量
                    std::string bt = n.kids[0] ? typeOf(*n.kids[0]) : "auto";
                    std::string et = "auto";
                    if (bt.rfind("array<", 0) == 0) et = unwrapOnce(bt);
                    else if (bt.rfind("vec", 0) == 0) et = unwrapOnce(bt);
                    else if (bt == "str") et = "char";
                    Sym s; s.type = et; s.line = n.line; s.col = n.col; s.file = n.file;
                    declare(n.text, s, n);
                }
                checkStmt(*n.kids[3]);
                popScope();
            }
            break;
        }
        case K::LoopStmt:
        case K::UnsafeBlock:
            for (auto& k : n.kids) checkStmt(*k);
            break;
        case K::MatchStmt: {
            if (!n.kids.empty()) typeOf(*n.kids[0]);
            for (size_t k = 1; k < n.kids.size(); k++) {
                Node& c = *n.kids[k];
                if (!c.kids.empty() && c.kids[0]->kind != K::Ident) typeOf(*c.kids[0]);
                if (c.kids.size() > 1) checkStmt(*c.kids[1]);
            }
            break;
        }
        case K::TryStmt: {
            for (auto& k : n.kids) {
                if (k->kind == K::MatchCase) {
                    pushScope();
                    Sym s; s.type = k->ty ? typeOfType(*k->ty) : "err";
                    s.line = k->line; s.col = k->col; s.file = k->file;
                    if (!k->text.empty()) declare(k->text, s, *k);
                    if (!k->kids.empty()) checkStmt(*k->kids[0]);
                    popScope();
                } else if (k->kind == K::Block && k->text == "finally") {
                    checkStmt(*k);
                } else {
                    checkStmt(*k);
                }
            }
            break;
        }
        case K::ThrowStmt:
            if (!n.kids.empty()) typeOf(*n.kids[0]);
            break;
        case K::ReturnStmt: {
            std::string rt = currentFn ? (currentFn->ty ? typeOfType(*currentFn->ty) : "void") : "void";
            if (n.kids.empty()) {
                if (rt != "void" && rt != "err")
                    error(1002, n, "类型不匹配: 函数返回 " + rt + "，但 return 未携带值");
            } else {
                std::string t = typeOf(*n.kids[0]);
                if (rt != "void" && rt != "err" && t != "err" && t != rt)
                    error(1002, n, "类型不匹配: 期望返回 " + rt + "，得到 " + t);
            }
            break;
        }
        case K::ExprStmt:
            if (!n.kids.empty()) checkExpr(*n.kids[0]);
            break;
        case K::AsmBlock:
        case K::BreakStmt:
        case K::ContinueStmt:
        case K::EmptyStmt:
            break;
        case K::AsmLine:
            break;
        default:
            break;
    }
}

void Semantic::checkFn(Node& n, bool isMethod) {
    pushScope();
    currentFn = &n;
    bool inClass = isMethod || !thisTypeName_.empty();
    if (inClass) {
        Sym s; s.type = thisTypeName_.empty() ? n.text : thisTypeName_;
        s.line = n.line; s.col = n.col; s.file = n.file;
        declare("this", s, n);
        declare("self", s, n);
    }
    currentGparams = n.gparams;
    for (auto& p : n.kids) {
        if (p->kind != K::Param) continue;
        if (inClass && (p->text == "self" || p->text == "this")) continue;
        std::string pt = p->ty ? typeOfType(*p->ty)
                       : (p->text == "self" ? (thisTypeName_.empty() ? "auto" : thisTypeName_) : "auto");
        Sym s; s.type = pt; s.line = p->line; s.col = p->col; s.file = p->file;
        declare(p->text, s, *p);
    }
    // @require 能力
    for (auto& a : n.anns) {
        if (a->text == "require" && !a->kids.empty())
            currentFnCaps_.push_back(a->kids[0]->text);
    }
    if (!n.kids.empty()) {
        Node& body = *n.kids.back();
        if (body.kind == K::Block) checkStmt(body);
    }
    currentFn = nullptr;
    currentGparams.clear();
    popScope();
}

void Semantic::checkClass(Node& n) {
    ClassInfo& ci = classes[n.text];
    std::string clsName = n.text;
    thisTypeName_ = clsName;
    // 先收集字段类型（类字段由 parseVarDecl 生成 K::VarDecl）
    for (auto& m : n.kids) {
        if (m->kind == K::Field || m->kind == K::VarDecl) {
            std::string ft = m->ty ? typeOfType(*m->ty) : "auto";
            ci.fields[m->text] = ft;
        }
    }
    // 再检查方法（字段已可见）
    for (auto& m : n.kids) {
        if (m->kind == K::Method) {
            ci.methods.push_back(m->text);
            checkFn(*m, true);
        }
    }
    thisTypeName_.clear();
}

void Semantic::collectTopDecls(Node& p) {
    pushScope();
    // 第一遍：类型声明 + 函数原型登记（含权限能力）
    for (auto& d : p.kids) {
        switch (d->kind) {
            case K::ClassDecl:
            case K::StructDecl:
            case K::EnumDecl:
            case K::TraitDecl: {
                classes[d->text];   // 建立空条目
                Sym s; s.isType = true; s.type = d->text;
                s.line = d->line; s.col = d->col; s.file = d->file;
                declare(d->text, s, *d);
                break;
            }
            case K::FnDecl: {
                Sym s;
                s.isFn = true;
                s.ret = d->ty ? typeOfType(*d->ty) : "void";
                for (auto& p2 : d->kids)
                    if (p2->kind == K::Param)
                        s.params.push_back(p2->ty ? typeOfType(*p2->ty) : "auto");
                for (auto& a : d->anns)
                    if (a->text == "require" && !a->kids.empty())
                        s.caps.push_back(a->kids[0]->text);
                s.line = d->line; s.col = d->col; s.file = d->file;
                declare(d->text, s, *d);
                break;
            }
            case K::ImplDecl: {
                // 把 impl 方法并入类的方法表（简化）
                std::string cls = d->ty ? typeOfType(*d->ty) : "";
                if (classes.count(cls)) {
                    for (auto& m : d->kids)
                        if (m->kind == K::Method) classes[cls].methods.push_back(m->text);
                }
                break;
            }
            default:
                break;
        }
    }
    // 第二遍：函数体检查
    for (auto& d : p.kids) {
        if (d->kind == K::FnDecl) checkFn(*d, false);
        else if (d->kind == K::ClassDecl) checkClass(*d);
        else if (d->kind == K::StructDecl) {
            for (auto& m : d->kids)
                if (m->kind == K::Field) {
                    if (m->ty) typeOfType(*m->ty);
                    classes[d->text].fields[m->text] = m->ty ? typeOfType(*m->ty) : "auto";
                }
        } else if (d->kind == K::VarDecl) {
            checkStmt(*d);
        } else if (d->kind == K::ImplDecl) {
            thisTypeName_ = d->ty ? typeOfType(*d->ty) : "";
            for (auto& m : d->kids)
                if (m->kind == K::Method) checkFn(*m, false);
            thisTypeName_.clear();
        }
    }
    // 第三遍：未使用常量警告
    if (!scopes.empty()) {
        for (auto& kv : scopes[0]) {
            if (kv.second.isConst && !kv.second.used && !kv.second.isFn && !kv.second.isType) {
                Node dummy;
                dummy.kind = K::Ident;
                dummy.file = kv.second.file; dummy.line = kv.second.line; dummy.col = kv.second.col;
                warn(2001, dummy, "常量 '" + kv.first + "' 未使用");
            }
        }
    }
    popScope();
}

void Semantic::check(Node* prog) {
    collectTopDecls(*prog);
}

}
