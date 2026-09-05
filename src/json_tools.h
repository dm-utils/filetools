#pragma once
#include <string>

// ─── JSON tools ─────────────────────────────────────────────────────────────
// Built on libyaml (JSON is a subset of YAML). Errors come back with a leading
// SOH byte "\001" (same convention as yaml_convert.h) so the caller shows a
// message instead of replacing the buffer.

std::string json_pretty(const std::string& src);      // parse + re-emit, 2-space indent
std::string json_minify(const std::string& src);      // parse + re-emit, compact
std::string json_sort_keys(const std::string& src);   // pretty + object keys sorted (recursive)

std::string json_escape(const std::string& src);      // selection -> "…" (JSON string literal)
std::string json_unescape(const std::string& src);    // "…" -> raw text

namespace jsontools {
// Shared engine: indent <= 0 => compact. Multiple YAML docs become a JSON array.
std::string reserialize(const std::string& src, int indent, bool sort_keys);
std::string escape_string(const std::string& src);
std::string unescape_string(const std::string& src);
}
