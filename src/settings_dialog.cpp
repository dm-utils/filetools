#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include "settings.h"
#include "resource.h"

extern HINSTANCE g_module;

YamlSettings g_settings;

// ─── INI persistence ──────────────────────────────────────────────────────────

static std::wstring get_ini_path() {
    wchar_t appdata[MAX_PATH] = {};
    GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    std::wstring dir = std::wstring(appdata) + L"\\Notepad++\\plugins\\config";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\FileTools.ini";
}

static const wchar_t* bool_str(bool v) { return v ? L"1" : L"0"; }

static std::wstring to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}
static std::string to_narrow(const wchar_t* w) {
    if (!*w) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

void save_settings() {
    std::wstring p = get_ini_path();
    const wchar_t* f = p.c_str();
    auto ws = [&](LPCWSTR sec, LPCWSTR key, LPCWSTR val) {
        WritePrivateProfileStringW(sec, key, val, f);
    };
    auto wi = [&](LPCWSTR sec, LPCWSTR key, int val) {
        wchar_t buf[16]; wsprintfW(buf, L"%d", val);
        WritePrivateProfileStringW(sec, key, buf, f);
    };

    wi(L"YAML", L"IndentStep", g_settings.tidy.indent_step);
    wi(L"YAML", L"TabWidth",   g_settings.tidy.tab_width);
    wi(L"YAML", L"MaxBlank",   g_settings.tidy.max_blank);
    ws(L"YAML", L"StripTrailingWs", bool_str(g_settings.tidy.strip_trailing_ws));
    ws(L"YAML", L"FinalNewline",    bool_str(g_settings.tidy.final_newline));

    ws(L"JSON", L"Indent",    g_settings.json_indent == JsonIndent::Four ? L"4" : L"2");
    ws(L"JSON", L"SortKeys",  bool_str(g_settings.json_sort_keys));

    ws(L"CSV", L"QuoteMode", g_settings.csv_quote_mode == CsvQuoteMode::Always ? L"always" : L"ifneeded");

    ws(L"Parquet", L"DuckdbPath", to_wide(g_settings.duckdb_path).c_str());

    ws(L"General", L"FormatOnSave", bool_str(g_settings.format_on_save));
}

void load_settings() {
    std::wstring p = get_ini_path();
    const wchar_t* f = p.c_str();
    wchar_t buf[MAX_PATH] = {};

    g_settings.tidy.indent_step = (int)GetPrivateProfileIntW(L"YAML", L"IndentStep", 2, f);
    g_settings.tidy.tab_width   = (int)GetPrivateProfileIntW(L"YAML", L"TabWidth",   2, f);
    g_settings.tidy.max_blank   = (int)GetPrivateProfileIntW(L"YAML", L"MaxBlank",   1, f);
    g_settings.tidy.strip_trailing_ws = GetPrivateProfileIntW(L"YAML", L"StripTrailingWs", 1, f) != 0;
    g_settings.tidy.final_newline     = GetPrivateProfileIntW(L"YAML", L"FinalNewline",    1, f) != 0;

    GetPrivateProfileStringW(L"JSON", L"Indent", L"2", buf, 64, f);
    g_settings.json_indent = wcscmp(buf, L"4") == 0 ? JsonIndent::Four : JsonIndent::Two;
    g_settings.json_sort_keys = GetPrivateProfileIntW(L"JSON", L"SortKeys", 0, f) != 0;

    GetPrivateProfileStringW(L"CSV", L"QuoteMode", L"ifneeded", buf, 64, f);
    g_settings.csv_quote_mode = wcscmp(buf, L"always") == 0 ? CsvQuoteMode::Always : CsvQuoteMode::OnlyIfNeeded;

    GetPrivateProfileStringW(L"Parquet", L"DuckdbPath", L"", buf, MAX_PATH, f);
    g_settings.duckdb_path = to_narrow(buf);

    g_settings.format_on_save = GetPrivateProfileIntW(L"General", L"FormatOnSave", 0, f) != 0;
}

// ─── JSON serialization ───────────────────────────────────────────────────────

static std::string jstr(const char* key, const char* val) {
    return std::string("  \"") + key + "\": \"" + val + "\"";
}
static std::string jbool(const char* key, bool val) {
    return std::string("  \"") + key + "\": " + (val ? "true" : "false");
}
static std::string jint(const char* key, int val) {
    char buf[24] = {};
    wsprintfA(buf, "%d", val);
    return std::string("  \"") + key + "\": " + buf;
}

static std::string json_get_str(const std::string& j, const char* key) {
    std::string pat = std::string("\"") + key + "\": \"";
    auto p = j.find(pat);
    if (p == std::string::npos) return "";
    p += pat.size();
    auto e = j.find('"', p);
    return e == std::string::npos ? "" : j.substr(p, e - p);
}
static bool json_get_bool(const std::string& j, const char* key, bool def) {
    std::string pat = std::string("\"") + key + "\": ";
    auto p = j.find(pat);
    if (p == std::string::npos) return def;
    p += pat.size();
    if (p + 4 <= j.size() && j.compare(p, 4, "true")  == 0) return true;
    if (p + 5 <= j.size() && j.compare(p, 5, "false") == 0) return false;
    return def;
}
static int json_get_int(const std::string& j, const char* key, int def) {
    std::string pat = std::string("\"") + key + "\": ";
    auto p = j.find(pat);
    if (p == std::string::npos) return def;
    p += pat.size();
    if (p < j.size() && (std::isdigit((unsigned char)j[p]) || j[p] == '-'))
        return std::atoi(j.c_str() + p);
    return def;
}

static std::string settings_to_json(const YamlSettings& s) {
    std::string j = "{\n";
    std::vector<std::string> lines;
    lines.push_back(jint("indent_step",  s.tidy.indent_step));
    lines.push_back(jint("tab_width",    s.tidy.tab_width));
    lines.push_back(jint("max_blank",    s.tidy.max_blank));
    lines.push_back(jbool("strip_trailing_ws", s.tidy.strip_trailing_ws));
    lines.push_back(jbool("final_newline",     s.tidy.final_newline));
    lines.push_back(jstr("json_indent",    s.json_indent == JsonIndent::Four ? "4" : "2"));
    lines.push_back(jbool("json_sort_keys", s.json_sort_keys));
    lines.push_back(jstr("csv_quote_mode", s.csv_quote_mode == CsvQuoteMode::Always ? "always" : "ifneeded"));
    lines.push_back(jstr("duckdb_path",    s.duckdb_path.c_str()));
    lines.push_back(jbool("format_on_save", s.format_on_save));
    for (size_t i = 0; i < lines.size(); ++i)
        j += lines[i] + (i + 1 < lines.size() ? ",\n" : "\n");
    j += "}\n";
    return j;
}

static YamlSettings settings_from_json(const std::string& j) {
    YamlSettings s;
    auto gs = [&](const char* k)          { return json_get_str(j, k); };
    auto gb = [&](const char* k, bool d)  { return json_get_bool(j, k, d); };
    auto gi = [&](const char* k, int d)   { return json_get_int(j, k, d); };

    s.tidy.indent_step = gi("indent_step", 2);
    s.tidy.tab_width    = gi("tab_width", 2);
    s.tidy.max_blank    = gi("max_blank", 1);
    s.tidy.strip_trailing_ws = gb("strip_trailing_ws", true);
    s.tidy.final_newline     = gb("final_newline", true);

    s.json_indent    = gs("json_indent") == "4" ? JsonIndent::Four : JsonIndent::Two;
    s.json_sort_keys = gb("json_sort_keys", false);

    s.csv_quote_mode = gs("csv_quote_mode") == "always" ? CsvQuoteMode::Always : CsvQuoteMode::OnlyIfNeeded;

    s.duckdb_path = gs("duckdb_path");

    s.format_on_save = gb("format_on_save", false);

    return s;
}

// ─── profile file operations ──────────────────────────────────────────────────

static std::wstring get_profiles_dir() {
    wchar_t appdata[MAX_PATH] = {};
    GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    std::wstring dir = std::wstring(appdata) + L"\\Notepad++\\plugins\\config\\FileTools_profiles";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

static bool write_text_file(const std::wstring& path, const std::string& content) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    WriteFile(h, content.c_str(), (DWORD)content.size(), &written, nullptr);
    CloseHandle(h);
    return true;
}

static std::string read_text_file(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "";
    LARGE_INTEGER sz; sz.QuadPart = 0;
    GetFileSizeEx(h, &sz);
    if (sz.QuadPart == 0 || sz.QuadPart > 512 * 1024) { CloseHandle(h); return ""; }
    std::string buf((size_t)sz.QuadPart, '\0');
    DWORD nread = 0;
    ReadFile(h, &buf[0], (DWORD)sz.QuadPart, &nread, nullptr);
    CloseHandle(h);
    buf.resize(nread);
    return buf;
}

static std::vector<std::wstring> list_profiles() {
    std::wstring dir = get_profiles_dir();
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((dir + L"\\*.json").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return names;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring name = fd.cFileName;
        if (name.size() > 5) names.push_back(name.substr(0, name.size() - 5));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(names.begin(), names.end());
    return names;
}

// ─── tab pages ────────────────────────────────────────────────────────────────

struct TabPage { UINT id; LPCWSTR label; DLGPROC proc; };

static void settings_to_ui();
static void ui_to_settings();
static void refresh_profile_list(HWND hPage);

static INT_PTR CALLBACK page_proc(HWND, UINT msg, WPARAM, LPARAM) {
    return msg == WM_INITDIALOG ? TRUE : FALSE;
}

// ── Parquet page (Browse... button) ───────────────────────────────────────────

static INT_PTR CALLBACK parquet_page_proc(HWND hPage, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG:
        return TRUE;
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == IDC_DUCKDB_BROWSE) {
            wchar_t fname[MAX_PATH] = {};
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = GetParent(hPage);
            ofn.lpstrFilter = L"duckdb.exe\0duckdb.exe\0All files\0*.*\0";
            ofn.lpstrFile   = fname;
            ofn.nMaxFile    = MAX_PATH;
            ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn))
                SetDlgItemTextW(hPage, IDC_DUCKDB_PATH, fname);
        }
        break;
    }
    return FALSE;
}

