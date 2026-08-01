#include "archnet/http_message.h"

#include <cctype>

namespace archnet {

namespace {

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string to_lower_copy(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string trim_ws(const std::string& s) {
    size_t start = s.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t");
    return s.substr(start, end - start + 1);
}

// Parses "<digits>.<digits>" (an HTTP-version's number part) into major/minor.
// Also accepts a bare "<digits>" with no dot (e.g. "HTTP/2", "HTTP/3"),
// defaulting minor to 0: real HTTP/2+ has no textual status line at the
// wire level at all, but tools commonly log/display one in this form.
bool parse_version_numbers(const std::string& number_part, int& major, int& minor) {
    size_t dot = number_part.find('.');
    if (dot == std::string::npos) {
        if (number_part.empty() || number_part.size() > 9) return false;
        for (char c : number_part) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        }
        major = std::stoi(number_part);
        minor = 0;
        return true;
    }
    std::string major_str = number_part.substr(0, dot);
    std::string minor_str = number_part.substr(dot + 1);
    if (major_str.empty() || minor_str.empty() || major_str.size() > 9 || minor_str.size() > 9) {
        return false;
    }
    for (char c : major_str) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    for (char c : minor_str) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    major = std::stoi(major_str);
    minor = std::stoi(minor_str);
    return true;
}

}  // namespace

void HttpHeaders::add(const std::string& name, const std::string& value) {
    entries_.emplace_back(name, value);
}

std::optional<std::string> HttpHeaders::get(const std::string& name) const {
    for (const auto& [k, v] : entries_) {
        if (iequals(k, name)) return v;
    }
    return std::nullopt;
}

std::vector<std::string> HttpHeaders::get_all(const std::string& name) const {
    std::vector<std::string> out;
    for (const auto& [k, v] : entries_) {
        if (iequals(k, name)) out.push_back(v);
    }
    return out;
}

bool HttpHeaders::has(const std::string& name) const {
    for (const auto& [k, v] : entries_) {
        if (iequals(k, name)) return true;
    }
    return false;
}

bool parse_request_line(const std::string& line, RequestLine& out, std::string& error) {
    size_t first_space = line.find(' ');
    if (first_space == std::string::npos) {
        error = "malformed request line: missing method separator";
        return false;
    }
    size_t second_space = line.find(' ', first_space + 1);
    if (second_space == std::string::npos) {
        error = "malformed request line: missing target separator";
        return false;
    }

    out.method = line.substr(0, first_space);
    out.target = line.substr(first_space + 1, second_space - (first_space + 1));
    std::string version = line.substr(second_space + 1);

    if (out.method.empty()) {
        error = "empty method";
        return false;
    }
    if (out.target.empty()) {
        error = "empty request target";
        return false;
    }
    if (version.rfind("HTTP/", 0) != 0) {
        error = "malformed HTTP version";
        return false;
    }
    if (!parse_version_numbers(version.substr(5), out.http_major, out.http_minor)) {
        error = "malformed HTTP version";
        return false;
    }
    return true;
}

bool parse_status_line(const std::string& line, StatusLine& out, std::string& error) {
    if (line.rfind("HTTP/", 0) != 0) {
        error = "malformed status line: missing HTTP version";
        return false;
    }
    size_t space1 = line.find(' ');
    if (space1 == std::string::npos) {
        error = "malformed status line: missing status code";
        return false;
    }
    if (!parse_version_numbers(line.substr(5, space1 - 5), out.http_major, out.http_minor)) {
        error = "malformed HTTP version";
        return false;
    }

    size_t rest_start = space1 + 1;
    size_t space2 = line.find(' ', rest_start);
    std::string status_str =
        (space2 == std::string::npos) ? line.substr(rest_start) : line.substr(rest_start, space2 - rest_start);
    if (status_str.size() != 3) {
        error = "status code must be exactly 3 digits";
        return false;
    }
    for (char c : status_str) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            error = "status code must be numeric";
            return false;
        }
    }
    out.status_code = std::stoi(status_str);
    out.reason_phrase = (space2 == std::string::npos) ? "" : line.substr(space2 + 1);
    return true;
}

