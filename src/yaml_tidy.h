#pragma once
#include <string>

// ─── Datamodder YAML Tools: reindent / tidy ──────────────────────────────────
// Line-based, comment-preserving normaliser for block-style YAML.
//
// What it does:
//   - expands leading tabs to spaces
//   - re-emits every level of nesting at a consistent indent step, regardless
//     of how (in)consistently the source was indented
//   - strips trailing whitespace
//   - caps runs of blank lines
//   - guarantees a single trailing newline
//   - resets nesting at document markers (--- / ...)
//
// What it deliberately leaves untouched (never corrupts):
//   - the bodies of block scalars ( key: | , key: > ) — emitted byte-for-byte
//   - lines inside an unclosed flow collection ( { ... }, [ ... ] )
//   - the text of comments and quoted scalars
//
// This is the everyday command. Data-only operations (validate, YAML<->JSON,
// sort keys, ...) will go through a real YAML parser (libyaml) later.

struct TidyOptions {
    int  indent_step       = 2;    // spaces added per nesting level
    int  tab_width          = 2;    // a leading tab advances to the next multiple of this
    int  max_blank          = 1;    // at most this many consecutive blank lines
    bool strip_trailing_ws = true;
    bool final_newline      = true;
};

std::string yaml_tidy(const std::string& src, const TidyOptions& opt = TidyOptions{});
