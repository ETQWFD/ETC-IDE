// ============================================================
//  ETC Lang 编译器入口 —— come 命令行
//  come 名称.exe --oob     编译为 Windows EXE 并压缩
//  come 名称.apk --oot     编译为 Android APK 并压缩
// ============================================================
#include "lexer.h"
#include "parser.h"
#include "semantic.h"
#include "codegen.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <filesystem>
#include <cstdlib>
#include <cstdio>
#include <chrono>
#include <algorithm>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;
using namespace etc;

// ---------- 终端高亮 ----------
static const char* BOLD  = "\033[1m";
static const char* GREEN = "\033[32m";
static const char* CYAN  = "\033[36m";
static const char* YELLOW= "\033[33m";
static const char* RED   = "\033[31m";
static const char* RESET = "\033[0m";

static void banner() {
    std::cout << BOLD << CYAN
              << "  ┌─────────────────────────────────────────────┐\n"
              << "  │   ETC Lang 编译器 · come v1.3 (by etc)      │\n"
              << "  │   生来 root · 极限性能 · 完整进程保护        │\n"
              << "  └─────────────────────────────────────────────┘\n"
              << RESET << std::endl;
}

static std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void writeFile(const fs::path& p, const std::string& c) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << c;
}

static std::string trimWs(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, b - a + 1);
}

static int runCmd(const std::string& cmd, std::string* out = nullptr, bool verbose = false) {
    if (verbose) std::cout << "  > " << cmd << std::endl;
    std::string res;
    FILE* f = popen((cmd + " 2>&1").c_str(), "r");
    if (!f) return -1;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) res.append(buf, n);
    int rc = pclose(f);
    if (out) *out = res;
    return rc;
}

// ---------- 工具链定位 ----------
// 优先使用 come.exe 同目录的 toolchain\bin（便携版，安装包自带），找不到则回退 PATH。
static std::string exeDir() {
#ifdef _WIN32
    wchar_t buf[4096];
    DWORD n = GetModuleFileNameW(nullptr, buf, 4096);
    if (n == 0) return ".";
    std::wstring w(buf, n);
    size_t pos = w.find_last_of(L"\\/");
    if (pos != std::wstring::npos) w = w.substr(0, pos);
    std::string s;
    for (wchar_t c : w) s += (char)c;   // 安装路径为英文目录，直接窄化
    return s;
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    buf[n] = 0;
    std::string s(buf);
    size_t pos = s.find_last_of('/');
    return pos == std::string::npos ? "." : s.substr(0, pos);
#endif
}

static bool fileExists(const std::string& p) {
#ifdef _WIN32
    return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(p.c_str(), &st) == 0;
#endif
}

// 返回带引号的工具路径；未找到自带工具链时返回裸名字（Linux 回退交叉前缀，Windows 回退 PATH）
static std::string findTool(const std::string& name) {
    std::string tc = exeDir() + "/toolchain/bin/" + name + ".exe";
    if (fileExists(tc)) return "\"" + tc + "\"";
    std::string tc2 = exeDir() + "/toolchain/bin/x86_64-w64-mingw32-" + name + ".exe";
    if (fileExists(tc2)) return "\"" + tc2 + "\"";
#ifdef _WIN32
    return name;
#else
    return "x86_64-w64-mingw32-" + name;
#endif
}

static bool toolAvailable(const std::string& name) {
    std::string t = findTool(name);
    if (!t.empty() && t[0] == '"') return true;   // 自带工具链命中
#ifdef _WIN32
    return runCmd("where " + name + " >nul 2>nul") == 0;
#else
    return runCmd("command -v " + name + " >/dev/null 2>&1") == 0;
#endif
}

static bool mingwAvailable() {
    std::string t = findTool("g++");
    if (!t.empty() && t[0] == '"') return true;   // 自带工具链命中
#ifdef _WIN32
    return runCmd("where g++ >nul 2>nul") == 0 || runCmd("where x86_64-w64-mingw32-g++ >nul 2>nul") == 0;
#else
    return runCmd("command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1") == 0;
#endif
}

// ---------- 参数解析 ----------
struct Args {
    std::string target;
    std::string mode;          // oob / oot
    fs::path proj = ".";
    fs::path outDir;
    bool verbose = false;
};

