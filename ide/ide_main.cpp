// ============================================================
//  ETC IDE — 高仿 Visual Studio 的中文 ETC Lang 集成开发环境
//  作者: etc  (ET 协会)   版本: 1.0
//  纯 Win32 实现，内嵌 ETC 编译器模块（词法/语法/语义）做编辑器检查
//  编译: x86_64-w64-mingw32-g++ -std=c++17 -mwindows ide/ide_main.cpp
//        compiler/{lexer,parser,semantic,codegen}.cpp -lcomctl32 -lcomdlg32 ...
// ============================================================
#define _WIN32_WINNT 0x0601
#define _WIN32_IE 0x0600
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <shlobj.h>

#include <string>
#include <vector>
#include <map>
#include <set>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cwctype>

#include "lexer.h"
#include "parser.h"
#include "semantic.h"

// ---------------- 编译器模块命名空间 ----------------
using namespace etc;

// ============================================================
//  基础工具
// ============================================================
static std::wstring Utf8ToW(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
static std::string WToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
static std::wstring ExeDir() {
    wchar_t buf[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p = buf;
    size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : p.substr(0, pos);
}
static std::wstring FileName(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}
static std::wstring FileExt(const std::wstring& path) {
    size_t pos = path.find_last_of(L'.');
    if (pos == std::wstring::npos) return L"";
    std::wstring e = path.substr(pos);
    std::transform(e.begin(), e.end(), e.begin(), ::towlower);
    return e;
}
static bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool DirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}
static bool ReadFileW(const std::wstring& path, std::wstring& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(h, nullptr);
    std::string s(size, 0);
    DWORD rd = 0;
    BOOL ok = ReadFile(h, &s[0], size, &rd, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    s.resize(rd);
    // 去 BOM
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
        s = s.substr(3);
    out = Utf8ToW(s);
    return true;
}
static bool WriteFileW(const std::wstring& path, const std::wstring& content) {
    std::string s = WToUtf8(content);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    BOOL ok = WriteFile(h, s.data(), (DWORD)s.size(), &wr, nullptr);
    CloseHandle(h);
    return ok && wr == (DWORD)s.size();
}
// ============================================================
//  VS Dark+ 配色
// ============================================================
#define C_BG        RGB(0x1E, 0x1E, 0x1E)
#define C_BG_EDIT   RGB(0x1E, 0x1E, 0x1E)
#define C_GUTTER    RGB(0x21, 0x21, 0x21)
#define C_GUTTER_TX RGB(0x85, 0x85, 0x85)
#define C_BORDER    RGB(0x33, 0x33, 0x33)
#define C_CURLINE   RGB(0x26, 0x4F, 0x78)
#define C_SEL       RGB(0x26, 0x4F, 0x78)
#define C_TEXT      RGB(0xD4, 0xD4, 0xD4)
#define C_KW        RGB(0x56, 0x9C, 0xD6)
#define C_STR       RGB(0xCE, 0x91, 0x78)
#define C_CMT       RGB(0x6A, 0x99, 0x55)
#define C_TYPE      RGB(0x4E, 0xC9, 0xB0)
#define C_NUM       RGB(0xB5, 0xCE, 0x89)
#define C_ANN       RGB(0xC5, 0x86, 0xC0)
#define C_OP        RGB(0xD4, 0xD4, 0xD4)
#define C_ERR       RGB(0xF4, 0x87, 0x87)
#define C_PANEL_BG  RGB(0x25, 0x25, 0x25)
#define C_PANEL_TX  RGB(0xCC, 0xCC, 0xCC)
#define C_BTN_BG    RGB(0x3A, 0x3D, 0x41)
#define C_BTN_TX    RGB(0xCC, 0xCC, 0xCC)

// ============================================================
//  显示用词法着色（基于行扫描，字符级精确，支持中文）
// ============================================================
enum TokColor { T_DEF, T_KW, T_STR, T_CMT, T_NUM, T_TYPE, T_ANN, T_OP };
struct Run { int col, len; TokColor c; };

static const std::set<std::wstring>& KwSet() {
    static std::set<std::wstring> s = {
        L"fn", L"let", L"var", L"const", L"own", L"move", L"share", L"ptr", L"ref", L"weak",
        L"if", L"else", L"elif", L"loop", L"while", L"for", L"break", L"continue", L"return",
        L"class", L"struct", L"enum", L"union", L"trait", L"impl", L"new", L"del",
        L"true", L"false", L"null", L"nil", L"try", L"catch", L"throw", L"finally",
        L"as", L"is", L"in", L"then", L"where", L"match", L"select",
        L"alloc", L"free", L"load", L"store", L"cast", L"sizeof", L"alignof", L"typeof",
        L"pub", L"priv", L"prot", L"static", L"inline", L"extern", L"virtual", L"override",
        L"import", L"export", L"from", L"module", L"package", L"use",
        L"unsafe", L"pure", L"volatile", L"atomic", L"align", L"packed",
        L"thread", L"spawn", L"join", L"mutex", L"lock", L"channel", L"send", L"recv",
        L"vec2", L"vec4", L"vec8", L"vec16",
        L"compile_time", L"macro", L"type_info", L"this", L"self"
    };
    return s;
}
static const std::set<std::wstring>& TypeSet() {
    static std::set<std::wstring> s = {
        L"i8", L"i16", L"i32", L"i64", L"u8", L"u16", L"u32", L"u64",
        L"f32", L"f64", L"str", L"bool", L"char", L"byte", L"void",
        L"vec2<f32>", L"vec4<f32>", L"vec4<i32>", L"vec2<f64>", L"vec8<f32>", L"vec8<i32>",
        L"vec16<f32>", L"vec16<i32>"
    };
    return s;
}
static bool IsKw(const std::wstring& w) { return KwSet().count(w) > 0; }
static bool IsType(const std::wstring& w) { return TypeSet().count(w) > 0; }
static bool IsNumStart(wchar_t c) { return c >= L'0' && c <= L'9'; }
static bool IsIdentStart(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'_' || c >= 0x80;
}
static bool IsIdentPart(wchar_t c) {
    return IsIdentStart(c) || (c >= L'0' && c <= L'9');
}

// 逐行扫描着色；inBlockComment 为跨行块注释状态
struct LineRuns { std::vector<Run> runs; };
static LineRuns ScanLine(const std::wstring& line, bool& inBlockComment) {
    LineRuns out;
    int i = 0, n = (int)line.size();
    while (i < n) {
        wchar_t c;
        // 块注释（跨行状态）
        if (inBlockComment) {
            int s = i;
            size_t end = line.find(L"*/", i);
            if (end == std::wstring::npos) { out.runs.push_back({ s, n - s, T_CMT }); return out; }
            i = (int)end + 2;
            inBlockComment = false;
            out.runs.push_back({ s, i - s, T_CMT });
            continue;
        }
        c = line[i];
        // 行注释
        if (i + 1 < n && c == L'/' && line[i + 1] == L'/') {
            out.runs.push_back({ i, n - i, T_CMT });
            break;
        }
        c = line[i];
        // 块注释开始
        if (i + 1 < n && c == L'/' && line[i + 1] == L'*') {
            int s = i;
            size_t end = line.find(L"*/", i + 2);
            if (end == std::wstring::npos) { inBlockComment = true; out.runs.push_back({ s, n - s, T_CMT }); return out; }
            i = (int)end + 2;
            out.runs.push_back({ s, i - s, T_CMT });
            continue;
        }
        c = line[i];
        // 字符串
        if (c == L'"' || c == L'\'') {
            wchar_t q = c;
            int s = i;
            i++;
            while (i < n) {
                if (line[i] == L'\\' && i + 1 < n) { i += 2; continue; }
                if (line[i] == q) { i++; break; }
                i++;
            }
            out.runs.push_back({ s, i - s, T_STR });
            continue;
        }
        c = line[i];
        // 数字
        if (IsNumStart(c)) {
            int s = i;
            while (i < n && (IsNumStart(line[i]) || line[i] == L'_' || line[i] == L'.' ||
                   line[i] == L'x' || line[i] == L'X' || line[i] == L'b' || line[i] == L'o' ||
                   line[i] == L'a' || line[i] == L'c' || line[i] == L'd' || line[i] == L'e' ||
                   line[i] == L'f' || line[i] == L'p' || line[i] == L'E' || line[i] == L'F'))
                i++;
            out.runs.push_back({ s, i - s, T_NUM });
            continue;
        }
        c = line[i];
        // 注解 @xxx
        if (c == L'@') {
            int s = i;
            i++;
            while (i < n && (IsIdentPart(line[i]) || line[i] == L':' || line[i] == L'.'))
                i++;
            out.runs.push_back({ s, i - s, T_ANN });
            continue;
        }
        c = line[i];
        // 标识符
        if (IsIdentStart(c)) {
            int s = i;
            while (i < n && IsIdentPart(line[i])) i++;
            std::wstring w = line.substr(s, i - s);
            TokColor tc = T_DEF;
            if (IsKw(w)) tc = T_KW;
            else if (IsType(w)) tc = T_TYPE;
            out.runs.push_back({ s, i - s, tc });
            continue;
        }
        // 空白与其他
        i++;
    }
    return out;
}

// ============================================================
//  编辑器状态
// ============================================================
struct EditorState {
    std::wstring text;                 // 全文（\n 分行）
    std::vector<std::wstring> lines;   // 行缓存
    std::vector<LineRuns> lineRuns;    // 每行着色
    bool inBlockComment = false;
    int curLine = 0, curCol = 0;
    long selStart = -1;                // 选择起点（全局字符下标），-1 无选择
    int scrollY = 0, scrollX = 0;
    std::wstring path;
    bool dirty = false;
    std::vector<int> errLines;         // 语法错误行（0 基）
    std::vector<std::wstring> undoStack;
    std::vector<std::wstring> redoStack;
    int tabW = 4;

    void RebuildLines() {
        lines.clear();
        std::wstring cur;
        for (wchar_t c : text) {
            if (c == L'\n') { lines.push_back(cur); cur.clear(); }
            else cur += c;
        }
        lines.push_back(cur);
        if (lines.empty()) lines.push_back(L"");
        // 重扫着色
        lineRuns.clear();
        inBlockComment = false;
        for (auto& ln : lines) {
            lineRuns.push_back(ScanLine(ln, inBlockComment));
        }
        if (curLine >= (int)lines.size()) curLine = (int)lines.size() - 1;
        if (curLine < 0) curLine = 0;
        if (curCol > (int)lines[curLine].size()) curCol = (int)lines[curLine].size();
    }
    // 字符下标 <-> 行列
    long LineColToIdx(int line, int col) const {
        long idx = 0;
        int L = (int)lines.size();
        for (int i = 0; i < L && i < line; i++) idx += (long)lines[i].size() + 1;
        idx += col;
        return idx;
    }
    void IdxToLineCol(long idx, int& line, int& col) const {
        long pos = 0;
        line = 0; col = 0;
        int L = (int)lines.size();
        for (int i = 0; i < L; i++) {
            long next = pos + (long)lines[i].size() + 1;
            if (idx < next) { line = i; col = (int)(idx - pos); return; }
            pos = next;
        }
        line = L > 0 ? L - 1 : 0;
        col = L > 0 ? (int)lines[line].size() : 0;
    }
    void InsertAt(long idx, const std::wstring& s) {
        if (idx < 0) idx = 0;
        if (idx > (long)text.size()) idx = (long)text.size();
        text.insert(idx, s);
        RebuildLines();
        dirty = true;
    }
    void DeleteRange(long a, long b) {
        if (a > b) std::swap(a, b);
        if (a < 0) a = 0;
        if (b > (long)text.size()) b = (long)text.size();
        if (a == b) return;
        text.erase(a, b - a);
        RebuildLines();
        dirty = true;
    }
    void PushUndo() {
        undoStack.push_back(text);
        if (undoStack.size() > 200) undoStack.erase(undoStack.begin());
        redoStack.clear();
    }
    void SetText(const std::wstring& t) {
        text = t;
        RebuildLines();
        curLine = 0; curCol = 0; selStart = -1;
        dirty = false;
    }
};

// ============================================================
//  编辑器控件 (ETCEdit)
// ============================================================
static HFONT g_fontEdit = nullptr, g_fontUi = nullptr;
static const wchar_t* kEditCls = L"ETCEdit";
static const wchar_t* kConsoleCls = L"ETCConsole";

static COLORREF TokToColor(TokColor c) {
    switch (c) {
        case T_KW: return C_KW;
        case T_STR: return C_STR;
        case T_CMT: return C_CMT;
        case T_NUM: return C_NUM;
        case T_TYPE: return C_TYPE;
        case T_ANN: return C_ANN;
        default: return C_TEXT;
    }
}

static void InitFonts() {
    if (g_fontEdit) return;
    g_fontEdit = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN, L"Consolas");
    g_fontUi = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
}

static EditorState* GetEd(HWND hwnd) {
    return (EditorState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
}

// 编辑器绘制
static void EdPaint(HWND hwnd, HDC hdc) {
    EditorState* ed = GetEd(hwnd);
    RECT rc; GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(mem, bmp);
    HFONT of = (HFONT)SelectObject(mem, g_fontEdit);

    // 背景
    HBRUSH bg = CreateSolidBrush(C_BG_EDIT);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    TEXTMETRICW tm;
    GetTextMetricsW(mem, &tm);
    int lineH = tm.tmHeight + tm.tmExternalLeading + 2;
    if (lineH < 18) lineH = 18;
    int gutterW = 54;

    // 当前行高亮
    int curTop = (ed->curLine - ed->scrollY / lineH) * lineH;
    RECT curRc = { gutterW, curTop, rc.right, curTop + lineH };
    if (curRc.top >= 0 && curRc.bottom <= rc.bottom) {
        HBRUSH hb = CreateSolidBrush(C_CURLINE);
        FillRect(mem, &curRc, hb);
        DeleteObject(hb);
    }

    // 选择区高亮（按字符区间计算每行）
    if (ed->selStart >= 0) {
        long selA = ed->selStart;
        long selB = ed->LineColToIdx(ed->curLine, ed->curCol);
        if (selA > selB) std::swap(selA, selB);
        int L = (int)ed->lines.size();
        for (int li = 0; li < L; li++) {
            long lineStart = ed->LineColToIdx(li, 0);
            long lineEnd = lineStart + (long)ed->lines[li].size();
            if (selB <= lineStart || selA > lineEnd) continue;
            long a = std::max(selA, lineStart);
            long b = std::min(selB, lineEnd);
            int ca = (int)(a - lineStart);
            int cb = (int)(b - lineStart);
            int top = li * lineH - ed->scrollY;
            if (top + lineH < 0 || top > rc.bottom) continue;
            HDC tdc = mem;
            SelectObject(tdc, g_fontEdit);
            int x0 = gutterW;
            std::wstring pre = ed->lines[li].substr(0, ca);
            SIZE sz;
            GetTextExtentPoint32W(tdc, pre.c_str(), (int)pre.size(), &sz);
            x0 += sz.cx - ed->scrollX;
            std::wstring seg = ed->lines[li].substr(ca, cb - ca);
            SIZE sz2;
            GetTextExtentPoint32W(tdc, seg.c_str(), (int)seg.size(), &sz2);
            RECT sRc = { x0, top, x0 + sz2.cx, top + lineH };
            HBRUSH sb = CreateSolidBrush(C_SEL);
            FillRect(mem, &sRc, sb);
            DeleteObject(sb);
        }
    }

    // 行号栏背景 + 文本
    RECT grc = { 0, 0, gutterW, rc.bottom };
    HBRUSH gbg = CreateSolidBrush(C_GUTTER);
    FillRect(mem, &grc, gbg);
    DeleteObject(gbg);
    {
        HPEN pen = CreatePen(PS_SOLID, 1, C_BORDER);
        HGDIOBJ po = SelectObject(mem, pen);
        MoveToEx(mem, gutterW - 1, 0, nullptr);
        LineTo(mem, gutterW - 1, rc.bottom);
        SelectObject(mem, po);
        DeleteObject(pen);
    }

    // 行号
    SetTextColor(mem, C_GUTTER_TX);
    SetBkMode(mem, TRANSPARENT);
    int first = ed->scrollY / lineH;
    int last = first + rc.bottom / lineH + 1;
    int L = (int)ed->lines.size();
    bool hasErr = false;
    for (int li = first; li < L && li <= last; li++) {
        int top = li * lineH - ed->scrollY;
        std::wstring num = std::to_wstring(li + 1);
        SIZE sz;
        GetTextExtentPoint32W(mem, num.c_str(), (int)num.size(), &sz);
        int tx = gutterW - sz.cx - 8;
        if (li == ed->curLine) {
            SetTextColor(mem, RGB(0xFF, 0xFF, 0xFF));
            TextOutW(mem, tx, top, num.c_str(), (int)num.size());
            SetTextColor(mem, C_GUTTER_TX);
        } else {
            TextOutW(mem, tx, top, num.c_str(), (int)num.size());
        }
        // 错误行红条
        for (int e : ed->errLines) {
            if (e == li) {
                HBRUSH eb = CreateSolidBrush(C_ERR);
                RECT er = { 0, top + 2, 4, top + lineH - 2 };
                FillRect(mem, &er, eb);
                DeleteObject(eb);
                hasErr = true;
                break;
            }
        }
    }
    (void)hasErr;

    // 文本
    SetBkMode(mem, TRANSPARENT);
    for (int li = first; li < L && li <= last; li++) {
        int top = li * lineH - ed->scrollY;
        if (top + lineH < 0) continue;
        if (top > rc.bottom) break;
        const std::wstring& ln = ed->lines[li];
        const LineRuns& lr = ed->lineRuns[li];
        int x = gutterW - ed->scrollX;
        for (auto& r : lr.runs) {
            if (r.col + r.len <= 0) { x += (int)r.len; continue; }
            std::wstring seg = ln.substr(r.col, r.len);
            SIZE sz;
            GetTextExtentPoint32W(mem, seg.c_str(), (int)seg.size(), &sz);
            if (x + sz.cx > gutterW && x < rc.right) {
                SetTextColor(mem, TokToColor(r.c));
                TextOutW(mem, x, top, seg.c_str(), (int)seg.size());
            }
            x += sz.cx;
        }
    }

    // 光标
    if (GetFocus() == hwnd && GetCaretPos) {
        // 计算光标位置
        int cx = gutterW - ed->scrollX;
        std::wstring pre = ed->lines[ed->curLine].substr(0, ed->curCol);
        SIZE sz;
        GetTextExtentPoint32W(mem, pre.c_str(), (int)pre.size(), &sz);
        cx += sz.cx;
        int cy = ed->curLine * lineH - ed->scrollY;
        RECT cr = { cx, cy, cx + 2, cy + lineH };
        HBRUSH cb = CreateSolidBrush(RGB(0xAE, 0xAF, 0xAD));
        FillRect(mem, &cr, cb);
        DeleteObject(cb);
    }

    // 未保存标记
    if (ed->dirty) {
        SetTextColor(mem, RGB(0xE5, 0xB5, 0x67));
        SetBkMode(mem, TRANSPARENT);
        TextOutW(mem, rc.right - 60, 2, L"● 未保存", 6);
    }

    SelectObject(mem, of);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static void EdEnsureCaretVisible(HWND hwnd) {
    EditorState* ed = GetEd(hwnd);
    RECT rc; GetClientRect(hwnd, &rc);
    HDC dc = GetDC(hwnd);
    SelectObject(dc, g_fontEdit);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    int lineH = tm.tmHeight + tm.tmExternalLeading + 2;
    if (lineH < 18) lineH = 18;
    int gutterW = 54;
    // 垂直
    int top = ed->curLine * lineH;
    if (top < ed->scrollY) ed->scrollY = top;
    if (top + lineH > ed->scrollY + rc.bottom) ed->scrollY = top + lineH - rc.bottom;
    if (ed->scrollY < 0) ed->scrollY = 0;
    // 水平
    std::wstring pre = ed->lines[ed->curLine].substr(0, ed->curCol);
    SIZE sz;
    GetTextExtentPoint32W(dc, pre.c_str(), (int)pre.size(), &sz);
    int cx = gutterW + sz.cx;
    if (cx < ed->scrollX + gutterW) ed->scrollX = cx - gutterW;
    if (cx > ed->scrollX + rc.right - 20) ed->scrollX = cx - rc.right + 20;
    if (ed->scrollX < 0) ed->scrollX = 0;
    ReleaseDC(hwnd, dc);
}

static void EdMoveCaret(HWND hwnd, int line, int col, bool selExtend) {
    EditorState* ed = GetEd(hwnd);
    if (selExtend) {
        if (ed->selStart < 0) ed->selStart = ed->LineColToIdx(ed->curLine, ed->curCol);
    } else {
        ed->selStart = -1;
    }
    if (line < 0) line = 0;
    if (line >= (int)ed->lines.size()) line = (int)ed->lines.size() - 1;
    if (col < 0) col = 0;
    if (col > (int)ed->lines[line].size()) col = (int)ed->lines[line].size();
    ed->curLine = line;
    ed->curCol = col;
    EdEnsureCaretVisible(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void EdInsertAtCaret(HWND hwnd, const std::wstring& s) {
    EditorState* ed = GetEd(hwnd);
    long caret = ed->LineColToIdx(ed->curLine, ed->curCol);
    if (ed->selStart >= 0) {
        long a = ed->selStart, b = caret;
        if (a > b) std::swap(a, b);
        ed->PushUndo();
        ed->DeleteRange(a, b);
        caret = a;
    } else {
        ed->PushUndo();
    }
    ed->InsertAt(caret, s);
    int line, col;
    ed->IdxToLineCol(caret + (long)s.size(), line, col);
    ed->curLine = line; ed->curCol = col;
    ed->selStart = -1;
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void EdBackspace(HWND hwnd) {
    EditorState* ed = GetEd(hwnd);
    long caret = ed->LineColToIdx(ed->curLine, ed->curCol);
    if (ed->selStart >= 0) {
        long a = ed->selStart, b = caret;
        if (a > b) std::swap(a, b);
        ed->PushUndo();
        ed->DeleteRange(a, b);
        ed->selStart = -1;
        int line, col;
        ed->IdxToLineCol(a, line, col);
        ed->curLine = line; ed->curCol = col;
    } else if (caret > 0) {
        ed->PushUndo();
        ed->DeleteRange(caret - 1, caret);
        int line, col;
        ed->IdxToLineCol(caret - 1, line, col);
        ed->curLine = line; ed->curCol = col;
    }
    EdEnsureCaretVisible(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void EdDeleteFwd(HWND hwnd) {
    EditorState* ed = GetEd(hwnd);
    long caret = ed->LineColToIdx(ed->curLine, ed->curCol);
    if (ed->selStart >= 0) {
        long a = ed->selStart, b = caret;
        if (a > b) std::swap(a, b);
        ed->PushUndo();
        ed->DeleteRange(a, b);
        ed->selStart = -1;
        int line, col;
        ed->IdxToLineCol(a, line, col);
        ed->curLine = line; ed->curCol = col;
    } else if (caret < (long)ed->text.size()) {
        ed->PushUndo();
        ed->DeleteRange(caret, caret + 1);
    }
    EdEnsureCaretVisible(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static std::wstring EdGetSelection(HWND hwnd) {
    EditorState* ed = GetEd(hwnd);
    long caret = ed->LineColToIdx(ed->curLine, ed->curCol);
    if (ed->selStart < 0) return L"";
    long a = ed->selStart, b = caret;
    if (a > b) std::swap(a, b);
    return ed->text.substr(a, b - a);
}
// ============================================================
//  编辑器窗口过程 + 控制台控件 + 主窗口框架（第二部分）
// ============================================================

static void EdClipboardOp(HWND hwnd, bool copy) {
    EditorState* ed = GetEd(hwnd);
    if (copy) {
        std::wstring sel = EdGetSelection(hwnd);
        if (sel.empty()) return;
        if (OpenClipboard(hwnd)) {
            EmptyClipboard();
            HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (sel.size() + 1) * sizeof(wchar_t));
            if (hg) {
                wchar_t* dst = (wchar_t*)GlobalLock(hg);
                wcscpy_s(dst, sel.size() + 1, sel.c_str());
                GlobalUnlock(hg);
                SetClipboardData(CF_UNICODETEXT, hg);
            }
            CloseClipboard();
        }
    } else {
        // 粘贴
        if (!OpenClipboard(hwnd)) return;
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h) {
            const wchar_t* s = (const wchar_t*)GlobalLock(h);
            if (s) {
                EdInsertAtCaret(hwnd, s);
                GlobalUnlock(h);
            }
        }
        CloseClipboard();
    }
}

static void EdSelectAll(HWND hwnd) {
    EditorState* ed = GetEd(hwnd);
    ed->selStart = 0;
    ed->curLine = (int)ed->lines.size() - 1;
    ed->curCol = (int)ed->lines.back().size();
    InvalidateRect(hwnd, nullptr, FALSE);
}

// 编辑器窗口过程
LRESULT CALLBACK EdWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    EditorState* ed = GetEd(hwnd);
    switch (msg) {
        case WM_CREATE: {
            EditorState* s = new EditorState();
            s->SetText(L"");
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)s);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            EdPaint(hwnd, dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SETFOCUS:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_KILLFOCUS:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            SetCapture(hwnd);
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            HDC dc = GetDC(hwnd);
            SelectObject(dc, g_fontEdit);
            TEXTMETRICW tm;
            GetTextMetricsW(dc, &tm);
            int lineH = tm.tmHeight + tm.tmExternalLeading + 2;
            if (lineH < 18) lineH = 18;
            int li = (pt.y + ed->scrollY) / lineH;
            if (li < 0) li = 0;
            if (li >= (int)ed->lines.size()) li = (int)ed->lines.size() - 1;
            int col = 0;
            if (pt.x > 54) {
                std::wstring ln = ed->lines[li];
                int x = 54 - ed->scrollX;
                for (int ci = 0; ci < (int)ln.size(); ci++) {
                    std::wstring ch(1, ln[ci]);
                    SIZE sz;
                    GetTextExtentPoint32W(dc, ch.c_str(), 1, &sz);
                    if (x + sz.cx / 2 > pt.x) { col = ci; break; }
                    x += sz.cx;
                    col = ci + 1;
                }
            }
            ReleaseDC(hwnd, dc);
            bool extend = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            EdMoveCaret(hwnd, li, col, extend);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if ((wp & MK_LBUTTON) && GetCapture() == hwnd) {
                POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                HDC dc = GetDC(hwnd);
                SelectObject(dc, g_fontEdit);
                TEXTMETRICW tm;
                GetTextMetricsW(dc, &tm);
                int lineH = tm.tmHeight + tm.tmExternalLeading + 2;
                if (lineH < 18) lineH = 18;
                int li = (pt.y + ed->scrollY) / lineH;
                if (li < 0) li = 0;
                if (li >= (int)ed->lines.size()) li = (int)ed->lines.size() - 1;
                int col = 0;
                if (pt.x > 54) {
                    std::wstring ln = ed->lines[li];
                    int x = 54 - ed->scrollX;
                    for (int ci = 0; ci < (int)ln.size(); ci++) {
                        std::wstring ch(1, ln[ci]);
                        SIZE sz;
                        GetTextExtentPoint32W(dc, ch.c_str(), 1, &sz);
                        if (x + sz.cx / 2 > pt.x) { col = ci; break; }
                        x += sz.cx;
                        col = ci + 1;
                    }
                }
                ReleaseDC(hwnd, dc);
                if (ed->selStart < 0) ed->selStart = ed->LineColToIdx(ed->curLine, ed->curCol);
                EdMoveCaret(hwnd, li, col, true);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            HDC dc = GetDC(hwnd);
            SelectObject(dc, g_fontEdit);
            TEXTMETRICW tm;
            GetTextMetricsW(dc, &tm);
            int lineH = tm.tmHeight + tm.tmExternalLeading + 2;
            if (lineH < 18) lineH = 18;
            int li = (pt.y + ed->scrollY) / lineH;
            if (li < 0 || li >= (int)ed->lines.size()) { ReleaseDC(hwnd, dc); return 0; }
            std::wstring ln = ed->lines[li];
            int col = 0;
            if (pt.x > 54) {
                int x = 54 - ed->scrollX;
                for (int ci = 0; ci < (int)ln.size(); ci++) {
                    std::wstring ch(1, ln[ci]);
                    SIZE sz;
                    GetTextExtentPoint32W(dc, ch.c_str(), 1, &sz);
                    if (x + sz.cx / 2 > pt.x) { col = ci; break; }
                    x += sz.cx;
                    col = ci + 1;
                }
            }
            ReleaseDC(hwnd, dc);
            int a = col;
            while (a > 0 && IsIdentPart(ln[a - 1])) a--;
            int b = col;
            while (b < (int)ln.size() && IsIdentPart(ln[b])) b++;
            ed->selStart = ed->LineColToIdx(li, a);
            ed->curLine = li;
            ed->curCol = b;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            int z = GET_WHEEL_DELTA_WPARAM(wp);
            ed->scrollY -= z * 3 / 120 * 2;
            if (ed->scrollY < 0) ed->scrollY = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_CHAR: {
            if (wp == 8) { EdBackspace(hwnd); return 0; }
            if (wp == 9) { EdInsertAtCaret(hwnd, L"    "); return 0; }
            if (wp == 13) { EdInsertAtCaret(hwnd, L"\n"); return 0; }
            if (wp == 27) return 0;
            wchar_t c = (wchar_t)wp;
            if (c >= 32) EdInsertAtCaret(hwnd, std::wstring(1, c));
            return 0;
        }
        case WM_IME_COMPOSITION: {
            if (lp & GCS_RESULTSTR) {
                HIMC himc = ImmGetContext(hwnd);
                if (himc) {
                    LONG n = ImmGetCompositionStringW(himc, GCS_RESULTSTR, nullptr, 0);
                    if (n > 0) {
                        std::wstring s(n / 2, 0);
                        ImmGetCompositionStringW(himc, GCS_RESULTSTR, &s[0], n);
                        EdInsertAtCaret(hwnd, s);
                    }
                    ImmReleaseContext(hwnd, himc);
                }
            }
            return 0;
        }
        case WM_KEYDOWN: {
            int vk = (int)wp;
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (ctrl && vk == 'S') { SendMessageW(GetParent(hwnd), WM_APP + 11, 0, 0); return 0; }
            if (ctrl && vk == 'A') { EdSelectAll(hwnd); return 0; }
            if (ctrl && vk == 'C') { EdClipboardOp(hwnd, true); return 0; }
            if (ctrl && vk == 'X') {
                std::wstring sel = EdGetSelection(hwnd);
                EdClipboardOp(hwnd, true);
                if (!sel.empty()) EdDeleteFwd(hwnd);
                return 0;
            }
            if (ctrl && vk == 'V') { EdClipboardOp(hwnd, false); return 0; }
            if (ctrl && vk == 'Z') {
                if (!ed->undoStack.empty()) {
                    ed->redoStack.push_back(ed->text);
                    ed->SetText(ed->undoStack.back());
                    ed->undoStack.pop_back();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            if (ctrl && vk == 'Y') {
                if (!ed->redoStack.empty()) {
                    ed->PushUndo();
                    ed->SetText(ed->redoStack.back());
                    ed->redoStack.pop_back();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            if (vk == VK_LEFT) {
                int col = ed->curCol;
                if (ctrl) {
                    while (col > 0 && !IsIdentPart(ed->lines[ed->curLine][col - 1])) col--;
                    while (col > 0 && IsIdentPart(ed->lines[ed->curLine][col - 1])) col--;
                } else col--;
                if (col < 0) {
                    if (ed->curLine > 0) { ed->curLine--; col = (int)ed->lines[ed->curLine].size(); }
                    else col = 0;
                }
                EdMoveCaret(hwnd, ed->curLine, col, shift);
                return 0;
            }
            if (vk == VK_RIGHT) {
                int col = ed->curCol;
                if (ctrl) {
                    int n = (int)ed->lines[ed->curLine].size();
                    while (col < n && !IsIdentPart(ed->lines[ed->curLine][col])) col++;
                    while (col < n && IsIdentPart(ed->lines[ed->curLine][col])) col++;
                } else col++;
                if (col > (int)ed->lines[ed->curLine].size()) {
                    if (ed->curLine + 1 < (int)ed->lines.size()) { ed->curLine++; col = 0; }
                    else col = (int)ed->lines[ed->curLine].size();
                }
                EdMoveCaret(hwnd, ed->curLine, col, shift);
                return 0;
            }
            if (vk == VK_UP) { EdMoveCaret(hwnd, ed->curLine - 1, ed->curCol, shift); return 0; }
            if (vk == VK_DOWN) { EdMoveCaret(hwnd, ed->curLine + 1, ed->curCol, shift); return 0; }
            if (vk == VK_HOME) {
                if (ctrl) EdMoveCaret(hwnd, 0, 0, shift);
                else EdMoveCaret(hwnd, ed->curLine, 0, shift);
                return 0;
            }
            if (vk == VK_END) {
                if (ctrl) EdMoveCaret(hwnd, (int)ed->lines.size() - 1, (int)ed->lines.back().size(), shift);
                else EdMoveCaret(hwnd, ed->curLine, (int)ed->lines[ed->curLine].size(), shift);
                return 0;
            }
            if (vk == VK_BACK) { EdBackspace(hwnd); return 0; }
            if (vk == VK_DELETE) { EdDeleteFwd(hwnd); return 0; }
            if (vk == VK_RETURN) { EdInsertAtCaret(hwnd, L"\n"); return 0; }
            if (vk == VK_TAB) { EdInsertAtCaret(hwnd, L"    "); return 0; }
            return 0;
        }
        case WM_GETDLGCODE: return DLGC_WANTALLKEYS;
        case WM_NCHITTEST: {
            LRESULT r = DefWindowProcW(hwnd, msg, wp, lp);
            return r;
        }
        case WM_DESTROY: {
            delete GetEd(hwnd);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ============================================================
//  控制台控件（只读输出，彩色行）
// ============================================================
struct ConsoleState {
    std::vector<std::pair<std::wstring, COLORREF>> lines;
    int scrollY = 0;
    bool autoScroll = true;
    void Clear() { lines.clear(); scrollY = 0; }
    void Append(const std::wstring& s, COLORREF c) {
        // 按行拆
        std::wstring cur;
        for (wchar_t ch : s) {
            if (ch == L'\n') { lines.push_back({ cur, c }); cur.clear(); }
            else if (ch != L'\r') cur += ch;
        }
        if (!cur.empty()) lines.push_back({ cur, c });
        if (lines.size() > 20000) lines.erase(lines.begin(), lines.begin() + (lines.size() - 20000));
    }
};

static void ConsolePaint(HWND hwnd, HDC hdc) {
    ConsoleState* cs = (ConsoleState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    RECT rc; GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ ob = SelectObject(mem, bmp);
    HFONT of = (HFONT)SelectObject(mem, g_fontUi);
    HBRUSH bg = CreateSolidBrush(C_BG);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);
    TEXTMETRICW tm;
    GetTextMetricsW(mem, &tm);
    int lineH = tm.tmHeight + tm.tmExternalLeading + 2;
    if (lineH < 16) lineH = 16;
    SetBkMode(mem, TRANSPARENT);
    int first = cs->scrollY / lineH;
    int last = first + rc.bottom / lineH + 1;
    for (int i = first; i < (int)cs->lines.size() && i <= last; i++) {
        int top = i * lineH - cs->scrollY;
        SetTextColor(mem, cs->lines[i].second);
        TextOutW(mem, 4, top, cs->lines[i].first.c_str(), (int)cs->lines[i].first.size());
    }
    SelectObject(mem, of);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
}

LRESULT CALLBACK ConsoleWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            ConsoleState* s = new ConsoleState();
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)s);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            ConsolePaint(hwnd, dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEWHEEL: {
            ConsoleState* cs = (ConsoleState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            int z = GET_WHEEL_DELTA_WPARAM(wp);
            cs->scrollY -= z * 3 / 120 * 2;
            if (cs->scrollY < 0) cs->scrollY = 0;
            if (cs->scrollY > (int)cs->lines.size() * 16) cs->scrollY = (int)cs->lines.size() * 16;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_DESTROY: {
            delete (ConsoleState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ============================================================
//  宽字符控件辅助（mingw 兼容）
// ============================================================
static HTREEITEM TV_Insert(HWND h, HTREEITEM parent, LPWSTR text) {
    TVINSERTSTRUCTW is = {0};
    is.hParent = parent;
    is.hInsertAfter = TVI_LAST;
    is.item.mask = TVIF_TEXT;
    is.item.pszText = text;
    return (HTREEITEM)SendMessageW(h, TVM_INSERTITEMW, 0, (LPARAM)&is);
}
static int LV_InsertItem(HWND h, const LVITEMW* it) {
    return (int)SendMessageW(h, LVM_INSERTITEMW, 0, (LPARAM)it);
}
static void LV_SetItemText(HWND h, int i, int sub, LPWSTR s) {
    LVITEMW it = {0};
    it.mask = LVIF_TEXT;
    it.iItem = i;
    it.iSubItem = sub;
    it.pszText = s;
    SendMessageW(h, LVM_SETITEMTEXTW, 0, (LPARAM)&it);
}

// ============================================================
//  全局状态
// ============================================================
// 前向声明（跨文件分段定义）
static void RefreshTree();
static void ReloadProps();
static void ResizeEditorHost();
static void SaveActiveFile();
static void StartCompile();
static void CheckCurrentFile();
struct GlobalState {
    std::wstring projDir;
    std::wstring targetName = L"我的程序";
    std::wstring targetType = L"exe";        // exe / apk
    std::vector<std::wstring> files;         // 项目文件（相对路径）
    HWND hTree = nullptr, hTabs = nullptr, hStatus = nullptr;
    HWND hConsole = nullptr, hErrList = nullptr, hEditHost = nullptr;
    HWND hTopBar = nullptr, hProps = nullptr;
    HWND hEditors[16];                       // 打开的编辑器
    EditorState* eds[16];
    int nOpen = 0, activeTab = -1;
    std::vector<std::pair<int,int>> errList; // 错误: (编辑器下标, 行)
    bool compiling = false;
    HANDLE hCompileThread = nullptr;
    HWND hBtnCompile = nullptr, hBtnStop = nullptr, hCmbTarget = nullptr;
    std::vector<HWND> propEdits;             // 属性面板控件
    std::vector<std::wstring> propKeys;
    bool propLoading = false;
};
static GlobalState g;

static EditorState* ActiveEd() {
    if (g.activeTab < 0 || g.activeTab >= g.nOpen) return nullptr;
    return g.eds[g.activeTab];
}

// 更新状态栏
static void UpdateStatus() {
    wchar_t buf[512];
    EditorState* ed = ActiveEd();
    std::wstring linecol = L"行: 0  列: 0";
    if (ed) {
        swprintf(buf, 512, L"行: %d  列: %d", ed->curLine + 1, ed->curCol + 1);
        linecol = buf;
    }
    swprintf(buf, 512, L"目标: %s", g.targetType == L"apk" ? L"Android APP" : L"Windows EXE");
    std::wstring tgt = buf;
    swprintf(buf, 512, L"状态: %s", g.compiling ? L"编译中…" : L"就绪");
    std::wstring st = buf;
    int parts[] = { 120, 260, 400, -1 };
    SendMessageW(g.hStatus, SB_SETPARTS, 4, (LPARAM)parts);
    SendMessageW(g.hStatus, SB_SETTEXTW, 0, (LPARAM)linecol.c_str());
    SendMessageW(g.hStatus, SB_SETTEXTW, 1, (LPARAM)tgt.c_str());
    SendMessageW(g.hStatus, SB_SETTEXTW, 2, (LPARAM)st.c_str());
    SendMessageW(g.hStatus, SB_SETTEXTW, 3, (LPARAM)L"ETC IDE v1.0 (by etc)");
}

// 输出
static void ConsolePrint(const std::wstring& s, COLORREF c) {
    ConsoleState* cs = (ConsoleState*)GetWindowLongPtrW(g.hConsole, GWLP_USERDATA);
    if (!cs) return;
    cs->Append(s, c);
    cs->scrollY = (int)cs->lines.size() * 16;
    InvalidateRect(g.hConsole, nullptr, FALSE);
}
static void ConsoleClear() {
    ConsoleState* cs = (ConsoleState*)GetWindowLongPtrW(g.hConsole, GWLP_USERDATA);
    if (cs) { cs->Clear(); InvalidateRect(g.hConsole, nullptr, FALSE); }
}
static COLORREF ColorForLine(const std::wstring& s) {
    if (s.find(L"错误") != std::wstring::npos) return C_ERR;
    if (s.find(L"警告") != std::wstring::npos) return RGB(0xD7, 0xBA, 0x7D);
    if (s.find(L"✓") != std::wstring::npos) return RGB(0x89, 0xD1, 0x85);
    if (s.find(L"▸") != std::wstring::npos) return RGB(0x4F, 0xC1, 0xFF);
    if (s.find(L"┌") != std::wstring::npos || s.find(L"└") != std::wstring::npos) return RGB(0x4F, 0xC1, 0xFF);
    if (s.find(L"|") != std::wstring::npos) return RGB(0x4F, 0xC1, 0xFF);
    return C_TEXT;
}

// 错误列表刷新
static void ErrListClear() {
    ListView_DeleteAllItems(g.hErrList);
    g.errList.clear();
}
static void ErrListAdd(int edIdx, int line, const std::wstring& text) {
    LVITEMW it = {0};
    it.mask = LVIF_TEXT;
    std::wstring ls = std::to_wstring(line + 1);
    it.pszText = (LPWSTR)ls.c_str();
    int idx = LV_InsertItem(g.hErrList, &it);
    wchar_t buf[1024];
    swprintf(buf, 1024, L"%d", line + 1);
    LV_SetItemText(g.hErrList, idx, 1, buf);
    LV_SetItemText(g.hErrList, idx, 2, (LPWSTR)text.c_str());
    g.errList.push_back({ edIdx, line });
}
// ============================================================
//  文件管理 / 语法检查 / 属性面板 / 对话框 / 编译（第三部分）
// ============================================================

// ---- 语法检查（内嵌编译器模块）----
static void CheckCurrentFile() {
    EditorState* ed = ActiveEd();
    if (!ed) return;
    ed->errLines.clear();
    ErrListClear();
    std::string utf8 = WToUtf8(ed->text);
    std::string fname = WToUtf8(FileName(ed->path));
    std::vector<Diag> ds;
    try {
        Lexer lx(utf8, fname);
        auto toks = lx.tokenize();
        Parser ps(std::move(toks), fname);
        auto prog = ps.parseProgram();
        Semantic sem({ "root", "system", "user", "*" });
        sem.check(prog.get());
        ds = sem.diags();
    } catch (const CompileError& e) {
        Diag d;
        d.isErr = true; d.code = e.code; d.file = e.file; d.line = e.line; d.col = e.col; d.msg = e.msg;
        ds.push_back(d);
    }
    int nErr = 0, nWarn = 0;
    wchar_t buf[1024];
    for (auto& d : ds) {
        swprintf(buf, 1024, L"%s E%d: %s:%d:%d %s",
            d.isErr ? L"错误" : L"警告", d.code,
            Utf8ToW(d.file).c_str(), d.line, d.col, Utf8ToW(d.msg).c_str());
        if (d.isErr) {
            ErrListAdd(g.activeTab, d.line > 0 ? d.line - 1 : 0, buf);
            if (d.line > 0) ed->errLines.push_back(d.line - 1);
            nErr++;
        } else nWarn++;
    }
    std::sort(ed->errLines.begin(), ed->errLines.end());
    ed->errLines.erase(std::unique(ed->errLines.begin(), ed->errLines.end()), ed->errLines.end());
    swprintf(buf, 1024, L"语法检查: %d 个错误，%d 个警告", nErr, nWarn);
    ConsolePrint(buf, nErr ? C_ERR : RGB(0x89, 0xD1, 0x85));
    ConsolePrint(L"", C_TEXT);
    InvalidateRect(ed ? GetParent(g.hEditors[g.activeTab]) : nullptr, nullptr, FALSE);
    InvalidateRect(g.hEditors[g.activeTab], nullptr, FALSE);
    InvalidateRect(g.hErrList, nullptr, FALSE);
    UpdateStatus();
}

// ---- 标签页管理 ----
static void TabSetTitle(int idx, const std::wstring& name) {
    TCITEMW ti = {0};
    ti.mask = TCIF_TEXT;
    ti.pszText = (LPWSTR)name.c_str();
    SendMessageW(g.hTabs, TCM_SETITEMW, idx, (LPARAM)&ti);
}

static int OpenEditor(const std::wstring& path) {
    // 已打开则激活
    for (int i = 0; i < g.nOpen; i++) {
        if (g.eds[i] && _wcsicmp(g.eds[i]->path.c_str(), path.c_str()) == 0) {
            SendMessageW(g.hTabs, TCM_SETCURSEL, i, 0);
            g.activeTab = i;
            ShowWindow(g.hEditors[i], SW_SHOW);
            for (int k = 0; k < g.nOpen; k++) if (k != i) ShowWindow(g.hEditors[k], SW_HIDE);
            UpdateStatus();
            return i;
        }
    }
    if (g.nOpen >= 16) return -1;
    std::wstring content;
    ReadFileW(path, content);
    int idx = g.nOpen;
    HWND hw = CreateWindowExW(0, kEditCls, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 100, 100, g.hEditHost, nullptr, GetModuleHandleW(nullptr), nullptr);
    g.hEditors[idx] = hw;
    EditorState* ed = GetEd(hw);
    ed->path = path;
    ed->SetText(content);
    g.eds[idx] = ed;
    g.nOpen++;
    TCITEMW ti = {0};
    ti.mask = TCIF_TEXT;
    std::wstring t = FileName(path);
    ti.pszText = (LPWSTR)t.c_str();
    SendMessageW(g.hTabs, TCM_INSERTITEMW, idx, (LPARAM)&ti);
    SendMessageW(g.hTabs, TCM_SETCURSEL, idx, 0);
    g.activeTab = idx;
    for (int k = 0; k < g.nOpen; k++) ShowWindow(g.hEditors[k], k == idx ? SW_SHOW : SW_HIDE);
    ResizeEditorHost();
    UpdateStatus();
    return idx;
}

static void SaveActiveFile() {
    EditorState* ed = ActiveEd();
    if (!ed) return;
    if (ed->path.empty()) {
        wchar_t path[MAX_PATH] = {0};
        OPENFILENAMEW ofn = {0};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = GetParent(g.hEditors[g.activeTab]);
        ofn.lpstrFilter = L"ETC 源文件 (*.ce;*.ve;*.xo;*.ca;*.io;*.ru;*.ec)\0*.ce;*.ve;*.xo;*.ca;*.io;*.ru;*.ec\0所有文件\0*.*\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        ofn.lpstrDefExt = L"ce";
        if (!GetSaveFileNameW(&ofn)) return;
        ed->path = path;
        TabSetTitle(g.activeTab, FileName(path));
    }
    WriteFileW(ed->path, ed->text);
    ed->dirty = false;
    InvalidateRect(g.hEditors[g.activeTab], nullptr, FALSE);
    ConsolePrint(L"已保存: " + ed->path, C_TEXT);
    RefreshTree();
    CheckCurrentFile();
}

// ---- 解决方案资源管理器 ----
static void RefreshTree() {
    TreeView_DeleteAllItems(g.hTree);
    if (g.projDir.empty()) return;
    std::wstring rootName = FileName(g.projDir);
    HTREEITEM root = TV_Insert(g.hTree, nullptr, (LPWSTR)rootName.c_str());
    g.files.clear();
    static const std::set<std::wstring> ext = { L".ce", L".ve", L".xo", L".ca", L".io", L".ru", L".ec" };
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g.projDir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (name == L"generated" || name == L"build" || name == L".git") continue;
                TV_Insert(g.hTree, root, (LPWSTR)name.c_str());
            } else {
                if (ext.count(FileExt(name))) {
                    TV_Insert(g.hTree, root, (LPWSTR)name.c_str());
                    g.files.push_back(name);
                }
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    TreeView_Expand(g.hTree, root, TVE_EXPAND);
    // 读 config.ec 属性
    ReloadProps();
}

// ---- 属性面板（读写 config.ec）----
static std::map<std::wstring, std::wstring> ReadEcProps() {
    std::map<std::wstring, std::wstring> m;
    std::wstring cfg = JoinPath(g.projDir, L"config.ec");
    std::wstring content;
    if (!ReadFileW(cfg, content)) return m;
    std::wstring section;
    std::wistringstream ss(content);
    std::wstring line;
    while (std::getline(ss, line)) {
        // 去注释
        size_t c = line.find(L'#');
        if (c != std::wstring::npos) line = line.substr(0, c);
        // 去空白
        size_t b = line.find_first_not_of(L" \t\r");
        if (b == std::wstring::npos) continue;
        line = line.substr(b);
        if (line.empty()) continue;
        if (line.front() == L'[') {
            section = line.substr(1, line.find(L']') - 1);
            continue;
        }
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring k = line.substr(0, eq);
        std::wstring v = line.substr(eq + 1);
        // trim
        size_t kb = k.find_first_not_of(L" \t");
        size_t ke = k.find_last_not_of(L" \t");
        k = kb == std::wstring::npos ? L"" : k.substr(kb, ke - kb + 1);
        size_t vb = v.find_first_not_of(L" \t\"");
        size_t ve = v.find_last_not_of(L" \t\"");
        v = vb == std::wstring::npos ? L"" : v.substr(vb, ve - vb + 1);
        if (!k.empty()) m[section + L"." + k] = v;
    }
    return m;
}

static void ReloadProps() {
    if (g.propLoading || g.propEdits.empty()) return;
    g.propLoading = true;
    auto m = ReadEcProps();
    for (size_t i = 0; i < g.propKeys.size() && i < g.propEdits.size(); i++) {
        std::wstring v = m.count(g.propKeys[i]) ? m[g.propKeys[i]] : L"";
        SetWindowTextW(g.propEdits[i], v.c_str());
    }
    g.propLoading = false;
}

static void SaveProps() {
    if (g.projDir.empty()) return;
    std::wstring cfg = JoinPath(g.projDir, L"config.ec");
    std::wstring out;
    std::wstring sec;
    // 生成新 config.ec
    for (size_t i = 0; i < g.propKeys.size(); i++) {
        wchar_t val[512];
        GetWindowTextW(g.propEdits[i], val, 512);
        std::wstring key = g.propKeys[i];
        std::wstring section = key.substr(0, key.find(L'.'));
        std::wstring name = key.substr(key.find(L'.') + 1);
        if (section != sec) {
            out += L"\n[" + section + L"]\n";
            sec = section;
        }
        out += name + L" = \"" + val + L"\"\n";
    }
    WriteFileW(cfg, out);
    ConsolePrint(L"项目属性已保存到 config.ec", C_TEXT);
}

// 属性面板控件布局（固定字段组）
static void RebuildPropPanel(HWND hwnd) {
    // 清空旧控件
    for (HWND h : g.propEdits) DestroyWindow(h);
    g.propEdits.clear();
    g.propKeys.clear();
    RECT rc; GetClientRect(hwnd, &rc);
    // 标题
    static HWND hTitle = nullptr;
    if (hTitle) { DestroyWindow(hTitle); hTitle = nullptr; }
    hTitle = CreateWindowExW(0, L"STATIC", L"属性面板（写入 config.ec）", WS_CHILD | WS_VISIBLE,
        8, 6, rc.right - 16, 20, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SetTextColor(GetDC(hTitle), C_PANEL_TX);

    struct Field { std::wstring key, label; };
    std::vector<Field> fields;
    if (g.targetType == L"apk") {
        fields = {
            { L"android.软件名称", L"软件名称" },
            { L"android.包名", L"包名" },
            { L"android.版本号", L"版本号" },
            { L"android.版本名称", L"版本名称" },
            { L"android.最低支持安卓", L"最低支持安卓" },
            { L"android.最高支持安卓", L"最高支持安卓" },
            { L"android.资源压缩", L"资源压缩(true/false)" },
            { L"android.代码混淆", L"代码混淆(true/false)" },
        };
    } else {
        fields = {
            { L"windows.产品名称", L"产品名称" },
            { L"windows.文件说明", L"文件说明" },
            { L"windows.公司名称", L"公司名称" },
            { L"windows.版权信息", L"版权信息" },
            { L"windows.文件版本", L"文件版本" },
            { L"windows.产品版本", L"产品版本" },
            { L"windows.压缩打包", L"压缩打包(true/false)" },
            { L"windows.压缩级别", L"压缩级别(0-9)" },
            { L"windows.去符号表", L"去符号表(true/false)" },
            { L"windows.UPX", L"UPX(true/false)" },
        };
    }
    int y = 32;
    for (auto& f : fields) {
        HWND lbl = CreateWindowExW(0, L"STATIC", f.label.c_str(), WS_CHILD | WS_VISIBLE,
            8, y, rc.right - 16, 18, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(lbl, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
        HWND ed = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            8, y + 20, rc.right - 16, 22, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(ed, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
        SendMessageW(ed, WM_SETTEXT, 0, (LPARAM)L"");
        g.propEdits.push_back(ed);
        g.propKeys.push_back(f.key);
        y += 48;
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

// ---- 新建项目向导 ----
static std::wstring g_wizName, g_wizDir;
static int g_wizType = 0;      // 0 EXE 1 APK
static bool g_wizMain = true, g_wizCfg = true, g_wizUtils = true, g_wizUi = true;

static void CreateProjectFiles(const std::wstring& dir, bool isApk) {
    std::wstring mainCe = L"// ============================================================\n"
        L"//  ETC Lang 项目入口（main.ce）\n"
        L"//  作者: etc (ET 协会)\n"
        L"//  @grant(root)：编译出的 EXE 双击即请求管理员权限\n"
        L"// ============================================================\n"
        L"@grant(root)\n\n"
        L"fn main() -> i32 {\n"
        L"    println(\"ETC 项目已创建！\");\n"
        L"    println(\"语言: ETC Lang  目标: " + std::wstring(isApk ? L"Android APP" : L"Windows EXE") + L"\");\n"
        L"    return 0;\n"
        L"}\n";
    WriteFileW(JoinPath(dir, L"main.ce"), mainCe);
    std::wstring cfg = L"# ETC Lang 项目配置（config.ec）\n"
        L"# 编译时自动读取，生成 version.rc / manifest / AndroidManifest.xml\n\n"
        L"[windows]\n"
        L"产品名称 = \"ETC 程序\"\n"
        L"文件说明 = \"ETC Lang 编译产物\"\n"
        L"公司名称 = \"ET\"\n"
        L"版权信息 = \"Copyright (c) ET 2026\"\n"
        L"文件版本 = \"1.0.0.0\"\n"
        L"产品版本 = \"1.0.0.0\"\n"
        L"压缩打包 = true\n"
        L"压缩级别 = 9\n"
        L"去符号表 = true\n"
        L"UPX = true\n\n"
        L"[android]\n"
        L"软件名称 = \"ETC 程序\"\n"
        L"包名 = \"com.etc.app\"\n"
        L"版本号 = 1\n"
        L"版本名称 = \"1.0\"\n"
        L"最低支持安卓 = 21\n"
        L"最高支持安卓 = 35\n"
        L"资源压缩 = true\n"
        L"代码混淆 = true\n";
    WriteFileW(JoinPath(dir, L"config.ec"), cfg);
    WriteFileW(JoinPath(dir, L"utils.ru"),
        L"// utils.ru —— 运行时工具库\n"
        L"fn 双倍(n: i32) -> i32 {\n"
        L"    return n * 2;\n"
        L"}\n");
    WriteFileW(JoinPath(dir, L"ui.ca"),
        L"// ui.ca —— 界面类库（预留）\n"
        L"class 窗口 {\n"
        L"    let 标题: str = \"ETC\";\n"
        L"}\n");
}

INT_PTR CALLBACK WizProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            SetWindowTextW(GetDlgItem(hwnd, 101), g_wizName.c_str());
            SetWindowTextW(GetDlgItem(hwnd, 102), g_wizDir.c_str());
            CheckRadioButton(hwnd, 103, 104, g_wizType == 0 ? 103 : 104);
            CheckDlgButton(hwnd, 105, g_wizMain ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, 106, g_wizCfg ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, 107, g_wizUtils ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, 108, g_wizUi ? BST_CHECKED : BST_UNCHECKED);
            return TRUE;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == 109) {   // 浏览
                wchar_t dir[MAX_PATH] = {0};
                BROWSEINFOW bi = {0};
                bi.hwndOwner = hwnd;
                bi.lpszTitle = L"选择项目保存位置";
                bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
                if (pidl) {
                    SHGetPathFromIDListW(pidl, dir);
                    CoTaskMemFree(pidl);
                    SetWindowTextW(GetDlgItem(hwnd, 102), dir);
                }
                return TRUE;
            }
            if (id == 103 || id == 104) {
                CheckRadioButton(hwnd, 103, 104, id);
                return TRUE;
            }
            if (id == IDOK) {
                wchar_t name[256] = {0}, dir[MAX_PATH] = {0};
                GetDlgItemTextW(hwnd, 101, name, 256);
                GetDlgItemTextW(hwnd, 102, dir, MAX_PATH);
                g_wizName = name;
                g_wizDir = dir;
                g_wizType = IsDlgButtonChecked(hwnd, 104) ? 1 : 0;
                g_wizMain = IsDlgButtonChecked(hwnd, 105) == BST_CHECKED;
                g_wizCfg = IsDlgButtonChecked(hwnd, 106) == BST_CHECKED;
                g_wizUtils = IsDlgButtonChecked(hwnd, 107) == BST_CHECKED;
                g_wizUi = IsDlgButtonChecked(hwnd, 108) == BST_CHECKED;
                if (g_wizName.empty()) { MessageBoxW(hwnd, L"请输入项目名称", L"ETC IDE", MB_ICONWARNING); return TRUE; }
                if (g_wizDir.empty()) { MessageBoxW(hwnd, L"请选择保存位置", L"ETC IDE", MB_ICONWARNING); return TRUE; }
                std::wstring dir2 = JoinPath(g_wizDir, g_wizName);
                if (!CreateDirectoryW(dir2.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
                    MessageBoxW(hwnd, L"创建目录失败", L"ETC IDE", MB_ICONERROR);
                    return TRUE;
                }
                CreateProjectFiles(dir2, g_wizType == 1);
                EndDialog(hwnd, IDOK);
                return TRUE;
            }
            if (id == IDCANCEL) { EndDialog(hwnd, IDCANCEL); return TRUE; }
            return TRUE;
        }
    }
    return FALSE;
}

// ---- 项目属性对话框 ----
INT_PTR CALLBACK PropDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_INITDIALOG: {
            CheckRadioButton(hwnd, 301, 302, g.targetType == L"apk" ? 302 : 301);
            auto m = ReadEcProps();
            struct F { int id; std::wstring key; };
            std::vector<F> fs = {
                { 303, L"windows.产品名称" }, { 304, L"windows.文件说明" },
                { 305, L"windows.公司名称" }, { 306, L"windows.版权信息" },
                { 307, L"windows.文件版本" }, { 308, L"windows.产品版本" },
                { 309, L"android.软件名称" }, { 310, L"android.包名" },
                { 311, L"android.版本号" }, { 312, L"android.版本名称" },
            };
            for (auto& f : fs) {
                std::wstring v = m.count(f.key) ? m[f.key] : L"";
                SetDlgItemTextW(hwnd, f.id, v.c_str());
            }
            return TRUE;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == 301 || id == 302) {
                CheckRadioButton(hwnd, 301, 302, id);
                return TRUE;
            }
            if (id == IDOK) {
                g.targetType = IsDlgButtonChecked(hwnd, 302) ? L"apk" : L"exe";
                auto m = ReadEcProps();
                struct F { int id; std::wstring key; };
                std::vector<F> fs = {
                    { 303, L"windows.产品名称" }, { 304, L"windows.文件说明" },
                    { 305, L"windows.公司名称" }, { 306, L"windows.版权信息" },
                    { 307, L"windows.文件版本" }, { 308, L"windows.产品版本" },
                    { 309, L"android.软件名称" }, { 310, L"android.包名" },
                    { 311, L"android.版本号" }, { 312, L"android.版本名称" },
                };
                for (auto& f : fs) {
                    wchar_t v[512];
                    GetDlgItemTextW(hwnd, f.id, v, 512);
                    m[f.key] = v;
                }
                // 写回
                std::wstring cfg = JoinPath(g.projDir, L"config.ec");
                std::wstring out;
                std::wstring sec;
                std::vector<std::wstring> order = {
                    L"windows.产品名称", L"windows.文件说明", L"windows.公司名称",
                    L"windows.版权信息", L"windows.文件版本", L"windows.产品版本",
                    L"android.软件名称", L"android.包名", L"android.版本号", L"android.版本名称"
                };
                for (auto& k : order) {
                    std::wstring section = k.substr(0, k.find(L'.'));
                    std::wstring name = k.substr(k.find(L'.') + 1);
                    if (section != sec) { out += L"\n[" + section + L"]\n"; sec = section; }
                    out += name + L" = \"" + m[k] + L"\"\n";
                }
                WriteFileW(cfg, out);
                EndDialog(hwnd, IDOK);
                return TRUE;
            }
            if (id == IDCANCEL) { EndDialog(hwnd, IDCANCEL); return TRUE; }
            return TRUE;
        }
    }
    return FALSE;
}

// ---- 编译（come 子进程）----
static std::wstring g_compileCmd;
static DWORD WINAPI CompileThreadFn(LPVOID) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hOutR, hOutW;
    CreatePipe(&hOutR, &hOutW, &sa, 0);
    SetHandleInformation(hOutR, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hOutW;
    si.hStdError = hOutW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {0};
    std::wstring cmd = g_compileCmd;
    // 命令行需要带引号
    std::vector<wchar_t> buf(cmd.size() + 32);
    wcscpy(buf.data(), cmd.c_str());
    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hOutW);
    if (!ok) {
        PostMessageW(g.hTopBar, WM_APP + 20, (WPARAM)GetLastError(), 0);
        CloseHandle(hOutR);
        return 1;
    }
    char tmp[4097];
    DWORD rd = 0;
    std::string acc;
    while (ReadFile(hOutR, tmp, 4096, &rd, nullptr) && rd > 0) {
        tmp[rd] = 0;
        acc += tmp;
        size_t pos;
        while ((pos = acc.find('\n')) != std::string::npos) {
            std::string line = acc.substr(0, pos);
            acc.erase(0, pos + 1);
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            PostMessageW(g.hTopBar, WM_APP + 21, 0, (LPARAM)_wcsdup(Utf8ToW(line).c_str()));
        }
    }
    if (!acc.empty())
        PostMessageW(g.hTopBar, WM_APP + 21, 0, (LPARAM)_wcsdup(Utf8ToW(acc).c_str()));
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hOutR);
    PostMessageW(g.hTopBar, WM_APP + 22, code, 0);
    return 0;
}

static void StartCompile() {
    if (g.compiling) return;
    if (g.projDir.empty()) {
        MessageBoxW(g.hTopBar, L"请先打开或新建项目", L"ETC IDE", MB_ICONWARNING);
        return;
    }
    // 保存当前文件
    EditorState* ed = ActiveEd();
    if (ed && ed->dirty) SaveActiveFile();
    std::wstring comeExe = JoinPath(ExeDir(), L"come.exe");
    if (!FileExists(comeExe)) {
        MessageBoxW(g.hTopBar, L"未找到 come.exe（编译器），请将 come.exe 与 ETC-IDE.exe 放在同一目录。",
            L"ETC IDE", MB_ICONERROR);
        return;
    }
    ConsoleClear();
    ErrListClear();
    for (int i = 0; i < g.nOpen; i++) {
        if (g.eds[i]) g.eds[i]->errLines.clear();
    }
    std::wstring outName = g.targetName + (g.targetType == L"apk" ? L".apk" : L".exe");
    std::wstring flag = g.targetType == L"apk" ? L"--oot" : L"--oob";
    g_compileCmd = L"\"" + comeExe + L"\" \"" + outName + L"\" " + flag + L" --proj \"" + g.projDir + L"\"";
    g.compiling = true;
    UpdateStatus();
    EnableWindow(g.hBtnCompile, FALSE);
    EnableWindow(g.hBtnStop, TRUE);
    ConsolePrint(L"▸ 开始编译: " + outName, RGB(0x4F, 0xC1, 0xFF));
    CreateThread(nullptr, 0, CompileThreadFn, nullptr, 0, nullptr);
}
// ============================================================
//  主窗口布局 / 菜单 / 事件 / 入口（第四部分）
// ============================================================

static HWND g_hwnd = nullptr, g_hProps = nullptr;

static void ResizeEditorHost() {
    RECT rc;
    GetClientRect(g.hEditHost, &rc);
    for (int i = 0; i < g.nOpen; i++)
        MoveWindow(g.hEditors[i], 0, 0, rc.right, rc.bottom, TRUE);
    InvalidateRect(g.hEditHost, nullptr, FALSE);
}

static void Layout() {
    if (!g_hwnd) return;
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    int topH = 44, leftW = 210, rightW = 245, botH = 190;
    // 顶栏
    MoveWindow(g.hTopBar, 0, 0, rc.right, topH, TRUE);
    // 解决方案
    MoveWindow(g.hTree, 0, topH, leftW, rc.bottom - topH - botH, TRUE);
    // 属性
    MoveWindow(g.hProps, rc.right - rightW, topH, rightW, rc.bottom - topH - botH, TRUE);
    // 底部（错误列表 + 输出）
    MoveWindow(g.hErrList, 0, rc.bottom - botH, rc.right / 2, 90, TRUE);
    MoveWindow(g.hConsole, rc.right / 2, rc.bottom - botH, rc.right / 2, 90, TRUE);
    MoveWindow(g.hConsole, 0, rc.bottom - botH + 94, rc.right, 96, TRUE);
    // 错误列表也调整
    MoveWindow(g.hErrList, 0, rc.bottom - botH, rc.right, 92, TRUE);
    // 中央编辑区
    MoveWindow(g.hEditHost, leftW, topH, rc.right - leftW - rightW, rc.bottom - topH - botH, TRUE);
    // 顶栏内部
    {
        RECT tr;
        GetClientRect(g.hTopBar, &tr);
        MoveWindow(g.hBtnCompile, 8, 6, 90, 30, TRUE);
        MoveWindow(g.hBtnStop, 104, 6, 90, 30, TRUE);
        MoveWindow(g.hCmbTarget, 210, 6, 160, 30, TRUE);
        // 标签
        HWND lbl = GetDlgItem(g.hTopBar, 2001);
        if (!lbl) {
            lbl = CreateWindowExW(0, L"STATIC", L"目标:", WS_CHILD | WS_VISIBLE,
                380, 10, 40, 20, g.hTopBar, (HMENU)2001, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(lbl, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
        }
        MoveWindow(lbl, 380, 10, 40, 20, TRUE);
        // 目标名编辑
        HWND en = GetDlgItem(g.hTopBar, 2002);
        if (!en) {
            en = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"我的程序", WS_CHILD | WS_VISIBLE,
                420, 7, 180, 26, g.hTopBar, (HMENU)2002, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(en, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
        }
        MoveWindow(en, 420, 7, 180, 26, TRUE);
        // 输出名提示
        HWND lbl2 = GetDlgItem(g.hTopBar, 2003);
        if (!lbl2) {
            lbl2 = CreateWindowExW(0, L"STATIC", L"输出名称:", WS_CHILD | WS_VISIBLE,
                370, 10, 60, 20, g.hTopBar, (HMENU)2003, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(lbl2, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
        }
        MoveWindow(lbl2, 370, 10, 56, 20, TRUE);
    }
    ResizeEditorHost();
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

static void SetTargetType(const std::wstring& t) {
    g.targetType = t;
    SendMessageW(g.hCmbTarget, CB_SETCURSEL, t == L"apk" ? 1 : 0, 0);
    RebuildPropPanel(g.hProps);
    ReloadProps();
    UpdateStatus();
}

static void OpenProject(const std::wstring& dir) {
    if (dir.empty()) return;
    g.projDir = dir;
    RefreshTree();
    SetTargetType(g.targetType);
    // 打开 main.ce（若存在），否则打开第一个源文件
    std::wstring mainCe = JoinPath(dir, L"main.ce");
    if (FileExists(mainCe)) OpenEditor(mainCe);
    else if (!g.files.empty()) OpenEditor(JoinPath(dir, g.files[0]));
    ConsolePrint(L"已打开项目: " + dir, RGB(0x4F, 0xC1, 0xFF));
    UpdateStatus();
}

static void NewProjectFlow() {
    wchar_t defDir[MAX_PATH] = {0};
    SHGetSpecialFolderPathW(nullptr, defDir, CSIDL_MYDOCUMENTS, FALSE);
    g_wizDir = defDir;
    g_wizName = L"我的ETC项目";
    g_wizType = 0;
    if (DialogBoxW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), g_hwnd, WizProc) == IDOK) {
        std::wstring dir = JoinPath(g_wizDir, g_wizName);
        OpenProject(dir);
    }
}

// 菜单创建
static HMENU BuildMenu() {
    HMENU bar = CreateMenu();
    HMENU mFile = CreatePopupMenu();
    AppendMenuW(mFile, MF_STRING, 1001, L"新建项目(&N)...\tCtrl+Shift+N");
    AppendMenuW(mFile, MF_STRING, 1002, L"打开项目(&O)...");
    AppendMenuW(mFile, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(mFile, MF_STRING, 1003, L"新建文件(&F)\tCtrl+N");
    AppendMenuW(mFile, MF_STRING, 1004, L"保存(&S)\tCtrl+S");
    AppendMenuW(mFile, MF_STRING, 1005, L"另存为(&A)...");
    AppendMenuW(mFile, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(mFile, MF_STRING, 1006, L"退出(&X)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)mFile, L"文件(&F)");

    HMENU mEdit = CreatePopupMenu();
    AppendMenuW(mEdit, MF_STRING, 1014, L"撤销\tCtrl+Z");
    AppendMenuW(mEdit, MF_STRING, 1015, L"重做\tCtrl+Y");
    AppendMenuW(mEdit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(mEdit, MF_STRING, 1010, L"剪切\tCtrl+X");
    AppendMenuW(mEdit, MF_STRING, 1011, L"复制\tCtrl+C");
    AppendMenuW(mEdit, MF_STRING, 1012, L"粘贴\tCtrl+V");
    AppendMenuW(mEdit, MF_STRING, 1013, L"全选\tCtrl+A");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)mEdit, L"编辑(&E)");

    HMENU mBuild = CreatePopupMenu();
    AppendMenuW(mBuild, MF_STRING, 1020, L"编译(&B)\tF7");
    AppendMenuW(mBuild, MF_STRING, 1021, L"停止编译");
    AppendMenuW(mBuild, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(mBuild, MF_STRING, 1022, L"语法检查当前文件");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)mBuild, L"编译(&B)");

    HMENU mTgt = CreatePopupMenu();
    AppendMenuW(mTgt, MF_STRING, 1030, L"Windows EXE(&W)");
    AppendMenuW(mTgt, MF_STRING, 1031, L"Android APP(&A)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)mTgt, L"目标(&T)");

    HMENU mTool = CreatePopupMenu();
    AppendMenuW(mTool, MF_STRING, 1040, L"项目属性(&P)...");
    AppendMenuW(mTool, MF_STRING, 1041, L"在资源管理器中打开项目文件夹");
    AppendMenuW(mTool, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(mTool, MF_STRING, 1042, L"内嵌语法检查(开关)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)mTool, L"工具(&O)");

    HMENU mHelp = CreatePopupMenu();
    AppendMenuW(mHelp, MF_STRING, 1050, L"关于 ETC IDE(&A)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)mHelp, L"帮助(&H)");
    return bar;
}

// 顶栏按钮
static void BuildTopBar(HWND hwnd) {
    g.hTopBar = CreateWindowExW(0, L"ETCHost", L"", WS_CHILD | WS_VISIBLE,
        0, 0, 100, 44, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    g.hBtnCompile = CreateWindowExW(0, L"BUTTON", L"▶ 编译", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        8, 6, 90, 30, g.hTopBar, (HMENU)2000, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g.hBtnCompile, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    g.hBtnStop = CreateWindowExW(0, L"BUTTON", L"■ 停止", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        104, 6, 90, 30, g.hTopBar, (HMENU)2005, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g.hBtnStop, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    EnableWindow(g.hBtnStop, FALSE);
    g.hCmbTarget = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        210, 6, 150, 200, g.hTopBar, (HMENU)2004, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g.hCmbTarget, WM_SETFONT, (WPARAM)g_fontUi, TRUE);
    SendMessageW(g.hCmbTarget, CB_ADDSTRING, 0, (LPARAM)L"Windows EXE");
    SendMessageW(g.hCmbTarget, CB_ADDSTRING, 0, (LPARAM)L"Android APP");
    SendMessageW(g.hCmbTarget, CB_SETCURSEL, 0, 0);
}

// 编译输出行解析（错误 E1001: file:line:col msg）
static void ParseOutputLine(const std::wstring& line) {
    size_t ep = line.find(L"错误 E");
    size_t wp = line.find(L"警告 W");
    bool isErr = ep != std::wstring::npos;
    size_t p = isErr ? ep : wp;
    if (p == std::wstring::npos) return;
    // 读错误码
    p += 3;
    size_t cp = line.find(L':', p);
    if (cp == std::wstring::npos) return;
    // 找文件:行:列（从 cp 后找两个冒号）
    size_t c1 = line.find(L':', cp + 1);
    if (c1 == std::wstring::npos) return;
    size_t c2 = line.find(L':', c1 + 1);
    if (c2 == std::wstring::npos) return;
    std::wstring file = line.substr(cp + 2, c1 - cp - 2);
    int ln = _wtoi(line.substr(c1 + 1, c2 - c1 - 1).c_str());
    std::wstring msg = line.substr(c2 + 1);
    // 找对应编辑器
    int edIdx = -1;
    for (int i = 0; i < g.nOpen; i++) {
        if (g.eds[i] && FileName(g.eds[i]->path) == file) { edIdx = i; break; }
    }
    // 从项目目录找文件并打开
    if (edIdx < 0 && !g.projDir.empty()) {
        std::wstring fp = JoinPath(g.projDir, file);
        if (FileExists(fp)) edIdx = OpenEditor(fp);
    }
    if (edIdx >= 0) {
        ErrListAdd(edIdx, ln > 0 ? ln - 1 : 0, line);
        if (ln > 0 && g.eds[edIdx]) {
            g.eds[edIdx]->errLines.push_back(ln - 1);
            std::sort(g.eds[edIdx]->errLines.begin(), g.eds[edIdx]->errLines.end());
            g.eds[edIdx]->errLines.erase(std::unique(g.eds[edIdx]->errLines.begin(), g.eds[edIdx]->errLines.end()), g.eds[edIdx]->errLines.end());
            InvalidateRect(g.hEditors[edIdx], nullptr, FALSE);
        }
    }
}

// 跳转到错误行
static void JumpToError(int row) {
    if (row < 0 || row >= (int)g.errList.size()) return;
    int edIdx = g.errList[row].first;
    int ln = g.errList[row].second;
    if (edIdx >= 0 && edIdx < g.nOpen) {
        SendMessageW(g.hTabs, TCM_SETCURSEL, edIdx, 0);
        g.activeTab = edIdx;
        for (int k = 0; k < g.nOpen; k++) ShowWindow(g.hEditors[k], k == edIdx ? SW_SHOW : SW_HIDE);
        EditorState* ed = g.eds[edIdx];
        ed->curLine = ln;
        ed->curCol = 0;
        if (ed->curLine < 0) ed->curLine = 0;
        if (ed->curLine >= (int)ed->lines.size()) ed->curLine = (int)ed->lines.size() - 1;
        EdEnsureCaretVisible(g.hEditors[edIdx]);
        InvalidateRect(g.hEditors[edIdx], nullptr, FALSE);
        ResizeEditorHost();
        UpdateStatus();
    }
}

static void CompileDone(DWORD code) {
    g.compiling = false;
    EnableWindow(g.hBtnCompile, TRUE);
    EnableWindow(g.hBtnStop, FALSE);
    if (code == 0) {
        ConsolePrint(L"✓ 编译成功", RGB(0x89, 0xD1, 0x85));
    } else {
        ConsolePrint(L"✗ 编译失败（退出码 " + std::to_wstring(code) + L"）", C_ERR);
    }
    UpdateStatus();
}

// ---- 主窗口过程 ----
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_hwnd = hwnd;
            InitFonts();
            SetMenu(hwnd, BuildMenu());
            BuildTopBar(hwnd);
            // 解决方案资源管理器
            g.hTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
                WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
                0, 0, 200, 100, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(g.hTree, TVM_SETBKCOLOR, 0, (LPARAM)C_BG);
            SendMessageW(g.hTree, TVM_SETTEXTCOLOR, 0, (LPARAM)C_TEXT);
            SendMessageW(g.hTree, TVM_SETLINECOLOR, 0, (LPARAM)C_BORDER);
            // 属性面板
            g.hProps = CreateWindowExW(0, L"ETCHost", L"", WS_CHILD | WS_VISIBLE,
                0, 0, 240, 100, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            // 错误列表
            g.hErrList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                0, 0, 200, 90, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(g.hErrList, LVM_SETBKCOLOR, 0, (LPARAM)C_BG);
            SendMessageW(g.hErrList, LVM_SETTEXTCOLOR, 0, (LPARAM)C_TEXT);
            SendMessageW(g.hErrList, LVM_SETTEXTBKCOLOR, 0, (LPARAM)C_BG);
            LVCOLUMNW col = {0};
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            col.cx = 60;
            col.pszText = (LPWSTR)L"行";
            SendMessageW(g.hErrList, LVM_INSERTCOLUMNW, 0, (LPARAM)&col);
            col.cx = 60;
            col.pszText = (LPWSTR)L"列";
            SendMessageW(g.hErrList, LVM_INSERTCOLUMNW, 1, (LPARAM)&col);
            col.cx = 900;
            col.pszText = (LPWSTR)L"错误列表";
            SendMessageW(g.hErrList, LVM_INSERTCOLUMNW, 2, (LPARAM)&col);
            ListView_SetExtendedListViewStyle(g.hErrList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            // 输出控制台
            g.hConsole = CreateWindowExW(WS_EX_CLIENTEDGE, kConsoleCls, L"", WS_CHILD | WS_VISIBLE,
                0, 0, 200, 90, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            // 编辑区宿主（Tab 页放这里）
            g.hEditHost = CreateWindowExW(0, L"ETCHost", L"", WS_CHILD | WS_VISIBLE,
                0, 0, 100, 100, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            // 标签页
            g.hTabs = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | TCS_FIXEDWIDTH,
                0, 0, 100, 24, g.hEditHost, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(g.hTabs, TCM_SETPADDING, 0, MAKELPARAM(12, 4));
            SendMessageW(g.hTabs, TCM_SETITEMSIZE, 0, MAKELPARAM(150, 22));
            // 状态栏
            g.hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                0, 0, 0, 0, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            Layout();
            return 0;
        }
        case WM_SIZE:
            Layout();
            return 0;
        case WM_NOTIFY: {
            NMHDR* nm = (NMHDR*)lp;
            if (nm->hwndFrom == g.hTree && nm->code == NM_DBLCLK) {
                TVITEMW item = {0};
                HTREEITEM sel = TreeView_GetSelection(g.hTree);
                if (sel) {
                    wchar_t buf[512] = {0};
                    item.hItem = sel;
                    item.mask = TVIF_TEXT;
                    item.pszText = buf;
                    item.cchTextMax = 512;
                    SendMessageW(g.hTree, TVM_GETITEMW, 0, (LPARAM)&item);
                    std::wstring name = buf;
                    if (name != FileName(g.projDir)) {
                        std::wstring fp = JoinPath(g.projDir, name);
                        if (FileExists(fp)) OpenEditor(fp);
                    }
                }
                return 0;
            }
            if (nm->hwndFrom == g.hTabs && nm->code == TCN_SELCHANGE) {
                int idx = (int)SendMessageW(g.hTabs, TCM_GETCURSEL, 0, 0);
                if (idx >= 0 && idx < g.nOpen) {
                    g.activeTab = idx;
                    for (int k = 0; k < g.nOpen; k++)
                        ShowWindow(g.hEditors[k], k == idx ? SW_SHOW : SW_HIDE);
                    ResizeEditorHost();
                    UpdateStatus();
                }
                return 0;
            }
            if (nm->hwndFrom == g.hErrList && nm->code == NM_DBLCLK) {
                LVHITTESTINFO ht = {0};
                GetCursorPos(&ht.pt);
                ScreenToClient(g.hErrList, &ht.pt);
                ListView_HitTest(g.hErrList, &ht);
                if (ht.iItem >= 0) JumpToError(ht.iItem);
                return 0;
            }
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == 2000) { StartCompile(); return 0; }
            if (id == 2005) { return 0; }  // 停止（v1 简单处理）
            if (id == 2004) {
                if (HIWORD(wp) == CBN_SELCHANGE) {
                    int s = (int)SendMessageW(g.hCmbTarget, CB_GETCURSEL, 0, 0);
                    SetTargetType(s == 1 ? L"apk" : L"exe");
                }
                return 0;
            }
            if (id == 2002 && HIWORD(wp) == EN_CHANGE) {
                wchar_t buf[256];
                GetDlgItemTextW(g.hTopBar, 2002, buf, 256);
                g.targetName = buf;
                return 0;
            }
            switch (id) {
                case 1001: NewProjectFlow(); return 0;
                case 1002: {
                    wchar_t dir[MAX_PATH] = {0};
                    BROWSEINFOW bi = {0};
                    bi.hwndOwner = hwnd;
                    bi.lpszTitle = L"打开 ETC 项目（选择项目文件夹）";
                    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
                    if (pidl) {
                        SHGetPathFromIDListW(pidl, dir);
                        CoTaskMemFree(pidl);
                        OpenProject(dir);
                    }
                    return 0;
                }
                case 1003: {
                    OpenEditor(L"");
                    return 0;
                }
                case 1004: SaveActiveFile(); return 0;
                case 1005: {
                    EditorState* ed = ActiveEd();
                    if (ed) ed->path = L"";
                    SaveActiveFile();
                    return 0;
                }
                case 1006: PostMessageW(hwnd, WM_CLOSE, 0, 0); return 0;
                case 1010: {
                    EditorState* ed = ActiveEd();
                    if (ed) { EdClipboardOp(g.hEditors[g.activeTab], true); EdDeleteFwd(g.hEditors[g.activeTab]); }
                    return 0;
                }
                case 1011: EdClipboardOp(g.hEditors[g.activeTab], true); return 0;
                case 1012: EdClipboardOp(g.hEditors[g.activeTab], false); return 0;
                case 1013: EdSelectAll(g.hEditors[g.activeTab]); return 0;
                case 1014: {
                    EditorState* ed = ActiveEd();
                    if (ed && !ed->undoStack.empty()) {
                        ed->redoStack.push_back(ed->text);
                        ed->SetText(ed->undoStack.back());
                        ed->undoStack.pop_back();
                        InvalidateRect(g.hEditors[g.activeTab], nullptr, FALSE);
                    }
                    return 0;
                }
                case 1015: {
                    EditorState* ed = ActiveEd();
                    if (ed && !ed->redoStack.empty()) {
                        ed->PushUndo();
                        ed->SetText(ed->redoStack.back());
                        ed->redoStack.pop_back();
                        InvalidateRect(g.hEditors[g.activeTab], nullptr, FALSE);
                    }
                    return 0;
                }
                case 1020: StartCompile(); return 0;
                case 1021: return 0;
                case 1022: CheckCurrentFile(); return 0;
                case 1030: SetTargetType(L"exe"); return 0;
                case 1031: SetTargetType(L"apk"); return 0;
                case 1040: {
                    if (g.projDir.empty()) { MessageBoxW(hwnd, L"请先打开项目", L"ETC IDE", MB_ICONWARNING); return 0; }
                    DialogBoxW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(2), hwnd, PropDlgProc);
                    RebuildPropPanel(g.hProps);
                    ReloadProps();
                    UpdateStatus();
                    return 0;
                }
                case 1041: {
                    if (!g.projDir.empty()) {
                        ShellExecuteW(hwnd, L"open", g.projDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    }
                    return 0;
                }
                case 1050:
                    MessageBoxW(hwnd,
                        L"ETC IDE v1.0\n\n"
                        L"ETC Lang 集成开发环境（高仿 Visual Studio）\n"
                        L"作者: etc  (ET 协会)\n\n"
                        L"· 7 种后缀源码高亮（.ce .ve .xo .ca .io .ru .ec）\n"
                        L"· 内嵌词法/语法/语义检查，错误行标红\n"
                        L"· 一键编译 EXE / APK（配合 come.exe）\n"
                        L"· 项目属性写入 config.ec，真实生效\n",
                        L"关于 ETC IDE", MB_OK | MB_ICONINFORMATION);
                    return 0;
            }
            return 0;
        }
        case WM_APP + 11:
            SaveActiveFile();
            return 0;
        case WM_APP + 20: {
            // 编译启动失败
            ConsolePrint(L"✗ 无法启动 come.exe（错误码 " + std::to_wstring(wp) + L"）", C_ERR);
            g.compiling = false;
            EnableWindow(g.hBtnCompile, TRUE);
            EnableWindow(g.hBtnStop, FALSE);
            UpdateStatus();
            return 0;
        }
        case WM_APP + 21: {
            LPWSTR s = (LPWSTR)lp;
            if (s) {
                std::wstring line = s;
                ConsolePrint(line, ColorForLine(line));
                ParseOutputLine(line);
                free(s);
            }
            return 0;
        }
        case WM_APP + 22: {
            CompileDone((DWORD)wp);
            return 0;
        }
        case WM_CLOSE: {
            // 提示保存
            for (int i = 0; i < g.nOpen; i++) {
                if (g.eds[i] && g.eds[i]->dirty && !g.eds[i]->path.empty()) {
                    std::wstring msg = L"文件 '" + FileName(g.eds[i]->path) + L"' 已修改，是否保存？";
                    int r = MessageBoxW(hwnd, msg.c_str(), L"ETC IDE", MB_YESNOCANCEL | MB_ICONQUESTION);
                    if (r == IDYES) { g.activeTab = i; SaveActiveFile(); }
                    else if (r == IDCANCEL) return 0;
                }
            }
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- 消息转发宿主（把子控件的 WM_NOTIFY/WM_COMMAND 转给主窗口） ----
LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NOTIFY || msg == WM_COMMAND || msg == WM_DRAWITEM) {
        LRESULT r = SendMessageW(GetParent(hwnd), msg, wp, lp);
        return r;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- 入口 ----
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSW wc = {0};
    wc.hbrBackground = (HBRUSH)CreateSolidBrush(C_BG);
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hInstance = hInst;
    wc.lpszClassName = L"ETCIDE_Main";
    wc.lpfnWndProc = MainWndProc;
    RegisterClassW(&wc);

    WNDCLASSW we = {0};
    we.hbrBackground = (HBRUSH)CreateSolidBrush(C_BG_EDIT);
    we.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_IBEAM);
    we.hInstance = hInst;
    we.lpszClassName = kEditCls;
    we.lpfnWndProc = EdWndProc;
    RegisterClassW(&we);

    WNDCLASSW wc2 = {0};
    wc2.hbrBackground = (HBRUSH)CreateSolidBrush(C_BG);
    wc2.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc2.hInstance = hInst;
    wc2.lpszClassName = kConsoleCls;
    wc2.lpfnWndProc = ConsoleWndProc;
    RegisterClassW(&wc2);

    WNDCLASSW wh = {0};
    wh.hbrBackground = (HBRUSH)CreateSolidBrush(C_BG);
    wh.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wh.hInstance = hInst;
    wh.lpszClassName = L"ETCHost";
    wh.lpfnWndProc = HostWndProc;
    RegisterClassW(&wh);

    InitFonts();

    HWND hwnd = CreateWindowExW(0, L"ETCIDE_Main", L"ETC IDE - ETC Lang 集成开发环境 (by etc)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800,
        nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    // 打开命令行参数指定的项目（可选）
    LPWSTR cmd = GetCommandLineW();
    // 简化：不解析参数

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
