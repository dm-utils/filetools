#pragma once
#include <string>
#include "settings.h"

// ─── FileTools.ini read/write, no UI ────────────────────────────────────────
// Shared by the plugin (settings_dialog.cpp) and the filetools CLI, so a
// setting chosen in Notepad++ is also the CLI's default.

// %APPDATA%\Notepad++\plugins\config\FileTools.ini (the directory is not created).
std::wstring default_ini_path();

// Missing file or keys -> the defaults in YamlSettings.
YamlSettings read_settings_ini(const std::wstring& path);

void write_settings_ini(const std::wstring& path, const YamlSettings& s);

std::wstring settings_widen(const std::string& utf8);
std::string  settings_narrow(const wchar_t* w);
