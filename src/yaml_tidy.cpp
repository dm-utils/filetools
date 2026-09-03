#include "yaml_tidy.h"
#include <vector>
#include <utility>

namespace {

std::string rstrip(const std::string& s) {
    size_t e = s.size();
    while (e > 0 && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
    return s.substr(0, e);
}

// Leading-whitespace width with tabs expanded to `tab_width` stops.
int indent_width(const std::string& line, int tab_width, size_t& first_non_ws) {
    int w = 0;
    size_t i = 0;
    for (; i < line.size(); ++i) {
        if (line[i] == ' ') { ++w; }
        else if (line[i] == '\t') { w += tab_width - (w % tab_width); }
        else break;
    }
    first_non_ws = i;
    return w;
}

// Everything in `s` before an unquoted `#` comment (leading or " #").
// Quote-aware: '...' uses '' as the escape, "..." uses backslash.
std::string content_before_comment(const std::string& s) {
    bool in_sq = false, in_dq = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (in_sq) {
            if (c == '\'') { if (i + 1 < s.size() && s[i + 1] == '\'') { ++i; continue; } in_sq = false; }
            continue;
        }
        if (in_dq) {
            if (c == '\\') { ++i; continue; }
            if (c == '"') in_dq = false;
            continue;
        }
        if (c == '\'') { in_sq = true; continue; }
        if (c == '"') { in_dq = true; continue; }
        if (c == '#' && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t'))
            return rstrip(s.substr(0, i));
    }
    return rstrip(s);
}

// Net flow-collection depth change over `s` (quote/comment aware).
int flow_delta(const std::string& s) {
    std::string c = content_before_comment(s);
    bool in_sq = false, in_dq = false;
    int d = 0;
    for (size_t i = 0; i < c.size(); ++i) {
        char ch = c[i];
        if (in_sq) { if (ch == '\'') { if (i + 1 < c.size() && c[i + 1] == '\'') { ++i; continue; } in_sq = false; } continue; }
        if (in_dq) { if (ch == '\\') { ++i; continue; } if (ch == '"') in_dq = false; continue; }
        if (ch == '\'') { in_sq = true; continue; }
        if (ch == '"') { in_dq = true; continue; }
        if (ch == '{' || ch == '[') ++d;
        else if (ch == '}' || ch == ']') --d;
    }
    return d;
}

// Does `content` (already comment-stripped, rstripped) end in a block-scalar
// header, i.e. a value of the form  |  >  |-  >+  |2  |2+  ... ?
bool ends_with_block_scalar(const std::string& content) {
    if (content.empty()) return false;
    size_t ws = content.find_last_of(" \t");
    std::string tok = (ws == std::string::npos) ? content : content.substr(ws + 1);
    std::string before = (ws == std::string::npos) ? "" : rstrip(content.substr(0, ws));
    if (tok.empty() || (tok[0] != '|' && tok[0] != '>')) return false;
    for (size_t i = 1; i < tok.size(); ++i)
        if (tok[i] != '+' && tok[i] != '-' && (tok[i] < '0' || tok[i] > '9')) return false;
    // The value must sit right after "key:" or a "- " sequence dash.
    if (!before.empty() && before.back() == ':') return true;
    if (before == "-") return true;
    return false;
}

bool is_doc_marker(const std::string& rest) {
    if (rest == "..." || rest.rfind("...", 0) == 0) return true;
    if (rest == "---") return true;
    return rest.rfind("---", 0) == 0 && (rest.size() == 3 || rest[3] == ' ' || rest[3] == '\t');
}

} // namespace

std::string yaml_tidy(const std::string& src, const TidyOptions& opt) {
    if (src.empty()) return src;
    const bool crlf = src.find("\r\n") != std::string::npos;

    // Split into lines (drop a trailing \r on each).
    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : src) {
            if (c == '\n') { lines.push_back(cur); cur.clear(); }
            else if (c != '\r') cur += c;
        }
        lines.push_back(cur);
    }

    std::vector<std::string> out;
    std::vector<std::pair<int, int>> stack;   // (source indent, target indent) of open ancestors
    int  pending_blanks   = 0;
    int  flow_depth       = 0;
    bool in_block_scalar  = false;
    int  bs_src_indent    = -1;

    auto predict = [&](int s) -> std::pair<size_t, int> {
        size_t i = stack.size();
        while (i > 0 && stack[i - 1].first >= s) --i;
        int dst = (i == 0) ? 0 : stack[i - 1].second + opt.indent_step;
        return { i, dst };
    };
    auto flush_blanks = [&]() {
        if (out.empty()) { pending_blanks = 0; return; }   // no leading blank lines
        for (int i = 0; i < pending_blanks && i < opt.max_blank; ++i) out.push_back("");
        pending_blanks = 0;
    };

    for (const std::string& raw : lines) {
        const bool blank = raw.find_first_not_of(" \t") == std::string::npos;

        if (in_block_scalar) {
            if (blank) { out.push_back(""); continue; }
            size_t fnw = 0;
            int si = indent_width(raw, opt.tab_width, fnw);
            if (si > bs_src_indent) { out.push_back(raw); continue; }   // body: byte-for-byte
            in_block_scalar = false;                                     // dedent: block ended
        }

        if (blank) { ++pending_blanks; continue; }

        size_t fnw = 0;
        int src_indent = indent_width(raw, opt.tab_width, fnw);
        std::string rest = raw.substr(fnw);

        flush_blanks();

        if (flow_depth > 0) {                                            // inside multiline { } / [ ]
            out.push_back(opt.strip_trailing_ws ? rstrip(raw) : raw);
            flow_depth += flow_delta(rest);
            if (flow_depth < 0) flow_depth = 0;
            continue;
        }

        if (is_doc_marker(rest)) {
            out.push_back(rstrip(rest));
            stack.clear();
            in_block_scalar = false;
            continue;
        }

        const bool comment = rest[0] == '#';
        auto pr = predict(src_indent);
        int dst = pr.second;
        if (!comment) {
            stack.resize(pr.first);
            stack.push_back({ src_indent, dst });
        }

        out.push_back(std::string(dst, ' ') + (opt.strip_trailing_ws ? rstrip(rest) : rest));

        if (!comment) {
            std::string content = content_before_comment(rest);
            if (ends_with_block_scalar(content)) {
                in_block_scalar = true;
                bs_src_indent   = src_indent;
            } else {
                flow_depth += flow_delta(rest);
                if (flow_depth < 0) flow_depth = 0;
            }
        }
    }

    // Trim trailing blank lines, then re-add one newline if wanted.
    while (!out.empty() && out.back().empty()) out.pop_back();

    std::string result;
    for (size_t i = 0; i < out.size(); ++i) {
        result += out[i];
        if (i + 1 < out.size()) result += '\n';
    }
    if (opt.final_newline && !result.empty()) result += '\n';

    if (crlf) {
        std::string tmp;
        tmp.reserve(result.size() + result.size() / 20);
        for (char c : result) { if (c == '\n') tmp += '\r'; tmp += c; }
        result.swap(tmp);
    }
    return result;
}
