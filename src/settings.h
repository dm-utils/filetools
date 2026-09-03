#pragma once
#include "yaml_tidy.h"

// v1: no Settings UI / persistence yet — just the defaults. The FormatSQL-style
// tabbed dialog + INI/JSON persistence is the next milestone (see DESIGN.md).
struct YamlSettings {
    TidyOptions tidy;
    bool        format_on_save = false;
};

extern YamlSettings g_settings;
