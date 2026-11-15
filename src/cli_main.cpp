// filetools.exe: the File Tools operations as a command-line tool, for
// batches of files without Notepad++. Built from the same modules as the
// plugin (yaml_tidy, yaml_convert, json_tools, csv_tools, parquet_tools) and
// reads the same FileTools.ini for its defaults. Windows-only.
//
//   filetools <operation> [options] [file|pattern ...]
//   filetools list
//
// Without files the operation reads stdin and writes stdout. Patterns are
// expanded here (cmd.exe does not): * and ? within a name, ** for any depth.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#include "yaml_tidy.h"
#include "yaml_convert.h"
#include "json_tools.h"
#include "csv_tools.h"
#include "parquet_tools.h"
#include "settings_io.h"
#include "resource.h"

YamlSettings g_settings;

namespace {

using StrFn = std::function<std::string(const std::string&)>;

enum class Kind { Text, Validate, ParquetIn, ParquetOut };

struct Op {
    const char* name;
    Kind        kind;
    const char* out_ext;   // "" = same as input (in-place rewrite)
    const char* desc;
    StrFn       fn;        // Text ops only
    parquet::Fmt fmt = parquet::Fmt::Csv;   // Parquet ops: the text side
};

bool is_err(const std::string& s) { return !s.empty() && s[0] == '\001'; }

int json_indent() { return g_settings.json_indent == JsonIndent::Four ? 4 : 2; }
int g_csv_col = 0;   // --col, 0-based internally

std::string yaml_to_csv_conv(const std::string& s) { std::string j = yaml_to_json(s); return is_err(j) ? j : jsontools::to_csv(j); }
std::string csv_to_yaml_conv(const std::string& s) { std::string j = csv_to_json(s);  return is_err(j) ? j : json_to_yaml(j); }

const std::vector<Op>& ops() {
    static const std::vector<Op> v = {
        { "yaml-tidy",         Kind::Text, "",      "reindent/tidy YAML, comments preserved",      [](const std::string& s) { return yaml_tidy(s, g_settings.tidy); } },
        { "yaml-validate",     Kind::Validate, "",  "report the first YAML syntax error per file", nullptr },
        { "yaml-to-json",      Kind::Text, ".json", "YAML -> pretty JSON",                         [](const std::string& s) { return yaml_to_json(s); } },
        { "yaml-to-csv",       Kind::Text, ".csv",  "YAML (sequence of mappings) -> CSV",          yaml_to_csv_conv },
        { "json-to-yaml",      Kind::Text, ".yaml", "JSON -> block-style YAML",                    [](const std::string& s) { return json_to_yaml(s); } },
        { "json-pretty",       Kind::Text, "",      "re-emit JSON with --indent (and --sort-keys)",[](const std::string& s) { return jsontools::reserialize(s, json_indent(), g_settings.json_sort_keys); } },
        { "json-minify",       Kind::Text, "",      "re-emit JSON compact, one line",              [](const std::string& s) { return json_minify(s); } },
        { "json-sort-keys",    Kind::Text, "",      "pretty JSON with object keys sorted",         [](const std::string& s) { return jsontools::reserialize(s, json_indent(), true); } },
        { "json-escape",       Kind::Text, "",      "wrap text as a JSON string literal",          [](const std::string& s) { return json_escape(s); } },
        { "json-unescape",     Kind::Text, "",      "JSON string literal -> raw text",             [](const std::string& s) { return json_unescape(s); } },
        { "json-to-csv",       Kind::Text, ".csv",  "JSON array of objects -> CSV",                [](const std::string& s) { return json_to_csv(s); } },
        { "csv-align",         Kind::Text, "",      "pad fields so columns line up (--quote)",     [](const std::string& s) { return csv_align(s, g_settings.csv_quote_mode == CsvQuoteMode::Always); } },
        { "csv-compact",       Kind::Text, "",      "undo align: trim padding, minimal quoting",   [](const std::string& s) { return csv_compact(s); } },
        { "csv-add-quotes",    Kind::Text, "",      "quote every field",                           [](const std::string& s) { return csv_add_quotes(s); } },
        { "csv-remove-quotes", Kind::Text, "",      "strip quotes where not required",             [](const std::string& s) { return csv_remove_quotes(s); } },
        { "csv-to-comma",      Kind::Text, "",      "re-emit with ',' delimiter",                  [](const std::string& s) { return csv_to_comma(s); } },
        { "csv-to-semicolon",  Kind::Text, "",      "re-emit with ';' delimiter",                  [](const std::string& s) { return csv_to_semicolon(s); } },
        { "csv-transpose",     Kind::Text, "",      "rows <-> columns",                            [](const std::string& s) { return csv_transpose(s); } },
        { "csv-sort",          Kind::Text, "",      "stable sort of data rows by --col N (1-based)",[](const std::string& s) { return csv_sort_by_column(s, g_csv_col); } },
        { "csv-to-json",       Kind::Text, ".json", "CSV with header -> JSON array of objects",    [](const std::string& s) { return csv_to_json(s); } },
        { "csv-to-yaml",       Kind::Text, ".yaml", "CSV with header -> YAML",                     csv_to_yaml_conv },
        { "csv-to-parquet",    Kind::ParquetOut, ".parquet", "CSV -> Parquet file (duckdb)",       nullptr, parquet::Fmt::Csv },
        { "json-to-parquet",   Kind::ParquetOut, ".parquet", "JSON -> Parquet file (duckdb)",      nullptr, parquet::Fmt::Json },
        { "yaml-to-parquet",   Kind::ParquetOut, ".parquet", "YAML -> Parquet file (duckdb)",      nullptr, parquet::Fmt::Yaml },
        { "parquet-to-csv",    Kind::ParquetIn,  ".csv",  "Parquet -> CSV (duckdb)",               nullptr, parquet::Fmt::Csv },
        { "parquet-to-json",   Kind::ParquetIn,  ".json", "Parquet -> JSON (duckdb)",              nullptr, parquet::Fmt::Json },
        { "parquet-to-yaml",   Kind::ParquetIn,  ".yaml", "Parquet -> YAML (duckdb)",              nullptr, parquet::Fmt::Yaml },
    };
    return v;
}

void print_usage() {
    std::cout <<
        "Datamodder File Tools CLI " << FILETOOLS_VERSION << "\n\n"
        "Usage: filetools <operation> [options] [file|pattern ...]\n"
        "       filetools list                 all operations\n\n"
        "Without files: stdin -> stdout. Patterns: * ? in a name, ** = any depth.\n\n"
        "Output (text operations):\n"
        "  (none)           one file: result to stdout; several files: pick one below\n"
        "  --in-place       rewrite the file; a conversion writes <name>.<new ext> next to it\n"
        "  --out-dir <dir>  write under <dir>, keeping the folder structure of the pattern\n"
        "  --check          write nothing; list files that would change (exit 1 if any)\n"
        "Parquet operations always work on files; the default target is next to the source.\n\n"
        "Options:\n"
        "  --indent <n>       JSON indent (2 or 4)      --sort-keys        json-pretty sorts keys\n"
        "  --yaml-indent <n>  YAML indent step          --tab-width <n>    YAML tab width\n"
        "  --max-blank <n>    YAML max blank lines      --quote always|needed  CSV quoting for csv-align\n"
        "  --col <n>          csv-sort column, 1-based  --limit <n>        parquet-to-*: first n rows only\n"
        "  --duckdb <path>    duckdb.exe to use (else PATH, then next to filetools.exe)\n"
        "  --ini <path>       settings file (default: the plugin's FileTools.ini, if present)\n"
        "  --no-ini           ignore FileTools.ini, use built-in defaults\n"
        "  --quiet            no per-file lines, only the summary and errors\n"
        "  --version, --help\n\n"
        "Exit codes: 0 ok, 1 --check found changes / invalid YAML, 2 a file failed, 3 usage.\n";
}

void print_list() {
    for (const auto& o : ops()) {
        std::string n = o.name;
        n.resize(20, ' ');
        std::cout << n << o.desc << "\n";
    }
}

// ── small helpers ─────────────────────────────────────────────────────────────

std::string narrow(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring widen(const std::string& s) { return settings_widen(s); }

bool read_file(const std::wstring& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}
bool write_file(const std::wstring& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << content;
    return (bool)f;
}
bool file_exists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
bool dir_exists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
void make_dirs(const std::wstring& dir) {
    for (size_t i = 1; i <= dir.size(); ++i)
        if (i == dir.size() || dir[i] == L'\\' || dir[i] == L'/') {
            std::wstring part = dir.substr(0, i);
            if (part.size() == 2 && part[1] == L':') continue;
            CreateDirectoryW(part.c_str(), nullptr);
        }
}
std::wstring exe_dir() {
    wchar_t here[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, here, MAX_PATH);
    std::wstring p = here;
    return p.substr(0, p.find_last_of(L"\\/"));
}
std::wstring with_ext(const std::wstring& path, const std::string& ext) {
    size_t slash = path.find_last_of(L"\\/"), dot = path.find_last_of(L'.');
    std::wstring stem = (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) ? path.substr(0, dot) : path;
    return stem + widen(ext);
}
std::wstring join(const std::wstring& a, const std::wstring& b) {
    if (a.empty() || a == L".") return b;
    return (a.back() == L'\\' || a.back() == L'/') ? a + b : a + L"\\" + b;
}

// ── pattern expansion ─────────────────────────────────────────────────────────

struct Input { std::wstring path; std::wstring rel; };  // rel: path below the pattern's fixed prefix

bool has_wild(const std::wstring& s) { return s.find_first_of(L"*?") != std::wstring::npos; }

// * and ? within one name, case-insensitive (Windows).
bool name_match(const wchar_t* p, const wchar_t* n) {
    for (;;) {
        if (*p == L'*') {
            while (*p == L'*') ++p;
            if (!*p) return true;
            for (; *n; ++n) if (name_match(p, n)) return true;
            return false;
        }
        if (!*n) return !*p;
        if (*p != L'?' && towlower(*p) != towlower(*n)) return false;
        ++p; ++n;
    }
}

void list_dir(const std::wstring& dir, std::vector<std::pair<std::wstring, bool>>& out) {
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW(join(dir, L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        out.push_back({ n, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 });
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
}

void walk(const std::wstring& dir, const std::wstring& rel, const std::vector<std::wstring>& segs, size_t i, std::vector<Input>& out) {
    if (i >= segs.size()) return;
    std::vector<std::pair<std::wstring, bool>> entries;
    list_dir(dir, entries);
    if (segs[i] == L"**") {
        walk(dir, rel, segs, i + 1, out);                       // ** matches zero folders
        for (auto& [n, is_dir] : entries)
            if (is_dir) walk(join(dir, n), join(rel, n), segs, i, out);   // ...or one more level
        return;
    }
    bool last = (i + 1 == segs.size());
    for (auto& [n, is_dir] : entries) {
        if (!name_match(segs[i].c_str(), n.c_str())) continue;
        if (last) { if (!is_dir) out.push_back({ join(dir, n), join(rel, n) }); }
        else if (is_dir) walk(join(dir, n), join(rel, n), segs, i + 1, out);
    }
}

std::vector<Input> expand(const std::wstring& pattern) {
    std::vector<std::wstring> segs;
    std::wstring cur;
    for (wchar_t c : pattern) {
        if (c == L'\\' || c == L'/') { if (!cur.empty()) segs.push_back(cur); cur.clear(); if (segs.empty() && c == L'\\' && pattern.size() > 1) {} }
        else cur += c;
    }
    if (!cur.empty()) segs.push_back(cur);
    // Fixed prefix: leading segments without wildcards.
    size_t first = 0;
    while (first < segs.size() && !has_wild(segs[first])) ++first;
    std::wstring base;
    bool rooted = !pattern.empty() && (pattern[0] == L'\\' || pattern[0] == L'/');
    for (size_t k = 0; k < first; ++k) base = (k == 0 && !rooted) ? segs[k] : base + L"\\" + segs[k];
    if (rooted) base = L"\\" + base;
    if (base.size() == 2 && base[1] == L':') base += L"\\";
    std::vector<std::wstring> tail(segs.begin() + first, segs.end());
    std::vector<Input> out;
    walk(base.empty() ? L"." : base, L"", tail, 0, out);
    return out;
}

// ── main ──────────────────────────────────────────────────────────────────────

int usage_error(const std::string& m) { std::cerr << "error: " << m << "\n"; return 3; }

} // namespace

int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_BINARY);   // byte-exact output
    SetConsoleOutputCP(CP_UTF8);

    std::vector<std::wstring> patterns;
    std::string op_name, ini_path, duckdb_path, quote_mode;
    std::wstring out_dir;
    bool in_place = false, check = false, no_ini = false, quiet = false, sort_keys = false;
    int indent = 0, yaml_indent = 0, tab_width = 0, max_blank = -1, col = 0;
    long limit = 0;

    auto need = [&](int& i, const char* what) -> const wchar_t* {
        if (++i >= argc) { std::cerr << "error: " << what << " requires a value\n"; exit(3); }
        return argv[i];
    };
    for (int i = 1; i < argc; ++i) {
        std::string a = narrow(argv[i]);
        if (a == "--help" || a == "-h" || a == "help") { print_usage(); return 0; }
        if (a == "--version") { std::cout << FILETOOLS_VERSION << "\n"; return 0; }
        if (a == "list" && op_name.empty()) { print_list(); return 0; }
        if (a == "--in-place") { in_place = true; continue; }
        if (a == "--check") { check = true; continue; }
        if (a == "--no-ini") { no_ini = true; continue; }
        if (a == "--quiet" || a == "-q") { quiet = true; continue; }
        if (a == "--sort-keys") { sort_keys = true; continue; }
        if (a == "--out-dir") { out_dir = need(i, "--out-dir"); continue; }
        if (a == "--ini") { ini_path = narrow(need(i, "--ini")); continue; }
        if (a == "--duckdb") { duckdb_path = narrow(need(i, "--duckdb")); continue; }
        if (a == "--quote") { quote_mode = narrow(need(i, "--quote")); continue; }
        if (a == "--indent") { indent = _wtoi(need(i, "--indent")); continue; }
        if (a == "--yaml-indent") { yaml_indent = _wtoi(need(i, "--yaml-indent")); continue; }
        if (a == "--tab-width") { tab_width = _wtoi(need(i, "--tab-width")); continue; }
        if (a == "--max-blank") { max_blank = _wtoi(need(i, "--max-blank")); continue; }
        if (a == "--col") { col = _wtoi(need(i, "--col")); continue; }
        if (a == "--limit") { limit = _wtol(need(i, "--limit")); continue; }
        if (!a.empty() && a[0] == '-') return usage_error("unknown option '" + a + "'");
        if (op_name.empty()) op_name = a; else patterns.push_back(argv[i]);
    }
    if (op_name.empty()) { print_usage(); return 3; }

    const Op* op = nullptr;
    for (const auto& o : ops()) if (op_name == o.name) op = &o;
    if (!op) return usage_error("unknown operation '" + op_name + "' (see: filetools list)");

    if ((int)in_place + (int)check + (int)!out_dir.empty() > 1)
        return usage_error("--in-place, --out-dir and --check are mutually exclusive");
    if (indent && indent != 2 && indent != 4) return usage_error("--indent must be 2 or 4");
    if (!quote_mode.empty() && quote_mode != "always" && quote_mode != "needed") return usage_error("--quote must be always or needed");
    if (op_name == "csv-sort" && col < 1) return usage_error("csv-sort needs --col <n> (1-based)");

    // Settings: FileTools.ini (the plugin's, or --ini), then command-line overrides.
    std::wstring ini = ini_path.empty() ? default_ini_path() : widen(ini_path);
    if (!ini_path.empty() && !file_exists(ini)) return usage_error("cannot read ini file '" + ini_path + "'");
    g_settings = (no_ini || !file_exists(ini)) ? YamlSettings{} : read_settings_ini(ini);
    if (indent) g_settings.json_indent = indent == 4 ? JsonIndent::Four : JsonIndent::Two;
    if (sort_keys) g_settings.json_sort_keys = true;
    if (yaml_indent > 0) g_settings.tidy.indent_step = yaml_indent;
    if (tab_width > 0) g_settings.tidy.tab_width = tab_width;
    if (max_blank >= 0) g_settings.tidy.max_blank = max_blank;
    if (!quote_mode.empty()) g_settings.csv_quote_mode = quote_mode == "always" ? CsvQuoteMode::Always : CsvQuoteMode::OnlyIfNeeded;
    if (!duckdb_path.empty()) g_settings.duckdb_path = duckdb_path;
    g_csv_col = col - 1;

    bool is_parquet = op->kind == Kind::ParquetIn || op->kind == Kind::ParquetOut;
    std::wstring duck;
    if (is_parquet) {
        duck = parquet::find_duckdb(g_settings.duckdb_path, exe_dir());
        if (duck.empty()) return usage_error("duckdb.exe not found: put it on PATH, next to filetools.exe, or pass --duckdb <path>. Download: https://duckdb.org/");
        if (check && op->kind == Kind::ParquetOut) return usage_error("--check is not available for *-to-parquet");
    }

    // ── stdin -> stdout ──
    if (patterns.empty()) {
        if (is_parquet) return usage_error(op_name + std::string(" works on files; give a file or pattern"));
        if (in_place || check || !out_dir.empty()) return usage_error("no files given; reading stdin writes to stdout only");
        _setmode(_fileno(stdin), _O_BINARY);
        std::string src((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
        if (op->kind == Kind::Validate) {
            std::string r = yaml_validate(src);
            std::cout << (r.empty() ? "OK" : r) << "\n";
            return r.empty() ? 0 : 1;
        }
        std::string r = op->fn(src);
        if (is_err(r)) { std::cerr << "error: " << r.substr(1) << "\n"; return 2; }
        std::cout << r;
        return 0;
    }

    // ── collect files ──
    std::vector<Input> files;
    for (const auto& p : patterns) {
        if (has_wild(p)) {
            auto m = expand(p);
            if (m.empty()) std::cerr << "warning: '" << narrow(p) << "' matched no files\n";
            files.insert(files.end(), m.begin(), m.end());
        } else {
            size_t slash = p.find_last_of(L"\\/");
            files.push_back({ p, slash == std::wstring::npos ? p : p.substr(slash + 1) });
        }
    }
    if (files.empty()) return usage_error("no input files");

    bool to_stdout = !in_place && !check && out_dir.empty() && op->kind != Kind::ParquetOut;
    if (to_stdout && files.size() > 1 && op->kind != Kind::Validate)
        return usage_error(std::to_string(files.size()) + " files matched; choose --in-place, --out-dir or --check");

    int changed = 0, unchanged = 0, failed = 0, invalid = 0;
    std::string oext = op->out_ext;
    auto note = [&](const std::string& what, const std::wstring& path) { if (!quiet) std::cerr << what << ": " << narrow(path) << "\n"; };

    for (const auto& in : files) {
        // Target path (text and Parquet alike).
        std::wstring target;
        if (!out_dir.empty()) target = oext.empty() ? join(out_dir, in.rel) : with_ext(join(out_dir, in.rel), oext);
        else target = oext.empty() ? in.path : with_ext(in.path, oext);

        std::string result;
        if (op->kind == Kind::ParquetIn) {
            std::string err = parquet::read_parquet(duck, in.path, op->fmt, limit, result);
            if (!err.empty()) { std::cerr << "error: " << narrow(in.path) << ": " << err << "\n"; ++failed; continue; }
        } else {
            std::string src;
            if (!read_file(in.path, src)) { std::cerr << "error: cannot read '" << narrow(in.path) << "'\n"; ++failed; continue; }
            if (op->kind == Kind::Validate) {
                std::string r = yaml_validate(src);
                if (r.empty()) note("ok", in.path);
                else { std::cerr << narrow(in.path) << ": " << r << "\n"; ++invalid; }
                continue;
            }
            if (op->kind == Kind::ParquetOut) {
                if (check) continue;
                if (!out_dir.empty()) make_dirs(target.substr(0, target.find_last_of(L"\\/")));
                std::string err = parquet::write_parquet(duck, src, op->fmt, target);
                if (!err.empty()) { std::cerr << "error: " << narrow(in.path) << ": " << err << "\n"; ++failed; continue; }
                note("written", target); ++changed;
                continue;
            }
            result = op->fn(src);
            if (is_err(result)) { std::cerr << "error: " << narrow(in.path) << ": " << result.substr(1) << "\n"; ++failed; continue; }
            if (oext.empty() && result == src) { if (!check) note("unchanged", in.path); ++unchanged; continue; }
        }

        if (to_stdout) { std::cout << result; continue; }
        if (check) { note("would change", target); ++changed; continue; }

        std::string existing;
        if (target != in.path && file_exists(target) && read_file(target, existing) && existing == result) {
            note("unchanged", target); ++unchanged; continue;
        }
        size_t slash = target.find_last_of(L"\\/");
        if (slash != std::wstring::npos) make_dirs(target.substr(0, slash));
        if (!write_file(target, result)) { std::cerr << "error: cannot write '" << narrow(target) << "'\n"; ++failed; continue; }
        note(target == in.path ? "changed" : "written", target); ++changed;
    }

    if (op->kind == Kind::Validate) {
        std::cerr << (files.size() - invalid - failed) << " ok, " << invalid << " invalid" << (failed ? ", " + std::to_string(failed) + " unreadable" : "") << "\n";
        return failed ? 2 : invalid ? 1 : 0;
    }
    if (!to_stdout) {
        std::cerr << changed << (check ? " would change, " : " written, ") << unchanged << " unchanged, " << failed << " failed\n";
    }
    if (failed) return 2;
    if (check && changed) return 1;
    return 0;
}
