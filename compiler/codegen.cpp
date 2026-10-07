// ============================================================
//  ETC Lang 代码生成器实现
// ============================================================
#include "codegen.h"
#include <cstdio>
#include <sstream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <cctype>

namespace etc {

// ---------- .ec 项目配置解析 ----------
static std::string trimWs(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, b - a + 1);
}

bool parseEcFile(const std::string& path, BuildConfig& cfg, std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "无法打开 " + path; return false; }
    std::string section = "global";
    std::string line;
    while (std::getline(f, line)) {
        line = trimWs(line);
        if (line.empty() || line[0] == '#' || line[0] == '/') continue;
        if (line.front() == '[' && line.back() == ']') { section = line.substr(1, line.size() - 2); continue; }
        size_t eq = line.find_first_of("=:");
        if (eq == std::string::npos) continue;
        std::string k = trimWs(line.substr(0, eq));
        std::string v = trimWs(line.substr(eq + 1));
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        if (section == "windows" || k == "target") {
            if (k == "产品名称" || k == "product_name") cfg.productName = v;
            else if (k == "文件说明" || k == "file_desc") cfg.fileDesc = v;
            else if (k == "公司名称" || k == "company") cfg.company = v;
            else if (k == "版权信息" || k == "copyright") cfg.copyright = v;
            else if (k == "文件版本" || k == "file_version") cfg.fileVersion = v;
            else if (k == "产品版本" || k == "product_version") cfg.productVersion = v;
            else if (k == "程序图标" || k == "icon") cfg.icon = v;
            else if (k == "压缩打包" || k == "compress") cfg.compress = (v == "true" || v == "1" || v == "是");
            else if (k == "压缩级别" || k == "compress_level") cfg.compressLevel = v;
            else if (k == "去符号表" || k == "strip") cfg.stripSymbols = (v == "true" || v == "1" || v == "是");
            else if (k == "UPX" || k == "upx") cfg.useUPX = (v == "true" || v == "1" || v == "是");
            else if (k == "target" || k == "类型") cfg.target = (v == "apk" || v == "android") ? "apk" : "exe";
        } else if (section == "android") {
            if (k == "软件名称" || k == "app_name") cfg.appName = v;
            else if (k == "包名" || k == "package") cfg.package = v;
            else if (k == "版本号" || k == "version_code") cfg.versionCode = v;
            else if (k == "版本名称" || k == "version_name") cfg.versionName = v;
            else if (k == "应用图标" || k == "app_icon") cfg.appIcon = v;
            else if (k == "最低支持安卓" || k == "min_sdk") cfg.minSdk = atoi(v.c_str());
            else if (k == "最高支持安卓" || k == "max_sdk") cfg.maxSdk = atoi(v.c_str());
            else if (k == "资源压缩" || k == "resource_shrink") cfg.resourceShrink = (v == "true" || v == "1" || v == "是");
            else if (k == "代码混淆" || k == "obfuscate") cfg.obfuscate = (v == "true" || v == "1" || v == "是");
            else if (k == "压缩打包" || k == "compress") cfg.compress = (v == "true" || v == "1" || v == "是");
        } else {
            if (k == "产品名称" || k == "product_name") cfg.productName = v;
            else if (k == "公司名称" || k == "company") cfg.company = v;
            else if (k == "版权信息" || k == "copyright") cfg.copyright = v;
            else if (k == "软件名称" || k == "app_name") cfg.appName = v;
            else if (k == "包名" || k == "package") cfg.package = v;
            else if (k == "版本号" || k == "version_code") cfg.versionCode = v;
            else if (k == "版本名称" || k == "version_name") cfg.versionName = v;
            else if (k == "target" || k == "类型") cfg.target = (v == "apk" || v == "android") ? "apk" : "exe";
            else if (k == "最低支持安卓" || k == "min_sdk") cfg.minSdk = atoi(v.c_str());
            else if (k == "最高支持安卓" || k == "max_sdk") cfg.maxSdk = atoi(v.c_str());
        }
    }
    return true;
}

// ---------- 工具 ----------
CodeGen::CodeGen(BuildConfig cfg) : cfg_(std::move(cfg)) {}

std::string CodeGen::gens(const std::string& s) const {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '\\': o += "\\\\"; break;
            case '"': o += "\\\""; break;
            case '\n': o += "\\n"; break;
            case '\t': o += "\\t"; break;
            case '\r': o += "\\r"; break;
            case '\0': o += "\\0"; break;
            default: o += c;
        }
    }
    return o;
}

std::string CodeGen::joinVec(const std::vector<std::string>& v, const std::string& sep) const {
    std::string o;
    for (size_t i = 0; i < v.size(); i++) { if (i) o += sep; o += v[i]; }
    return o;
}

void CodeGen::line(const std::string& s) {
    for (int i = 0; i < indent_; i++) out_ += "    ";
    out_ += s;
    out_ += "\n";
}

// ---------- 类型映射 ----------
static std::string unwrapStr(const std::string& t) {
    size_t lt = t.find('<'), gt = t.rfind('>');
    if (lt != std::string::npos && gt != std::string::npos && gt > lt)
        return t.substr(lt + 1, gt - lt - 1);
    return t;
}

