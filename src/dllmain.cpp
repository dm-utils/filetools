#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <cwctype>
#include "yaml_tidy.h"
#include "yaml_convert.h"
#include "json_tools.h"
#include "csv_tools.h"
#include "settings.h"
#include "resource.h"

YamlSettings g_settings;

// ─── Notepad++ / Scintilla ABI (only what this plugin uses) ──────────────────

struct NppData {
    HWND _nppHandle;
    HWND _scintillaMainHandle;
    HWND _scintillaSecondHandle;
};
struct ShortcutKey {
    bool  _isCtrl;
    bool  _isAlt;
    bool  _isShift;
    UCHAR _key;
};
typedef void (*PFUNCPLUGINCMD)();
struct FuncItem {
    wchar_t        _menuItemName[64];
    PFUNCPLUGINCMD _pFunc;
    int            _cmdID;
    bool           _init2Check;
    ShortcutKey*   _pShKey;
};
struct SCNotification {
    HWND         hwndFrom;
    uintptr_t    idFrom;
    unsigned int code;
};
enum {
    SCI_GETLENGTH         = 2006,
    SCI_GETTEXT           = 2182,
    SCI_SETTEXT           = 2181,
    SCI_BEGINUNDOACTION   = 2560,
    SCI_ENDUNDOACTION     = 2561,
    SCI_GETSELECTIONSTART = 2143,
    SCI_GETSELECTIONEND   = 2145,
    SCI_REPLACESEL        = 2170,
    SCI_GETCURRENTPOS     = 2008,
    SCI_LINEFROMPOSITION  = 2166,
    SCI_POSITIONFROMLINE  = 2167,
};
enum {
    NPPM_GETCURRENTSCINTILLA = 2028,
    NPPM_DOOPEN              = 2101,
    NPPM_MENUCOMMAND        = 2072,   // NPPMSG + 48
    NPPM_GETFULLCURRENTPATH  = 2082,   // NPPMSG + 58
    IDM_FILE_NEW            = 41001,
    NPPN_READY              = 1001,
    NPPN_FILEBEFORESAVE      = 1007,
    NPPN_FILESAVED          = 1008,
};

// ─── globals ────────────────────────────────────────────────────────────────

static NppData     g_npp    = {};
static HINSTANCE   g_module = nullptr;
static DWORD       g_save_tick = 0;
static bool        g_menu_built = false;
static ShortcutKey g_sk_tidy = { true, true, false, 'Y' };   // Ctrl+Alt+Y

static const int   NFUNCS = 36;
static FuncItem    g_funcs[NFUNCS] = {};

// g_funcs is a FLAT list (Notepad++ requires it). At NPPN_READY the plugin
// regroups it into submenus in build_all_submenus() — YAML / JSON / CSV /
// Parquet / Format on Save. Indices below are referenced there, so keep them
// in sync.
//
// Each format submenu carries the full "from this format" conversion matrix.
//
//  0  Reindent / tidy (Ctrl+Alt+Y)   YAML, line-based, comments preserved
//  1  ---
//  2  Validate                       YAML/JSON, libyaml       -> YAML
//  3  YAML -> JSON                    libyaml                  -> YAML
//  4  YAML -> CSV                     libyaml (via JSON)       -> YAML
//  5  YAML -> Parquet (file)          duckdb.exe (via JSON)    -> YAML
//  6  ---
//  7  JSON: Pretty-print             libyaml                  -> JSON
//  8  JSON: Minify                   libyaml                  -> JSON
//  9  JSON: Sort keys                libyaml                  -> JSON
//  10 JSON: Escape as string         pure                     -> JSON
//  11 JSON: Unescape string          pure                     -> JSON
//  12 JSON -> YAML                    libyaml                  -> JSON
//  13 JSON -> CSV                     libyaml                  -> JSON
//  14 JSON -> Parquet (file)          duckdb.exe               -> JSON
//  15 ---
//  16 CSV: Align columns             pure                     -> CSV
//  17 CSV: Compact                   pure                     -> CSV
//  18 CSV: To comma delimiter        pure                     -> CSV
//  19 CSV: To semicolon delimiter    pure                     -> CSV
//  20 CSV: Sort by column at cursor  pure                     -> CSV
//  21 CSV: Transpose                 pure                     -> CSV
//  22 CSV -> JSON                     pure                     -> CSV
//  23 CSV -> YAML                     libyaml (via JSON)       -> CSV
//  24 CSV -> Parquet (file)           duckdb.exe               -> CSV
//  25 ---
//  26 Parquet -> CSV  (preview)      read-only, duckdb.exe    -> Parquet
//  27 Parquet -> JSON (preview)      read-only, duckdb.exe    -> Parquet
//  28 Parquet -> YAML (preview)      duckdb.exe (via JSON)    -> Parquet
//  29 ---
//  30 Format on Save: On
//  31 Format on Save: Off
//  32 ---
//  33 Settings...                    (stub until the tabbed dialog)
//  34 About
//  35 Help

