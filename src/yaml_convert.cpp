#include "yaml_convert.h"

// Error convention for yaml_to_json / json_to_yaml: a result starting with the
// SOH byte "\001" is an error message, not a converted document. The caller
// shows it in a message box instead of replacing the buffer. yaml_validate
// keeps its own convention: "" means valid, anything else is the problem.
#define ERRMARK "\001"

#ifndef HAVE_LIBYAML

// ─── stubs (libyaml not vendored — see vendor/VENDORING.md) ─────────────────
static const char* kNote =
    "libyaml is niet meegecompileerd. Zie vendor/VENDORING.md "
    "(git clone ... vendor/libyaml) en bouw opnieuw.";

bool        yaml_convert_available()          { return false; }
std::string yaml_validate(const std::string&) { return kNote; }
std::string yaml_to_json(const std::string&)  { return ERRMARK + std::string(kNote); }
std::string json_to_yaml(const std::string&)  { return ERRMARK + std::string(kNote); }

#else

#include <yaml.h>
#include <cstdio>
#include <cctype>
#include <vector>

bool yaml_convert_available() { return true; }

namespace {

std::string err_at(yaml_parser_t& p) {
    const char* m = p.problem ? p.problem : "parse error";
    char buf[96];
    std::snprintf(buf, sizeof buf, "regel %lu, kolom %lu: %s",
                  (unsigned long)(p.problem_mark.line + 1),
                  (unsigned long)(p.problem_mark.column + 1), m);
    return buf;
}

void json_escape(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
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

// Plain (unquoted) YAML scalar -> JSON token. Quoted scalars are always strings.
void plain_scalar_to_json(const std::string& v, std::string& out) {
    if (v.empty() || v == "~" || v == "null" || v == "Null" || v == "NULL") { out += "null"; return; }
    if (v == "true"  || v == "True"  || v == "TRUE")  { out += "true";  return; }
    if (v == "false" || v == "False" || v == "FALSE") { out += "false"; return; }

    auto looks_number = [](const std::string& t) {
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
    };
    if (looks_number(v)) out += v;
    else json_escape(v, out);
}

struct Frame { int kind; bool first; bool expect_key; };  // 1 map, 2 seq

} // namespace

std::string yaml_validate(const std::string& src) {
    yaml_parser_t p;
    if (!yaml_parser_initialize(&p)) return "kan de YAML-parser niet initialiseren";
    yaml_parser_set_input_string(&p, (const unsigned char*)src.data(), src.size());
    std::string problem;
    yaml_event_t ev;
    for (;;) {
        if (!yaml_parser_parse(&p, &ev)) { problem = err_at(p); break; }
        yaml_event_type_t t = ev.type;
        yaml_event_delete(&ev);
        if (t == YAML_STREAM_END_EVENT) break;
    }
    yaml_parser_delete(&p);
    return problem;
}

std::string yaml_to_json(const std::string& src) {
    yaml_parser_t p;
    if (!yaml_parser_initialize(&p)) return ERRMARK "kan de YAML-parser niet initialiseren";
    yaml_parser_set_input_string(&p, (const unsigned char*)src.data(), src.size());

    std::string out, err;
    std::vector<Frame> st;
    std::vector<std::string> docs;
    yaml_event_t ev;

    auto before_value = [&](bool& is_key) {
        is_key = false;
        if (st.empty()) return;
        Frame& f = st.back();
        if (f.kind == 2) { if (!f.first) out += ','; f.first = false; }
        else if (f.kind == 1 && f.expect_key) {
            if (!f.first) out += ',';
            f.first = false;
            is_key = true;
        }
    };
    auto after_value = [&]() {
        if (!st.empty() && st.back().kind == 1)
            st.back().expect_key = true;
    };

    for (;;) {
        if (!yaml_parser_parse(&p, &ev)) { err = err_at(p); break; }
        bool stop = false;

        switch (ev.type) {
        case YAML_STREAM_START_EVENT:   break;
        case YAML_STREAM_END_EVENT:     stop = true; break;
        case YAML_DOCUMENT_START_EVENT: out.clear(); st.clear(); break;
        case YAML_DOCUMENT_END_EVENT:   docs.push_back(out); break;

        case YAML_ALIAS_EVENT:
            err = "anchors/aliases worden nog niet ondersteund bij JSON-conversie";
            stop = true;
            break;

        case YAML_SCALAR_EVENT: {
            bool is_key = false;
            before_value(is_key);
            std::string v((const char*)ev.data.scalar.value, ev.data.scalar.length);
            if (is_key) {
                json_escape(v, out);
                out += ':';
                st.back().expect_key = false;
            } else {
                if (ev.data.scalar.style == YAML_PLAIN_SCALAR_STYLE) plain_scalar_to_json(v, out);
                else json_escape(v, out);
                after_value();
            }
            break;
        }
        case YAML_SEQUENCE_START_EVENT: {
            bool is_key = false; before_value(is_key);
            if (is_key) { err = "complexe keys worden niet ondersteund"; stop = true; break; }
            out += '[';
            st.push_back({2, true, false});
            break;
        }
        case YAML_SEQUENCE_END_EVENT:
            out += ']'; st.pop_back(); after_value();
            break;
        case YAML_MAPPING_START_EVENT: {
            bool is_key = false; before_value(is_key);
            if (is_key) { err = "complexe keys worden niet ondersteund"; stop = true; break; }
            out += '{';
            st.push_back({1, true, true});
            break;
        }
        case YAML_MAPPING_END_EVENT:
            out += '}'; st.pop_back(); after_value();
            break;
        default: break;
        }

        yaml_event_delete(&ev);
        if (stop) break;
    }
    yaml_parser_delete(&p);

    if (!err.empty()) return ERRMARK + err;
    if (docs.empty()) return "null";
    if (docs.size() == 1) return docs[0];
    std::string arr = "[";
    for (size_t i = 0; i < docs.size(); ++i) { if (i) arr += ','; arr += docs[i]; }
    arr += ']';
    return arr;
}

std::string json_to_yaml(const std::string& src) {
    // JSON is (near enough) a subset of YAML: reparse and re-emit in block style.
    yaml_parser_t p;
    yaml_emitter_t em;
    if (!yaml_parser_initialize(&p)) return ERRMARK "kan de parser niet initialiseren";
    if (!yaml_emitter_initialize(&em)) { yaml_parser_delete(&p); return ERRMARK "kan de emitter niet initialiseren"; }

    yaml_parser_set_input_string(&p, (const unsigned char*)src.data(), src.size());

    std::string out;
    yaml_emitter_set_output(&em, [](void* ctx, unsigned char* buf, size_t sz) -> int {
        static_cast<std::string*>(ctx)->append((const char*)buf, sz);
        return 1;
    }, &out);
    yaml_emitter_set_unicode(&em, 1);
    yaml_emitter_set_width(&em, -1);
    yaml_emitter_set_break(&em, YAML_LN_BREAK);

    std::string err;
    yaml_event_t ev;
    for (;;) {
        if (!yaml_parser_parse(&p, &ev)) { err = err_at(p); break; }

        if (ev.type == YAML_MAPPING_START_EVENT)
            ev.data.mapping_start.style = YAML_BLOCK_MAPPING_STYLE;
        else if (ev.type == YAML_SEQUENCE_START_EVENT)
            ev.data.sequence_start.style = YAML_BLOCK_SEQUENCE_STYLE;
        else if (ev.type == YAML_SCALAR_EVENT) {
            ev.data.scalar.style = YAML_ANY_SCALAR_STYLE;
            ev.data.scalar.plain_implicit = 1;
            ev.data.scalar.quoted_implicit = 1;
        }

        int done = (ev.type == YAML_STREAM_END_EVENT);
        if (!yaml_emitter_emit(&em, &ev)) {           // takes ownership of ev
            err = em.problem ? em.problem : "emit-fout";
            break;
        }
        if (done) break;
    }

    yaml_emitter_delete(&em);
    yaml_parser_delete(&p);
    return err.empty() ? out : (ERRMARK + err);
}

#endif  // HAVE_LIBYAML
