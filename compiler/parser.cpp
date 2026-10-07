// ============================================================
//  ETC Lang 递归下降语法分析器实现
//  报错格式：错误 E12xx: file:line:col 消息
// ============================================================
#include "parser.h"
#include <cstdio>

namespace etc {

Parser::Parser(std::vector<Token> t, std::string f) : toks(std::move(t)), file(std::move(f)) {}

const Token& Parser::peekTk(size_t off) const {
    size_t j = i + off;
    if (j >= toks.size()) return toks.back();
    return toks[j];
}

bool Parser::isOp(const char* s) const { return cur().kind == Tok::Op && cur().text == s; }
bool Parser::isKw(const char* s) const { return cur().kind == Tok::Kw && cur().text == s; }
bool Parser::atEof() const { return cur().kind == Tok::Eof; }
void Parser::adv() {
    if (i + 1 < toks.size()) { lastConsumed = toks[i]; i++; }
}

void Parser::fail(int code, const std::string& msg, const Token& t) {
    CompileError e;
    e.code = code; e.file = t.file; e.line = t.line; e.col = t.col;
    e.msg = msg + " [前一个token: '" + lastConsumed.text + "' @" +
           std::to_string(lastConsumed.line) + ":" + std::to_string(lastConsumed.col) + "]";
    throw e;
}

static std::string tokDesc(const Token& t) {
    if (t.kind == Tok::Eof) return "文件结束";
    if (t.kind == Tok::Str) return "字符串";
    if (t.kind == Tok::Char) return "字符";
    if (t.kind == Tok::Int || t.kind == Tok::Float) return "数字";
    return "'" + t.text + "'";
}

void Parser::expectOp(const char* s) {
    if (!isOp(s)) fail(1201, "期望运算符 '" + std::string(s) + "'，实际得到 " + tokDesc(cur()), cur());
    adv();
}

void Parser::expectKw(const char* s) {
    if (!isKw(s)) fail(1201, "期望关键字 '" + std::string(s) + "'，实际得到 " + tokDesc(cur()), cur());
    adv();
}

Token Parser::expectIdent(const char* what) {
    if (cur().kind != Tok::Ident)
        fail(1201, std::string("期望") + what + "，实际得到 " + tokDesc(cur()), cur());
    Token t = cur();
    adv();
    return t;
}

// ---------- 注解 ----------
NodeList Parser::parseAnns() {
    NodeList out;
    while (cur().kind == Tok::Ann) {
        NodePtr a = mk(K::Ann, cur().line, cur().col, cur().file);
        a->text = cur().text;
        adv();
        if (isOp("(")) {                       // @grant(root) 参数
            adv();
            std::string arg;
            int depth = 1;
            while (!atEof() && depth > 0) {
                if (isOp("(")) depth++;
                else if (isOp(")")) { depth--; if (depth == 0) break; }
                if (!arg.empty()) arg += " ";
                arg += cur().text;
                adv();
            }
            expectOp(")");
            NodePtr argn = mk(K::Ident, a->line, a->col, a->file);
            argn->text = arg;
            a->kids.push_back(std::move(argn));
        } else if (isOp("{")) {                 // @protect { ... } 块参数
            adv();
            std::string arg;
            int depth = 1;
            while (!atEof() && depth > 0) {
                if (isOp("{")) depth++;
                else if (isOp("}")) { depth--; if (depth == 0) break; }
                if (!arg.empty()) arg += " ";
                arg += cur().text;
                adv();
            }
            expectOp("}");
            NodePtr argn = mk(K::Ident, a->line, a->col, a->file);
            argn->text = arg;
            a->kids.push_back(std::move(argn));
        }
        out.push_back(std::move(a));
    }
    return out;
}

std::vector<std::string> Parser::parseGenericParams() {
    std::vector<std::string> out;
    if (!isOp("<")) return out;
    adv();
    while (!atEof() && !isOp(">")) {
        Token t = expectIdent("泛型参数名");
        out.push_back(t.text);
        if (isOp(",")) adv();
    }
    expectOp(">");
    return out;
}

// 试探性解析 `<T, U>`：失败则回退（用于调用泛型歧义消解）
std::vector<std::string> Parser::tryParseTypeArgs() {
    size_t save = i;
    std::vector<std::string> out;
    if (!isOp("<")) return out;
    adv();
    bool ok = true;
    int depth = 0;
    for (;;) {
        if (cur().kind != Tok::Ident && cur().kind != Tok::Kw) { ok = false; break; }
        out.push_back(cur().text);
        adv();
        if (isOp("<")) { depth++; adv(); continue; }
        if (isOp(">")) { if (depth == 0) { adv(); break; } depth--; adv(); continue; }
        if (isOp(",")) { adv(); continue; }
        ok = false; break;
    }
    if (!ok) { i = save; out.clear(); }
    return out;
}

// ---------- 顶层声明 ----------
NodePtr Parser::parseProgram() {
    NodePtr prog = mk(K::Program, 1, 1, file);
    while (!atEof()) {
        NodeList anns = parseAnns();
        if (cur().kind == Tok::Kw && (cur().text == "import" || cur().text == "export" ||
             cur().text == "from" || cur().text == "module" || cur().text == "package" ||
             cur().text == "use")) {
            prog->kids.push_back(parseModuleStmt());
            continue;
        }
        if (anns.empty() && !isDeclStart()) {
            fail(1202, "意外的顶层记号 " + tokDesc(cur()), cur());
        }
        if (!anns.empty() && !isDeclStart()) {       // 文件级注解（如 @grant / @protect）
            for (auto& a : anns) prog->anns.push_back(std::move(a));
            continue;
        }
        prog->kids.push_back(parseDecl(std::move(anns)));
    }
    return prog;
}

bool isModifier(const std::string& s) {
    static const std::vector<std::string> m = {"pub","priv","prot","static","inline","extern",
        "virtual","override","pure","atomic","volatile","packed","align"};
    for (auto& x : m) if (x == s) return true;
    return false;
}

bool Parser::isDeclStart() {
    if (cur().kind != Tok::Kw) return false;
    const std::string& s = cur().text;
    return s == "fn" || s == "class" || s == "struct" || s == "enum" || s == "trait" ||
           s == "impl" || s == "let" || s == "var" || s == "const" || s == "own" ||
           s == "share" || s == "move" || s == "ptr" || s == "ref" || s == "weak" ||
           isModifier(s);
}

NodePtr Parser::parseModuleStmt() {
    NodePtr n = mk(K::Import, cur().line, cur().col, cur().file);
    if (cur().text == "import") { n->kind = K::Import; adv(); }
    else if (cur().text == "export") { n->kind = K::Export; adv(); }
    else if (cur().text == "use") { n->kind = K::Use; adv(); }
    else if (cur().text == "module" || cur().text == "package") {
        n->kind = K::Use; adv();
    } else if (cur().text == "from") {                    // from "path" import a, b;
        adv();
        if (cur().kind == Tok::Str) { n->sval = cur().sval; adv(); }
        else { Token t = expectIdent("模块路径"); n->sval = t.text; }
        expectKw("import");
    }
    while (!atEof() && !isOp(";")) {
        if (cur().kind == Tok::Str) { n->sval = cur().sval; adv(); }
        else if (cur().kind == Tok::Ident) { n->text = cur().text; adv(); }
        else adv();
        if (isOp(",")) adv();
    }
    if (isOp(";")) adv();
    return n;
}

NodePtr Parser::parseDecl(NodeList anns) {
    // 可见性/修饰符
    while (cur().kind == Tok::Kw && isModifier(cur().text)) {
        NodePtr a = mk(K::Ann, cur().line, cur().col, cur().file);
        a->text = cur().text;
        adv();
        anns.push_back(std::move(a));
    }
    const std::string& s = cur().text;
    if (s == "fn") return parseFnDecl(std::move(anns));
    if (s == "class") return parseClassDecl(std::move(anns));
    if (s == "struct") return parseStructDecl(std::move(anns));
    if (s == "enum") return parseEnumDecl(std::move(anns));
    if (s == "trait") return parseTraitDecl(std::move(anns));
    if (s == "impl") return parseImplDecl(std::move(anns));
    if (s == "let" || s == "var" || s == "const" || s == "own" || s == "share" ||
        s == "move" || s == "ptr" || s == "ref" || s == "weak")
        return parseVarDecl(std::move(anns), true);
    fail(1202, "意外的声明 " + tokDesc(cur()), cur());
}

NodePtr Parser::parseFnDecl(NodeList anns, K kind) {
    expectKw("fn");
    Token name = expectIdent("函数名");
    NodePtr n = mk(kind, name.line, name.col, name.file);
    n->text = name.text;
    n->anns = std::move(anns);
    n->gparams = parseGenericParams();
    expectOp("(");
    while (!isOp(")")) {
        NodePtr p = mk(K::Param, cur().line, cur().col, cur().file);
        Token pn = expectIdent("参数名");
        p->text = pn.text;
        if (isOp(":")) {                        // 类型可省略（如 self）
            adv();
            p->ty = parseType();
        }
        if (isOp("=")) { adv(); p->kids.push_back(parseExpr()); }
        n->kids.push_back(std::move(p));
        if (isOp(",")) adv();
    }
    expectOp(")");
    if (isOp("->")) {
        adv();
        n->ty = parseType();
    }
    if (isOp(";")) { adv(); n->kids.push_back(mk(K::EmptyStmt, n->line, n->col, n->file)); return n; }
    n->kids.push_back(parseBlock());
    return n;
}

NodePtr Parser::parseClassDecl(NodeList anns) {
    expectKw("class");
    Token name = expectIdent("类名");
    NodePtr n = mk(K::ClassDecl, name.line, name.col, name.file);
    n->text = name.text;
    n->anns = std::move(anns);
    n->gparams = parseGenericParams();
    if (isKw("extends")) { adv(); n->kids.push_back(parseType()); }
    if (isKw("implements")) {
        adv();
        while (!isOp("{")) {
            n->kids.push_back(parseType());
            if (isOp(",")) adv(); else break;
        }
    }
    expectOp("{");
    while (!isOp("}")) {
        NodePtr m = parseClassMember();
        if (m) n->kids.push_back(std::move(m));
    }
    expectOp("}");
    return n;
}

NodePtr Parser::parseClassMember() {
    NodeList anns = parseAnns();
    while (cur().kind == Tok::Kw && isModifier(cur().text)) {
        NodePtr a = mk(K::Ann, cur().line, cur().col, cur().file);
        a->text = cur().text;
        adv();
        anns.push_back(std::move(a));
    }
    if (isKw("fn")) return parseFnDecl(std::move(anns), K::Method);
    if (isKw("let") || isKw("var") || isKw("const") || isKw("own") || isKw("share") ||
        isKw("move") || isKw("ptr") || isKw("ref") || isKw("weak"))
        return parseVarDecl(std::move(anns), true);
    if (isOp(";")) { adv(); return nullptr; }
    fail(1202, "类成员格式错误: " + tokDesc(cur()), cur());
}

NodePtr Parser::parseStructDecl(NodeList anns) {
    expectKw("struct");
    Token name = expectIdent("结构体名");
    NodePtr n = mk(K::StructDecl, name.line, name.col, name.file);
    n->text = name.text;
    n->anns = std::move(anns);
    n->gparams = parseGenericParams();
    expectOp("{");
    while (!isOp("}")) {
        NodeList fanns = parseAnns();
        NodePtr f = mk(K::Field, cur().line, cur().col, cur().file);
        if (cur().kind == Tok::Kw && (cur().text == "let" || cur().text == "var" ||
             cur().text == "const" || cur().text == "own" || cur().text == "share"))
            { f->isConst = (cur().text == "const"); adv(); }
        Token fn = expectIdent("字段名");
        f->text = fn.text;
        expectOp(":");
        f->ty = parseType();
        if (isOp("=")) { adv(); f->kids.push_back(parseExpr()); }
        f->anns = std::move(fanns);
        n->kids.push_back(std::move(f));
        expectOp(";");
    }
    expectOp("}");
    return n;
}

NodePtr Parser::parseEnumDecl(NodeList anns) {
    expectKw("enum");
    Token name = expectIdent("枚举名");
    NodePtr n = mk(K::EnumDecl, name.line, name.col, name.file);
    n->text = name.text;
    n->anns = std::move(anns);
    expectOp("{");
    while (!isOp("}")) {
        NodePtr c = mk(K::EnumCase, cur().line, cur().col, cur().file);
        Token cn = expectIdent("枚举成员名");
        c->text = cn.text;
        if (isOp("=")) { adv(); c->kids.push_back(parseExpr()); }
        else if (isOp("(")) { adv(); c->kids.push_back(parseType()); expectOp(")"); }
        n->kids.push_back(std::move(c));
        if (isOp(",")) adv();
    }
    expectOp("}");
    return n;
}

NodePtr Parser::parseTraitDecl(NodeList anns) {
    expectKw("trait");
    Token name = expectIdent("trait 名");
    NodePtr n = mk(K::TraitDecl, name.line, name.col, name.file);
    n->text = name.text;
    n->anns = std::move(anns);
    n->gparams = parseGenericParams();
    expectOp("{");
    while (!isOp("}")) {
        NodeList m = parseAnns();
        NodePtr fn = parseFnDecl(std::move(m), K::Method);
        n->kids.push_back(std::move(fn));
    }
    expectOp("}");
    return n;
}

NodePtr Parser::parseImplDecl(NodeList anns) {
    expectKw("impl");
    Token t0 = cur();
    NodePtr subject = parseType();          // 第一个类型
    NodePtr n = mk(K::ImplDecl, t0.line, t0.col, t0.file);
    n->anns = std::move(anns);
    if (isKw("for")) {
        adv();
        n->kids.push_back(std::move(subject));  // kids[0]=trait
        n->ty = parseType();                    // ty=impl 目标类型
    } else {
        n->ty = std::move(subject);             // impl Foo { ... }
    }
    expectOp("{");
    while (!isOp("}")) {
        NodeList m = parseAnns();
        NodePtr fn = parseFnDecl(std::move(m), K::Method);
        n->kids.push_back(std::move(fn));
    }
    expectOp("}");
    return n;
}

NodePtr Parser::parseVarDecl(NodeList anns, bool needSemi) {
    const std::string& mod = cur().text;
    Token t = cur();
    NodePtr n = mk(K::VarDecl, t.line, t.col, t.file);
    n->anns = std::move(anns);
    if (mod == "const") n->isConst = true;
    else if (mod == "own") n->isOwn = true;
    else if (mod == "share") n->isShare = true;
    else if (mod == "move") n->isMove = true;
    else if (mod == "ptr") n->isPtr = true;
    else if (mod == "ref") n->isRef = true;
    else if (mod == "weak") n->isWeak = true;
    adv();
    Token name = expectIdent("变量名");
    n->text = name.text;
    if (isOp(":")) {
        adv();
        n->ty = parseType();
    }
    if (isOp("=")) {
        adv();
        n->kids.push_back(parseExpr());
    }
    if (needSemi) expectOp(";");
    return n;
}

// ---------- 语句 ----------
NodePtr Parser::parseStmt() {
    if (isOp("{")) return parseBlock();
    if (isOp(";")) { NodePtr e = mk(K::EmptyStmt, cur().line, cur().col, cur().file); adv(); return e; }
    if (cur().kind == Tok::Kw) {
        const std::string& s = cur().text;
        if (s == "let" || s == "var" || s == "const" || s == "own" || s == "share" ||
            s == "move" || s == "ptr" || s == "ref" || s == "weak")
            return parseVarDecl({}, true);
        if (s == "if") return parseIf();
        if (s == "while") return parseWhile();
        if (s == "for") return parseFor();
        if (s == "loop") {
            NodePtr n = mk(K::LoopStmt, cur().line, cur().col, cur().file);
            adv(); n->kids.push_back(parseBlock()); return n;
        }
        if (s == "break") {
            NodePtr n = mk(K::BreakStmt, cur().line, cur().col, cur().file);
            adv(); expectOp(";"); return n;
        }
        if (s == "continue") {
            NodePtr n = mk(K::ContinueStmt, cur().line, cur().col, cur().file);
            adv(); expectOp(";"); return n;
        }
        if (s == "return") {
            NodePtr n = mk(K::ReturnStmt, cur().line, cur().col, cur().file);
            adv();
            if (!isOp(";")) n->kids.push_back(parseExpr());
            expectOp(";");
            return n;
        }
        if (s == "match" || s == "select") return parseMatch();
        if (s == "try") return parseTry();
        if (s == "throw") {
            NodePtr n = mk(K::ThrowStmt, cur().line, cur().col, cur().file);
            adv(); n->kids.push_back(parseExpr()); expectOp(";"); return n;
        }
        if (s == "unsafe") {
            NodePtr n = mk(K::UnsafeBlock, cur().line, cur().col, cur().file);
            adv(); n->kids.push_back(parseBlock()); return n;
        }
        if (s == "asm") return parseAsm();
    }
    NodePtr e = mk(K::ExprStmt, cur().line, cur().col, cur().file);
    e->kids.push_back(parseExpr());
    expectOp(";");
    return e;
}

NodePtr Parser::parseBlock() {
    expectOp("{");
    NodePtr b = mk(K::Block, cur().line, cur().col, cur().file);
    while (!isOp("}")) b->kids.push_back(parseStmt());
    expectOp("}");
    return b;
}

NodePtr Parser::parseIf() {
    NodePtr n = mk(K::IfStmt, cur().line, cur().col, cur().file);
    expectKw("if");
    n->kids.push_back(parseExpr());
    n->kids.push_back(parseBlock());
    while (isKw("elif")) {
        adv();
        n->kids.push_back(parseExpr());
        n->kids.push_back(parseBlock());
    }
    if (isKw("else")) { adv(); n->kids.push_back(parseBlock()); }
    return n;
}

NodePtr Parser::parseWhile() {
    NodePtr n = mk(K::WhileStmt, cur().line, cur().col, cur().file);
    expectKw("while");
    n->kids.push_back(parseExpr());
    n->kids.push_back(parseBlock());
    return n;
}

NodePtr Parser::parseFor() {
    NodePtr n = mk(K::ForStmt, cur().line, cur().col, cur().file);
    expectKw("for");
    if (isOp("(")) {                      // for (init; cond; step)
        adv();
        if (cur().kind == Tok::Kw && (cur().text == "let" || cur().text == "var" ||
             cur().text == "const" || cur().text == "own" || cur().text == "share" ||
             cur().text == "move"))
            n->kids.push_back(parseVarDecl({}, false));
        else if (!isOp(";")) n->kids.push_back(parseExpr());
        else n->kids.push_back(nullptr);
        expectOp(";");
        if (!isOp(";")) n->kids.push_back(parseExpr());
        else n->kids.push_back(nullptr);
        expectOp(";");
        if (!isOp(")")) n->kids.push_back(parseExpr());
        else n->kids.push_back(nullptr);
        expectOp(")");
    } else {                              // for x : iterable  /  for x in iterable
        Token v = expectIdent("循环变量名");
        n->text = v.text;
        if (isOp(":")) adv();
        else if (isKw("in")) adv();
        else fail(1201, "期望 ':' 或 'in'，实际得到 " + tokDesc(cur()), cur());
        n->kids.push_back(parseExpr());
        n->kids.push_back(nullptr);
        n->kids.push_back(nullptr);
    }
    n->kids.push_back(parseBlock());
    return n;
}

NodePtr Parser::parseMatch() {
    NodePtr n = mk(K::MatchStmt, cur().line, cur().col, cur().file);
    expectKw(cur().text == "select" ? "select" : "match");
    n->kids.push_back(parseExpr());
    expectOp("{");
    while (!isOp("}")) {
        NodePtr c = mk(K::MatchCase, cur().line, cur().col, cur().file);
        if (isOp("_")) { c->text = "_"; adv(); }
        else c->kids.push_back(parseExpr());
        expectOp("=>");
        if (isOp("{")) c->kids.push_back(parseBlock());
        else {
            NodePtr b = mk(K::Block, cur().line, cur().col, cur().file);
            NodePtr st = mk(K::ExprStmt, cur().line, cur().col, cur().file);
            st->kids.push_back(parseExpr());
            if (isOp(";")) adv();
            b->kids.push_back(std::move(st));
            c->kids.push_back(std::move(b));
        }
        n->kids.push_back(std::move(c));
        if (isOp(",")) adv();               // 分支间允许尾逗号
    }
    expectOp("}");
    return n;
}

NodePtr Parser::parseTry() {
    NodePtr n = mk(K::TryStmt, cur().line, cur().col, cur().file);
    expectKw("try");
    n->kids.push_back(parseBlock());
    while (isKw("catch")) {
        adv();
        NodePtr c = mk(K::MatchCase, cur().line, cur().col, cur().file);
        expectOp("(");
        Token v = expectIdent("异常变量名");
        c->text = v.text;
        expectOp(":");
        c->ty = parseType();
        expectOp(")");
        c->kids.push_back(parseBlock());
        n->kids.push_back(std::move(c));
    }
    if (isKw("finally")) {
        adv();
        expectOp("{");
        NodePtr f = mk(K::Block, cur().line, cur().col, cur().file);
        f->text = "finally";
        while (!isOp("}")) f->kids.push_back(parseStmt());
        expectOp("}");
        n->kids.push_back(std::move(f));
    }
    return n;
}

NodePtr Parser::parseAsm() {
    NodePtr n = mk(K::AsmBlock, cur().line, cur().col, cur().file);
    expectKw("asm");
    expectOp("{");
    std::string line;
    int depth = 1;
    while (!atEof() && depth > 0) {
        if (isOp("{")) depth++;
        else if (isOp("}")) { depth--; if (depth == 0) break; }
        if (!line.empty()) line += " ";
        line += cur().text;
        adv();
    }
    expectOp("}");
    NodePtr l = mk(K::AsmLine, n->line, n->col, n->file);
    l->text = line;
    n->kids.push_back(std::move(l));
    return n;
}

// ---------- 类型 ----------
NodePtr Parser::parseType() {
    if (cur().kind != Tok::Ident && !(cur().kind == Tok::Kw &&
        (cur().text == "ptr" || cur().text == "ref" || cur().text == "own" ||
         cur().text == "share" || cur().text == "weak" || cur().text == "vec2" ||
         cur().text == "vec4" || cur().text == "vec8" || cur().text == "vec16" ||
         cur().text == "unsafe")))
        fail(1205, "期望类型名，实际得到 " + tokDesc(cur()), cur());
    NodePtr t = mk(K::TypeNode, cur().line, cur().col, cur().file);
    t->text = cur().text;
    adv();
    if (isOp("<")) {
        adv();
        while (!isOp(">")) {
            t->kids.push_back(parseType());
            if (isOp(",")) adv();
        }
        expectOp(">");
    }
    return t;
}

// ---------- 表达式（优先级从低到高） ----------
NodePtr Parser::parseExpr() {
    NodePtr left = parseThen();
    if (cur().kind == Tok::Op) {
        static const std::vector<std::string> assigns = {
            "=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>="
        };
        for (auto& a : assigns) {
            if (isOp(a.c_str())) {
                NodePtr n = mk(K::Assign, cur().line, cur().col, cur().file);
                n->text = a;
                adv();
                n->kids.push_back(std::move(left));
                n->kids.push_back(parseExpr());
                return n;
            }
        }
    }
    return left;
}

NodePtr Parser::parseThen() {
    NodePtr left = parseNullish();
    while (isKw("then")) {
        NodePtr n = mk(K::BinOp, cur().line, cur().col, cur().file);
        n->text = "then";
        adv();
        n->kids.push_back(std::move(left));
        n->kids.push_back(parseNullish());
        left = std::move(n);
    }
    return left;
}

NodePtr Parser::parseNullish() {
    NodePtr left = parseOr();
    while (isOp("??")) {
        NodePtr n = mk(K::BinOp, cur().line, cur().col, cur().file);
        n->text = "??";
        adv();
        n->kids.push_back(std::move(left));
        n->kids.push_back(parseOr());
        left = std::move(n);
    }
    return left;
}

NodePtr Parser::parseOr() {
    NodePtr left = parseAnd();
    while (isOp("||")) {
        NodePtr n = mk(K::BinOp, cur().line, cur().col, cur().file);
        n->text = "||";
        adv();
        n->kids.push_back(std::move(left));
        n->kids.push_back(parseAnd());
        left = std::move(n);
    }
    return left;
}

NodePtr Parser::parseAnd() {
    NodePtr left = parseBitor();
    while (isOp("&&")) {
        NodePtr n = mk(K::BinOp, cur().line, cur().col, cur().file);
        n->text = "&&";
        adv();
        n->kids.push_back(std::move(left));
        n->kids.push_back(parseBitor());
        left = std::move(n);
    }
    return left;
}

NodePtr Parser::parseBitor() {
    NodePtr left = parseBitxor();
    while (isOp("|")) {
        NodePtr n = mk(K::BinOp, cur().line, cur().col, cur().file);
        n->text = "|";
        adv();
        n->kids.push_back(std::move(left));
        n->kids.push_back(parseBitxor());
        left = std::move(n);
    }
    return left;
}

NodePtr Parser::parseBitxor() {
    NodePtr left = parseBitand();
    while (isOp("^")) {
        NodePtr n = mk(K::BinOp, cur().line, cur().col, cur().file);
        n->text = "^";
        adv();
        n->kids.push_back(std::move(left));
        n->kids.push_back(parseBitand());
        left = std::move(n);
    }
    return left;
}

NodePtr Parser::parseBitand() {
    NodePtr left = parseEquality();
    while (isOp("&")) {
        NodePtr n = mk(K::BinOp, cur().line, cur().col, cur().file);
        n->text = "&";
        adv();
        n->kids.push_back(std::move(left));
        n->kids.push_back(parseEquality());
        left = std::move(n);
    }
    return left;
}

NodePtr Parser::parseEquality() {
    NodePtr left = parseRel();
    while (isOp("==") || isOp("!=") || isOp("===") || isOp("!==")) {
        NodePtr n = mk(K::BinOp, cur().line, cur().col, cur().file);
        n->text = cur().text;
        adv();
        n->kids.push_back(std::move(left));
        n->kids.push_back(parseRel());
        left = std::move(n);
    }
    return left;
}

NodePtr Parser::parseRel() {
    NodePtr left = parseShift();
    while (isOp("<") || isOp(">") || isOp("<=") || isOp(">=") ||
           isKw("as") || isKw("is") || isKw("in")) {
        NodePtr n = mk(K::BinOp, cur().line, cur().col, cur().file);
        n->text = cur().text;
        adv();
        n->kids.push_back(std::move(left));
        n->kids.push_back(parseShift());
        left = std::move(n);
    }
    return left;
}

NodePtr Parser::parseShift() {
    NodePtr left = parseAdd();
    while (isOp("<<") || isOp(">>") || isOp(">>>")) {
        NodePtr n = mk(K::BinOp, cur().line, cur().col, cur().file);
        n->text = cur().text;
        adv();
        n->kids.push_back(std::move(left));
        n->kids.push_back(parseAdd());
        left = std::move(n);
    }
    return left;
}

NodePtr Parser::parseAdd() {
    NodePtr left = parseMul();
    while (isOp("+") || isOp("-")) {
        NodePtr n = mk(K::BinOp, cur().line, cur().col, cur().file);
        n->text = cur().text;
        adv();
        n->kids.push_back(std::move(left));
        n->kids.push_back(parseMul());
        left = std::move(n);
    }
    return left;
}

NodePtr Parser::parseMul() {
    NodePtr left = parseUnary();
    while (isOp("*") || isOp("/") || isOp("%")) {
        NodePtr n = mk(K::BinOp, cur().line, cur().col, cur().file);
        n->text = cur().text;
        adv();
        n->kids.push_back(std::move(left));
        n->kids.push_back(parseUnary());
        left = std::move(n);
    }
    return left;
}

NodePtr Parser::parseUnary() {
    if (cur().kind == Tok::Op && (cur().text == "!" || cur().text == "~" ||
         cur().text == "-" || cur().text == "+" || cur().text == "*" || cur().text == "&")) {
        NodePtr n = mk(K::UnaryOp, cur().line, cur().col, cur().file);
        n->text = cur().text;
        adv();
        n->kids.push_back(parseUnary());
        return n;
    }
    if (cur().kind == Tok::Kw) {
        const std::string& s = cur().text;
        if (s == "alloc" || s == "free" || s == "load" || s == "store" ||
            s == "new" || s == "del" || s == "cast" || s == "sizeof" ||
            s == "typeof" || s == "move") {
            NodePtr n = mk(s == "alloc" ? K::Alloc : s == "free" ? K::Free :
                           s == "load" ? K::Load : s == "store" ? K::Store :
                           s == "new" ? K::New : s == "del" ? K::Del :
                           s == "cast" ? K::Cast : s == "sizeof" ? K::Sizeof :
                           s == "typeof" ? K::Typeof : K::UnaryOp,
                           cur().line, cur().col, cur().file);
            n->text = s;
            adv();
            if (s == "new") {                 // new 类型(args) / new Type(args)
                if (cur().kind == Tok::Ident ||
                    (cur().kind == Tok::Kw && (cur().text == "vec2" || cur().text == "vec4" ||
                     cur().text == "vec8" || cur().text == "vec16")))
                    n->ty = parseType();
                expectOp("(");
                while (!isOp(")")) {
                    n->kids.push_back(parseExpr());
                    if (isOp(",")) adv();
                }
                expectOp(")");
                return n;
            }
            if (isOp("<")) {                 // alloc<T> / cast<T>
                adv();
                n->ty = parseType();
                expectOp(">");
            }
            expectOp("(");
            if (s == "store") {              // store(p, v)
                n->kids.push_back(parseExpr());
                expectOp(",");
                n->kids.push_back(parseExpr());
            } else if (s == "sizeof") {      // sizeof(T) 或 sizeof(expr)
                if (cur().kind == Tok::Ident && peekTk().kind == Tok::Op &&
                    (peekTk().text == ")" || peekTk().text == "<"))
                    n->ty = parseType();
                else if (!isOp(")"))
                    n->kids.push_back(parseExpr());
            } else if (!isOp(")")) {
                n->kids.push_back(parseExpr());
            }
            expectOp(")");
            return n;
        }
    }
    return parsePostfix();
}

NodePtr Parser::parsePostfix() {
    NodePtr e = parsePrimary();
    for (;;) {
        std::vector<std::string> gargs = tryParseTypeArgs();   // foo<i32>(x)
        if (isOp("(")) {
            e = parseCallArgs(std::move(e), gargs);
        } else if (!gargs.empty()) {
            fail(1201, "泛型参数后必须紧跟函数调用", cur());
        } else if (isOp(".")) {
            NodePtr n = mk(K::Member, cur().line, cur().col, cur().file);
            adv();
            Token m = expectIdent("成员名");
            n->text = m.text;
            n->kids.push_back(std::move(e));
            e = std::move(n);
        } else if (isOp("::")) {
            NodePtr n = mk(K::Member, cur().line, cur().col, cur().file);
            n->text = "::";
            adv();
            Token m = expectIdent("成员名");
            n->sval = m.text;
            n->kids.push_back(std::move(e));
            e = std::move(n);
        } else if (isOp("[")) {
            NodePtr n = mk(K::Index, cur().line, cur().col, cur().file);
            adv();
            n->kids.push_back(std::move(e));
            n->kids.push_back(parseExpr());
            expectOp("]");
            e = std::move(n);
        } else {
            break;
        }
    }
    return e;
}

NodePtr Parser::parseCallArgs(NodePtr callee, std::vector<std::string> gargs) {
    NodePtr n = mk(K::Call, callee->line, callee->col, callee->file);
    n->gparams = std::move(gargs);
    expectOp("(");
    n->kids.push_back(std::move(callee));
    while (!isOp(")")) {
        n->kids.push_back(parseExpr());
        if (isOp(",")) adv();
    }
    expectOp(")");
    return n;
}

NodePtr Parser::parsePrimary() {
    const Token& t = cur();
    if (t.kind == Tok::Int) {
        NodePtr n = mk(K::IntLit, t.line, t.col, t.file);
        n->ival = t.ival; n->text = t.text;
        adv(); return n;
    }
    if (t.kind == Tok::Float) {
        NodePtr n = mk(K::FloatLit, t.line, t.col, t.file);
        n->fval = t.fval; n->text = t.text;
        adv(); return n;
    }
    if (t.kind == Tok::Str) {
        NodePtr n = mk(K::StrLit, t.line, t.col, t.file);
        n->sval = t.sval; n->text = t.text;
        adv(); return n;
    }
    if (t.kind == Tok::Char) {
        NodePtr n = mk(K::CharLit, t.line, t.col, t.file);
        n->sval = t.sval; n->text = t.text;
        adv(); return n;
    }
    if (t.kind == Tok::Kw) {
        if (t.text == "true" || t.text == "false") {
            NodePtr n = mk(K::BoolLit, t.line, t.col, t.file);
            n->bval = (t.text == "true");
            adv(); return n;
        }
        if (t.text == "null" || t.text == "nil") {
            NodePtr n = mk(K::NullLit, t.line, t.col, t.file);
            adv(); return n;
        }
        if (t.text == "this" || t.text == "self") {
            NodePtr n = mk(K::This, t.line, t.col, t.file);
            adv(); return n;
        }
    }
    if (t.kind == Tok::Ident) {
        if (t.text == "this" || t.text == "self") {
            NodePtr n = mk(K::This, t.line, t.col, t.file);
            n->text = t.text;
            adv(); return n;
        }
        NodePtr n = mk(K::Ident, t.line, t.col, t.file);
        n->text = t.text;
        adv(); return n;
    }
    if (isOp("(")) {
        adv();
        NodePtr e = parseExpr();
        expectOp(")");
        return e;
    }
    if (isOp("[")) {
        NodePtr n = mk(K::ArrayLit, t.line, t.col, t.file);
        adv();
        while (!isOp("]")) {
            n->kids.push_back(parseExpr());
            if (isOp(",")) adv();
        }
        expectOp("]");
        return n;
    }
    fail(1203, "无法解析的表达式: " + tokDesc(t), t);
}

}