std::string CodeGen::genTypeStr(const std::string& etcType) {
    const std::string& t = etcType;
    if (t == "i8") return "int8_t";
    if (t == "u8" || t == "byte") return "uint8_t";
    if (t == "i16") return "int16_t";
    if (t == "u16") return "uint16_t";
    if (t == "i32") return "int32_t";
    if (t == "u32") return "uint32_t";
    if (t == "i64") return "int64_t";
    if (t == "u64") return "uint64_t";
    if (t == "f32") return "float";
    if (t == "f64") return "double";
    if (t == "str") return "std::string";
    if (t == "bool") return "bool";
    if (t == "char") return "char";
    if (t == "void") return "void";
    if (t == "auto") return "auto";
    if (t == "type_info") return "const char*";
    if (t == "nil" || t == "null") return "nullptr_t";
    if (t.rfind("ptr<", 0) == 0) return genTypeStr(unwrapStr(t)) + "*";
    if (t.rfind("ref<", 0) == 0) return genTypeStr(unwrapStr(t)) + "&";
    if (t.rfind("own<", 0) == 0) return "std::unique_ptr<" + genTypeStr(unwrapStr(t)) + ">";
    if (t.rfind("share<", 0) == 0) return "std::shared_ptr<" + genTypeStr(unwrapStr(t)) + ">";
    if (t.rfind("weak<", 0) == 0) return "std::weak_ptr<" + genTypeStr(unwrapStr(t)) + ">";
    if (t.rfind("array<", 0) == 0) return "std::vector<" + genTypeStr(unwrapStr(t)) + ">";
    auto addVec = [&](const std::string& v) {
        for (auto& x : usedVecs_) if (x == v) return;
        usedVecs_.push_back(v);
    };
    if (t == "vec2<f32>" || t == "vec4<f32>") { needSimd_ = true; addVec("v4sf"); return "v4sf"; }
    if (t == "vec4<i32>") { needSimd_ = true; addVec("v4si"); return "v4si"; }
    if (t == "vec2<f64>") { needSimd_ = true; addVec("v2df"); return "v2df"; }
    if (t == "vec2<i32>") { needSimd_ = true; addVec("v2si"); return "v2si"; }
    if (t == "vec8<f32>") { needSimd_ = true; addVec("v8sf"); return "v8sf"; }
    if (t == "vec8<i32>") { needSimd_ = true; addVec("v8si"); return "v8si"; }
    if (t == "vec16<i32>") { needSimd_ = true; addVec("v16si"); return "v16si"; }
    if (t == "vec16<f32>") { needSimd_ = true; addVec("v16sf"); return "v16sf"; }
    return t;   // 用户类型 / 泛型参数原样
}

std::string CodeGen::genType(Node& t) {
    std::string s = t.text;
    for (auto& a : t.kids) s += "<" + genType(*a) + ">";
    return s;
}

