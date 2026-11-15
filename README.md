# Datamodder File Tools (FileTools)

A Notepad++ plugin for tidying, validating and converting text data formats
(YAML, JSON, CSV) plus a read-only Parquet preview. The same operations ship
as a command-line tool, `filetools.exe`, for batches of files without Notepad++.

**Website:** [datamodder.com/en/utils/file-tools](https://datamodder.com/en/utils/file-tools)
— features, [settings with examples](https://datamodder.com/en/utils/file-tools/settings)
and the [full conversion matrix](https://datamodder.com/en/utils/file-tools/features/conversion-matrix).

For 64-bit, 32-bit and ARM64 Notepad++.

## Status

Working:

- **Reindent / tidy** (`Ctrl+Alt+Y`) — normalise a block-style YAML document
  (tabs → spaces, consistent indent step, trailing-whitespace strip,
  blank-line cap, single trailing newline, nesting reset at `---` / `...`).
  Block-scalar bodies, unclosed flow collections and quoted text are left
  untouched.
- **Format on Save** (On / Off) — reindents `.yaml`/`.yml` files only; off again after a Notepad++ restart.
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
- **Command line** (`filetools.exe`, in the plugin folder) — every operation
  above on many files at once: `filetools yaml-tidy "config\**\*.yaml" --in-place`,
  `filetools csv-to-json data\*.csv --out-dir json`, `--check` for CI, stdin →
  stdout without files. Reads the same `FileTools.ini`. `filetools --help`.
- **Settings dialog** (`Settings...`) — tabbed (YAML / JSON / CSV / Parquet /
  Behavior), INI-persisted, with named profiles (save/load/delete,
  export/import as JSON) — lifted from the FormatSQL plugin's dialog pattern.

The menu is grouped into **YAML / JSON / CSV / Parquet** submenus (built at
`NPPN_READY` from the flat `g_funcs` list — see `build_all_submenus`).

Planned:

- Validate, YAML ↔ JSON, sort keys (with pinned keys), expand anchors,
  minify/flow, lint.

## Build

Visual Studio C++ build tools required. libyaml comes in as a git submodule,
so clone with `--recursive` (or run `git submodule update --init` in an
existing clone). Then, from an **elevated** terminal:

```
git clone --recursive https://github.com/dm-utils/filetools.git
cd filetools
build.cmd
```

Compiles the plugin and the CLI, closes Notepad++, deploys `FileTools.dll`,
`filetools.exe` and `help.txt` to `C:\Program Files\Notepad++\plugins\FileTools`,
and restarts Notepad++.

## Release

```
release.cmd
```

Builds `FileTools.dll` and `filetools.exe` and zips them with `help.txt` into
`release_staging\FileTools.zip` (x64) or `FileTools_<arch>.zip`, printing the
SHA-256 the plugin lists need. By default only x64; `release.cmd x64 x86 arm64` (or the commented line
in the script) also builds the Win32 and ARM64 versions. ARM64 needs the
"MSVC C++ ARM64/ARM64EC build tools" component of Visual Studio.

## Test

```
test.cmd
```

Builds a console harness and `filetools.exe` (into `build\harness`, no
deploy) and diffs their output against `tests/golden_*.txt`: the tidy profiles
`default` and `wide`, the JSON/CSV/convert operations, and the CLI's batch
behaviour (`**` patterns, `--check` exit codes, `--in-place`, `--out-dir`, a
failing file in a batch). When output changes on purpose, regenerate:

```
build\harness\test_harness.exe test_docs.yaml default > tests\golden_default.txt
build\harness\test_harness.exe test_docs.yaml wide    > tests\golden_wide.txt
```

## Layout

```
src/yaml_tidy.{h,cpp}   the YAML reindenter — pure, no Windows deps, unit-tested
src/yaml_convert.cpp    validate, YAML <-> JSON (libyaml)
src/json_tools.cpp      pretty/minify/sort/escape, JSON <-> CSV
src/csv_tools.cpp       align/compact/quotes/delimiters/sort/transpose, CSV -> JSON
src/parquet_tools.cpp   Parquet via duckdb.exe, no UI (plugin and CLI)
src/settings_io.cpp     FileTools.ini read/write, no UI (plugin and CLI)
src/cli_main.cpp        filetools.exe: operations + pattern expansion + batch output
src/dllmain.cpp         Notepad++ plugin glue (self-declared NPP ABI)
src/settings.h          YamlSettings struct + load/save/dialog declarations
src/settings_dialog.cpp tabbed Settings dialog, profiles, About dialog
src/settings.rc         dialog resources, version resource + manifest
src/test_harness.cpp    console runner
test_docs.yaml          numbered test cases
tests/golden_*.txt      expected output per profile
```

## License

MIT — see [LICENSE](LICENSE).
