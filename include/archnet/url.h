#pragma once

#include <optional>
#include <string>

namespace archnet {

// A parsed URL. Field meanings follow the WHATWG URL Standard's naming,
// but this is a deliberately reduced subset of it; see parse_url's comment
// for exactly what's covered and what isn't.
struct Url {
    std::string scheme;      // lowercased, without the trailing ':'
    std::string username;    // percent-decoded; empty if absent
    std::string password;    // percent-decoded; empty if absent
    std::string host;        // lowercased; IPv6 literals keep their brackets
    std::optional<int> port;  // absent means "use the scheme's default port"
    std::string path;        // percent-decoded; always starts with '/' if the
                              // URL has an authority and any path at all
    std::optional<std::string> query;     // without the leading '?', not decoded
    std::optional<std::string> fragment;  // without the leading '#', not decoded

    // The effective port: `port` if set, else the scheme's well-known
    // default, else nullopt if the scheme has no well-known default.
    std::optional<int> effective_port() const;
};

// Parses an absolute URL of the form
// scheme://[user[:pass]@]host[:port][/path][?query][#fragment]
// or scheme:path (no authority, e.g. "mailto:a@b.com").
//
// Returns true and populates `out` on success; returns false and
// populates `error` with a human-readable reason on failure.
//
// Deliberately NOT a full WHATWG URL Standard implementation:
//   - No IDNA/punycode processing for non-ASCII hostnames.
//   - No relative URL resolution against a base URL.
//   - No scheme-specific special-casing (the standard treats "file",
//     "blob", and a few others specially; this parser treats every
//     scheme the same way).
//   - IPv6 literals are recognized only well enough to avoid splitting
//     their colons as a port separator; they are not validated as
//     well-formed addresses.
bool parse_url(const std::string& input, Url& out, std::string& error);

// Serializes a Url back into a URL string. For a Url produced by
// parse_url, this round-trips to the original input exactly, as long as
// that input didn't need any re-encoding to be well-formed (a path or
// userinfo containing characters outside the relevant percent-encode set
// gets re-encoded here, which is not always byte-for-byte identical to
// however the original happened to encode the same characters).
std::string to_string(const Url& url);

// Percent-decodes a string (e.g. "%20" -> " "). Passes through any '%' not
// followed by two hex digits unchanged, rather than treating it as an
// error, matching how most real URL parsers handle malformed input found
// in the wild.
std::string percent_decode(const std::string& input);

// Which characters percent_encode leaves unescaped, beyond the universally
// unreserved set (A-Z a-z 0-9 - . _ ~). Chosen per component per the
// WHATWG URL Standard's percent-encode sets, simplified: this parser uses
// one "path-like" set and one "userinfo" set rather than the standard's
// full collection of slightly different sets per component.
enum class EncodeSet {
    UserInfo,    // for the username/password component
    PathLike,    // for path, query, and fragment as whole strings
    QueryParam,  // for one query parameter's value when building a query
                 // string from individual key/value pairs: escapes '&',
                 // '=', and '+' too, since PathLike deliberately leaves
                 // those unescaped and they are structurally significant
                 // within a query string (an unescaped '&' in a value
                 // would be misread as a new parameter).
};

std::string percent_encode(const std::string& input, EncodeSet set);

}  // namespace archnet
