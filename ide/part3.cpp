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