// ─── editor helpers ─────────────────────────────────────────────────────────

static HWND current_editor() {
    int which = 0;
    SendMessage(g_npp._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
    return which ? g_npp._scintillaSecondHandle : g_npp._scintillaMainHandle;
}
static std::string get_text() {
    HWND sci = current_editor();
    int len = (int)SendMessage(sci, SCI_GETLENGTH, 0, 0);
    if (len <= 0) return {};
    std::string buf(len + 1, '\0');
    SendMessage(sci, SCI_GETTEXT, (WPARAM)(len + 1), (LPARAM)buf.data());
    buf.resize(len);
    return buf;
}
static void set_text(const std::string& t) {
    HWND sci = current_editor();
    SendMessage(sci, SCI_BEGINUNDOACTION, 0, 0);
    SendMessage(sci, SCI_SETTEXT, 0, (LPARAM)t.c_str());
    SendMessage(sci, SCI_ENDUNDOACTION, 0, 0);
}
static void replace_sel(const std::string& t) {
    HWND sci = current_editor();
    SendMessage(sci, SCI_BEGINUNDOACTION, 0, 0);
    SendMessage(sci, SCI_REPLACESEL, 0, (LPARAM)t.c_str());
    SendMessage(sci, SCI_ENDUNDOACTION, 0, 0);
}
static void apply_transform(std::string (*fn)(const std::string&)) {
    HWND sci = current_editor();
    int ss = (int)SendMessage(sci, SCI_GETSELECTIONSTART, 0, 0);
    int se = (int)SendMessage(sci, SCI_GETSELECTIONEND,   0, 0);
    if (ss != se) {
        std::string buf = get_text();
        if (se > (int)buf.size()) return;
        replace_sel(fn(buf.substr(ss, se - ss)));
    } else {
        std::string buf = get_text();
        if (buf.empty()) return;
        set_text(fn(buf));
    }
}

// ─── commands ───────────────────────────────────────────────────────────────

static std::string tidy_fn(const std::string& s) { return yaml_tidy(s, g_settings.tidy); }

static void cmd_tidy() { apply_transform(tidy_fn); }

// Current selection, or the whole document if nothing is selected.
static std::string current_src() {
    HWND sci = current_editor();
    int ss = (int)SendMessage(sci, SCI_GETSELECTIONSTART, 0, 0);
    int se = (int)SendMessage(sci, SCI_GETSELECTIONEND,   0, 0);
    std::string buf = get_text();
    if (ss != se && se <= (int)buf.size()) return buf.substr(ss, se - ss);
    return buf;
}

static void msg(const std::wstring& w, UINT icon) {
    MessageBoxW(g_npp._nppHandle, w.c_str(), L"Datamodder File Tools", MB_OK | icon);
}
static std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

static void cmd_validate() {
    if (!yaml_convert_available()) { msg(widen(yaml_validate("")), MB_ICONWARNING); return; }
    std::string src = current_src();
    if (src.empty()) return;
    std::string problem = yaml_validate(src);
    if (problem.empty()) msg(L"Geldige YAML.", MB_ICONINFORMATION);
    else                 msg(L"Ongeldige YAML:\n\n" + widen(problem), MB_ICONWARNING);
}

// Replace the buffer/selection with fn(src), unless fn returned an error
// (result begins with SOH "\001").
static void run_string_op(std::string (*fn)(const std::string&)) {
    HWND sci = current_editor();
    int ss = (int)SendMessage(sci, SCI_GETSELECTIONSTART, 0, 0);
    int se = (int)SendMessage(sci, SCI_GETSELECTIONEND,   0, 0);
    std::string buf = get_text();
    bool sel = (ss != se && se <= (int)buf.size());
    std::string src = sel ? buf.substr(ss, se - ss) : buf;
    if (src.empty()) return;

    std::string r = fn(src);
    if (!r.empty() && r[0] == '\001') { msg(widen(r.substr(1)), MB_ICONWARNING); return; }
    if (sel) replace_sel(r); else set_text(r);
}

// Same, but first check that libyaml was compiled in.
static void run_convert(std::string (*fn)(const std::string&)) {
    if (!yaml_convert_available()) {
        std::string note = fn("");
        msg(widen(note.empty() || note[0] != '\001' ? note : note.substr(1)), MB_ICONWARNING);
        return;
    }
    run_string_op(fn);
}

static void cmd_yaml_to_json() { run_convert(yaml_to_json); }
static void cmd_json_to_yaml() { run_convert(json_to_yaml); }
static void cmd_json_pretty()   { run_convert(json_pretty); }
static void cmd_json_minify()   { run_convert(json_minify); }
static void cmd_json_sort()     { run_convert(json_sort_keys); }
static void cmd_json_escape()   { run_string_op(json_escape); }
static void cmd_json_unescape() { run_string_op(json_unescape); }

static void cmd_csv_align()     { run_string_op(csv_align); }
static void cmd_csv_compact()   { run_string_op(csv_compact); }
static void cmd_csv_comma()     { run_string_op(csv_to_comma); }
static void cmd_csv_semicolon() { run_string_op(csv_to_semicolon); }
static void cmd_csv_transpose() { run_string_op(csv_transpose); }
static void cmd_csv_to_json()   { run_string_op(csv_to_json); }
static void cmd_json_to_csv()   { run_convert(json_to_csv); }

// Composite conversions — JSON is the hub. Errors (leading SOH) propagate.
static bool is_conv_err(const std::string& s) { return !s.empty() && s[0] == '\001'; }

static std::string yaml_to_csv_conv(const std::string& src) {   // YAML -> JSON -> CSV
    std::string j = yaml_to_json(src);
    return is_conv_err(j) ? j : jsontools::to_csv(j);
}
static std::string csv_to_yaml_conv(const std::string& src) {   // CSV -> JSON -> YAML
    std::string j = csv_to_json(src);
    return is_conv_err(j) ? j : json_to_yaml(j);
}
static void cmd_yaml_to_csv()  { run_convert(yaml_to_csv_conv); }
static void cmd_csv_to_yaml()  { run_convert(csv_to_yaml_conv); }

// Which CSV field the caret sits in on its current line (delimiter sniffed
// from the first line; quote-aware). Used by "Sort by column at cursor".
static int cursor_csv_col() {
    HWND sci = current_editor();
    int pos    = (int)SendMessage(sci, SCI_GETCURRENTPOS, 0, 0);
    int line   = (int)SendMessage(sci, SCI_LINEFROMPOSITION, pos, 0);
    int lstart = (int)SendMessage(sci, SCI_POSITIONFROMLINE, line, 0);
    std::string buf = get_text();
    if (lstart < 0 || pos < lstart || pos > (int)buf.size()) return 0;

    char d = ',';
    {
        size_t e = buf.find('\n');
        std::string f = buf.substr(0, e == std::string::npos ? buf.size() : e);
        int best = -1;
        for (char cd : { ',', ';', '\t', '|' }) {
            int n = 0; bool q = false;
            for (char c : f) {
                if (q) { if (c == '"') q = false; }
                else if (c == '"') q = true;
                else if (c == cd) ++n;
            }
            if (n > best) { best = n; d = cd; }
        }
    }
    int col = 0; bool q = false;
    for (int i = lstart; i < pos; ++i) {
        char c = buf[i];
        if (q) { if (c == '"') q = false; }
        else if (c == '"') q = true;
        else if (c == d) ++col;
    }
    return col;
}

static void cmd_csv_sort() {
    int col = cursor_csv_col();
    HWND sci = current_editor();
    int ss = (int)SendMessage(sci, SCI_GETSELECTIONSTART, 0, 0);
    int se = (int)SendMessage(sci, SCI_GETSELECTIONEND,   0, 0);
    std::string buf = get_text();
    bool sel = (ss != se && se <= (int)buf.size());
    std::string src = sel ? buf.substr(ss, se - ss) : buf;
    if (src.empty()) return;
    std::string r = csv_sort_by_column(src, col);
    if (!r.empty() && r[0] == '\001') { msg(widen(r.substr(1)), MB_ICONWARNING); return; }
    if (sel) replace_sel(r); else set_text(r);
}
// ─── Parquet (shells out to DuckDB) ────────────────────────────────────────
//
// Parquet is columnar binary: there is no in-buffer editing story and a real
// reader (Arrow) is far too big to link into an NPP plugin. So everything
// Parquet goes through duckdb.exe:
//   Parquet -> CSV / JSON / YAML   read the file, dump into a new tab (read-only)
//   CSV / JSON / YAML -> Parquet   write a .parquet file next to the document
//
// YAML is not a format DuckDB knows, so the YAML directions route through JSON
// (yaml_to_json on the way in, json_to_yaml on the way out).

enum class Fmt { Csv, Json, Yaml };

// Run cmdline, capture stdout+stderr into `out`. Returns the process exit code,
// or -1 if it could not be started.
static int run_capture(const std::wstring& cmdline, std::string& out) {
    SECURITY_ATTRIBUTES sa = { sizeof sa, nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof si };
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError  = wr;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

    std::wstring cl = cmdline;   // CreateProcessW may modify the buffer
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(nullptr, &cl[0], nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); return -1; }

    char buf[4096];
    DWORD n = 0;
    while (ReadFile(rd, buf, sizeof buf, &n, nullptr) && n) out.append(buf, n);
    CloseHandle(rd);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

// Locate duckdb.exe: PATH, then next to this DLL. Empty + a message box if
// it can't be found anywhere.
static std::wstring duckdb_or_warn() {
    wchar_t found[MAX_PATH] = {};
    if (SearchPathW(nullptr, L"duckdb", L".exe", MAX_PATH, found, nullptr)) return found;
    wchar_t here[MAX_PATH] = {};
    GetModuleFileNameW(g_module, here, MAX_PATH);
    if (wchar_t* s = wcsrchr(here, L'\\')) {
        wcscpy_s(s + 1, MAX_PATH - (s + 1 - here), L"duckdb.exe");
        if (GetFileAttributesW(here) != INVALID_FILE_ATTRIBUTES) return here;
    }
    msg(L"duckdb.exe niet gevonden. Zet 'm op je PATH of naast de plugin "
        L"(plugins\\FileTools\\duckdb.exe). Download: https://duckdb.org/",
        MB_ICONWARNING);
    return L"";
}

// Full path of the current document ("" if the buffer was never saved).
static std::wstring current_doc_path() {
    wchar_t path[MAX_PATH] = {};
    SendMessage(g_npp._nppHandle, NPPM_GETFULLCURRENTPATH, (WPARAM)MAX_PATH, (LPARAM)path);
    return path;
}

// Lower-cased extension incl. the dot, e.g. L".parquet".
static std::wstring lower_ext(const std::wstring& p) {
    size_t dot = p.find_last_of(L".\\/");
    if (dot == std::wstring::npos || p[dot] != L'.') return L"";
    std::wstring e = p.substr(dot);
    for (auto& c : e) c = (wchar_t)towlower(c);
    return e;
}

// Backslashes -> forward slashes, so the path is safe inside a single-quoted
// DuckDB SQL string literal.
static std::wstring fwd(std::wstring p) {
    for (auto& c : p) if (c == L'\\') c = L'/';
    return p;
}

// Parquet -> CSV / JSON / YAML : dump the rows into a fresh tab. Read-only.
static void preview_parquet(Fmt to) {
    std::wstring p = current_doc_path();
    if (p.empty() || GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES ||
        lower_ext(p) != L".parquet") {
        msg(L"Open eerst een opgeslagen .parquet-bestand.", MB_ICONWARNING);
        return;
    }
    std::wstring duck = duckdb_or_warn();
    if (duck.empty()) return;

    // YAML has no DuckDB writer — ask for JSON and convert it afterwards.
    const wchar_t* flag = (to == Fmt::Csv) ? L"-csv" : L"-json";
    std::wstring cmd = L"\"" + duck + L"\" " + flag +
        L" -c \"SELECT * FROM read_parquet('" + fwd(p) + L"') LIMIT 100000\"";

    std::string out;
    int code = run_capture(cmd, out);
    if (code != 0) {
        std::string m = out.empty() ? "duckdb kon niet gestart worden" : out;
        msg(L"Parquet preview mislukt:\n\n" + widen(m.substr(0, 2000)), MB_ICONWARNING);
        return;
    }
    if (to == Fmt::Yaml) {
        std::string y = json_to_yaml(out);
        if (is_conv_err(y)) { msg(L"JSON \x2192 YAML mislukt:\n\n" + widen(y.substr(1)), MB_ICONWARNING); return; }
        out.swap(y);
    }
    SendMessage(g_npp._nppHandle, NPPM_MENUCOMMAND, 0, IDM_FILE_NEW);  // new tab
    set_text(out);
}

static void cmd_parquet_csv()  { preview_parquet(Fmt::Csv); }
static void cmd_parquet_json() { preview_parquet(Fmt::Json); }
static void cmd_parquet_yaml() { preview_parquet(Fmt::Yaml); }

// CSV / JSON / YAML -> Parquet : write <current dir>\<name>.parquet from the
// current buffer (unsaved edits included — the buffer is spilled to a temp
// file). YAML is converted to JSON first.
static void export_parquet(Fmt from) {
    std::wstring p = current_doc_path();
    std::wstring ext = lower_ext(p);
    bool ok_ext = (from == Fmt::Csv  && ext == L".csv")  ||
                  (from == Fmt::Json && ext == L".json") ||
                  (from == Fmt::Yaml && (ext == L".yaml" || ext == L".yml"));
    if (!ok_ext) {
        msg(from == Fmt::Csv  ? L"Dit werkt op een CSV-document." :
            from == Fmt::Json ? L"Dit werkt op een JSON-document (array van objecten)." :
                                L"Dit werkt op een YAML-document.", MB_ICONWARNING);
        return;
    }
    std::wstring duck = duckdb_or_warn();
    if (duck.empty()) return;

    // Bytes and temp extension DuckDB will read: YAML is pre-converted to JSON.
    std::string payload = get_text();
    std::wstring tmp_ext = (from == Fmt::Csv) ? L".csv" : L".json";
    if (from == Fmt::Yaml) {
        if (!yaml_convert_available()) { msg(widen(yaml_validate("")), MB_ICONWARNING); return; }
        std::string j = yaml_to_json(payload);
        if (is_conv_err(j)) { msg(L"YAML \x2192 JSON mislukt:\n\n" + widen(j.substr(1)), MB_ICONWARNING); return; }
        payload.swap(j);
    }

    wchar_t tdir[MAX_PATH] = {}, tin[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tdir);
    GetTempFileNameW(tdir, L"ft_", 0, tin);
    std::wstring in = tin; DeleteFileW(tin); in += tmp_ext;
    {
        HANDLE h = CreateFileW(in.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) { msg(L"Kon geen tijdelijk bestand schrijven.", MB_ICONWARNING); return; }
        DWORD wr = 0;
        WriteFile(h, payload.data(), (DWORD)payload.size(), &wr, nullptr);
        CloseHandle(h);
    }

    // Output: <same folder>\<same stem>.parquet, or the temp folder if unsaved.
    std::wstring out;
    if (!p.empty() && p.find_last_of(L"\\/") != std::wstring::npos)
        out = p.substr(0, p.find_last_of(L'.')) + L".parquet";
    else
        out = std::wstring(tdir) + L"export.parquet";

    std::wstring reader = (tmp_ext == L".csv") ? L"read_csv_auto" : L"read_json_auto";
    std::wstring cmd = L"\"" + duck + L"\" -c \"COPY (SELECT * FROM " + reader +
        L"('" + fwd(in) + L"')) TO '" + fwd(out) + L"' (FORMAT PARQUET)\"";

    std::string res;
    int code = run_capture(cmd, res);
    DeleteFileW(in.c_str());
    if (code != 0) {
        std::string m = res.empty() ? "duckdb kon niet gestart worden" : res;
        msg(L"Omzetten naar Parquet mislukt:\n\n" + widen(m.substr(0, 2000)), MB_ICONWARNING);
        return;
    }
    msg(L"Parquet geschreven:\n\n" + out, MB_ICONINFORMATION);
}

static void cmd_csv_to_parquet()  { export_parquet(Fmt::Csv); }
static void cmd_json_to_parquet() { export_parquet(Fmt::Json); }
static void cmd_yaml_to_parquet() { export_parquet(Fmt::Yaml); }

static void cmd_fos_on()  { g_settings.format_on_save = true; }
static void cmd_fos_off() { g_settings.format_on_save = false; }
static void cmd_settings() {
    MessageBoxW(g_npp._nppHandle,
        L"De instellingen-UI (indent, keys, lint, profielen) volgt.\n"
        L"Voorlopig: 2 spaties per niveau, tabs -> spaties, max 1 lege regel.",
        L"Datamodder File Tools", MB_OK | MB_ICONINFORMATION);
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
            L"- CSV: align columns, compact, delimiter convert, sort, transpose\n"
            L"- Convert between YAML, JSON, CSV and Parquet (via duckdb.exe)\n"
            L"- Format on Save\n\n"
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

static void cmd_about() {
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LINK_CLASS };
    InitCommonControlsEx(&icc);
    DialogBox(g_module, MAKEINTRESOURCE(IDD_ABOUT), g_npp._nppHandle, about_proc);
}
static void cmd_help() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(g_module, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"help.txt");
    SendMessage(g_npp._nppHandle, NPPM_DOOPEN, 0, (LPARAM)path);
}

