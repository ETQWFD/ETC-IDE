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
