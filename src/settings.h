#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include "yaml_tidy.h"

// ─── enums ────────────────────────────────────────────────────────────────────
enum class JsonIndent   { Two, Four };
enum class CsvQuoteMode { OnlyIfNeeded, Always };

// ─── settings struct (defaults = current hardcoded behavior) ─────────────────
struct YamlSettings {
    TidyOptions   tidy;                              // YAML tab (indent_step, tab_width, max_blank, ...)
    bool          format_on_save = false;            // Behavior tab; session-only, never persisted

    JsonIndent    json_indent    = JsonIndent::Two;  // JSON tab
    bool          json_sort_keys = false;             // JSON tab: default for Pretty-print

    CsvQuoteMode  csv_quote_mode = CsvQuoteMode::OnlyIfNeeded;  // CSV tab

    std::string   duckdb_path;                        // Parquet tab; empty = auto (PATH or next to DLL)
};

extern YamlSettings g_settings;

void load_settings();
void save_settings();
void show_settings_dialog(HWND parent);
bool confirm_format_on_save(HWND owner);   // warning before turning Format on Save on
void show_about_dialog(HWND parent);
