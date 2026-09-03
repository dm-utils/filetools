# Datamodder YAML Tools (YamlTools)

A Notepad++ plugin for tidying, and later converting and validating, YAML.

**64-bit Notepad++ only.**

## Status: early scaffold

Working:

- **Reindent / tidy** (`Ctrl+Alt+Y`) — normalise a block-style YAML document
  (tabs → spaces, consistent indent step, trailing-whitespace strip,
  blank-line cap, single trailing newline, nesting reset at `---` / `...`).
  Block-scalar bodies, unclosed flow collections and quoted text are left
  untouched.
- **Format on Save** (On / Off, not yet persisted).

Stubbed / planned — see [DESIGN.md](DESIGN.md):

- Validate, YAML ↔ JSON, sort keys (with pinned keys), expand anchors,
  minify/flow, lint.
- Tabbed **Settings** dialog (indent / keys / lint / profiles) + INI/JSON
  persistence — to be lifted from the FormatSQL plugin.

## Build

Visual Studio C++ build tools required. From an **elevated** terminal:

```
build.cmd
```

Compiles the plugin, closes Notepad++, deploys `YamlTools.dll` + `help.txt`
to `C:\Program Files\Notepad++\plugins\YamlTools`, and restarts Notepad++.

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
src/yaml_tidy.{h,cpp}   the reindenter — pure, no Windows deps, unit-tested
src/dllmain.cpp         Notepad++ plugin glue (self-declared NPP ABI)
src/settings.h          YamlSettings (defaults only for now)
src/settings.rc         version resource + manifest
src/test_harness.cpp    console runner
test_docs.yaml          numbered test cases
tests/golden_*.txt      expected output per profile
```

## License

MIT — see [LICENSE](LICENSE).
