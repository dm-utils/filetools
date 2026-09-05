#include "json_tools.h"
#include <cctype>
#include <cstdio>

#define ERRMARK "\001"

namespace {

void esc(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
            else out += (char)c;
        }
    }
    out += '"';
}

bool looks_number(const std::string& t) {
    size_t i = 0;
    if (i < t.size() && (t[i] == '-' || t[i] == '+')) ++i;
    bool digit = false, dot = false, exp = false;
    for (; i < t.size(); ++i) {
        char c = t[i];
        if (std::isdigit((unsigned char)c)) { digit = true; continue; }
        if (c == '.' && !dot && !exp) { dot = true; continue; }
        if ((c == 'e' || c == 'E') && digit && !exp) {
            exp = true;
            if (i + 1 < t.size() && (t[i + 1] == '-' || t[i + 1] == '+')) ++i;
            continue;
        }
        return false;
    }
    return digit;
}

} // namespace

// ── plugin-facing wrappers ────────────────────────────────────────────────

std::string json_pretty(const std::string& src)    { return jsontools::reserialize(src, 2, false); }
std::string json_minify(const std::string& src)    { return jsontools::reserialize(src, 0, false); }
std::string json_sort_keys(const std::string& src) { return jsontools::reserialize(src, 2, true); }
std::string json_escape(const std::string& src)    { return jsontools::escape_string(src); }
std::string json_unescape(const std::string& src)  { return jsontools::unescape_string(src); }
std::string json_to_csv(const std::string& src)    { return jsontools::to_csv(src); }

// ── escape / unescape: pure, always available ──────────────────────────────

std::string jsontools::escape_string(const std::string& src) {
    std::string out;
    esc(src, out);
    return out;
}

std::string jsontools::unescape_string(const std::string& src) {
    size_t i = 0, n = src.size();
    while (i < n && std::isspace((unsigned char)src[i])) ++i;
    size_t j = n;
    while (j > i && std::isspace((unsigned char)src[j - 1])) --j;
    if (j - i < 2 || src[i] != '"' || src[j - 1] != '"')
        return ERRMARK "selectie is geen JSON-stringliteral (moet tussen dubbele quotes staan)";

    std::string out;
    for (size_t k = i + 1; k < j - 1; ++k) {
        char c = src[k];
        if (c != '\\') { out += c; continue; }
        if (++k >= j - 1) return ERRMARK "onvolledige escape aan het eind";
        switch (src[k]) {
        case '"':  out += '"';  break;
        case '\\': out += '\\'; break;
        case '/':  out += '/';  break;
        case 'b':  out += '\b'; break;
        case 'f':  out += '\f'; break;
        case 'n':  out += '\n'; break;
        case 'r':  out += '\r'; break;
        case 't':  out += '\t'; break;
        case 'u': {
            if (k + 4 >= j) return ERRMARK "onvolledige \\u-escape";
            unsigned cp = 0;
            for (int d = 0; d < 4; ++d) {
                char h = src[++k];
                cp <<= 4;
                if      (h >= '0' && h <= '9') cp |= h - '0';
                else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
                else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
                else return ERRMARK "ongeldige \\u-escape";
            }
            if (cp < 0x80) out += (char)cp;
            else if (cp < 0x800) {
                out += (char)(0xC0 | (cp >> 6));
                out += (char)(0x80 | (cp & 0x3F));
            } else {
                out += (char)(0xE0 | (cp >> 12));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            }
            break;
        }
        default: return ERRMARK "onbekende escape-sequence";
        }
    }
    return out;
}

// ── reserialize: needs libyaml ────────────────────────────────────────────

#ifndef HAVE_LIBYAML

std::string jsontools::reserialize(const std::string&, int, bool) {
    return ERRMARK "libyaml is niet meegecompileerd. Zie vendor/VENDORING.md.";
}
std::string jsontools::to_csv(const std::string&) {
    return ERRMARK "libyaml is niet meegecompileerd. Zie vendor/VENDORING.md.";
}

#else

#include <yaml.h>
#include <algorithm>
#include <utility>
#include <vector>

