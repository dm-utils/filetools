# libyaml

The data-only operations (Validate, YAML → JSON, JSON → YAML, sort keys,
expand anchors) are built on **libyaml** (MIT), included as a git submodule
pinned to tag `0.2.5`:

```
git clone --recursive https://github.com/dm-utils/filetools.git
```

or, in a clone made without `--recursive`:

```
git submodule update --init
```

That gives you:

```
vendor/
  config.h                 <- provided by this repo (MSVC version macros)
  libyaml/                 <- submodule
    include/yaml.h
    src/*.c                 <- api reader scanner parser loader writer emitter dumper
    License
```

`build.cmd` and `build_harness.cmd` compile the libyaml sources along with the
plugin, and stop with a message when the submodule is missing.

## Updating libyaml

```
cd vendor/libyaml
git fetch --tags
git checkout <new tag>
cd ../..
git add vendor/libyaml
```

Keep `vendor/config.h`'s `YAML_VERSION_*` in step with the tag.