bool parse_headers(const std::string& input, size_t& pos, HttpHeaders& out, std::string& error) {
    auto read_line = [&](std::string& line) -> bool {
        size_t start = pos;
        size_t end = input.find('\n', start);
        if (end == std::string::npos) return false;
        size_t content_end = end;
        if (content_end > start && input[content_end - 1] == '\r') content_end--;
        line = input.substr(start, content_end - start);
        pos = end + 1;
        return true;
    };

    bool have_pending = false;
    std::string pending_name;
    std::string pending_value;
    auto flush_pending = [&]() {
        if (have_pending) {
            out.add(pending_name, pending_value);
            have_pending = false;
        }
    };

    for (;;) {
        std::string line;
        if (!read_line(line)) {
            error = "incomplete header block: no terminating blank line found";
            return false;
        }
        if (line.empty()) {
            flush_pending();
            return true;
        }

        if (line[0] == ' ' || line[0] == '\t') {
            // obs-fold continuation. Tolerated for compatibility, but note
            // this is a known HTTP request-smuggling vector (RFC 9112
            // section 5.2): a security-hardened implementation sitting in
            // front of another server should reject it outright instead.
            if (!have_pending) {
                error = "header continuation line with no preceding header";
                return false;
            }
            pending_value += ' ';
            pending_value += trim_ws(line);
            continue;
        }

        flush_pending();

        size_t colon = line.find(':');
        if (colon == std::string::npos) {
            error = "malformed header line: missing ':'";
            return false;
        }
        std::string name = line.substr(0, colon);
        if (name.empty() || name.back() == ' ' || name.back() == '\t') {
            error = "empty or malformed header name";
            return false;
        }
        pending_name = name;
        pending_value = trim_ws(line.substr(colon + 1));
        have_pending = true;
    }
}

BodyFraming determine_body_framing(const HttpHeaders& headers, size_t& content_length) {
    auto transfer_encoding = headers.get("Transfer-Encoding");
    if (transfer_encoding) {
        const std::string& te = *transfer_encoding;
        size_t last_comma = te.find_last_of(',');
        std::string last_encoding = (last_comma == std::string::npos) ? te : te.substr(last_comma + 1);
        if (to_lower_copy(trim_ws(last_encoding)) == "chunked") return BodyFraming::Chunked;
    }

    auto cl = headers.get("Content-Length");
    if (cl) {
        const std::string& s = *cl;
        // 19 digits safely fits in a 64-bit size_t without risking
        // overflow (max 19-digit value is under 2^63), so this rejects
        // absurd inputs without needing a try/catch around stoull.
        bool all_digits = !s.empty() && s.size() <= 19;
        for (char c : s) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                all_digits = false;
                break;
            }
        }
        if (all_digits) {
            content_length = static_cast<size_t>(std::stoull(s));
            return BodyFraming::ContentLength;
        }
    }

    return BodyFraming::None;
}

bool decode_chunked_body(const std::string& input, std::string& out, std::string& error) {
    size_t pos = 0;
    out.clear();

    for (;;) {
        size_t line_end = input.find('\n', pos);
        if (line_end == std::string::npos) {
            error = "incomplete chunked body: missing chunk-size line";
            return false;
        }
        size_t content_end = line_end;
        if (content_end > pos && input[content_end - 1] == '\r') content_end--;
        std::string size_line = input.substr(pos, content_end - pos);
        pos = line_end + 1;

        size_t semicolon = size_line.find(';');
        std::string hex_part = trim_ws(semicolon == std::string::npos ? size_line : size_line.substr(0, semicolon));
        if (hex_part.empty()) {
            error = "empty chunk size";
            return false;
        }
        if (hex_part.size() > 16) {  // 16 hex digits covers the full 64-bit range
            error = "chunk size too large";
            return false;
        }

        size_t chunk_size = 0;
        for (char c : hex_part) {
            int v;
            if (c >= '0' && c <= '9') v = c - '0';
            else if (c >= 'a' && c <= 'f') v = 10 + (c - 'a');
            else if (c >= 'A' && c <= 'F') v = 10 + (c - 'A');
            else {
                error = "invalid chunk size";
                return false;
            }
            chunk_size = chunk_size * 16 + static_cast<size_t>(v);
        }

        if (chunk_size == 0) {
            // Trailer section: consume lines until the terminating blank line.
            for (;;) {
                size_t t_end = input.find('\n', pos);
                if (t_end == std::string::npos) {
                    error = "incomplete chunked body: missing terminating blank line";
                    return false;
                }
                size_t t_content_end = t_end;
                if (t_content_end > pos && input[t_content_end - 1] == '\r') t_content_end--;
                bool blank = (t_content_end == pos);
                pos = t_end + 1;
                if (blank) return true;
            }
        }

        if (pos + chunk_size > input.size()) {
            error = "chunk data runs past the end of input";
            return false;
        }
        out.append(input, pos, chunk_size);
        pos += chunk_size;

        if (pos < input.size() && input[pos] == '\r') pos++;
        if (pos < input.size() && input[pos] == '\n') {
            pos++;
        } else {
            error = "missing line terminator after chunk data";
            return false;
        }
    }
}

}  // namespace archnet
