# Vendoring libyaml

The data-only operations (Validate, YAML → JSON, JSON → YAML, and later sort
keys / expand anchors) are built on **libyaml** (MIT). libyaml is *not*
checked in; drop it here once:

```
cd C:\Datamodder\YamlTools
git clone --depth 1 --branch 0.2.5 https://github.com/yaml/libyaml vendor/libyaml
```

That gives you:

```
vendor/
  config.h                 <- provided by this repo (MSVC version macros)
  libyaml/
    include/yaml.h
    src/*.c                 <- api reader scanner parser loader writer emitter dumper
    LICENSE
```

`build.cmd` and `build_harness.cmd` detect `vendor/libyaml/src/api.c`
automatically: when present they add

```
/I vendor  /I vendor\libyaml\include  /DYAML_DECLARE_STATIC  /DHAVE_LIBYAML
vendor\libyaml\src\api.c ... vendor\libyaml\src\dumper.c
```

to the compile, and `src/yaml_convert.cpp` switches from stubs to the real
implementation. Nothing else to configure.

Do **not** commit `vendor/libyaml/` — it's in `.gitignore`. Keep the pinned
tag (`0.2.5`) and `vendor/config.h`'s `YAML_VERSION_*` in step.
