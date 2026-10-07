// ============================================================
//  ETC Lang 词法分析器 (Lexer)  —  by etc
//  支持 7 种后缀 .ce .ve .xo .ca .io .ru .ec
//  中英文标识符 / 全量关键字 / @ 注解 / 全量运算符
// ============================================================
#pragma once
#include <string>
#include <vector>
#include <stdexcept>

namespace etc {

enum class Tok : int { Ident, Int, Float, Str, Char, Kw, Ann, Op, Eof };

struct Token {
    Tok kind = Tok::Eof;
    std::string text;          // 标识符 / 关键字 / 注解名 / 运算符原文
    long long ival = 0;        // 整数
    double fval = 0;           // 浮点
    std::string sval;          // 字符串/字符 解码后的值
    int line = 1, col = 1;
    std::string file;
};

// 统一编译错误（词法/语法/语义共用）
struct CompileError {
    int code = 0;              // E1100 词法 / E1xxx 语法 / E3xxx 语义
    std::string file, msg;
    int line = 0, col = 0;
};

bool isKeyword(const std::string& s);
bool isAnnotation(const std::string& s);

class Lexer {
public:
    Lexer(std::string src, std::string file);
    Token next();
    std::vector<Token> tokenize();
private:
    std::string src, file;
    size_t pos = 0;
    int line = 1, col = 1;
    char peek(size_t off = 0) const { return pos + off < src.size() ? src[pos + off] : '\0'; }
    void advance();
    bool eof() const { return pos >= src.size(); }
    Token lexNumber();
    Token lexString();
    Token lexChar();
    Token lexIdent();
    Token lexOp();
    void skipWsAndComments();
    [[noreturn]] void fail(const std::string& msg) const;
};

}