static int usage() {
    std::cout << BOLD << "用法:\n" << RESET
              << "  come 名称.exe --oob      编译为 Windows EXE 并压缩\n"
              << "  come 名称.apk --oot      编译为 Android APK 并压缩\n"
              << "  [--proj 目录]  指定项目目录（默认当前目录）\n"
              << "  [--out 目录]   指定输出目录\n"
              << "  [--verbose]    打印详细编译命令\n";
    return 2;
}

// ---------- 主流程 ----------
int main(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; i++) {
        std::string s = argv[i];
        if (s == "--oob") a.mode = "oob";
        else if (s == "--oot") a.mode = "oot";
        else if (s == "--proj" && i + 1 < argc) a.proj = argv[++i];
        else if (s == "--out" && i + 1 < argc) a.outDir = argv[++i];
        else if (s == "--verbose") a.verbose = true;
        else if (a.target.empty()) a.target = s;
        else { std::cerr << "未知参数: " << s << "\n"; return usage(); }
    }
    if (a.target.empty() || a.mode.empty()) return usage();
    std::string lower = a.target;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    bool isExe = lower.rfind(".exe", lower.size()) == lower.size() - 4 || a.mode == "oob";
    bool isApk = lower.rfind(".apk", lower.size()) == lower.size() - 4 || a.mode == "oot";
    if (a.mode == "oob" && !isExe) { std::cerr << "错误: --oob 需要 .exe 目标\n"; return usage(); }
    if (a.mode == "oot" && !isApk) { std::cerr << "错误: --oot 需要 .apk 目标\n"; return usage(); }

    banner();
    auto t0 = std::chrono::steady_clock::now();
    auto elapsed = [&]() {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0).count();
        return ms;
    };

    if (!fs::exists(a.proj)) { std::cerr << RED << "错误 E9001: 项目目录不存在: " << a.proj << RESET << "\n"; return 1; }

    // ── 1. 扫描项目源文件（7 种后缀）──
    std::vector<fs::path> srcs;
    fs::path ecFile;
    std::cout << CYAN << "▸ 第 1 步 / 8  扫描项目源文件" << RESET << std::endl;
    for (auto& e : fs::recursive_directory_iterator(a.proj)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        if (ext == ".ce" || ext == ".ve" || ext == ".xo" || ext == ".ca" || ext == ".io" || ext == ".ru")
            srcs.push_back(e.path());
        if (ext == ".ec" && ecFile.empty()) ecFile = e.path();
    }
    if (srcs.empty()) {
        std::cerr << RED << "错误 E9002: 项目中没有找到 .ce/.ve/.xo/.ca/.io/.ru 源文件" << RESET << "\n";
        return 1;
    }
    std::cout << "  找到 " << srcs.size() << " 个源文件";
    for (auto& s : srcs) std::cout << "  " << s.filename().string();
    std::cout << std::endl;

    BuildConfig cfg;
    if (!ecFile.empty()) {
        std::string err;
        if (parseEcFile(ecFile.string(), cfg, err))
            std::cout << "  项目配置: " << ecFile.filename().string() << "  (目标: "
                      << (cfg.target == "apk" ? "Android APP" : "Windows EXE") << ")\n";
        else
            std::cout << YELLOW << "  警告: " << err << RESET << "\n";
    }

    // ── 2+3. 词法 + 语法 ──
    std::cout << CYAN << "▸ 第 2 步 / 8  词法分析" << RESET << std::endl;
    std::vector<Diag> diags;
    int nErr = 0, nWarn = 0;
    NodePtr merged = mk(K::Program, 1, 1, "project");

    for (auto& p : srcs) {
        std::string code = readFile(p);
        try {
            Lexer lx(code, p.string());
            auto toks = lx.tokenize();
            size_t tokCount = toks.size();
            Parser ps(std::move(toks), p.string());
            NodePtr prog = ps.parseProgram();
            for (auto& d : prog->kids) merged->kids.push_back(std::move(d));
            for (auto& an : prog->anns) merged->anns.push_back(std::move(an));
            std::cout << "  ✓ " << p.filename().string() << "  词法/语法通过 (" << tokCount << " tokens)\n";
        } catch (const CompileError& e) {
            Diag d;
            d.isErr = true; d.code = e.code; d.file = e.file; d.line = e.line; d.col = e.col; d.msg = e.msg;
            diags.push_back(d);
        }
    }
    std::cout << CYAN << "▸ 第 3 步 / 8  语法分析" << RESET << std::endl;

    // ── 4. 语义分析 ──
    std::cout << CYAN << "▸ 第 4 步 / 8  语义分析（类型 / 权限 / 所有权）" << RESET << std::endl;
    std::vector<std::string> grantedCaps;
    for (auto& an : merged->anns)
        if (an->kind == K::Ann && an->text == "grant" && !an->kids.empty())
            grantedCaps.push_back(an->kids[0]->text);
    if (!diags.empty() || nErr == 0) {
        Semantic sem(grantedCaps);
        sem.check(merged.get());
        for (auto& d : sem.diags()) diags.push_back(d);
    }

    for (auto& d : diags) {
        if (d.isErr) nErr++; else nWarn++;
        std::cout << (d.isErr ? RED : YELLOW)
                  << (d.isErr ? "错误 E" : "警告 W")
                  << d.code << ": " << d.file << ":" << d.line << ":" << d.col
                  << " " << d.msg << RESET << "\n";
    }
    if (nErr > 0) {
        std::cout << RED << BOLD << nErr << " 个错误，" << nWarn << " 个警告\n编译失败" << RESET << "\n";
        return 1;
    }

    // ── 5. 代码生成 ──
    std::cout << CYAN << "▸ 第 5 步 / 8  代码生成（ETC → C++）" << RESET << std::endl;
    CodeGen cg(cfg);
    if (!cg.generate(merged.get(), diags)) {
        for (auto& d : diags) {
            std::cout << RED << "错误 E" << d.code << ": " << d.file << ":" << d.line << ":" << d.col
                      << " " << d.msg << RESET << "\n";
        }
        return 1;
    }
    // 权限清单（编译期强制打印，权限透明）
    std::cout << GREEN << "  权限清单:" << RESET;
    auto pl = cg.permissionList();
    if (pl.empty()) std::cout << " (未声明 @grant，程序以普通权限运行)";
    for (auto& p : pl) std::cout << "  [" << p << "]";
    std::cout << "\n";

    fs::path outDir = a.outDir.empty() ? fs::path(a.target).parent_path() : a.outDir;
    if (outDir.empty()) outDir = ".";
    std::string base = fs::path(a.target).stem().string();
    fs::path genDir = outDir / "generated";
    fs::create_directories(genDir);
    writeFile(genDir / "app.cpp", cg.cpp());
    std::cout << "  生成 C++ 中间代码: " << (genDir / "app.cpp").string() << " ("
              << cg.cpp().size() / 1024 << " KB)\n";

    // ── 6. 元数据 ──
    std::cout << CYAN << "▸ 第 6 步 / 8  元数据生成（version.rc / manifest / AndroidManifest.xml）"
              << RESET << std::endl;
    if (isExe) {
        cg.writeVersionRc((genDir / "version.rc").string(), base + ".exe");
        cg.writeWinManifest((genDir / "app.manifest").string());
        std::cout << "  ✓ version.rc（版权/版本/图标）  ✓ app.manifest（"
                  << (cg.wantsElevate() ? "requireAdministrator 提权" : "asInvoker 普通权限") << "）\n";
    } else {
        cg.writeAndroidManifest((genDir / "AndroidManifest.xml").string());
        std::cout << "  ✓ AndroidManifest.xml（包名 " << cfg.package << " / 版本 " << cfg.versionName << "）\n";
    }

    // ── 7+8. 构建 ──
    int rc = 0;
    if (isExe) {
        std::cout << CYAN << "▸ 第 7 步 / 8  编译 EXE" << RESET << std::endl;
        bool hasMingw = mingwAvailable();
        bool hasUpx = toolAvailable("upx");
        std::string upxT = findTool("upx");
        std::string exeOut = (outDir / (base + ".exe")).string();
        std::string cmd;
        if (hasMingw) {
            std::string gxx = findTool("g++");
            std::string wr = findTool("windres");
            // 资源（版本信息 + 提权清单）
            runCmd(wr + " " + (genDir / "version.rc").string() + " -o " + (genDir / "version.o").string(), nullptr, a.verbose);
            std::ofstream mr(genDir / "manifest.rc");
            mr << "1 24 \"" << (genDir / "app.manifest").string() << "\"\n";
            mr.close();
            runCmd(wr + " " + (genDir / "manifest.rc").string() + " -o " + (genDir / "manifest.o").string(), nullptr, a.verbose);
            cmd = gxx + " -std=c++17 -O3 -march=native -s -ffunction-sections -fdata-sections -Wl,--gc-sections -fstack-protector-all -static -static-libgcc -static-libstdc++ "
                  + (genDir / "app.cpp").string() + " " + (genDir / "version.o").string() + " " + (genDir / "manifest.o").string()
                  + " -o \"" + exeOut + "\"";
        } else {
            std::cout << YELLOW << "  警告: 未检测到 mingw-w64，使用本机 g++ 生成可执行文件（开发/测试用途）" << RESET << "\n";
            cmd = "g++ -std=c++17 -O3 -march=native -s -ffunction-sections -fdata-sections -Wl,--gc-sections -fstack-protector-all -pthread "
                  + (genDir / "app.cpp").string() + " -o \"" + exeOut + "\"";
        }
        std::string log;
        rc = runCmd(cmd, &log, a.verbose);
        if (rc != 0) {
            std::cout << RED << log << RESET;
            std::cout << RED << "错误 E9003: 链接失败（请检查 g++ / mingw 工具链）" << RESET << "\n";
            return 1;
        }
        // 8. UPX 压缩
        if (cfg.useUPX && hasUpx) {
            std::cout << CYAN << "▸ 第 8 步 / 8  UPX 二次压缩" << RESET << std::endl;
            runCmd(upxT + " --best \"" + exeOut + "\"", &log, a.verbose);
        } else {
            std::cout << CYAN << "▸ 第 8 步 / 8  压缩打包" << RESET << std::endl;
            std::cout << "  (未启用 UPX: " << (hasUpx ? "配置关闭" : "未安装 upx") << ")\n";
        }
        std::cout << GREEN << BOLD << "✓ 编译成功: " << exeOut << RESET << "\n";
        uintmax_t sz = fs::exists(exeOut) ? fs::file_size(exeOut) : 0;
        std::cout << "  大小: " << (sz / 1024) << " KB   用时: " << elapsed() << " ms\n";
    } else {
        std::cout << CYAN << "▸ 第 7 步 / 8  编译 APK（NDK）" << RESET << std::endl;
        std::string ndk;
        if (const char* h = getenv("ANDROID_NDK_HOME")) ndk = h;
        else if (runCmd("command -v ndk-build >/dev/null 2>&1") == 0) ndk = "ndk-build";
        if (ndk.empty()) {
            std::cout << RED << "错误 E9001: 未检测到 Android NDK。\n"
                      << "  请安装 NDK 并设置环境变量 ANDROID_NDK_HOME，或把 ndk-build 加入 PATH。\n"
                      << "  已生成: " << (genDir / "AndroidManifest.xml").string() << " 与 C++ 源码，可手动用 NDK 构建。" << RESET << "\n";
            return 1;
        }
        fs::path apkDir = outDir / "apk";
        fs::create_directories(apkDir / "jni");
        writeFile(apkDir / "jni" / "app.cpp", cg.cpp());
        std::ofstream mk(apkDir / "jni" / "Android.mk");
        mk << "LOCAL_PATH := $(call my-dir)\ninclude $(CLEAR_VARS)\n"
              "LOCAL_MODULE := etcapp\nLOCAL_SRC_FILES := app.cpp\n"
              "LOCAL_CPPFLAGS := -std=c++17 -O3 -fstack-protector-all\nLOCAL_LDLIBS := -llog\n"
              "include $(BUILD_SHARED_LIBRARY)\n";
        mk.close();
        std::ofstream amk(apkDir / "jni" / "Application.mk");
        amk << "APP_ABI := arm64-v8a armeabi-v7a\nAPP_PLATFORM := android-" << cfg.minSdk << "\n";
        amk.close();
        std::string log;
        rc = runCmd("ndk-build NDK_PROJECT_PATH=" + apkDir.string() + " APP_BUILD_SCRIPT=" +
                    (apkDir / "jni" / "Android.mk").string() + " NDK_APPLICATION_MK=" +
                    (apkDir / "jni" / "Application.mk").string(), &log, a.verbose);
        if (rc != 0) {
            std::cout << RED << log << RESET << "\n错误 E9003: NDK 构建失败\n";
            return 1;
        }
        std::cout << GREEN << BOLD << "✓ NDK 编译成功: " << (apkDir / "libs").string() << "/arm64-v8a/libetcapp.so" << RESET << "\n";
        std::cout << "  (APK 打包（aapt2/d8/apksigner）为高级集成，请在 Android Studio 中完成最终签名打包)\n";
    }
    std::cout << GREEN << "✓ 编译完成，用时 " << elapsed() << " ms" << RESET << "\n";
    return 0;
}
