#pragma once
#include <string>

// ─── CSV tools ─────────────────────────────────────────────────────────────
// Pure string transforms — no libyaml, no deps. RFC-4180-ish parsing:
// fields split on the delimiter; a field may be double-quoted, "" is a literal
// quote, quoted fields may contain the delimiter and newlines. The delimiter
// is sniffed from the first data line ( , ; tab | ), CR/LF style is preserved.
//
// Errors come back with a leading SOH byte "\001" (same convention as the
// other modules) so the caller shows a message instead of replacing the buffer.

std::string csv_align(const std::string& src);            // pad fields so columns line up (a view)
std::string csv_compact(const std::string& src);          // undo align: trim padding, minimal output
std::string csv_to_comma(const std::string& src);         // re-emit with ',' delimiter
std::string csv_to_semicolon(const std::string& src);     // re-emit with ';' delimiter
std::string csv_transpose(const std::string& src);        // rows <-> columns
std::string csv_sort_by_column(const std::string& src, int col);  // stable sort data rows; header kept
std::string csv_to_json(const std::string& src);          // header row -> array of objects, pretty JSON