// ---------- 表达式 ----------
std::string CodeGen::gen(Node& n) {
    switch (n.kind) {
        case K::IntLit: {
            const std::string& t = n.text;
            if (t.rfind("0o", 0) == 0 || t.rfind("0O", 0) == 0)
                return std::to_string(std::strtoll(t.c_str() + 2, nullptr, 8));
            if (t.rfind("0b", 0) == 0 || t.rfind("0B", 0) == 0)
                return "0b" + t.substr(2);   // C++14 起支持 0b
            std::string s;
            for (char c : t) if (c != '_') s += c;
            return s;
        }
        case K::FloatLit: return n.text;
        case K::StrLit: return "\"" + gens(n.sval) + "\"";
        case K::CharLit: return "'" + gens(n.sval) + "'";
        case K::BoolLit: return n.bval ? "true" : "false";
        case K::NullLit: return "nullptr";
        case K::Ident: return (n.text == "self") ? "this" : n.text;
        case K::This: return "this";
        case K::ArrayLit: {
            std::string e0 = n.kids.empty() ? "int32_t" : "decltype(" + gen(*n.kids[0]) + ")";
            std::string o = "std::vector<" + e0 + ">{";
            for (size_t i = 0; i < n.kids.size(); i++) { if (i) o += ", "; o += gen(*n.kids[i]); }
            return o + "}";
        }
        case K::BinOp: {
            std::string l = gen(*n.kids[0]), r = gen(*n.kids[1]);
            const std::string& op = n.text;
            if (op == "as") return "(" + genTypeStr(n.kids[1]->kind == K::Ident ? n.kids[1]->text : gen(*n.kids[1])) + ")(" + l + ")";
            if (op == "is") return "(typeid(" + l + ") == typeid(" + genTypeStr(n.kids[1]->kind == K::Ident ? n.kids[1]->text : gen(*n.kids[1])) + "))";
            if (op == "in") {
                return "((" + l + ").find(" + r + ") != std::string::npos)";
            }
            if (op == "then") return "(" + l + ", " + r + ")";
            if (op == "??") return "((" + l + ") ? (" + l + ") : (" + r + "))";
            if (op == "===" || op == "!==") return "((" + l + ") " + (op == "===" ? "==" : "!=") + " (" + r + "))";
            if (op == ">>>") return "((uint32_t)(" + l + ") >> (" + r + "))";
            return "((" + l + ") " + op + " (" + r + "))";
        }
        case K::UnaryOp: {
            std::string x = gen(*n.kids[0]);
            if (n.text == "move") return "std::move(" + x + ")";
            return "(" + n.text + "(" + x + "))";
        }
        case K::Assign: {
            std::string l = gen(*n.kids[0]), r = gen(*n.kids[1]);
            if (n.text == "=") return "((" + l + ") = (" + r + "))";
            return "((" + l + ") " + n.text + " (" + r + "))";
        }
        case K::Call: {
            Node& callee = *n.kids[0];
            std::string args;
            for (size_t i = 1; i < n.kids.size(); i++) { if (i > 1) args += ", "; args += gen(*n.kids[i]); }
            std::string calleeName = (callee.kind == K::Ident) ? callee.text : "";
            // 成员方法内建映射：obj.len() / obj.push(x) / obj.pop()
            if (callee.kind == K::Member) {
                std::string base = gen(*callee.kids[0]);
                const std::string& m = callee.text;
                std::string bt = varTypeOf(callee.kids[0].get());
                bool ptrLike = (base == "this") ||
                    bt.rfind("own<", 0) == 0 || bt.rfind("ptr<", 0) == 0 ||
                    bt.rfind("share<", 0) == 0 || bt.rfind("ref<", 0) == 0 ||
                    bt.rfind("weak<", 0) == 0;
                std::string op = ptrLike ? "->" : ".";
                if (m == "len") return "(" + base + op + "size()";
                if (m == "push" || m == "append") return "((" + base + ").push_back(" + args + "))";
                if (m == "pop") return "etc_pop(" + base + ")";
                return base + op + m + "(" + args + ")";
            }
            // 内建函数（仅裸调用形式）
            static const std::map<std::string, std::string> b = {
                {"print","std::cout << __A__"},
                {"println","std::cout << __A__ << \"\\n\""},
                {"printl","std::cout << __A__ << \"\\n\""},
                {"sleep","std::this_thread::sleep_for(std::chrono::milliseconds(__A__))"},
                {"strlen","(__A__).size()"},
                {"memset","std::memset(__A__)"},
                {"memcpy","std::memcpy(__A__)"},
                {"sqrt","std::sqrt(__A__)"},
                {"abs","std::abs(__A__)"},
                {"min","std::min(__A__)"},
                {"max","std::max(__A__)"},
                {"clock","etc_clock()"},
                {"len","(__A__).size()"},
                {"append","(__A__).push_back(__B__)"},
                {"push","(__A__).push_back(__B__)"},
                {"pop","etc_pop(__A__)"},
                {"panic","(throw std::runtime_error(etc_str(__A__)))"},
                {"assert","etc_assert(__A__)"},
                {"read_file","etc_read_file(__A__)"},
                {"write_file","etc_write_file(__A__)"},
                {"to_str","etc_str(__A__)"},
                {"spawn","std::thread([&]{ __A__; }).detach()"}
            };
            if (!calleeName.empty()) {
                if (calleeName == "print" || calleeName == "println" || calleeName == "printl") {
                    std::vector<std::string> parts;
                    std::string cur;
                    int depth = 0;
                    for (char c : args) {
                        if (c == ',' && depth == 0) { parts.push_back(cur); cur.clear(); }
                        else { if (c == '(' || c == '[' || c == '{') depth++; if (c == ')' || c == ']' || c == '}') depth--; cur += c; }
                    }
                    if (!cur.empty()) parts.push_back(cur);
                    std::string o = "std::cout";
                    for (auto& pt : parts) o += " << (" + pt + ")";
                    if (calleeName != "print") o += " << \"\\n\"";
                    return "(" + o + ")";
                }
                auto it = b.find(calleeName);
                if (it != b.end()) {
                    std::string tpl = it->second;
                    size_t pa = tpl.find("__A__"), pb = tpl.find("__B__");
                    if (pb != std::string::npos) {
                        size_t c1 = args.find(',');
                        if (c1 != std::string::npos) {
                            std::string a1 = args.substr(0, c1), a2 = args.substr(c1 + 1);
                            if (pa != std::string::npos) tpl.replace(pa, 5, a1);
                            tpl.replace(tpl.find("__B__"), 5, a2);
                            return "(" + tpl + ")";
                        }
                    }
                    if (pa != std::string::npos) tpl.replace(pa, 5, args);
                    return "(" + tpl + ")";
                }
            }
            // 普通函数 / 泛型调用 / 类方法
            std::string base;
            if (callee.kind == K::Member) {
                base = gen(*callee.kids[0]) + "." + callee.text;
            } else if (callee.kind == K::Ident) {
                base = callee.text;
            } else {
                base = gen(callee);
            }
            std::string g;
            if (!n.gparams.empty()) {
                std::vector<std::string> gs;
                for (auto& gp : n.gparams) {
                    std::string t = gp;
                    for (char& c : t) if (c == ',') c = ' ';
                    gs.push_back(genTypeStr(trimWs(t)));
                }
                g = "<" + joinVec(gs, ", ") + ">";
            }
            return base + g + "(" + args + ")";
        }
        case K::Member: {
            if (n.text == "::") return gen(*n.kids[0]) + "::" + n.sval;
            if (n.kids[0]->kind == K::This ||
                (n.kids[0]->kind == K::Ident && (n.kids[0]->text == "self" || n.kids[0]->text == "this")))
                return "this->" + n.text;
            std::string b = gen(*n.kids[0]);
            std::string bt = varTypeOf(n.kids[0].get());
            if (bt.rfind("own<", 0) == 0 || bt.rfind("ptr<", 0) == 0 ||
                bt.rfind("share<", 0) == 0 || bt.rfind("ref<", 0) == 0 ||
                bt.rfind("weak<", 0) == 0)
                return b + "->" + n.text;
            return b + "." + n.text;
        }
        case K::Index: return gen(*n.kids[0]) + "[" + gen(*n.kids[1]) + "]";
        case K::Cast: return "(" + genTypeStr(genType(*n.ty)) + ")(" + (n.kids.empty() ? "" : gen(*n.kids[0])) + ")";
        case K::Alloc: {
            std::string T = genTypeStr(n.ty ? genType(*n.ty) : "i32");
            std::string cnt = n.kids.empty() ? "1" : gen(*n.kids[0]);
            return "((" + T + "*)std::malloc(sizeof(" + T + ") * (" + cnt + ")))";
        }
        case K::Free: return "(std::free((void*)(" + (n.kids.empty() ? "nullptr" : gen(*n.kids[0])) + ")))";
        case K::Load: return "(*(" + gen(*n.kids[0]) + "))";
        case K::Store: return "(*(" + gen(*n.kids[0]) + ") = (" + gen(*n.kids[1]) + "))";
        case K::New: {
            std::string T;
            if (n.ty) T = genTypeStr(genType(*n.ty));
            else if (!n.kids.empty() && n.kids[0]->kind == K::Ident) T = genTypeStr(n.kids[0]->text);
            std::string args;
            size_t from = (n.ty || (n.kids.size() > 1)) ? ((n.ty) ? 0 : 1) : 0;
            if (n.ty) from = 0;
            for (size_t i = from; i < n.kids.size(); i++) {
                if (!args.empty()) args += ", ";
                args += gen(*n.kids[i]);
            }
            return "(std::make_unique<" + T + ">(" + args + "))";
        }
        case K::Del: return "etc_del(" + gen(*n.kids[0]) + ")";
        case K::Sizeof: return "sizeof(" + genTypeStr(n.ty ? genType(*n.ty) : "i32") + ")";
        case K::Typeof: return "typeid(" + (n.kids.empty() ? "0" : gen(*n.kids[0])) + ").name()";
        default: return "";
    }
}

