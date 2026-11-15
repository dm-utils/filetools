#include "settings_io.h"

std::wstring settings_widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

std::string settings_narrow(const wchar_t* w) {
    if (!*w) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring default_ini_path() {
    wchar_t appdata[MAX_PATH] = {};
    GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    return std::wstring(appdata) + L"\\Notepad++\\plugins\\config\\FileTools.ini";
}

YamlSettings read_settings_ini(const std::wstring& path) {
    YamlSettings s;
    const wchar_t* f = path.c_str();
    wchar_t buf[MAX_PATH] = {};

    s.tidy.indent_step = (int)GetPrivateProfileIntW(L"YAML", L"IndentStep", 2, f);
    s.tidy.tab_width   = (int)GetPrivateProfileIntW(L"YAML", L"TabWidth",   2, f);
    s.tidy.max_blank   = (int)GetPrivateProfileIntW(L"YAML", L"MaxBlank",   1, f);
    s.tidy.strip_trailing_ws = GetPrivateProfileIntW(L"YAML", L"StripTrailingWs", 1, f) != 0;
    s.tidy.final_newline     = GetPrivateProfileIntW(L"YAML", L"FinalNewline",    1, f) != 0;

    GetPrivateProfileStringW(L"JSON", L"Indent", L"2", buf, 64, f);
    s.json_indent = wcscmp(buf, L"4") == 0 ? JsonIndent::Four : JsonIndent::Two;
    s.json_sort_keys = GetPrivateProfileIntW(L"JSON", L"SortKeys", 0, f) != 0;

    GetPrivateProfileStringW(L"CSV", L"QuoteMode", L"ifneeded", buf, 64, f);
    s.csv_quote_mode = wcscmp(buf, L"always") == 0 ? CsvQuoteMode::Always : CsvQuoteMode::OnlyIfNeeded;

    GetPrivateProfileStringW(L"Parquet", L"DuckdbPath", L"", buf, MAX_PATH, f);
    s.duckdb_path = settings_narrow(buf);

    s.format_on_save = false;  // session-only, never read back from the .ini
    return s;
}

void write_settings_ini(const std::wstring& path, const YamlSettings& s) {
    const wchar_t* f = path.c_str();
    auto ws = [&](LPCWSTR sec, LPCWSTR key, LPCWSTR val) { WritePrivateProfileStringW(sec, key, val, f); };
    auto wi = [&](LPCWSTR sec, LPCWSTR key, int val) {
        wchar_t buf[16]; wsprintfW(buf, L"%d", val);
        WritePrivateProfileStringW(sec, key, buf, f);
    };
    auto bs = [](bool v) { return v ? L"1" : L"0"; };

    wi(L"YAML", L"IndentStep", s.tidy.indent_step);
    wi(L"YAML", L"TabWidth",   s.tidy.tab_width);
    wi(L"YAML", L"MaxBlank",   s.tidy.max_blank);
    ws(L"YAML", L"StripTrailingWs", bs(s.tidy.strip_trailing_ws));
    ws(L"YAML", L"FinalNewline",    bs(s.tidy.final_newline));

    ws(L"JSON", L"Indent",   s.json_indent == JsonIndent::Four ? L"4" : L"2");
    ws(L"JSON", L"SortKeys", bs(s.json_sort_keys));

    ws(L"CSV", L"QuoteMode", s.csv_quote_mode == CsvQuoteMode::Always ? L"always" : L"ifneeded");

    ws(L"Parquet", L"DuckdbPath", settings_widen(s.duckdb_path).c_str());

    // Format on Save is session-only (off after every restart); drop the key
    // that versions before 1.3.1.6 persisted.
    ws(L"General", L"FormatOnSave", nullptr);
}
