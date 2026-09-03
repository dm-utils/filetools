// Standalone console harness for yaml_tidy.cpp, built by build_harness.cmd and
// run by test.cmd. Not part of the shipped plugin.
#include "yaml_tidy.h"
#include <fstream>
#include <iostream>
#include <sstream>

static std::string read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: test_harness <file.yaml> [profile]\n";
        return 1;
    }
    std::string content = read_file(argv[1]);
    std::string profile = argc > 2 ? argv[2] : "default";

    TidyOptions o;
    if (profile == "wide") {         // 4-space indent, keep up to 2 blank lines
        o.indent_step = 4;
        o.tab_width   = 4;
        o.max_blank   = 2;
    }

    std::cout << yaml_tidy(content, o);
    return 0;
}
