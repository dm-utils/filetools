#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include "yaml_tidy.h"
#include "yaml_convert.h"
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
};
enum {
    NPPM_GETCURRENTSCINTILLA = 2028,
    NPPM_DOOPEN              = 2101,
    NPPN_FILEBEFORESAVE      = 1007,
    NPPN_FILESAVED          = 1008,
};

// ─── globals ────────────────────────────────────────────────────────────────

static NppData     g_npp    = {};
static HINSTANCE   g_module = nullptr;
static DWORD       g_save_tick = 0;
static ShortcutKey g_sk_tidy = { true, true, false, 'Y' };   // Ctrl+Alt+Y

static const int   NFUNCS = 12;
static FuncItem    g_funcs[NFUNCS] = {};

// Slot layout (flat menu — submenus come with the Settings milestone):
//  0  Reindent / tidy (Ctrl+Alt+Y)   line-based, comments preserved
//  1  ---
//  2  Validate                       libyaml
//  3  YAML -> JSON                    libyaml, data-only
//  4  JSON -> YAML                    libyaml, data-only
//  5  ---
//  6  Format on Save: On
//  7  Format on Save: Off
//  8  ---
//  9  Settings...                     (stub until the tabbed dialog)
//  10 About
//  11 Help

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
    MessageBoxW(g_npp._nppHandle, w.c_str(), L"Datamodder YAML Tools", MB_OK | icon);
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
// (result begins with SOH "\001") or libyaml is missing.
static void run_convert(std::string (*fn)(const std::string&)) {
    if (!yaml_convert_available()) {
        std::string note = fn("");
        msg(widen(note.empty() || note[0] != '\001' ? note : note.substr(1)), MB_ICONWARNING);
        return;
    }
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

static void cmd_yaml_to_json() { run_convert(yaml_to_json); }
static void cmd_json_to_yaml() { run_convert(json_to_yaml); }
static void cmd_fos_on()  { g_settings.format_on_save = true; }
static void cmd_fos_off() { g_settings.format_on_save = false; }
static void cmd_settings() {
    MessageBoxW(g_npp._nppHandle,
        L"De instellingen-UI (indent, keys, lint, profielen) volgt.\n"
        L"Voorlopig: 2 spaties per niveau, tabs -> spaties, max 1 lege regel.",
        L"Datamodder YAML Tools", MB_OK | MB_ICONINFORMATION);
}
static void cmd_about() {
    MessageBoxW(g_npp._nppHandle,
        L"Datamodder YAML Tools  " YAMLTOOLS_VERSION_W L"\n\n"
        L"Reindent, tidy and (later) convert/validate YAML.\n"
        L"64-bit Notepad++ only.  MIT.\n"
        L"https://github.com/dm-utils/yamltools",
        L"About", MB_OK | MB_ICONINFORMATION);
}
static void cmd_help() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(g_module, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"help.txt");
    SendMessage(g_npp._nppHandle, NPPM_DOOPEN, 0, (LPARAM)path);
}

// ─── plugin exports ─────────────────────────────────────────────────────────

extern "C" {

__declspec(dllexport) const wchar_t* getName() { return L"Datamodder YAML Tools"; }

__declspec(dllexport) void setInfo(NppData d) { g_npp = d; }

__declspec(dllexport) FuncItem* getFuncsArray(int* n) { *n = NFUNCS; return g_funcs; }

__declspec(dllexport) void beNotified(SCNotification* n) {
    if ((n->code == NPPN_FILEBEFORESAVE || n->code == NPPN_FILESAVED) && g_settings.format_on_save) {
        DWORD now = GetTickCount();
        if (now - g_save_tick > 1000) { g_save_tick = now; cmd_tidy(); }
    }
}

__declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) { return FALSE; }

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
        init_func(4,  L"JSON \x2192 YAML",             cmd_json_to_yaml);
        init_func(5,  L"-",                            nullptr);
        init_func(6,  L"Format on Save: On",           cmd_fos_on);
        init_func(7,  L"Format on Save: Off",          cmd_fos_off);
        init_func(8,  L"-",                            nullptr);
        init_func(9,  L"Settings...",                  cmd_settings);
        init_func(10, L"About",                        cmd_about);
        init_func(11, L"Help",                         cmd_help);
    }
    return TRUE;
}
