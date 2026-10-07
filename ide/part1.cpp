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