// ---------- 语句 ----------
void CodeGen::genBlock(Node& b) {
    pushVarScope();
    line("{");
    indent_++;
    for (auto& k : b.kids) genStmt(*k);
    indent_--;
    line("}");
    popVarScope();
}

// ---------- 变量类型表（用于 own/ptr 成员解引用） ----------
void CodeGen::pushVarScope() { varScopes_.emplace_back(); }
void CodeGen::popVarScope() { if (!varScopes_.empty()) varScopes_.pop_back(); }
void CodeGen::recordVar(const std::string& name, const std::string& type) {
    if (varScopes_.empty()) varScopes_.emplace_back();
    varScopes_.back()[name] = type;
}
std::string CodeGen::varTypeOf(const Node* base) const {
    if (!base || base->kind != K::Ident) return "";
    for (size_t s = varScopes_.size(); s-- > 0;) {
        auto it = varScopes_[s].find(base->text);
        if (it != varScopes_[s].end()) return it->second;
    }
    return "";
}
std::string CodeGen::inferType(Node& e) {
    switch (e.kind) {
        case K::New:
            if (e.ty) return "own<" + genTypeStr(genType(*e.ty)) + ">";
            break;
        case K::Alloc:
            return "ptr<" + genTypeStr(e.ty ? genType(*e.ty) : "i32") + ">";
        case K::UnaryOp:
            if (e.text == "move") return varTypeOf(e.kids.empty() ? nullptr : e.kids[0].get());
            if (e.text == "&") return "ptr<" + varTypeOf(e.kids.empty() ? nullptr : e.kids[0].get()) + ">";
            break;
        default:
            break;
    }
    return "";
}

void CodeGen::genStmt(Node& n) {
    switch (n.kind) {
        case K::VarDecl: {
            std::string init = n.kids.empty() ? "" : gen(*n.kids[0]);
            std::string ty;
            if (n.ty) {
                ty = genTypeStr(genType(*n.ty));
            } else if (!init.empty() && n.kids[0]->kind == K::StrLit) {
                ty = "std::string";
            } else if (!init.empty() && n.kids[0]->kind == K::NullLit) {
                ty = "nullptr_t";
            } else {
                ty = "auto";
            }
            if (n.isMove) { line("auto " + n.text + " = std::move(" + init + ");"); return; }
            if (init.empty()) {
                if (ty == "auto") line("int32_t " + n.text + "{};   /* 未指定类型与初值，默认 i32 */");
                else line(ty + " " + n.text + "{};");
            } else {
                line(ty + " " + n.text + " = " + init + ";");
            }
            {   // 记录 ETC 类型（原始 own<X> 形式），供 own/ptr 成员解引用
                std::string etcType;
                if (n.ty) etcType = genType(*n.ty);
                else if (!n.kids.empty()) etcType = inferType(*n.kids[0]);
                if (!etcType.empty()) recordVar(n.text, etcType);
            }
            break;
        }
        case K::Block: genBlock(n); break;
        case K::ExprStmt: line(gen(*n.kids[0]) + ";"); break;
        case K::EmptyStmt: break;
        case K::IfStmt: {
            line("if (" + gen(*n.kids[0]) + ")");
            genBlock(*n.kids[1]);
            size_t k = 2;
            while (k + 1 < n.kids.size()) {
                line("else if (" + gen(*n.kids[k]) + ")");
                genBlock(*n.kids[k + 1]);
                k += 2;
            }
            if (k < n.kids.size()) { line("else"); genBlock(*n.kids[k]); }
            break;
        }
        case K::WhileStmt:
            line("while (" + gen(*n.kids[0]) + ")");
            genBlock(*n.kids[1]);
            break;
        case K::LoopStmt:
            line("while (true)");
            genBlock(*n.kids[0]);
            break;
        case K::ForStmt: {
            if (!n.text.empty()) {
                line("for (auto& " + n.text + " : " + gen(*n.kids[0]) + ")");
                genBlock(*n.kids[3]);
            } else {
                std::string init;
                if (n.kids[0]) {
                    if (n.kids[0]->kind == K::VarDecl) {
                        Node& vd = *n.kids[0];
                        std::string ty = vd.ty ? genTypeStr(genType(*vd.ty)) : "auto";
                        std::string iv = vd.kids.empty() ? "" : gen(*vd.kids[0]);
                        init = ty + " " + vd.text + (iv.empty() ? "" : " = " + iv);
                    } else {
                        init = gen(*n.kids[0]);
                    }
                }
                std::string cond = n.kids[1] ? gen(*n.kids[1]) : "true";
                std::string step = n.kids[2] ? gen(*n.kids[2]) : "";
                line("for (" + init + "; " + cond + "; " + step + ")");
                genBlock(*n.kids[3]);
            }
            break;
        }
        case K::BreakStmt: line("break;"); break;
        case K::ContinueStmt: line("continue;"); break;
        case K::ReturnStmt:
            if (n.kids.empty()) line("return;");
            else line("return " + gen(*n.kids[0]) + ";");
            break;
        case K::MatchStmt: {
            std::string v = gen(*n.kids[0]);
            std::string tmp = "__match";
            line("{"); indent_++;
            line("auto " + tmp + " = " + v + ";");
            for (size_t k = 1; k < n.kids.size(); k++) {
                Node& c = *n.kids[k];
                bool wild = c.kids.empty() || (c.kids[0]->kind == K::Ident && c.kids[0]->text == "_");
                bool first = (k == 1);
                bool last = (k == n.kids.size() - 1);
                std::string head;
                if (wild && first && last) head = "if (true)";          // 只有通配
                else if (wild) head = "else";
                else if (first) head = "if (" + tmp + " == " + gen(*c.kids[0]) + ")";
                else head = "else if (" + tmp + " == " + gen(*c.kids[0]) + ")";
                line(head);
                indent_++;
                if (c.kids.size() > 1) genBlock(*c.kids[1]);
                else line("(void)0;");
                indent_--;
            }
            indent_--; line("}");
            break;
        }
        case K::TryStmt: {
            // 先取 finally
            Node* fin = nullptr;
            for (auto& k : n.kids)
                if (k->kind == K::Block && k->text == "finally") { fin = k.get(); break; }
            if (fin) {
                std::string fbody;
                std::string save = out_;
                out_.clear();
                int saveInd = indent_;
                for (auto& s : fin->kids) genStmt(*s);
                fbody = out_;
                out_ = save;
                indent_ = saveInd;
                line("struct etc_fin { std::function<void()> f; etc_fin(std::function<void()>&& fn) : f(std::move(fn)) {} ~etc_fin(){ f(); } } _etc_f([&]{");
                indent_++;
                for (auto& s : fin->kids) genStmt(*s);
                indent_--;
                line("});");
            }
            line("try");
            genBlock(*n.kids[0]);
            for (size_t k = 1; k < n.kids.size(); k++) {
                Node& c = *n.kids[k];
                if (c.kind != K::MatchCase) continue;
                std::string ty = c.ty ? genTypeStr(genType(*c.ty)) : "std::exception";
                std::string bind;
                if (ty == "std::string") {
                    bind = "auto " + c.text + " = _etc_e.msg;";
                    line("catch (const etc_error& _etc_e)");
                } else if (ty == "int32_t") {
                    bind = "auto " + c.text + " = std::stoi(_etc_e.msg);";
                    line("catch (const etc_error& _etc_e)");
                } else if (ty == "int64_t") {
                    bind = "auto " + c.text + " = std::stoll(_etc_e.msg);";
                    line("catch (const etc_error& _etc_e)");
                } else if (ty == "double" || ty == "float") {
                    bind = "auto " + c.text + " = std::stod(_etc_e.msg);";
                    line("catch (const etc_error& _etc_e)");
                } else {
                    bind = "auto " + c.text + " = _etc_e;";
                    line("catch (const std::exception& _etc_e)");
                }
                line("{");
                indent_++;
                line(bind);
                if (!c.kids.empty() && c.kids[0]->kind == K::Block)
                    for (auto& s : c.kids[0]->kids) genStmt(*s);
                indent_--;
                line("}");
            }
            {
                bool hasCatch = false;
                for (size_t k = 1; k < n.kids.size(); k++)
                    if (n.kids[k]->kind == K::MatchCase) hasCatch = true;
                if (!hasCatch) line("catch (...) { (void)0; }");
            }
            break;
        }
        case K::ThrowStmt:
            line("throw etc_error{etc_str(" + gen(*n.kids[0]) + ")};");
            break;
        case K::UnsafeBlock:
            line("/* unsafe 块：ETC 极限模式下 C++ 侧无额外保护 */");
            genBlock(*n.kids[0]);
            break;
        case K::AsmBlock: {
            if (!n.kids.empty()) {
                std::string a = n.kids[0]->text;
                if (!a.empty()) line("__asm__ volatile(\"" + gens(a) + "\");");
            }
            break;
        }
        default:
            break;
    }
}

