// ============================================================
//  ETC Lang 递归下降语法分析器 (Parser)
// ============================================================
#pragma once
#include "ast.h"
#include "lexer.h"
#include <vector>
#include <string>

namespace etc {

class Parser {
public:
    Parser(std::vector<Token> toks, std::string file);
    NodePtr parseProgram();
    size_t curIndex() const { return i; }   // 调试用

private:
    std::vector<Token> toks;
    std::string file;
    size_t i = 0;
    Token lastConsumed;              // 调试：最近消费的 token

    const Token& cur() const { return toks[i]; }
    const Token& peekTk(size_t off = 1) const;
    bool isOp(const char* s) const;
    bool isKw(const char* s) const;
    bool atEof() const;
    bool isDeclStart();
    void adv();
    [[noreturn]] void fail(int code, const std::string& msg, const Token& t);
    void expectOp(const char* s);
    void expectKw(const char* s);
    Token expectIdent(const char* what);

    NodeList parseAnns();
    std::vector<std::string> parseGenericParams();
    std::vector<std::string> tryParseTypeArgs();

    NodePtr parseDecl(NodeList anns);
    NodePtr parseModuleStmt();
    NodePtr parseFnDecl(NodeList anns, K kind = K::FnDecl);
    NodePtr parseClassDecl(NodeList anns);
    NodePtr parseStructDecl(NodeList anns);
    NodePtr parseEnumDecl(NodeList anns);
    NodePtr parseTraitDecl(NodeList anns);
    NodePtr parseImplDecl(NodeList anns);
    NodePtr parseVarDecl(NodeList anns, bool needSemi);
    NodePtr parseClassMember();

    NodePtr parseStmt();
    NodePtr parseBlock();
    NodePtr parseIf();
    NodePtr parseWhile();
    NodePtr parseFor();
    NodePtr parseMatch();
    NodePtr parseTry();
    NodePtr parseAsm();

    NodePtr parseType();
    NodePtr parseExpr();
    NodePtr parseThen();
    NodePtr parseNullish();
    NodePtr parseOr();
    NodePtr parseAnd();
    NodePtr parseBitor();
    NodePtr parseBitxor();
    NodePtr parseBitand();
    NodePtr parseEquality();
    NodePtr parseRel();
    NodePtr parseShift();
    NodePtr parseAdd();
    NodePtr parseMul();
    NodePtr parseUnary();
    NodePtr parsePostfix();
    NodePtr parsePrimary();
    NodePtr parseCallArgs(NodePtr callee, std::vector<std::string> gargs);
};

}
