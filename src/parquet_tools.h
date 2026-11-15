#pragma once
#include <string>

// ─── Parquet via duckdb.exe (no UI) ─────────────────────────────────────────
// Parquet is columnar binary; a real reader (Arrow) is far too big to link in,
// so everything Parquet shells out to duckdb.exe. Shared by the Notepad++
// plugin (dllmain.cpp wraps these in message boxes) and the filetools CLI.
// YAML is not a format DuckDB knows: the YAML directions route through JSON.
//
// Windows-only (CreateProcess). Errors come back as a non-empty std::string.

namespace parquet {

enum class Fmt { Csv, Json, Yaml };

// Locate duckdb.exe: `hint` (a path from the settings), then PATH, then
// `beside_dir` (the plugin's / exe's own folder). "" if nowhere.
std::wstring find_duckdb(const std::string& hint_utf8, const std::wstring& beside_dir);

// Run a command line, capture stdout+stderr. Exit code, or -1 if it could not start.
int run_capture(const std::wstring& cmdline, std::string& out);

// .parquet file -> text in `fmt`. limit <= 0 means all rows. "" on success.
std::string read_parquet(const std::wstring& duck, const std::wstring& parquet_path,
                         Fmt fmt, long limit, std::string& out);

// CSV / JSON / YAML bytes -> .parquet file. "" on success.
std::string write_parquet(const std::wstring& duck, const std::string& payload,
                          Fmt from, const std::wstring& out_path);

} // namespace parquet