// ---------- 函数 / 类型声明 ----------
void CodeGen::genFn(Node& n, bool method, const std::string& cls) {
    std::string ret = n.ty ? genTypeStr(genType(*n.ty)) : "void";
    std::string params;
    bool skipFirst = method && !n.kids.empty() && n.kids[0]->kind == K::Param &&
                     (n.kids[0]->text == "self" || n.kids[0]->text == "this");
    for (auto& p : n.kids) {
        if (p->kind != K::Param) continue;
        if (skipFirst && p.get() == n.kids[0].get()) continue;   // 成员函数省略 self 参数
        std::string pt = p->ty ? genTypeStr(genType(*p->ty)) : "auto";
        if (!params.empty()) params += ", ";
        params += pt + " " + p->text;
    }
    std::string tpl;
    if (!n.gparams.empty()) {
        std::vector<std::string> ts;
        for (auto& g : n.gparams) ts.push_back("typename " + g);
        tpl = "template<" + joinVec(ts, ", ") + "> ";
    }
    line(tpl + ret + " " + n.text + "(" + params + ")");
    genBlock(*n.kids.back());
}

void CodeGen::genClass(Node& n, bool isStruct) {
    line("struct " + n.text + " {");
    indent_++;
    std::vector<Node*> fields;
    bool hasRef = false, ctorOk = true;
    for (auto& m : n.kids) {
        if (m->kind != K::Field && m->kind != K::VarDecl) continue;   // 类字段可能是 VarDecl
        fields.push_back(m.get());
        if (!m->ty) ctorOk = false;
        else if (genType(*m->ty).rfind("ref<", 0) == 0) hasRef = true;
    }
    for (auto* f : fields) {
        std::string ft = f->ty ? genTypeStr(genType(*f->ty)) : "auto";
        if (f->kids.empty()) line(ft + " " + f->text + "{};");
        else line(ft + " " + f->text + " = " + gen(*f->kids[0]) + ";");
    }
    // 构造函数（字段默认值 → 默认参数），让 new Type(args) 可用
    if (ctorOk && !fields.empty() && !hasRef) {
        std::string params, inits;
        for (auto* f : fields) {
            std::string ft = genTypeStr(genType(*f->ty));
            std::string pn = "_etc_" + f->text;
            if (!params.empty()) params += ", ";
            params += ft + " " + pn;
            if (!f->kids.empty()) params += " = " + gen(*f->kids[0]);
            if (!inits.empty()) inits += ", ";
            inits += f->text + "(std::move(" + pn + "))";
        }
        line(n.text + "() = default;");
        line(n.text + "(" + params + ") : " + inits + " {}");
    }
    for (auto& m : n.kids) {
        if (m->kind == K::Method) {
            line("");
            genFn(*m, true, n.text);
        }
    }
    auto it = extraMethods_.find(n.text);
    if (it != extraMethods_.end()) {
        for (auto* m : it->second) { line(""); genFn(*m, true, n.text); }
    }
    indent_--;
    line("};");
}

