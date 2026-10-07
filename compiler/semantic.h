// ============================================================
//  ETC Lang 语义分析器 (Semantic)
//  类型检查 E1002 / 权限检查 E3001 / 所有权检查 E1003 / 作用域 E1001
// ============================================================
#pragma once
#include "ast.h"
#include <vector>
#include <string>
#include <unordered_map>

namespace etc {

struct Sym {
    std::string type;               // i32 / ptr<i32> / own<Foo> / fn(...)
    bool isConst = false;
    bool isOwn = false;
    bool moved = false;
    bool used = false;
    bool isFn = false;
    bool isType = false;
    std::vector<std::string> params;
    std::string ret;
    std::vector<std::string> caps;  // @require 能力
    int line = 0, col = 0;
    std::string file;
};

struct ClassInfo {
    std::unordered_map<std::string, std::string> fields;
    std::vector<std::string> methods;
};

struct Diag {
    bool isErr = true;
    int code = 0;
    std::string file, msg;
    int line = 0, col = 0;
};

class Semantic {
public:
    Semantic(std::vector<std::string> grantedCaps);
    void check(Node* prog);
    const std::vector<Diag>& diags() const { return diags_; }
    bool ok() const { return hasErr_ == false; }

private:
    std::vector<std::unordered_map<std::string, Sym>> scopes;
    std::unordered_map<std::string, ClassInfo> classes;
    std::vector<std::string> granted;
    std::vector<Diag> diags_;
    bool hasErr_ = false;
    std::vector<std::string> currentGparams;
    Node* currentFn = nullptr;
    std::string thisTypeName_;
    std::vector<std::string> currentFnCaps_;

    void pushScope() { scopes.emplace_back(); }
    void popScope() { scopes.pop_back(); }
    Sym* find(const std::string& name);
    void declare(const std::string& name, Sym s, Node& at);
    void error(int code, Node& at, const std::string& msg);
    void warn(int code, Node& at, const std::string& msg);

    bool isPrimitive(const std::string& t) const;
    bool isNumeric(const std::string& t) const;
    bool isKnownTypeName(const std::string& n) const;
    std::string typeOfType(Node& t);
    std::string typeOf(Node& e);
    void checkExpr(Node& n);
    void checkStmt(Node& n);
    void checkFn(Node& n, bool isMethod);
    void checkClass(Node& n);
    void collectTopDecls(Node& p);
};

}
