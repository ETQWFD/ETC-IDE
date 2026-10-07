// ============================================================
//  ETC Lang 词法分析器实现
// ============================================================
#include "lexer.h"
#include <unordered_set>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace etc {

static const std::unordered_set<std::string>& kwSet() {
    static const std::unordered_set<std::string> s = {
        "fn","let","var","const","own","move","share","ptr","ref","weak",
        "if","else","elif","loop","while","for","break","continue","return",
        "class","struct","enum","union","trait","impl","new","del",
        "true","false","null","nil","try","catch","throw","finally",
        "as","is","in","then","where","match","select",
        "alloc","free","load","store","cast","sizeof","alignof","typeof",
        "pub","priv","prot","static","inline","extern","virtual","override",
        "import","export","from","module","package","use",
        "unsafe","pure","volatile","atomic","align","packed",
        "thread","spawn","join","mutex","lock","channel","send","recv",
        "vec2","vec4","vec8","vec16",
        "compile_time","macro","type_info"
    };
    return s;
}

static const std::unordered_set<std::string>& annSet() {
    static const std::unordered_set<std::string> s = {
        "grant","protect","authority","require",
        "kernel","interrupt","naked","asm","register","volatile",
        "atomic","barrier","dma","mmio","port_io","audit","align","packed"
    };
    return s;
}

bool isKeyword(const std::string& s) { return kwSet().count(s) > 0; }
bool isAnnotation(const std::string& s) { return annSet().count(s) > 0; }

Lexer::Lexer(std::string s, std::string f) : src(std::move(s)), file(std::move(f)) {}

void Lexer::advance() {
    if (pos < src.size()) {
        if (src[pos] == '\n') { line++; col = 1; } else { col++; }
        pos++;
    }
}

void Lexer::fail(const std::string& msg) const {
    CompileError e;
    e.code = 1100;
    e.file = file;
    e.line = line;
    e.col = col;
    e.msg = msg;
    throw e;
}

static bool isIdStart(char c) {
    unsigned char u = (unsigned char)c;
    return std::isalpha(u) || c == '_' || c == '$' || u >= 0x80;  // 中文等 UTF-8 多字节
}

static bool isIdChar(char c) {
    unsigned char u = (unsigned char)c;
    return std::isalnum(u) || c == '_' || c == '$' || u >= 0x80;
}

void Lexer::skipWsAndComments() {
    for (;;) {
        while (!eof() && (peek() == ' ' || peek() == '\t' || peek() == '\r' ||
               peek() == '\n' || peek() == '\f' || peek() == '\v')) advance();
        if (pos == 0 && peek() == '#' && peek(1) == '!') {      // shebang
            while (!eof() && peek() != '\n') advance();
            continue;
        }
        if (peek() == '/' && peek(1) == '/') {                  // 行注释
            while (!eof() && peek() != '\n') advance();
            continue;
        }
        if (peek() == '/' && peek(1) == '*') {                  // 块注释（可嵌套）
            advance(); advance();
            int depth = 1;
            while (!eof() && depth > 0) {
                if (peek() == '/' && peek(1) == '*') { advance(); advance(); depth++; }
                else if (peek() == '*' && peek(1) == '/') { advance(); advance(); depth--; }
                else advance();
            }
            continue;
        }
        break;
    }
}

Token Lexer::next() {
    skipWsAndComments();
    Token t;
    t.line = line; t.col = col; t.file = file;
    if (eof()) { t.kind = Tok::Eof; return t; }
    char c = peek();
    if (c == '@') {                                            // 注解
        advance();
        t.kind = Tok::Ann;
        if (!isIdStart(peek())) fail("注解 '@' 后需要名称");
        int sl = line, sc = col;
        std::string name;
        while (!eof() && isIdChar(peek())) { name += peek(); advance(); }
        if (!isAnnotation(name)) fail("未知注解 '@" + name + "'");
        t.text = name;
        t.line = sl; t.col = sc;
        return t;
    }
    if (std::isdigit((unsigned char)c)) return lexNumber();
    if (c == '"') return lexString();
    if (c == '\'') return lexChar();
    if (isIdStart(c)) return lexIdent();
    return lexOp();
}