// ── Behavior & Profiles page ──────────────────────────────────────────────────

static INT_PTR CALLBACK behavior_page_proc(HWND hPage, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG:
        refresh_profile_list(hPage);
        return TRUE;
    case WM_COMMAND: {
        UINT notif = HIWORD(wParam);
        UINT ctrl  = LOWORD(wParam);

        if (ctrl == IDC_PROFILE_LIST && notif == LBN_DBLCLK)
            goto do_load;

        if (notif != BN_CLICKED) break;
        switch (ctrl) {
        case IDC_PROFILE_LOAD: do_load: {
            HWND hList = GetDlgItem(hPage, IDC_PROFILE_LIST);
            int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR) break;
            wchar_t name[512] = {};
            SendMessageW(hList, LB_GETTEXT, sel, reinterpret_cast<LPARAM>(name));
            std::string json = read_text_file(get_profiles_dir() + L"\\" + name + L".json");
            if (!json.empty()) {
                g_settings = settings_from_json(json);
                settings_to_ui();
                std::wstring m = std::wstring(L"Profile '") + name + L"' loaded.";
                MessageBoxW(hPage, m.c_str(), L"Datamodder File Tools", MB_OK | MB_ICONINFORMATION);
            }
            break;
        }
        case IDC_PROFILE_SAVE: {
            wchar_t name[512] = {};
            GetDlgItemTextW(hPage, IDC_PROFILE_NAME, name, 512);
            if (!name[0]) break;
            ui_to_settings();
            write_text_file(get_profiles_dir() + L"\\" + name + L".json", settings_to_json(g_settings));
            refresh_profile_list(hPage);
            break;
        }
        case IDC_PROFILE_DELETE: {
            HWND hList = GetDlgItem(hPage, IDC_PROFILE_LIST);
            int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR) break;
            wchar_t name[512] = {};
            SendMessageW(hList, LB_GETTEXT, sel, reinterpret_cast<LPARAM>(name));
            std::wstring m2 = std::wstring(L"Delete profile \"") + name + L"\"?";
            if (MessageBoxW(GetParent(hPage), m2.c_str(), L"Datamodder File Tools",
                            MB_YESNO | MB_ICONQUESTION) == IDYES) {
                DeleteFileW((get_profiles_dir() + L"\\" + name + L".json").c_str());
                refresh_profile_list(hPage);
            }
            break;
        }
        case IDC_PROFILE_EXPORT: {
            wchar_t fname[MAX_PATH] = L"settings";
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = GetParent(hPage);
            ofn.lpstrFilter = L"JSON files\0*.json\0All files\0*.*\0";
            ofn.lpstrFile   = fname;
            ofn.nMaxFile    = MAX_PATH;
            ofn.Flags       = OFN_OVERWRITEPROMPT;
            ofn.lpstrDefExt = L"json";
            if (GetSaveFileNameW(&ofn)) {
                ui_to_settings();
                write_text_file(fname, settings_to_json(g_settings));
            }
            break;
        }
        case IDC_PROFILE_IMPORT: {
            wchar_t fname[MAX_PATH] = {};
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = GetParent(hPage);
            ofn.lpstrFilter = L"JSON files\0*.json\0All files\0*.*\0";
            ofn.lpstrFile   = fname;
            ofn.nMaxFile    = MAX_PATH;
            ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                std::string json = read_text_file(fname);
                if (!json.empty()) { g_settings = settings_from_json(json); settings_to_ui(); }
            }
            break;
        }
        }
        break;
    }
    }
    return FALSE;
}

