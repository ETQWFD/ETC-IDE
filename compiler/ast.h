// ============================================================
//  ETC Lang AST（统一节点 + kind 分派，带 visitor 风格遍历）
// ============================================================
#pragma once
#include <string>
#include <vector>
#include <memory>

namespace etc {

struct Node;
using NodePtr = std::unique_ptr<Node>;
using NodeList = std::vector<NodePtr>;

enum class K : int {
    Program,
    Import, Export, Use,
    Ann,                        // @grant(root) 等注解（text=注解名, kids[0]=参数串）
    VarDecl, FnDecl, ClassDecl, StructDecl, EnumDecl, TraitDecl, ImplDecl,
    Param, Field, Method, EnumCase,
    Block, ExprStmt, IfStmt, WhileStmt, ForStmt, LoopStmt, BreakStmt,
    ContinueStmt, ReturnStmt, MatchStmt, MatchCase, TryStmt, ThrowStmt,
    UnsafeBlock, AsmBlock, AsmLine, EmptyStmt,
    TypeNode,
    IntLit, FloatLit, StrLit, CharLit, BoolLit, NullLit,
    Ident, BinOp, UnaryOp, Assign, Call, Member, Index, Cast,
    Alloc, Free, Load, Store, New, Del, This, Sizeof, Typeof,
    Lambda,
    ArrayLit, SelectStmt
};

struct Node {
    K kind = K::Program;
    int line = 1, col = 1;
    std::string file;
    std::string text;           // 标识符/关键字/运算符/类型名
    long long ival = 0;
    double fval = 0;
    std::string sval;           // 字符串值 / asm 行文本
    bool bval = false;
    NodePtr ty;                 // 类型（变量/参数/返回/强转）
    NodeList kids;              // 子节点
    NodeList anns;              // 声明上的注解
    std::vector<std::string> gparams;   // 泛型参数 <T, U>
    bool isConst = false, isOwn = false, isShare = false, isMove = false;
    bool isRef = false, isPtr = false, isWeak = false;
};

inline NodePtr mk(K k, int line, int col, const std::string& file) {
    auto n = std::make_unique<Node>();
    n->kind = k; n->line = line; n->col = col; n->file = file;
    return n;
}

// 构造类型节点：text=类型名，kids=类型实参
inline NodePtr mkType(const std::string& name, int line, int col, const std::string& file) {
    auto n = mk(K::TypeNode, line, col, file);
    n->text = name;
    return n;
}

// Visitor 风格遍历接口
struct Visitor {
    virtual ~Visitor() = default;
    virtual void visit(Node& n) = 0;
};

inline void walk(Node& n, Visitor& v) {
    v.visit(n);
    for (auto& c : n.kids) if (c) walk(*c, v);
    for (auto& c : n.anns) if (c) walk(*c, v);
    if (n.ty) walk(*n.ty, v);
}

}
