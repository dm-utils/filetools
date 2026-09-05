// Standalone console harness, built by build_harness.cmd and run by test.cmd.
// Not part of the shipped plugin.
//
//   test_harness <file> default|wide                 -> yaml_tidy
//   test_harness <file> validate|tojson|toyaml       -> yaml_convert  (needs libyaml)
//   test_harness <file> jpretty|jmin|jsort           -> json_tools    (needs libyaml)
//   test_harness <file> jesc|junesc                  -> json_tools    (pure)
#include "yaml_tidy.h"
#include "yaml_convert.h"
#include "json_tools.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static std::string read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: test_harness <file> [default|wide|validate|tojson|toyaml|"
                     "jpretty|jmin|jsort|jesc|junesc]\n";
        return 1;
    }
    std::string src = read_file(argv[1]);
    std::string op  = argc > 2 ? argv[2] : "default";

    if (op == "validate") {
        std::string r = yaml_validate(src);
        std::cout << (r.empty() ? "OK\n" : r + "\n");
        return r.empty() ? 0 : 1;
    }
    if (op == "tojson")  { std::cout << yaml_to_json(src) << "\n"; return 0; }
    if (op == "toyaml")  { std::cout << json_to_yaml(src);        return 0; }
    if (op == "jpretty") { std::cout << json_pretty(src)   << "\n"; return 0; }
    if (op == "jmin")    { std::cout << json_minify(src)   << "\n"; return 0; }
    if (op == "jsort")   { std::cout << json_sort_keys(src) << "\n"; return 0; }
    if (op == "jesc")    { std::cout << json_escape(src)   << "\n"; return 0; }
    if (op == "junesc")  { std::cout << json_unescape(src) << "\n"; return 0; }

    TidyOptions o;
    if (op == "wide") { o.indent_step = 4; o.tab_width = 4; o.max_blank = 2; }
    std::cout << yaml_tidy(src, o);
    return 0;
}
