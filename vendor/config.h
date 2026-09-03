/* Minimal config.h for building vendored libyaml with MSVC.
   libyaml's real config.h (autotools/cmake) only supplies these version
   macros; everything else is handled in yaml.h. Keep the version in sync
   with whatever tag you vendored (see vendor/VENDORING.md). */
#ifndef YAML_CONFIG_H
#define YAML_CONFIG_H

#define YAML_VERSION_MAJOR  0
#define YAML_VERSION_MINOR  2
#define YAML_VERSION_PATCH  5
#define YAML_VERSION_STRING "0.2.5"

#endif