// ─── menu construction ─────────────────────────────────────────────────────
//
// Notepad++ builds one flat menu from g_funcs. Once, at NPPN_READY, we tear
// the flat items out and re-insert them grouped: YAML / JSON / CSV / Parquet /
// Format on Save. Indices below match the g_funcs slot map further up.

static HMENU find_my_menu() {
    HMENU bar = GetMenu(g_npp._nppHandle);
    if (!bar) return nullptr;
    for (int i = GetMenuItemCount(bar) - 1; i >= 0; --i) {
        HMENU top = GetSubMenu(bar, i);
        if (!top) continue;
        for (int j = 0; j < GetMenuItemCount(top); ++j) {
            wchar_t buf[128] = {};
            GetMenuStringW(top, j, buf, 128, MF_BYPOSITION);
            if (_wcsicmp(buf, L"Datamodder File Tools") == 0) return GetSubMenu(top, j);
        }
    }
    return nullptr;
}

static void build_all_submenus(HMENU hMine) {
    UINT id_validate = g_funcs[2]._cmdID;
    UINT id_y2j = g_funcs[3]._cmdID, id_y2c = g_funcs[4]._cmdID, id_y2pq = g_funcs[5]._cmdID;
    UINT id_jpretty = g_funcs[7]._cmdID, id_jmin = g_funcs[8]._cmdID, id_jsort = g_funcs[9]._cmdID;
    UINT id_jesc = g_funcs[10]._cmdID, id_junesc = g_funcs[11]._cmdID;
    UINT id_j2y = g_funcs[12]._cmdID, id_j2c = g_funcs[13]._cmdID, id_j2pq = g_funcs[14]._cmdID;
    UINT id_calign = g_funcs[16]._cmdID, id_ccompact = g_funcs[17]._cmdID;
    UINT id_ccomma = g_funcs[18]._cmdID, id_csemi = g_funcs[19]._cmdID;
    UINT id_csort = g_funcs[20]._cmdID, id_ctrans = g_funcs[21]._cmdID;
    UINT id_c2j = g_funcs[22]._cmdID, id_c2y = g_funcs[23]._cmdID, id_c2pq = g_funcs[24]._cmdID;
    UINT id_pq2c = g_funcs[26]._cmdID, id_pq2j = g_funcs[27]._cmdID, id_pq2y = g_funcs[28]._cmdID;
    UINT id_fos_on = g_funcs[30]._cmdID, id_fos_off = g_funcs[31]._cmdID;

    // Remove flat slots 2..31 (Validate .. Format on Save: Off + separators),
    // high to low. Left behind: 0 tidy, 1 sep, 2 sep(32), 3 Settings, 4 About,
    // 5 Help.
    for (int i = 31; i >= 2; --i) DeleteMenu(hMine, i, MF_BYPOSITION);

    HMENU hY = CreatePopupMenu();
    AppendMenuW(hY, MF_STRING,    id_validate, L"Validate");
    AppendMenuW(hY, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hY, MF_STRING,    id_y2j,      L"YAML \x2192 JSON");
    AppendMenuW(hY, MF_STRING,    id_y2c,      L"YAML \x2192 CSV");
    AppendMenuW(hY, MF_STRING,    id_y2pq,     L"YAML \x2192 Parquet  (file)");
    InsertMenuW(hMine, 2, MF_BYPOSITION | MF_POPUP, (UINT_PTR)hY, L"YAML");

    HMENU hJ = CreatePopupMenu();
    AppendMenuW(hJ, MF_STRING,    id_jpretty, L"Pretty-print");
    AppendMenuW(hJ, MF_STRING,    id_jmin,    L"Minify");
    AppendMenuW(hJ, MF_STRING,    id_jsort,   L"Sort keys");
    AppendMenuW(hJ, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hJ, MF_STRING,    id_jesc,    L"Escape as string");
    AppendMenuW(hJ, MF_STRING,    id_junesc,  L"Unescape string");
    AppendMenuW(hJ, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hJ, MF_STRING,    id_j2y,     L"JSON \x2192 YAML");
    AppendMenuW(hJ, MF_STRING,    id_j2c,     L"JSON \x2192 CSV");
    AppendMenuW(hJ, MF_STRING,    id_j2pq,    L"JSON \x2192 Parquet  (file)");
    InsertMenuW(hMine, 3, MF_BYPOSITION | MF_POPUP, (UINT_PTR)hJ, L"JSON");

    HMENU hC = CreatePopupMenu();
    AppendMenuW(hC, MF_STRING,    id_calign,   L"Align columns");
    AppendMenuW(hC, MF_STRING,    id_ccompact, L"Compact");
    AppendMenuW(hC, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hC, MF_STRING,    id_ccomma,   L"To comma delimiter");
    AppendMenuW(hC, MF_STRING,    id_csemi,    L"To semicolon delimiter");
    AppendMenuW(hC, MF_STRING,    id_csort,    L"Sort by column at cursor");
    AppendMenuW(hC, MF_STRING,    id_ctrans,   L"Transpose");
    AppendMenuW(hC, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hC, MF_STRING,    id_c2j,      L"CSV \x2192 JSON");
    AppendMenuW(hC, MF_STRING,    id_c2y,      L"CSV \x2192 YAML");
    AppendMenuW(hC, MF_STRING,    id_c2pq,     L"CSV \x2192 Parquet  (file)");
    InsertMenuW(hMine, 4, MF_BYPOSITION | MF_POPUP, (UINT_PTR)hC, L"CSV");

    HMENU hP = CreatePopupMenu();
    AppendMenuW(hP, MF_STRING,    id_pq2c, L"Parquet \x2192 CSV  (preview)");
    AppendMenuW(hP, MF_STRING,    id_pq2j, L"Parquet \x2192 JSON  (preview)");
    AppendMenuW(hP, MF_STRING,    id_pq2y, L"Parquet \x2192 YAML  (preview)");
    AppendMenuW(hP, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hP, MF_STRING,    id_c2pq, L"CSV \x2192 Parquet  (file)");
    AppendMenuW(hP, MF_STRING,    id_j2pq, L"JSON \x2192 Parquet  (file)");
    AppendMenuW(hP, MF_STRING,    id_y2pq, L"YAML \x2192 Parquet  (file)");
    InsertMenuW(hMine, 5, MF_BYPOSITION | MF_POPUP, (UINT_PTR)hP, L"Parquet");

    InsertMenuW(hMine, 6, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);

    HMENU hFOS = CreatePopupMenu();
    AppendMenuW(hFOS, MF_STRING, id_fos_on,  L"On");
    AppendMenuW(hFOS, MF_STRING, id_fos_off, L"Off");
    InsertMenuW(hMine, 7, MF_BYPOSITION | MF_POPUP, (UINT_PTR)hFOS, L"Format on Save");

    DrawMenuBar(g_npp._nppHandle);
}

