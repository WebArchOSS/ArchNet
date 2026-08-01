#include "archnet/url.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <unordered_map>

namespace archnet {

namespace {

bool is_scheme_start_char(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }
bool is_scheme_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '+' || c == '-' || c == '.';
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

const std::unordered_map<std::string, int>& default_ports() {
    static const std::unordered_map<std::string, int> ports = {
        {"http", 80}, {"https", 443}, {"ftp", 21}, {"ws", 80}, {"wss", 443},
    };
    return ports;
}

bool is_unreserved(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '.' || c == '_' ||
           c == '~';
}

bool is_allowed_userinfo(char c) {
    static const std::string extra = "!$&'()*+,;=";
    return is_unreserved(c) || extra.find(c) != std::string::npos;
}

bool is_allowed_path_like(char c) {
    // '?' and '#' are deliberately excluded even though callers pass path,
    // query, and fragment as separate strings: keeping them escaped means
    // re-encoding never accidentally re-introduces a component boundary.
    static const std::string extra = "!$&'()*+,;=:@/";
    return is_unreserved(c) || extra.find(c) != std::string::npos;
}

// Deliberately just the universally-unreserved set: see EncodeSet::QueryParam.
bool is_allowed_query_param(char c) { return is_unreserved(c); }

std::string to_lower_copy(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Validates and parses a port string (no leading '+'/'-', no sign,
// digits only), rejecting anything that couldn't fit in a real port
// number before calling stoi, so stoi can never throw here.
bool parse_port(const std::string& port_str, int& out_port, std::string& error) {
    if (port_str.empty() || port_str.size() > 5) {
        error = "invalid port";
        return false;
    }
    for (char c : port_str) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            error = "invalid port";
            return false;
        }
    }
    int value = std::stoi(port_str);
    if (value > 65535) {
        error = "port out of range";
        return false;
    }
    out_port = value;
    return true;
}

}  // namespace

std::optional<int> Url::effective_port() const {
    if (port) return port;
    auto it = default_ports().find(scheme);
    if (it == default_ports().end()) return std::nullopt;
    return it->second;
}

std::string to_string(const Url& url) {
    std::string out = url.scheme + ":";

    bool has_authority = !url.host.empty();
    if (has_authority) {
        out += "//";
        if (!url.username.empty() || !url.password.empty()) {
            out += percent_encode(url.username, EncodeSet::UserInfo);
            if (!url.password.empty()) {
                out += ":" + percent_encode(url.password, EncodeSet::UserInfo);
            }
            out += "@";
        }
        out += url.host;
        if (url.port) out += ":" + std::to_string(*url.port);
    }

    out += percent_encode(url.path, EncodeSet::PathLike);
    if (url.query) out += "?" + *url.query;
    if (url.fragment) out += "#" + *url.fragment;
    return out;
}

std::string percent_decode(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); i++) {
        if (input[i] == '%' && i + 2 < input.size() && hex_value(input[i + 1]) >= 0 &&
            hex_value(input[i + 2]) >= 0) {
            out += static_cast<char>(hex_value(input[i + 1]) * 16 + hex_value(input[i + 2]));
            i += 2;
        } else {
            out += input[i];
        }
    }
    return out;
}

std::string percent_encode(const std::string& input, EncodeSet set) {
    std::string out;
    out.reserve(input.size());
    for (unsigned char c : input) {
        bool allowed;
        switch (set) {
            case EncodeSet::UserInfo: allowed = is_allowed_userinfo(static_cast<char>(c)); break;
            case EncodeSet::QueryParam: allowed = is_allowed_query_param(static_cast<char>(c)); break;
            case EncodeSet::PathLike:
            default: allowed = is_allowed_path_like(static_cast<char>(c)); break;
        }
        if (allowed) {
            out += static_cast<char>(c);
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

bool parse_url(const std::string& input, Url& out, std::string& error) {
    out = Url{};
    size_t n = input.size();

    size_t colon = input.find(':');
    if (colon == std::string::npos || colon == 0) {
        error = "missing scheme";
        return false;
    }
    if (!is_scheme_start_char(input[0])) {
        error = "scheme must start with a letter";
        return false;
    }
    for (size_t i = 1; i < colon; i++) {
        if (!is_scheme_char(input[i])) {
            error = "invalid character in scheme";
            return false;
        }
    }
    out.scheme = to_lower_copy(input.substr(0, colon));

    size_t pos = colon + 1;
    bool has_authority = (pos + 1 < n && input[pos] == '/' && input[pos + 1] == '/');

    if (has_authority) {
        pos += 2;
        size_t authority_start = pos;
        while (pos < n && input[pos] != '/' && input[pos] != '?' && input[pos] != '#') pos++;
        std::string authority = input.substr(authority_start, pos - authority_start);

        std::string host_port = authority;
        size_t at = authority.rfind('@');
        if (at != std::string::npos) {
            std::string userinfo = authority.substr(0, at);
            host_port = authority.substr(at + 1);
            size_t user_colon = userinfo.find(':');
            if (user_colon == std::string::npos) {
                out.username = percent_decode(userinfo);
            } else {
                out.username = percent_decode(userinfo.substr(0, user_colon));
                out.password = percent_decode(userinfo.substr(user_colon + 1));
            }
        }

        if (!host_port.empty() && host_port[0] == '[') {
            size_t close = host_port.find(']');
            if (close == std::string::npos) {
                error = "unterminated IPv6 host literal";
                return false;
            }
            out.host = host_port.substr(0, close + 1);  // brackets kept
            std::string rest = host_port.substr(close + 1);
            if (!rest.empty()) {
                if (rest[0] != ':') {
                    error = "unexpected characters after an IPv6 host literal";
                    return false;
                }
                int port_value = 0;
                if (!parse_port(rest.substr(1), port_value, error)) return false;
                out.port = port_value;
            }
        } else {
            size_t port_colon = host_port.find(':');
            if (port_colon == std::string::npos) {
                out.host = to_lower_copy(host_port);
            } else {
                out.host = to_lower_copy(host_port.substr(0, port_colon));
                int port_value = 0;
                if (!parse_port(host_port.substr(port_colon + 1), port_value, error)) return false;
                out.port = port_value;
            }
        }
        if (out.host.empty()) {
            error = "empty host";
            return false;
        }
    }

    size_t path_start = pos;
    while (pos < n && input[pos] != '?' && input[pos] != '#') pos++;
    out.path = percent_decode(input.substr(path_start, pos - path_start));

    if (pos < n && input[pos] == '?') {
        pos++;
        size_t query_start = pos;
        while (pos < n && input[pos] != '#') pos++;
        out.query = input.substr(query_start, pos - query_start);
    }

    if (pos < n && input[pos] == '#') {
        out.fragment = input.substr(pos + 1);
    }

    return true;
}

}  // namespace archnet
