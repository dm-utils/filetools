#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "parquet_tools.h"
#include "yaml_convert.h"

namespace parquet {

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

bool file_exists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// Backslashes -> forward slashes, so the path is safe inside a single-quoted
// DuckDB SQL string literal.
std::wstring fwd(std::wstring p) {
    for (auto& c : p) if (c == L'\\') c = L'/';
    return p;
}

bool is_conv_err(const std::string& s) { return !s.empty() && s[0] == '\001'; }

} // namespace

std::wstring find_duckdb(const std::string& hint_utf8, const std::wstring& beside_dir) {
    if (!hint_utf8.empty()) {
        std::wstring w = widen(hint_utf8);
        if (file_exists(w)) return w;
    }
    wchar_t found[MAX_PATH] = {};
    if (SearchPathW(nullptr, L"duckdb", L".exe", MAX_PATH, found, nullptr)) return found;
    if (!beside_dir.empty()) {
        std::wstring p = beside_dir;
        if (p.back() != L'\\' && p.back() != L'/') p += L'\\';
        p += L"duckdb.exe";
        if (file_exists(p)) return p;
    }
    return L"";
}

int run_capture(const std::wstring& cmdline, std::string& out) {
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

std::string read_parquet(const std::wstring& duck, const std::wstring& parquet_path,
                         Fmt fmt, long limit, std::string& out) {
    if (!file_exists(parquet_path)) return "parquet file not found";
    // YAML has no DuckDB writer: ask for JSON and convert afterwards.
    const wchar_t* flag = (fmt == Fmt::Csv) ? L"-csv" : L"-json";
    std::wstring sql = L"SELECT * FROM read_parquet('" + fwd(parquet_path) + L"')";
    if (limit > 0) sql += L" LIMIT " + std::to_wstring(limit);
    std::wstring cmd = L"\"" + duck + L"\" " + flag + L" -c \"" + sql + L"\"";

    out.clear();
    int code = run_capture(cmd, out);
    if (code != 0) {
        std::string m = out.empty() ? "duckdb could not be started" : out.substr(0, 2000);
        out.clear();
        return m;
    }
    if (fmt == Fmt::Yaml) {
        std::string y = json_to_yaml(out);
        if (is_conv_err(y)) { out.clear(); return "JSON -> YAML failed: " + y.substr(1); }
        out.swap(y);
    }
    return "";
}

std::string write_parquet(const std::wstring& duck, const std::string& payload_in,
                          Fmt from, const std::wstring& out_path) {
    std::string payload = payload_in;
    std::wstring tmp_ext = (from == Fmt::Csv) ? L".csv" : L".json";
    if (from == Fmt::Yaml) {
        if (!yaml_convert_available()) return yaml_validate("");
        std::string j = yaml_to_json(payload);
        if (is_conv_err(j)) return "YAML -> JSON failed: " + j.substr(1);
        payload.swap(j);
    }

    // DuckDB reads files, not pipes: spill the payload to a temp file.
    wchar_t tdir[MAX_PATH] = {}, tin[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tdir);
    GetTempFileNameW(tdir, L"ft_", 0, tin);
    std::wstring in = tin; DeleteFileW(tin); in += tmp_ext;
    {
        HANDLE h = CreateFileW(in.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return "could not write a temporary file";
        DWORD wr = 0;
        WriteFile(h, payload.data(), (DWORD)payload.size(), &wr, nullptr);
        CloseHandle(h);
    }

    std::wstring reader = (tmp_ext == L".csv") ? L"read_csv_auto" : L"read_json_auto";
    std::wstring cmd = L"\"" + duck + L"\" -c \"COPY (SELECT * FROM " + reader +
        L"('" + fwd(in) + L"')) TO '" + fwd(out_path) + L"' (FORMAT PARQUET)\"";

    std::string res;
    int code = run_capture(cmd, res);
    DeleteFileW(in.c_str());
    if (code != 0) return res.empty() ? "duckdb could not be started" : res.substr(0, 2000);
    return "";
}

} // namespace parquet
