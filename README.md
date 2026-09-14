# Datamodder File Tools (FileTools)

A Notepad++ plugin for tidying, validating and converting text data formats
(YAML, JSON, CSV) plus a read-only Parquet preview.

**64-bit Notepad++ only.**

## Status

Working:

- **Reindent / tidy** (`Ctrl+Alt+Y`) — normalise a block-style YAML document
  (tabs → spaces, consistent indent step, trailing-whitespace strip,
  blank-line cap, single trailing newline, nesting reset at `---` / `...`).
  Block-scalar bodies, unclosed flow collections and quoted text are left
  untouched.
- **Format on Save** (On / Off, persisted).
- **JSON**: pretty-print (indent from Settings), minify, sort keys, escape/unescape.
- **CSV**: align columns (quote mode from Settings), compact, delimiter convert,
  sort by column, transpose, add/remove quotes.
- **Parquet** (via `duckdb.exe` on PATH, next to the DLL, or a path set in
  Settings): `Parquet → …` previews a saved `.parquet` file in a new tab
  (read-only); `… → Parquet` writes a `.parquet` file next to the current
  document.
- **Full conversion matrix** — every format submenu carries every conversion
  *from* that format, so YAML/JSON/CSV/Parquet all reach each other (YAML
  routes through JSON; to/from Parquet needs `duckdb.exe`).
- **Settings dialog** (`Settings...`) — tabbed (YAML / JSON / CSV / Parquet /
  Behavior), INI-persisted, with named profiles (save/load/delete,
  export/import as JSON) — lifted from the FormatSQL plugin's dialog pattern.

The menu is grouped into **YAML / JSON / CSV / Parquet** submenus (built at
`NPPN_READY` from the flat `g_funcs` list — see `build_all_submenus`).

Stubbed / planned — see [DESIGN.md](DESIGN.md):

- Validate, YAML ↔ JSON, sort keys (with pinned keys), expand anchors,
  minify/flow, lint.

## Build

Visual Studio C++ build tools required. From an **elevated** terminal:

```
build.cmd
```

Compiles the plugin, closes Notepad++, deploys `FileTools.dll` + `help.txt`
to `C:\Program Files\Notepad++\plugins\FileTools`, and restarts Notepad++.

## Test

```
test.cmd
```

Builds a console harness and diffs `yaml_tidy` output for `test_docs.yaml`
against `tests/golden_*.txt` (profiles `default` and `wide`). When output
changes on purpose, regenerate:

```
build\harness\test_harness.exe test_docs.yaml default > tests\golden_default.txt
build\harness\test_harness.exe test_docs.yaml wide    > tests\golden_wide.txt
```

## Layout

```
src/yaml_tidy.{h,cpp}   the YAML reindenter — pure, no Windows deps, unit-tested
src/dllmain.cpp         Notepad++ plugin glue (self-declared NPP ABI)
src/settings.h          YamlSettings struct + load/save/dialog declarations
src/settings_dialog.cpp tabbed Settings dialog, INI persistence, profiles, About dialog
src/settings.rc         dialog resources, version resource + manifest
src/test_harness.cpp    console runner
test_docs.yaml          numbered test cases
tests/golden_*.txt      expected output per profile
```

## License

MIT — see [LICENSE](LICENSE).
