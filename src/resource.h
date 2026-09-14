#pragma once

#define FILETOOLS_VERSION       "1.3.1.3"
#define FILETOOLS_VERSION_W     L"1.3.1.3"
#define FILETOOLS_VERSION_COMMA 1,3,1,3

// ─── main settings dialog ────────────────────────────────────────────────────
#define IDD_SETTINGS       101
#define IDD_TAB_YAML       102
#define IDD_TAB_JSON       103
#define IDD_TAB_CSV        104
#define IDD_TAB_PARQUET    105
#define IDD_TAB_BEHAVIOR   106

#define IDC_NAV            201

// ─── YAML tab ─────────────────────────────────────────────────────────────────
#define IDC_YAML_INDENT     301
#define IDC_YAML_TABWIDTH   302
#define IDC_YAML_MAXBLANK   303
#define IDC_YAML_STRIP_WS   304
#define IDC_YAML_FINAL_NL   305

// ─── JSON tab ─────────────────────────────────────────────────────────────────
#define IDC_JSON_INDENT_2   311
#define IDC_JSON_INDENT_4   312
#define IDC_JSON_SORT_KEYS  313

// ─── CSV tab ──────────────────────────────────────────────────────────────────
#define IDC_CSV_QUOTE_IFNEEDED 321
#define IDC_CSV_QUOTE_ALWAYS   322

// ─── Parquet tab ──────────────────────────────────────────────────────────────
#define IDC_DUCKDB_PATH     331
#define IDC_DUCKDB_BROWSE   332

// ─── Behavior & profiles tab ──────────────────────────────────────────────────
#define IDC_FORMAT_ON_SAVE  341
#define IDC_PROFILE_LIST    342
#define IDC_PROFILE_NAME    343
#define IDC_PROFILE_LOAD    344
#define IDC_PROFILE_SAVE    345
#define IDC_PROFILE_DELETE  346
#define IDC_PROFILE_EXPORT  347
#define IDC_PROFILE_IMPORT  348

// ─── about dialog ─────────────────────────────────────────────────────────────
#define IDD_ABOUT              1300
#define IDC_ABOUT_BODY         1301
#define IDC_ABOUT_LINK         1302
#define IDC_ABOUT_LINK_WEBSITE 1303