void CodeGen::genEnum(Node& n) {
    line("enum class " + n.text + " : int32_t {");
    indent_++;
    int next = 0;
    for (auto& c : n.kids) {
        if (c->kind != K::EnumCase) continue;
        if (c->kids.empty()) {
            line(c->text + " = " + std::to_string(next) + ",");
            next++;
        } else if (c->kids[0]->kind == K::IntLit) {
            line(c->text + " = " + std::to_string(c->kids[0]->ival) + ",");
            next = (int)c->kids[0]->ival + 1;
        } else {
            line(c->text + " = " + std::to_string(next) + ",");
            next++;
        }
    }
    indent_--;
    line("};");
}

void CodeGen::genTrait(Node& n) {
    line("struct " + n.text + " {");
    indent_++;
    for (auto& m : n.kids) {
        if (m->kind != K::Method) continue;
        std::string ret = m->ty ? genTypeStr(genType(*m->ty)) : "void";
        std::string params;
        bool first = true;
        for (auto& p : m->kids) {
            if (p->kind != K::Param) continue;
            if (first && (p->text == "self" || p->text == "this")) { first = false; continue; }
            if (!params.empty()) params += ", ";
            params += (p->ty ? genTypeStr(genType(*p->ty)) : "auto") + " " + p->text;
            first = false;
        }
        line("virtual " + ret + " " + m->text + "(" + params + ") = 0;");
    }
    indent_--;
    line("};");
}

void CodeGen::genImpl(Node& n) {
    std::string target = n.ty ? genTypeStr(genType(*n.ty)) : "";
    if (target.empty()) return;
    // 把方法挂到目标类（C++ 里作为成员函数注入）
    auto& v = extraMethods_[target];
    for (auto& m : n.kids)
        if (m->kind == K::Method) v.push_back(m.get());
}

// ---------- 权限收集 ----------
void CodeGen::collectPerms(Node* prog) {
    auto handle = [&](const NodePtr& a) {
        if (a->kind != K::Ann || a->text != "grant") return;
        std::string arg = a->kids.empty() ? "" : a->kids[0]->text;
        if (arg == "root") { perms_.push_back("root（系统级管理员）"); elevate_ = true; }
        else if (arg == "system") { perms_.push_back("system（系统级）"); elevate_ = true; }
        else if (arg == "user") { perms_.push_back("user（用户级）"); }
        else if (arg == "sandbox") { perms_.push_back("sandbox（沙箱）"); }
        else if (arg == "kernel") { perms_.push_back("kernel（内核态）"); }
        else if (arg == "*" || arg == "all") { perms_.push_back("*（全部能力）"); }
        else if (arg.rfind("module:", 0) == 0) perms_.push_back(arg);
        else if (arg.rfind("capability:", 0) == 0) perms_.push_back("capability: " + arg.substr(11));
        else if (!arg.empty()) perms_.push_back("capability: " + arg);
    };
    for (auto& a : prog->anns) handle(a);
    for (auto& d : prog->kids) {
        for (auto& a : d->anns) {
            if (a->text == "grant") handle(a);
            else if (a->text == "protect") {
                protect_ = true;
                if (!a->kids.empty()) {
                    std::string arg = a->kids[0]->text;
                    size_t hb = arg.find("heartbeat");
                    if (hb != std::string::npos) {
                        std::string rest = arg.substr(hb);
                        size_t c1 = rest.find(':');
                        size_t c2 = rest.find(',');
                        std::string num = c1 != std::string::npos ? rest.substr(c1 + 1, (c2 == std::string::npos ? rest.size() : c2) - c1 - 1) : "";
                        num = trimWs(num);
                        if (!num.empty()) heartbeat_ = atoi(num.c_str());
                    }
                }
            }
        }
        if (d->kind == K::Call) usesIo_ = true;   // 占位，实际由内建函数检测
    }
    // 检测 IO 内建函数使用
    struct IOFind : Visitor {
        bool found = false;
        void visit(Node& n) override {
            if (n.kind == K::Call && !n.kids.empty() && n.kids[0]->kind == K::Ident) {
                const std::string& s = n.kids[0]->text;
                if (s == "read_file" || s == "write_file" || s == "send" || s == "recv") found = true;
            }
        }
    } iof;
    walk(*prog, iof);
    usesIo_ = iof.found;
}

// ---------- 保护注入 ----------
void CodeGen::emitProtect() {
    line("");
    line("/* ── ETC 进程保护（@protect 自动注入）── */");
    line("static void etc_protect_init() {");
    indent_++;
    line("#ifdef _WIN32");
    line("if (IsDebuggerPresent()) { /* anti_debug: 检测到调试器 */ }");
    line("/* anti_inject: 常规线程/模块校验由编译器框架预留 */");
    line("#endif");
    line("/* stack_canary  → 构建期 -fstack-protector-all 提供 */");
    line("/* heap_guard    → 构建期 _FORTIFY_SOURCE 提供 */");
    line("/* code_integrity→ 需内核/驱动配合，用户态仅做启动自检（预留） */");
    line("/* watchdog      → 心跳线程（预留） */");
    indent_--;
    line("}");
}

// ---------- 运行时库 ----------
static std::string vecStructOps(const std::string& t, const std::string& elem, int n, const std::string& name) {
    std::string o;
    o += "typedef struct { " + elem + " x[" + std::to_string(n) + "]; inline " + t +
         "& operator[](int i){ return x[i]; } } " + name + ";\n";
    for (const char* op : {"+", "-", "*", "/"}) {
        o += "inline " + name + " operator" + op + "(" + name + " a, " + name + " b){ " + name +
             " r{}; for(int i=0;i<" + std::to_string(n) + ";i++) r.x[i]=a.x[i] " + op + " b.x[i]; return r; }\n";
    }
    return o;
}