// ── Tab page array ────────────────────────────────────────────────────────────
// Order: 0=YAML 1=JSON 2=CSV 3=Parquet 4=Behavior & Profiles

static const TabPage PAGES[] = {
    { IDD_TAB_YAML,     L"YAML",     page_proc          },
    { IDD_TAB_JSON,     L"JSON",     page_proc          },
    { IDD_TAB_CSV,      L"CSV",      page_proc          },
    { IDD_TAB_PARQUET,  L"Parquet",  parquet_page_proc  },
    { IDD_TAB_BEHAVIOR, L"Behavior", behavior_page_proc },
};
static const int PAGE_COUNT = 5;
static HWND g_pages[PAGE_COUNT] = {};

#define P_YAML     g_pages[0]
#define P_JSON     g_pages[1]
#define P_CSV      g_pages[2]
#define P_PARQUET  g_pages[3]
#define P_BEHAVIOR g_pages[4]

static void show_page(int idx) {
    for (int i = 0; i < PAGE_COUNT; ++i)
        ShowWindow(g_pages[i], i == idx ? SW_SHOW : SW_HIDE);
}

// ─── control helpers ──────────────────────────────────────────────────────────

static bool radio_set(HWND dlg, int id) { return IsDlgButtonChecked(dlg, id) == BST_CHECKED; }
static bool chk_set  (HWND dlg, int id) { return IsDlgButtonChecked(dlg, id) == BST_CHECKED; }
static int  edit_int (HWND dlg, int id) {
    wchar_t buf[16] = {};
    GetDlgItemTextW(dlg, id, buf, 16);
    return buf[0] ? _wtoi(buf) : 0;
}
static void set_edit(HWND dlg, int id, int v) {
    wchar_t buf[16]; wsprintfW(buf, L"%d", v);
    SetDlgItemTextW(dlg, id, buf);
}