static void try_build_menu() {
    if (g_menu_built) return;
    HMENU hMine = find_my_menu();
    if (!hMine || GetMenuItemCount(hMine) < NFUNCS) return;
    build_all_submenus(hMine);
    g_menu_built = true;
}

// ─── plugin exports ─────────────────────────────────────────────────────────

extern "C" {

__declspec(dllexport) const wchar_t* getName() { return L"Datamodder File Tools"; }

__declspec(dllexport) void setInfo(NppData d) { g_npp = d; }

__declspec(dllexport) FuncItem* getFuncsArray(int* n) { *n = NFUNCS; return g_funcs; }

__declspec(dllexport) void beNotified(SCNotification* n) {
    if (n->code == NPPN_READY) try_build_menu();
    if ((n->code == NPPN_FILEBEFORESAVE || n->code == NPPN_FILESAVED) && g_settings.format_on_save) {
        DWORD now = GetTickCount();
        if (now - g_save_tick > 1000) { g_save_tick = now; cmd_tidy(); }
    }
}

__declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) { try_build_menu(); return FALSE; }

__declspec(dllexport) BOOL isUnicode() { return TRUE; }

} // extern "C"

// ─── DLL entry ──────────────────────────────────────────────────────────────

static void init_func(int i, const wchar_t* name, PFUNCPLUGINCMD fn, ShortcutKey* sk = nullptr) {
    wcscpy_s(g_funcs[i]._menuItemName, name);
    g_funcs[i]._pFunc  = fn;
    g_funcs[i]._pShKey = sk;
}

