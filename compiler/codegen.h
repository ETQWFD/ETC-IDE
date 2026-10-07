// ============================================================
//  ETC Lang 代码生成器 (CodeGen / ProtectInject / GrantProcessor / MetadataGen)
//  ETC → C++（单翻译单元），并生成 version.rc / 提权 manifest / AndroidManifest.xml
// ============================================================
#pragma once
#include "ast.h"
#include "semantic.h"
#include <string>
#include <vector>
#include <map>

namespace etc {

struct BuildConfig {
    std::string productName = "ETC 程序";
    std::string fileDesc = "ETC Lang 编译产物";
    std::string company = "ET";
    std::string copyright = "Copyright (c) ET";
    std::string fileVersion = "1.0.0.0";
    std::string productVersion = "1.0.0.0";
    std::string icon;               // .ico 路径（Windows）
    std::string appName = "ETC 程序";
    std::string package = "com.etc.app";
    std::string versionName = "1.0";
    std::string versionCode = "1";
    std::string appIcon;
    int minSdk = 21, maxSdk = 35;
    std::string target = "exe";     // exe / apk
    bool compress = true;
    std::string compressLevel = "9";
    bool stripSymbols = true;
    bool useUPX = true;
    bool resourceShrink = true;
    bool obfuscate = true;
    bool debug = false;
};

// 解析 .ec 项目配置（键值 + [windows]/[android] 分区）
bool parseEcFile(const std::string& path, BuildConfig& cfg, std::string& err);

class CodeGen {
public:
    CodeGen(BuildConfig cfg);
    bool generate(Node* prog, std::vector<Diag>& diags);
    std::string cpp() const { return cpp_; }
    void writeVersionRc(const std::string& path, const std::string& exeName) const;
    void writeWinManifest(const std::string& path) const;
    void writeAndroidManifest(const std::string& path) const;
    const std::vector<std::string>& permissionList() const { return perms_; }
    bool wantsElevate() const { return elevate_; }
    bool usesIo() const { return usesIo_; }
    int heartbeatMs() const { return heartbeat_; }

private:
    BuildConfig cfg_;
    std::string cpp_;
    std::vector<std::string> perms_;
    bool usesIo_ = false, protect_ = false, elevate_ = false, needSimd_ = false;
    int heartbeat_ = 0;
    std::vector<std::string> usedVecs_;

    std::string out_;
    int indent_ = 0;
    std::map<std::string, std::vector<Node*>> extraMethods_;   // impl 注入的方法
    std::vector<std::map<std::string, std::string>> varScopes_; // 变量名 → ETC 类型

    void pushVarScope();
    void popVarScope();
    void recordVar(const std::string& name, const std::string& type);
    std::string varTypeOf(const Node* base) const;
    std::string inferType(Node& e);

    void line(const std::string& s);
    std::string gen(Node& n);
    void genStmt(Node& n);
    void genBlock(Node& b);
    void genFn(Node& n, bool method, const std::string& cls);
    void genClass(Node& n, bool isStruct);
    void genEnum(Node& n);
    void genTrait(Node& n);
    void genImpl(Node& n);
    std::string genType(Node& t);
    std::string genTypeStr(const std::string& etcType);
    std::string gens(const std::string& s) const;
    std::string joinVec(const std::vector<std::string>& v, const std::string& sep) const;
    void emitRuntime();
    void emitSimdTypedefs();
    void emitProtect();
    void collectPerms(Node* prog);
};

}
