#include "yaml_convert.h"
#include "json_tools.h"

// Error convention for yaml_to_json / json_to_yaml: a result starting with the
// SOH byte "\001" is an error message, not a converted document. The caller
// shows it in a message box instead of replacing the buffer. yaml_validate
// keeps its own convention: "" means valid, anything else is the problem.
#define ERRMARK "\001"

// YAML -> JSON delegates to the shared json_tools value-tree engine (pretty
// output, multi-document -> array). It handles the "no libyaml" case itself.
std::string yaml_to_json(const std::string& src) { return jsontools::reserialize(src, 2, false); }

#ifndef HAVE_LIBYAML

// ─── stubs (libyaml not vendored — see vendor/VENDORING.md) ─────────────────
static const char* kNote =
    "libyaml is niet meegecompileerd. Zie vendor/VENDORING.md "
    "(git clone ... vendor/libyaml) en bouw opnieuw.";

bool        yaml_convert_available()          { return false; }
std::string yaml_validate(const std::string&) { return kNote; }
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