BOOL APIENTRY DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = h;
        DisableThreadLibraryCalls(h);
        init_func(0,  L"Reindent / tidy\tCtrl+Alt+Y", cmd_tidy, &g_sk_tidy);
        init_func(1,  L"-",                            nullptr);
        init_func(2,  L"Validate",                     cmd_validate);
        init_func(3,  L"YAML \x2192 JSON",             cmd_yaml_to_json);
        init_func(4,  L"YAML \x2192 CSV",              cmd_yaml_to_csv);
        init_func(5,  L"YAML \x2192 Parquet  (file)",  cmd_yaml_to_parquet);
        init_func(6,  L"-",                            nullptr);
        init_func(7,  L"JSON: Pretty-print",           cmd_json_pretty);
        init_func(8,  L"JSON: Minify",                 cmd_json_minify);
        init_func(9,  L"JSON: Sort keys",              cmd_json_sort);
        init_func(10, L"JSON: Escape as string",       cmd_json_escape);
        init_func(11, L"JSON: Unescape string",        cmd_json_unescape);
        init_func(12, L"JSON \x2192 YAML",             cmd_json_to_yaml);
        init_func(13, L"JSON \x2192 CSV",              cmd_json_to_csv);
        init_func(14, L"JSON \x2192 Parquet  (file)",  cmd_json_to_parquet);
        init_func(15, L"-",                            nullptr);
        init_func(16, L"CSV: Align columns",           cmd_csv_align);
        init_func(17, L"CSV: Compact",                 cmd_csv_compact);
        init_func(18, L"CSV: To comma delimiter",      cmd_csv_comma);
        init_func(19, L"CSV: To semicolon delimiter",  cmd_csv_semicolon);
        init_func(20, L"CSV: Sort by column at cursor",cmd_csv_sort);
        init_func(21, L"CSV: Transpose",               cmd_csv_transpose);
        init_func(22, L"CSV \x2192 JSON",              cmd_csv_to_json);
        init_func(23, L"CSV \x2192 YAML",              cmd_csv_to_yaml);
        init_func(24, L"CSV \x2192 Parquet  (file)",   cmd_csv_to_parquet);
        init_func(25, L"-",                            nullptr);
        init_func(26, L"Parquet \x2192 CSV  (preview)",  cmd_parquet_csv);
        init_func(27, L"Parquet \x2192 JSON  (preview)", cmd_parquet_json);
        init_func(28, L"Parquet \x2192 YAML  (preview)", cmd_parquet_yaml);
        init_func(29, L"-",                            nullptr);
        init_func(30, L"Format on Save: On",           cmd_fos_on);
        init_func(31, L"Format on Save: Off",          cmd_fos_off);
        init_func(32, L"-",                            nullptr);
        init_func(33, L"Settings...",                  cmd_settings);
        init_func(34, L"About",                        cmd_about);
        init_func(35, L"Help",                         cmd_help);
    }
    return TRUE;
}
