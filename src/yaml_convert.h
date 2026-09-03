#pragma once
#include <string>

// ─── Data-only YAML operations (libyaml) ────────────────────────────────────
// These parse the document, so comments and original layout are lost — that is
// expected for convert/validate. The line-based yaml_tidy() is the everyday
// command; this is for structural transforms.
//
// Until libyaml is vendored (see vendor/VENDORING.md) these are stubs:
//   yaml_convert_available() == false, and the functions return a short note.

bool yaml_convert_available();

// "" if the document parses; otherwise "line L, col C: <problem>".
std::string yaml_validate(const std::string& src);

// YAML -> compact-ish JSON. Multiple documents become a JSON array.
// Anchors/aliases are not supported and produce an error string prefixed "! ".
std::string yaml_to_json(const std::string& src);

// JSON (a subset of YAML) -> block-style YAML.
std::string json_to_yaml(const std::string& src);
