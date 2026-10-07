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