namespace {

struct JV {
    enum Type { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false;
    std::string s;
    std::vector<JV> arr;
    std::vector<std::pair<std::string, JV>> obj;
};

std::string err_at(yaml_parser_t& p) {
    const char* m = p.problem ? p.problem : "parse error";
    char buf[96];
    std::snprintf(buf, sizeof buf, "regel %lu, kolom %lu: %s",
                  (unsigned long)(p.problem_mark.line + 1),
                  (unsigned long)(p.problem_mark.column + 1), m);
    return buf;
}

void scalar_to_jv(const std::string& v, bool plain, JV& out) {
    if (!plain) { out.t = JV::Str; out.s = v; return; }
    if (v.empty() || v == "~" || v == "null" || v == "Null" || v == "NULL") { out.t = JV::Null; return; }
    if (v == "true"  || v == "True"  || v == "TRUE")  { out.t = JV::Bool; out.b = true;  return; }
    if (v == "false" || v == "False" || v == "FALSE") { out.t = JV::Bool; out.b = false; return; }
    if (looks_number(v)) { out.t = JV::Num; out.s = v; return; }
    out.t = JV::Str; out.s = v;
}

bool node_to_jv(yaml_document_t* doc, yaml_node_t* node, JV& out, int depth) {
    if (!node || depth > 200) { out.t = JV::Null; return true; }
    if (node->type == YAML_SCALAR_NODE) {
        std::string v((const char*)node->data.scalar.value, node->data.scalar.length);
        scalar_to_jv(v, node->data.scalar.style == YAML_PLAIN_SCALAR_STYLE, out);
        return true;
    }
    if (node->type == YAML_SEQUENCE_NODE) {
        out.t = JV::Arr;
        for (yaml_node_item_t* it = node->data.sequence.items.start;
             it < node->data.sequence.items.top; ++it) {
            JV child;
            if (!node_to_jv(doc, yaml_document_get_node(doc, *it), child, depth + 1)) return false;
            out.arr.push_back(std::move(child));
        }
        return true;
    }
    if (node->type == YAML_MAPPING_NODE) {
        out.t = JV::Obj;
        for (yaml_node_pair_t* pr = node->data.mapping.pairs.start;
             pr < node->data.mapping.pairs.top; ++pr) {
            yaml_node_t* kn = yaml_document_get_node(doc, pr->key);
            std::string key = (kn && kn->type == YAML_SCALAR_NODE)
                ? std::string((const char*)kn->data.scalar.value, kn->data.scalar.length)
                : std::string("?");
            JV val;
            if (!node_to_jv(doc, yaml_document_get_node(doc, pr->value), val, depth + 1)) return false;
            out.obj.emplace_back(std::move(key), std::move(val));
        }
        return true;
    }
    out.t = JV::Null;
    return true;
}

void newline(std::string& out, int indent, int level) {
    if (indent <= 0) return;
    out += '\n';
    out.append((size_t)indent * level, ' ');
}

void serialize(const JV& v, std::string& out, int indent, int level, bool sort) {
    switch (v.t) {
    case JV::Null: out += "null"; break;
    case JV::Bool: out += v.b ? "true" : "false"; break;
    case JV::Num:  out += v.s; break;
    case JV::Str:  esc(v.s, out); break;
    case JV::Arr:
        if (v.arr.empty()) { out += "[]"; break; }
        out += '[';
        for (size_t i = 0; i < v.arr.size(); ++i) {
            if (i) out += ',';
            newline(out, indent, level + 1);
            serialize(v.arr[i], out, indent, level + 1, sort);
        }
        newline(out, indent, level);
        out += ']';
        break;
    case JV::Obj: {
        if (v.obj.empty()) { out += "{}"; break; }
        std::vector<const std::pair<std::string, JV>*> items;
        items.reserve(v.obj.size());
        for (const auto& p : v.obj) items.push_back(&p);
        if (sort)
            std::stable_sort(items.begin(), items.end(),
                [](auto* a, auto* b) { return a->first < b->first; });
        out += '{';
        for (size_t i = 0; i < items.size(); ++i) {
            if (i) out += ',';
            newline(out, indent, level + 1);
            esc(items[i]->first, out);
            out += indent > 0 ? ": " : ":";
            serialize(items[i]->second, out, indent, level + 1, sort);
        }
        newline(out, indent, level);
        out += '}';
        break;
    }
    }
}

} // namespace

std::string jsontools::reserialize(const std::string& src, int indent, bool sort_keys) {
    yaml_parser_t p;
    if (!yaml_parser_initialize(&p)) return ERRMARK "kan de parser niet initialiseren";
    yaml_parser_set_input_string(&p, (const unsigned char*)src.data(), src.size());

    std::vector<JV> docs;
    std::string err;
    for (;;) {
        yaml_document_t doc;
        if (!yaml_parser_load(&p, &doc)) { err = err_at(p); break; }
        yaml_node_t* root = yaml_document_get_root_node(&doc);
        if (!root) { yaml_document_delete(&doc); break; }   // no more documents
        JV v;
        bool ok = node_to_jv(&doc, root, v, 0);
        yaml_document_delete(&doc);
        if (!ok) { err = "documentstructuur te diep genest"; break; }
        docs.push_back(std::move(v));
    }
    yaml_parser_delete(&p);

    if (!err.empty()) return ERRMARK + err;
    if (docs.empty()) return "null";

    std::string out;
    if (docs.size() == 1) {
        serialize(docs[0], out, indent, 0, sort_keys);
    } else {
        JV wrap; wrap.t = JV::Arr; wrap.arr = std::move(docs);
        serialize(wrap, out, indent, 0, sort_keys);
    }
    return out;
}

namespace {

void csv_cell(const JV& v, std::string& out) {
    std::string raw;
    switch (v.t) {
    case JV::Null: break;
    case JV::Bool: raw = v.b ? "true" : "false"; break;
    case JV::Num:  raw = v.s; break;
    case JV::Str:  raw = v.s; break;
    default: serialize(v, raw, 0, 0, false); break;   // nested -> compact JSON in the cell
    }
    bool q = false;
    for (char c : raw) if (c == ',' || c == '"' || c == '\n' || c == '\r') { q = true; break; }
    if (!q) { out += raw; return; }
    out += '"';
    for (char c : raw) { if (c == '"') out += '"'; out += c; }
    out += '"';
}

} // namespace

std::string jsontools::to_csv(const std::string& src) {
    yaml_parser_t p;
    if (!yaml_parser_initialize(&p)) return ERRMARK "kan de parser niet initialiseren";
    yaml_parser_set_input_string(&p, (const unsigned char*)src.data(), src.size());
    yaml_document_t doc;
    bool ok = yaml_parser_load(&p, &doc) != 0;
    std::string perr = ok ? "" : err_at(p);
    JV root;
    if (ok) { ok = node_to_jv(&doc, yaml_document_get_root_node(&doc), root, 0); yaml_document_delete(&doc); }
    yaml_parser_delete(&p);
    if (!perr.empty()) return ERRMARK + perr;

    if (root.t != JV::Arr)
        return ERRMARK "JSON -> CSV verwacht een array (van objecten).";

    std::vector<std::string> cols;                    // key union, first-seen order
    for (const JV& row : root.arr)
        if (row.t == JV::Obj)
            for (const auto& kv : row.obj)
                if (std::find(cols.begin(), cols.end(), kv.first) == cols.end())
                    cols.push_back(kv.first);

    std::string out;
    if (cols.empty()) {                               // array of scalars -> one column
        out += "value\r\n";
        for (const JV& v : root.arr) { csv_cell(v, out); out += "\r\n"; }
        return out;
    }
    for (size_t c = 0; c < cols.size(); ++c) { if (c) out += ','; JV k; k.t = JV::Str; k.s = cols[c]; csv_cell(k, out); }
    out += "\r\n";
    for (const JV& row : root.arr) {
        for (size_t c = 0; c < cols.size(); ++c) {
            if (c) out += ',';
            const JV* cell = nullptr;
            if (row.t == JV::Obj)
                for (const auto& kv : row.obj) if (kv.first == cols[c]) { cell = &kv.second; break; }
            if (cell) csv_cell(*cell, out);
        }
        out += "\r\n";
    }
    return out;
}

#endif  // HAVE_LIBYAML