// ─── profile list helper (definition) ─────────────────────────────────────────

static void refresh_profile_list(HWND hPage) {
    HWND hList = GetDlgItem(hPage, IDC_PROFILE_LIST);
    SendMessage(hList, LB_RESETCONTENT, 0, 0);
    for (const auto& name : list_profiles())
        SendMessageW(hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
}

// ─── settings ↔ UI ────────────────────────────────────────────────────────────

static void settings_to_ui() {
    // YAML
    set_edit(P_YAML, IDC_YAML_INDENT,   g_settings.tidy.indent_step);
    set_edit(P_YAML, IDC_YAML_TABWIDTH, g_settings.tidy.tab_width);
    set_edit(P_YAML, IDC_YAML_MAXBLANK, g_settings.tidy.max_blank);
    CheckDlgButton(P_YAML, IDC_YAML_STRIP_WS, g_settings.tidy.strip_trailing_ws ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(P_YAML, IDC_YAML_FINAL_NL, g_settings.tidy.final_newline     ? BST_CHECKED : BST_UNCHECKED);

    // JSON
    CheckRadioButton(P_JSON, IDC_JSON_INDENT_2, IDC_JSON_INDENT_4,
        g_settings.json_indent == JsonIndent::Four ? IDC_JSON_INDENT_4 : IDC_JSON_INDENT_2);
    CheckDlgButton(P_JSON, IDC_JSON_SORT_KEYS, g_settings.json_sort_keys ? BST_CHECKED : BST_UNCHECKED);

    // CSV
    CheckRadioButton(P_CSV, IDC_CSV_QUOTE_IFNEEDED, IDC_CSV_QUOTE_ALWAYS,
        g_settings.csv_quote_mode == CsvQuoteMode::Always ? IDC_CSV_QUOTE_ALWAYS : IDC_CSV_QUOTE_IFNEEDED);

    // Parquet
    SetDlgItemTextW(P_PARQUET, IDC_DUCKDB_PATH, to_wide(g_settings.duckdb_path).c_str());

    // Behavior
    CheckDlgButton(P_BEHAVIOR, IDC_FORMAT_ON_SAVE, g_settings.format_on_save ? BST_CHECKED : BST_UNCHECKED);
}

static void ui_to_settings() {
    g_settings.tidy.indent_step = edit_int(P_YAML, IDC_YAML_INDENT);
    g_settings.tidy.tab_width   = edit_int(P_YAML, IDC_YAML_TABWIDTH);
    g_settings.tidy.max_blank   = edit_int(P_YAML, IDC_YAML_MAXBLANK);
    g_settings.tidy.strip_trailing_ws = chk_set(P_YAML, IDC_YAML_STRIP_WS);
    g_settings.tidy.final_newline     = chk_set(P_YAML, IDC_YAML_FINAL_NL);

    g_settings.json_indent    = radio_set(P_JSON, IDC_JSON_INDENT_4) ? JsonIndent::Four : JsonIndent::Two;
    g_settings.json_sort_keys = chk_set(P_JSON, IDC_JSON_SORT_KEYS);

    g_settings.csv_quote_mode = radio_set(P_CSV, IDC_CSV_QUOTE_ALWAYS) ? CsvQuoteMode::Always : CsvQuoteMode::OnlyIfNeeded;

    wchar_t wbuf[MAX_PATH] = {};
    GetDlgItemTextW(P_PARQUET, IDC_DUCKDB_PATH, wbuf, MAX_PATH);
    g_settings.duckdb_path = to_narrow(wbuf);

    g_settings.format_on_save = chk_set(P_BEHAVIOR, IDC_FORMAT_ON_SAVE);
}

// ─── settings dialog ──────────────────────────────────────────────────────────

static INT_PTR CALLBACK settings_proc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {

    case WM_INITDIALOG: {
        HWND hNav = GetDlgItem(hDlg, IDC_NAV);
        for (int i = 0; i < PAGE_COUNT; ++i)
            SendMessageW(hNav, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(PAGES[i].label));
        SendMessageW(hNav, LB_SETCURSEL, 0, 0);

        RECT navRc;
        GetWindowRect(hNav, &navRc);
        MapWindowPoints(HWND_DESKTOP, hDlg, reinterpret_cast<POINT*>(&navRc), 2);
        int cx = navRc.right + 5;
        int cy = navRc.top;

        for (int i = 0; i < PAGE_COUNT; ++i) {
            g_pages[i] = CreateDialog(g_module, MAKEINTRESOURCE(PAGES[i].id), hDlg, PAGES[i].proc);
            SetWindowPos(g_pages[i], HWND_TOP, cx, cy, 0, 0, SWP_NOSIZE | SWP_HIDEWINDOW);
        }

        settings_to_ui();
        show_page(0);
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_NAV && HIWORD(wParam) == LBN_SELCHANGE) {
            int sel = (int)SendMessageW(GetDlgItem(hDlg, IDC_NAV), LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) show_page(sel);
            return TRUE;
        }
        if (LOWORD(wParam) == IDOK) {
            ui_to_settings();
            save_settings();
            for (int i = 0; i < PAGE_COUNT; ++i)
                if (g_pages[i]) { DestroyWindow(g_pages[i]); g_pages[i] = nullptr; }
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            for (int i = 0; i < PAGE_COUNT; ++i)
                if (g_pages[i]) { DestroyWindow(g_pages[i]); g_pages[i] = nullptr; }
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        for (int i = 0; i < PAGE_COUNT; ++i)
            if (g_pages[i]) { DestroyWindow(g_pages[i]); g_pages[i] = nullptr; }
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

void show_settings_dialog(HWND parent) {
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LINK_CLASS };
    InitCommonControlsEx(&icc);
    DialogBox(g_module, MAKEINTRESOURCE(IDD_SETTINGS), parent, settings_proc);
}

// ─── about dialog ─────────────────────────────────────────────────────────────

static INT_PTR CALLBACK about_proc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemTextW(hDlg, IDC_ABOUT_BODY,
            L"Tidy, validate and convert YAML, JSON and CSV directly in Notepad++.\n\n"
            L"Features:\n"
            L"- Reindent / tidy YAML (Ctrl+Alt+Y)\n"
            L"- JSON: pretty-print, minify, sort keys, escape/unescape\n"
            L"- CSV: align columns, compact, delimiter convert, sort, transpose, add/remove quotes\n"
            L"- Convert between YAML, JSON, CSV and Parquet (via duckdb.exe)\n"
            L"- Format on Save, with a Settings dialog and saved profiles\n\n"
            L"This plugin is distributed under the MIT license.\n\n"
            L"For usage tips, see the plugin page. For updates or to report a bug, visit the project repository:");
        return TRUE;
    case WM_NOTIFY: {
        NMHDR* hdr = reinterpret_cast<NMHDR*>(lParam);
        if ((hdr->idFrom == IDC_ABOUT_LINK || hdr->idFrom == IDC_ABOUT_LINK_WEBSITE) &&
            (hdr->code == NM_CLICK || hdr->code == NM_RETURN)) {
            NMLINK* link = reinterpret_cast<NMLINK*>(lParam);
            ShellExecuteW(hDlg, L"open", link->item.szUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

void show_about_dialog(HWND parent) {
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LINK_CLASS };
    InitCommonControlsEx(&icc);
    DialogBox(g_module, MAKEINTRESOURCE(IDD_ABOUT), parent, about_proc);
}