Token Lexer::lexNumber() {
    Token t; t.kind = Tok::Int; t.line = line; t.col = col; t.file = file;
    std::string raw;
    int base = 10;
    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) { base = 16; raw += "0x"; advance(); advance(); }
    else if (peek() == '0' && (peek(1) == 'b' || peek(1) == 'B')) { base = 2; raw += "0b"; advance(); advance(); }
    else if (peek() == '0' && (peek(1) == 'o' || peek(1) == 'O')) { base = 8; raw += "0o"; advance(); advance(); }

    bool isFloat = false;
    std::string digits;
    while (!eof()) {
        char c = peek();
        if (c == '_') { advance(); continue; }                 // 数字分隔符
        if (base == 10 && c == '.') { isFloat = true; digits += c; advance(); continue; }
        if (base == 10 && (c == 'e' || c == 'E')) {
            isFloat = true; digits += c; advance();
            if (peek() == '+' || peek() == '-') { digits += peek(); advance(); }
            continue;
        }
        bool ok = base == 16 ? std::isxdigit((unsigned char)c) :
                  base == 2  ? (c == '0' || c == '1') :
                  base == 8  ? (c >= '0' && c <= '7') :
                               std::isdigit((unsigned char)c);
        if (!ok) break;
        digits += c; advance();
    }
    if (digits.empty()) fail("无效的数字字面量");
    raw += digits;
    if (isFloat) {
        t.kind = Tok::Float;
        t.fval = std::strtod(raw.c_str(), nullptr);
        t.text = raw;
    } else {
        errno = 0;
        t.ival = std::strtoll(raw.c_str(), nullptr, base);
        t.text = raw;
    }
    return t;
}

Token Lexer::lexString() {
    Token t; t.kind = Tok::Str; t.line = line; t.col = col; t.file = file;
    advance(); // 开引号
    std::string out;
    for (;;) {
        if (eof()) fail("字符串未闭合");
        char c = peek();
        if (c == '"') { advance(); break; }
        if (c == '\n') fail("字符串不能跨行");
        if (c == '\\') {
            advance();
            char e = peek();
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case '0': out += '\0'; break;
                case '\\': out += '\\'; break;
                case '"': out += '"'; break;
                case '\'': out += '\''; break;
                case 'x': {
                    advance();
                    int v = 0;
                    for (int i = 0; i < 2; i++) {
                        char h = peek();
                        if (!std::isxdigit((unsigned char)h)) fail("\\x 需要两位十六进制");
                        v = v * 16 + (h <= '9' ? h - '0' : (h | 32) - 'a' + 10);
                        advance();
                    }
                    out += (char)v;
                    continue;
                }
                default: fail("未知转义 '\\" + std::string(1, e) + "'");
            }
            advance();
        } else {
            out += c; advance();
        }
    }
    t.sval = out;
    t.text = out;
    return t;
}

Token Lexer::lexChar() {
    Token t; t.kind = Tok::Char; t.line = line; t.col = col; t.file = file;
    advance(); // 开引号
    std::string out;
    if (peek() == '\\') {
        advance();
        char e = peek();
        if (e == 'n') out += '\n'; else if (e == 't') out += '\t';
        else if (e == 'r') out += '\r'; else if (e == '0') out += '\0';
        else if (e == '\\') out += '\\'; else if (e == '\'') out += '\'';
        else fail("未知转义");
        advance();
    } else if (!eof() && peek() != '\'') {
        out += peek(); advance();
    } else fail("空字符字面量");
    if (peek() != '\'') fail("字符字面量未闭合");
    advance();
    t.sval = out;
    t.text = out;
    return t;
}

Token Lexer::lexIdent() {
    Token t; t.kind = Tok::Ident; t.line = line; t.col = col; t.file = file;
    std::string s;
    while (!eof() && isIdChar(peek())) { s += peek(); advance(); }
    t.text = s;
    if (isKeyword(s)) t.kind = Tok::Kw;
    return t;
}

Token Lexer::lexOp() {
    static const char* multi[] = {
        ">>>","===","!==","<<=",">>=","...","..","->","=>","::","==","!=",
        "<=",">=","<<",">>","&&","||","+=","-=","*=","/=","%=","&=","|=",
        "^=","++","--","??", nullptr
    };
    static const char* single = "+-*/%=!~&|^<>?:.,;()[]{}";
    Token t; t.kind = Tok::Op; t.line = line; t.col = col; t.file = file;
    for (int i = 0; multi[i]; i++) {
        size_t n = strlen(multi[i]);
        bool match = true;
        for (size_t j = 0; j < n; j++)
            if (peek(j) != multi[i][j]) { match = false; break; }
        if (match) {
            t.text = multi[i];
            for (size_t j = 0; j < n; j++) advance();
            return t;
        }
    }
    char c = peek();
    if (strchr(single, c)) { t.text = std::string(1, c); advance(); return t; }
    fail("无法识别的字符 '" + std::string(1, c) + "'");
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> out;
    for (;;) {
        Token t = next();
        out.push_back(t);
        if (t.kind == Tok::Eof) break;
    }
    return out;
}

}
