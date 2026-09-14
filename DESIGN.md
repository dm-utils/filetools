# Datamodder File Tools — design

Same build style as the FormatSQL plugin: Win32 C++, 64-bit, self-declared
Notepad++ ABI (no SDK), `g_funcs[]` menu, tabbed Settings with INI+JSON
persistence, `apply_transform` pattern, console regression harness,
`build.cmd` deploy, version bump in `settings.rc`.

Repo `dm-utils/filetools`, plugin folder `FileTools`, display name
"Datamodder File Tools", MIT.

## Why this plugin

YAML is indentation-sensitive, forbids tabs for indentation, and there is no
good reindent / lint / convert plugin for Notepad++. Audience: Kubernetes,
CI/CD, Ansible, dbt `.yml`, Docker Compose.

## Two engines (the main design choice)

| Operation | Engine | Comments kept? |
|---|---|---|
| Reindent/tidy, tabs→spaces, trailing-ws, blank-lines, fold, path-at-cursor, lint | **own line-based reformatter** (`yaml_tidy.cpp`, plus a quote/comment-aware scanner) | **yes** |
| Validate, YAML↔JSON, sort keys, expand anchors, minify/flow, quote-all/unquote | **libyaml** (MIT, C; vendor ~10 `.c` files into `build.cmd` like FormatSQL did with `dialects.cpp`) | no (data-only, warned once) |

Menu items carry a grey hint: `(comments preserved)` vs `(data-only)`.

## Menu layout (target)

```
YAML Tools
├─ Reindent / tidy            Ctrl+Alt+Y     (comments preserved)
├─ Minify (flow style)                       (data-only)
├─ ─────────
├─ Convert ▸ YAML → JSON  /  JSON → YAML  /  YAML → .env  /  .properties → YAML
├─ Keys ▸ Sort A→Z (recursive) / Z→A / top-level only
├─ Quotes ▸ Quote all ('…') / Unquote where safe / Single ⇄ double
├─ Anchors ▸ Expand anchors/aliases inline
├─ ─────────
├─ Validate            (syntax check, jump to first error)
├─ Lint               (indent step, duplicate keys, tabs, trailing ws, yes/no…)
├─ Path at cursor  →  dotted path (a.b[0].c) to clipboard
├─ Fold to level ▸ 1 / 2 / 3 / all
├─ ─────────
├─ Format on Save ▸ On / Off
├─ Settings…  ·  About  ·  Help
```