void CodeGen::emitSimdTypedefs() {
    if (usedVecs_.empty()) return;
    for (auto& v : usedVecs_) {
        if (v == "v2sf") out_ += vecStructOps("float", "float", 2, "v2sf");
        else if (v == "v4sf") out_ += vecStructOps("float", "float", 4, "v4sf");
        else if (v == "v2df") out_ += vecStructOps("double", "double", 2, "v2df");
        else if (v == "v2si") out_ += vecStructOps("int32_t", "int32_t", 2, "v2si");
        else if (v == "v4si") out_ += vecStructOps("int32_t", "int32_t", 4, "v4si");
        else if (v == "v8sf") out_ += vecStructOps("float", "float", 8, "v8sf");
        else if (v == "v8si") out_ += vecStructOps("int32_t", "int32_t", 8, "v8si");
        else if (v == "v16si") out_ += vecStructOps("int32_t", "int32_t", 16, "v16si");
        else if (v == "v16sf") out_ += vecStructOps("float", "float", 16, "v16sf");
    }
    line("");
    // 便捷构造（只生成用到的向量类型）
    bool hasV4sf = false, hasV4si = false, hasV2df = false, hasV8si = false, hasV8sf = false;
    for (auto& v : usedVecs_) {
        if (v == "v4sf") hasV4sf = true;
        else if (v == "v4si") hasV4si = true;
        else if (v == "v2df") hasV2df = true;
        else if (v == "v8si") hasV8si = true;
        else if (v == "v8sf") hasV8sf = true;
    }
    if (hasV4sf) line("static v4sf etc_vec4f(float a, float b, float c, float d){ v4sf r{}; r.x[0]=a; r.x[1]=b; r.x[2]=c; r.x[3]=d; return r; }");
    if (hasV4si) line("static v4si etc_vec4i(int32_t a, int32_t b, int32_t c, int32_t d){ v4si r{}; r.x[0]=a; r.x[1]=b; r.x[2]=c; r.x[3]=d; return r; }");
    if (hasV2df) line("static v2df etc_vec2d(double a, double b){ v2df r{}; r.x[0]=a; r.x[1]=b; return r; }");
    if (hasV8si) line("static v8si etc_vec8i(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f, int32_t g, int32_t h){ v8si r{}; r.x[0]=a; r.x[1]=b; r.x[2]=c; r.x[3]=d; r.x[4]=e; r.x[5]=f; r.x[6]=g; r.x[7]=h; return r; }");
    if (hasV8sf) line("static v8sf etc_vec8f(float a, float b, float c, float d, float e, float f, float g, float h){ v8sf r{}; r.x[0]=a; r.x[1]=b; r.x[2]=c; r.x[3]=d; r.x[4]=e; r.x[5]=f; r.x[6]=g; r.x[7]=h; return r; }");
}

void CodeGen::emitRuntime() {
    line("// ============================================================");
    line("//  ETC Lang → C++  生成代码（by etc / ETC-IDE 编译器）");
    line("// ============================================================");
    line("#include <cstdio>");
    line("#include <cstdlib>");
    line("#include <cstdint>");
    line("#include <cstring>");
    line("#include <string>");
    line("#include <iostream>");
    line("#include <memory>");
    line("#include <vector>");
    line("#include <map>");
    line("#include <algorithm>");
    line("#include <chrono>");
    line("#include <thread>");
    line("#include <functional>");
    line("#include <typeinfo>");
    line("#include <sstream>");
    line("#ifdef _WIN32");
    line("#include <windows.h>");
    line("#endif");
    line("using namespace std;");
    if (needSimd_) emitSimdTypedefs();
    line("");
    line("// ── ETC 运行时工具 ──");
    line("struct etc_error { std::string msg; };");
    line("static string etc_read_file(const string& p){ FILE* f = fopen(p.c_str(), \"rb\"); if(!f) return \"\"; fseek(f,0,SEEK_END); long n = ftell(f); fseek(f,0,SEEK_SET); string s; s.resize(n); size_t r = fread(&s[0],1,n,f); fclose(f); s.resize(r); return s; }");
    line("static void etc_write_file(const string& p, const string& c){ FILE* f = fopen(p.c_str(), \"wb\"); if(f){ fwrite(c.data(),1,c.size(),f); fclose(f);} }");
    line("template<typename T> void etc_del(T& x){ x.reset(); }");
    line("template<typename T> void etc_del(T* x){ delete x; }");
    line("template<typename T> T etc_pop(std::vector<T>& v){ T r = std::move(v.back()); v.pop_back(); return r; }");
    line("template<typename T> std::string etc_str(const T& v){ std::ostringstream o; o << v; return o.str(); }");
    line("static std::string etc_str(const std::string& s){ return s; }");
    line("static std::string etc_str(const char* s){ return s ? s : \"\"; }");
    line("static std::string etc_str(bool b){ return b ? \"true\" : \"false\"; }");
    line("template<typename T> void etc_assert(const T& b){ if(!(b)) throw std::runtime_error(\"断言失败: assert\"); }");
    line("static double etc_clock(){ using namespace std::chrono; return duration<double>(steady_clock::now().time_since_epoch()).count(); }");
    line("");
}

