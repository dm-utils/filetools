#include "csv_tools.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

#define ERRMARK "\001"

namespace {

using Row  = std::vector<std::string>;
using Grid = std::vector<Row>;

// Sniff the delimiter from the first non-empty line (quote-aware).
char sniff_delim(const std::string& s) {
    const char cands[] = { ',', ';', '\t', '|' };
    size_t line_end = s.find('\n');
    std::string first = s.substr(0, line_end == std::string::npos ? s.size() : line_end);
    int best_count = -1; char best = ',';
    for (char d : cands) {
        int n = 0; bool q = false;
        for (size_t i = 0; i < first.size(); ++i) {
            char c = first[i];
            if (q) { if (c == '"') { if (i + 1 < first.size() && first[i + 1] == '"') ++i; else q = false; } }
            else if (c == '"') q = true;
            else if (c == d) ++n;
        }
        if (n > best_count) { best_count = n; best = d; }
    }
    return best;
}

// Parse the whole text into a grid. Handles quoted fields spanning newlines.
Grid parse(const std::string& s, char delim) {
    Grid g;
    Row row;
    std::string field;
    bool q = false, any = false;
    size_t i = 0, n = s.size();
    auto push_field = [&] { row.push_back(field); field.clear(); any = true; };
    auto push_row   = [&] { push_field(); g.push_back(row); row.clear(); any = false; };

    while (i < n) {
        char c = s[i];
        if (q) {
            if (c == '"') {
                if (i + 1 < n && s[i + 1] == '"') { field += '"'; i += 2; continue; }
                q = false; ++i; continue;
            }
            field += c; ++i; continue;
        }
        if (c == '"') { q = true; any = true; ++i; continue; }
        if (c == delim) { push_field(); ++i; continue; }
        if (c == '\r') { ++i; continue; }
        if (c == '\n') { push_row(); ++i; continue; }
        field += c; any = true; ++i;
    }
    if (any || !field.empty() || !row.empty()) push_row();
    return g;
}

bool needs_quote(const std::string& f, char delim) {
    if (f.empty()) return false;
    if (f.front() == ' ' || f.back() == ' ') return true;
    for (char c : f)
        if (c == delim || c == '"' || c == '\n' || c == '\r') return true;
    return false;
}

std::string quote(const std::string& f, char delim) {
    if (!needs_quote(f, delim)) return f;
    std::string o = "\"";
    for (char c : f) { if (c == '"') o += '"'; o += c; }
    o += '"';
    return o;
}

std::string write(const Grid& g, char delim, bool crlf) {
    std::string o;
    const char* nl = crlf ? "\r\n" : "\n";
    for (size_t r = 0; r < g.size(); ++r) {
        for (size_t c = 0; c < g[r].size(); ++c) {
            if (c) o += delim;
            o += quote(g[r][c], delim);
        }
        o += nl;
    }
    return o;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

bool crlf_of(const std::string& s) { return s.find("\r\n") != std::string::npos; }

void json_field(const std::string& v, std::string& out) {
    // bare for number / true / false / null, else JSON string
    if (v == "true" || v == "false" || v == "null") { out += v; return; }
    bool num = !v.empty(); bool dot = false, dig = false;
    for (size_t i = 0; i < v.size() && num; ++i) {
        char c = v[i];
        if (c == '-' && i == 0) continue;
        if (c == '.' && !dot) { dot = true; continue; }
        if (std::isdigit((unsigned char)c)) { dig = true; continue; }
        num = false;
    }
    if (num && dig) { out += v; return; }
    out += '"';
    for (unsigned char c : v) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default: out += (char)c;
        }
    }
    out += '"';
}

} // namespace

std::string csv_align(const std::string& src) {
    char d = sniff_delim(src);
    Grid g = parse(src, d);
    if (g.empty()) return src;
    size_t cols = 0;
    for (auto& r : g) cols = std::max(cols, r.size());
    std::vector<size_t> w(cols, 0);
    std::vector<Row> q(g.size());
    for (size_t r = 0; r < g.size(); ++r) {
        q[r].resize(cols);
        for (size_t c = 0; c < cols; ++c) {
            std::string f = c < g[r].size() ? g[r][c] : std::string();
            q[r][c] = quote(f, d);
            w[c] = std::max(w[c], q[r][c].size());
        }
    }
    std::string o;
    const char* nl = crlf_of(src) ? "\r\n" : "\n";
    for (size_t r = 0; r < q.size(); ++r) {
        for (size_t c = 0; c < cols; ++c) {
            if (c) o += d, o += ' ';
            o += q[r][c];
            if (c + 1 < cols) o.append(w[c] - q[r][c].size(), ' ');
        }
        o += nl;
    }
    return o;
}

std::string csv_compact(const std::string& src) {
    char d = sniff_delim(src);
    Grid g = parse(src, d);
    for (auto& r : g) for (auto& f : r) f = trim(f);
    return write(g, d, crlf_of(src));
}

std::string csv_to_comma(const std::string& src)     { return write(parse(src, sniff_delim(src)), ',', crlf_of(src)); }
std::string csv_to_semicolon(const std::string& src) { return write(parse(src, sniff_delim(src)), ';', crlf_of(src)); }

std::string csv_transpose(const std::string& src) {
    char d = sniff_delim(src);
    Grid g = parse(src, d);
    if (g.empty()) return src;
    size_t cols = 0;
    for (auto& r : g) cols = std::max(cols, r.size());
    Grid t(cols, Row(g.size()));
    for (size_t r = 0; r < g.size(); ++r)
        for (size_t c = 0; c < cols; ++c)
            t[c][r] = c < g[r].size() ? g[r][c] : std::string();
    return write(t, d, crlf_of(src));
}

std::string csv_sort_by_column(const std::string& src, int col) {
    char d = sniff_delim(src);
    Grid g = parse(src, d);
    if (g.size() < 3) return src;                 // header + <2 data rows: nothing to do
    if (col < 0) col = 0;
    auto key = [&](const Row& r) { return (size_t)col < r.size() ? r[col] : std::string(); };
    auto num = [](const std::string& s, double& v) {
        if (s.empty()) return false;
        char* end = nullptr;
        v = std::strtod(s.c_str(), &end);
        return end && *end == '\0';
    };
    std::stable_sort(g.begin() + 1, g.end(), [&](const Row& a, const Row& b) {
        std::string ka = key(a), kb = key(b);
        double na, nb;
        if (num(ka, na) && num(kb, nb)) return na < nb;
        return ka < kb;
    });
    return write(g, d, crlf_of(src));
}

std::string csv_to_json(const std::string& src) {
    char d = sniff_delim(src);
    Grid g = parse(src, d);
    if (g.empty()) return "[]";
    const Row& head = g[0];
    std::string o = "[";
    for (size_t r = 1; r < g.size(); ++r) {
        if (r > 1) o += ',';
        o += "\n  {";
        for (size_t c = 0; c < head.size(); ++c) {
            if (c) o += ',';
            o += "\n    ";
            std::string kb; json_field(head[c], kb);
            if (kb.empty() || kb[0] != '"') { o += '"'; o += head[c]; o += '"'; }
            else o += kb;
            o += ": ";
            json_field(c < g[r].size() ? g[r][c] : std::string(), o);
        }
        o += "\n  }";
    }
    o += g.size() > 1 ? "\n]" : "]";
    return o;
}