The submenu builder (`build_all_submenus` / `find_my_menu`, copied from
FormatSQL's `dllmain.cpp`) and the Settings dialog have both shipped — see
"Settings dialog (v1, shipped)" below. A `set_*_checkmark` for Format on
Save (like FormatSQL's) hasn't landed yet; the menu shows "On"/"Off" as two
plain items rather than a checked radio state.

## Parquet — everything shells out to DuckDB

Parquet is columnar binary; there is no line-based editing story, and a real
reader (Apache Arrow) is far too big to statically link into an NPP plugin.
So all Parquet work goes through `duckdb.exe` (a single ~30 MB self-contained
binary, MIT), found on `PATH` or next to the plugin DLL; if missing, a message
box points at duckdb.org.

- **`Parquet → CSV / JSON` (preview)** — read-only. Runs
  `duckdb -csv|-json -c "SELECT * FROM read_parquet('<file>') LIMIT 100000"`,
  captures stdout via `CreateProcessW` + an anonymous pipe (`CREATE_NO_WINDOW`,
  stdout+stderr merged), drops the result into a **new tab**
  (`NPPM_MENUCOMMAND` + `IDM_FILE_NEW`, then `SCI_SETTEXT`). The `.parquet`
  file is never written.
- **`CSV → Parquet` / `JSON → Parquet` (file)** — the current buffer (unsaved
  edits included) is spilled to a temp `.csv` / `.json`, then
  `duckdb -c "COPY (SELECT * FROM read_csv_auto|read_json_auto('<tmp>')) TO
  '<doc dir>\<stem>.parquet' (FORMAT PARQUET)"`. A message box reports the
  output path.

Not golden-tested (needs the external binary + a fixture).

## Menu grouping

Notepad++ only takes a flat `getFuncsArray`. At `NPPN_READY` (and, as a
fallback, on the first `messageProc`) `try_build_menu` → `build_all_submenus`
finds the plugin's own popup with `find_my_menu` (walk the menu bar, match the
`getName()` string), `DeleteMenu`s the flat slots and re-inserts them as the
**YAML / JSON / CSV / Parquet** and **Format on Save** popups via
`CreatePopupMenu` + `AppendMenuW` + `InsertMenuW`, keyed by the `_cmdID`s
Notepad++ filled into `g_funcs`. Same pattern as FormatSQL's `build_all_submenus`.
`Reindent / tidy` stays at the top level so its `Ctrl+Alt+Y` hint shows.

Each format submenu carries the **full conversion matrix from that format**
(YAML → JSON/CSV/Parquet, JSON → YAML/CSV/Parquet, CSV → JSON/YAML/Parquet,
Parquet → CSV/JSON/YAML), and the Parquet submenu additionally repeats the
`… → Parquet` direction (same `_cmdID`s, listed in two menus). JSON is the hub:
the conversions DuckDB and libyaml don't do directly are composed —
`yaml_to_csv_conv` = `yaml_to_json` → `jsontools::to_csv`, `csv_to_yaml_conv` =
`csv_to_json` → `json_to_yaml`, `Parquet → YAML` = duckdb `-json` →
`json_to_yaml`, `YAML → Parquet` = `yaml_to_json` → temp `.json` → duckdb COPY.
Errors (leading SOH) short-circuit each chain.

## Settings dialog (v1, shipped)

Tabbed `IDD_SETTINGS` + `IDC_NAV` listbox, `settings_dialog.cpp` — same
pattern as FormatSQL's dialog (child `CreateDialog` pages positioned beside
the nav list, shown/hidden on selection). INI persistence at
`%APPDATA%\Notepad++\plugins\config\FileTools.ini`; named profiles as
`.json` files under `...\config\FileTools_profiles\`, with save/load/delete/
export/import — same `settings_to_json`/`settings_from_json` +
`get_profiles_dir` mechanism as FormatSQL.

- **YAML** — indent step, tab width, max. consecutive blank lines, strip
  trailing whitespace, ensure a final newline (all four `TidyOptions` fields).
- **JSON** — pretty-print indent (2 or 4 spaces), sort keys by default.
  `Sort keys` (the explicit menu command) always sorts regardless of this
  default; only `Pretty-print` reads it.
- **CSV** — quote mode for `Align columns`: only when needed, or always.
  Two new one-off menu commands, `Add quotes` / `Remove quotes`, ignore this
  setting and always force their own mode.
- **Parquet** — override the `duckdb.exe` path (Browse... button); empty
  falls back to PATH, then next to the DLL, as before.
- **Behavior** — Format on Save + the profiles UI.

Not done: a `set_*_checkmark`-style live indicator for Format on Save,
FQDN-style cross-field validation (not needed here), and the broader ideas
below remain unimplemented.

## Settings (tabs, further ideas / not yet started)

- **Indent** — the shipped YAML tab covers spaces-per-level, tab width, max
  blank lines, trailing newline, strip trailing ws. Not yet: sequences
  indented under key vs. at key level.
- **Style** — quote policy (minimal/single/double/preserve), block↔flow for
  short maps/seqs, `---` / `...` markers add/keep/remove.
- **Keys** — default sort (off/asc/desc), case-insensitive, **pinned keys**
  that stay on top (`apiVersion, kind, metadata, name` → Kubernetes), sort
  only under certain paths.
- **JSON** — indent width and sort-keys-by-default are shipped. Not yet:
  `null` / `~` / empty handling on the way back to YAML.
- **Lint** — per-rule on/off + severity: duplicate keys, wrong indent step,
  tabs, trailing ws, missing `---`, line length, empty values, unquoted
  `yes/no/on/off/NO` (the "Norway problem").
- **Profiles** — save/load/export/import is shipped (generic named JSON
  profiles). Not yet: curated starter sets (k8s / dbt / Ansible / CI).
- Follows Notepad++ dark mode.

## `yaml_tidy` — how the reindenter works

Line-based, no full parse. Per line:

1. Split on `\n`, drop `\r` (remember CRLF, restore at the end).
2. Blank lines → counted, later flushed capped at `max_blank` (no leading
   blanks).
3. Inside a block scalar (`key: |` / `>`), body lines (`indent > key indent`,
   or blank) are emitted **byte-for-byte**; the block ends at the first line
   that dedents to ≤ the key.
4. Document markers (`---`, `...`) → emitted at column 0, ancestor stack
   cleared.
5. Inside an unclosed flow collection (`{` / `[` net depth > 0) → lines
   emitted verbatim (rstripped) until balanced. Depth counting is
   quote/comment aware.
6. Otherwise, maintain a stack of `(source-indent, target-indent)` for open
   ancestors. Pop entries with `source-indent >= this line's`, the new target
   indent is `parent target + step` (or 0). Full-line comments and blanks are
   positioned from the stack but do **not** mutate it, so they can't corrupt
   the following real line's indent.
7. Emit `target-indent` spaces + the rstripped content. Interior spacing
   (e.g. `key:      value`) is left alone — value alignment is a separate,
   later feature.

Known v1 limitations (documented in `help.txt`):

- block-scalar bodies don't move with their key
- interior spacing / `:` alignment not touched
- list-item `-` handled generically (no "sequences under key" style option yet)
- libyaml is YAML **1.1** — has the `yes/no`→bool trap, octal, etc.; fine for
  a tools plugin, and lint can flag exactly those.

## MVP (v1.0)

Reindent/tidy · tabs→spaces · Validate · YAML→JSON + JSON→YAML · Sort keys
with pinned keys · Lint (duplicate keys, tabs, indent step, trailing ws) ·
Path at cursor · Settings (YAML/JSON/CSV/Parquet/Behavior + profiles,
shipped) · Format-on-Save (shipped) · About/Help (shipped) · multi-document
(`---`) from day one.

## Release / distribution

Same as FormatSQL: tag `vX.Y.Z.W`, GitHub Release with a `FileTools.zip`
(DLL + `help.txt` at zip root), version in `settings.rc` must match. To get an
in-app **Update** button, submit an entry to
`notepad-plus-plus/nppPluginList` (`src/pl.x64.json`: `folder-name`,
`display-name`, `version`, `id` = SHA-256 of the zip, `repository` = release
asset URL), then a small PR per version.