// ---------- 主流程 ----------
bool CodeGen::generate(Node* prog, std::vector<Diag>& diags) {
    collectPerms(prog);
    // 预扫描：提前收集 SIMD 类型（typedef 需在运行时库前生成）
    struct VecFind : Visitor {
        CodeGen* cg;
        void visit(Node& n) override { if (n.ty) cg->genTypeStr(cg->genType(*n.ty)); }
    } vf;
    vf.cg = this;
    walk(*prog, vf);
    // 先收集 impl 注入
    for (auto& d : prog->kids)
        if (d->kind == K::ImplDecl) genImpl(*d);

    emitRuntime();
    if (protect_) emitProtect();

    Node* main = nullptr;
    bool haveMain = false;
    for (auto& d : prog->kids) {
        switch (d->kind) {
            case K::FnDecl:
                if (d->text == "main") { main = d.get(); haveMain = true; continue; }
                line("");
                genFn(*d, false, "");
                break;
            case K::ClassDecl: line(""); genClass(*d, false); break;
            case K::StructDecl: line(""); genClass(*d, true); break;
            case K::EnumDecl: line(""); genEnum(*d); break;
            case K::TraitDecl: line(""); genTrait(*d); break;
            case K::VarDecl: line(""); genStmt(*d); break;
            default: break;
        }
    }
    if (!haveMain) {
        Diag d;
        d.isErr = true; d.code = 2001;
        d.file = "project"; d.line = 0; d.col = 0;
        d.msg = "缺少 main 入口函数（.ce 文件必须包含 fn main）";
        diags.push_back(d);
        return false;
    }
    // main 包装
    line("");
    line("int main() {");
    indent_++;
    if (protect_) line("etc_protect_init();");
    // 注入 main 函数体
    std::string saved = out_;
    out_.clear();
    int savedInd = indent_;
    for (auto& k : main->kids.back()->kids) genStmt(*k);
    std::string body = out_;
    out_ = saved;
    indent_ = savedInd;
    // 去掉 body 的缩进调整
    if (!body.empty()) {
        out_ += body;
        if (!body.empty() && body.back() != '\n') out_ += "\n";
    }
    bool hasRet = false;
    for (auto& k : main->kids.back()->kids)
        if (k->kind == K::ReturnStmt) hasRet = true;
    if (!hasRet) line("return 0;");
    indent_--;
    line("}");
    cpp_ = out_;
    return true;
}

// ---------- 元数据生成 ----------
void CodeGen::writeVersionRc(const std::string& path, const std::string& exeName) const {
    std::ofstream f(path);
    auto ver = [](const std::string& v) {
        std::string o;
        int n = 0;
        for (char c : v) {
            if (c == '.') { if (++n >= 4) break; o += ','; }
            else if (isdigit((unsigned char)c)) o += c;
        }
        while (n < 3) { o += ",0"; n++; }
        return o;
    };
    f << "1 VERSIONINFO\n";
    f << "FILEVERSION " << ver(cfg_.fileVersion) << "\n";
    f << "PRODUCTVERSION " << ver(cfg_.productVersion) << "\n";
    f << "FILEFLAGSMASK 0x3fL\nFILEFLAGS 0x0L\nFILEOS 0x40004L\nFILETYPE 0x1L\nFILESUBTYPE 0x0L\n";
    f << "BEGIN\n  BLOCK \"StringFileInfo\"\n  BEGIN\n    BLOCK \"080404b0\"\n    BEGIN\n";
    f << "      VALUE \"CompanyName\", \"" << cfg_.company << "\"\n";
    f << "      VALUE \"FileDescription\", \"" << cfg_.fileDesc << "\"\n";
    f << "      VALUE \"FileVersion\", \"" << cfg_.fileVersion << "\"\n";
    f << "      VALUE \"InternalName\", \"" << exeName << "\"\n";
    f << "      VALUE \"LegalCopyright\", \"" << cfg_.copyright << "\"\n";
    f << "      VALUE \"OriginalFilename\", \"" << exeName << "\"\n";
    f << "      VALUE \"ProductName\", \"" << cfg_.productName << "\"\n";
    f << "      VALUE \"ProductVersion\", \"" << cfg_.productVersion << "\"\n";
    f << "    END\n  END\n  BLOCK \"VarFileInfo\"\n  BEGIN\n    VALUE \"Translation\", 0x804, 1200\n  END\nEND\n";
    if (!cfg_.icon.empty()) {
        std::string ico = cfg_.icon;
        for (auto& c : ico) if (c == '\\') c = '/';
        f << "1 ICON \"" << ico << "\"\n";
    }
    f.close();
}

void CodeGen::writeWinManifest(const std::string& path) const {
    std::ofstream f(path);
    f << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    f << "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\n";
    f << "  <trustInfo xmlns=\"urn:schemas-microsoft-com:asm.v3\">\n";
    f << "    <security>\n      <requestedPrivileges>\n";
    if (elevate_) f << "        <requestedExecutionLevel level=\"requireAdministrator\" uiAccess=\"false\"/>\n";
    else f << "        <requestedExecutionLevel level=\"asInvoker\" uiAccess=\"false\"/>\n";
    f << "      </requestedPrivileges>\n    </security>\n  </trustInfo>\n";
    f << "  <compatibility xmlns=\"urn:schemas-microsoft-com:compatibility.v1\">\n";
    f << "    <application>\n      <supportedOS Id=\"{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}\"/>\n";
    f << "    </application>\n  </compatibility>\n";
    f << "</assembly>\n";
    f.close();
}

void CodeGen::writeAndroidManifest(const std::string& path) const {
    std::ofstream f(path);
    f << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    f << "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n";
    f << "    package=\"" << cfg_.package << "\"\n";
    f << "    android:versionCode=\"" << cfg_.versionCode << "\"\n";
    f << "    android:versionName=\"" << cfg_.versionName << "\">\n";
    f << "  <uses-sdk android:minSdkVersion=\"" << cfg_.minSdk << "\" android:targetSdkVersion=\"" << (cfg_.maxSdk > 33 ? 33 : cfg_.maxSdk) << "\"/>\n";
    if (usesIo_) f << "  <uses-permission android:name=\"android.permission.INTERNET\"/>\n";
    f << "  <application android:label=\"" << cfg_.appName << "\"";
    if (!cfg_.appIcon.empty()) f << " android:icon=\"@mipmap/ic_launcher\"";
    f << ">\n";
    f << "    <activity android:name=\".MainActivity\" android:exported=\"true\">\n";
    f << "      <intent-filter>\n";
    f << "        <action android:name=\"android.intent.action.MAIN\"/>\n";
    f << "        <category android:name=\"android.intent.category.LAUNCHER\"/>\n";
    f << "      </intent-filter>\n    </activity>\n  </application>\n</manifest>\n";
    f.close();
}

}
