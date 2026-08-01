#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace archnet {

// An ordered list of header name/value pairs, preserving duplicates (e.g.
// multiple Set-Cookie headers) and original casing, with case-insensitive
// lookup (per RFC 9110, header field names are case-insensitive).
class HttpHeaders {
public:
    void add(const std::string& name, const std::string& value);

    // The first value for `name` (case-insensitive), if any.
    std::optional<std::string> get(const std::string& name) const;

    // All values for `name`, in the order they appeared.
    std::vector<std::string> get_all(const std::string& name) const;

    bool has(const std::string& name) const;

    const std::vector<std::pair<std::string, std::string>>& entries() const { return entries_; }

private:
    std::vector<std::pair<std::string, std::string>> entries_;
};

struct RequestLine {
    std::string method;
    std::string target;
    int http_major = 1;
    int http_minor = 1;
};

struct StatusLine {
    int http_major = 1;
    int http_minor = 1;
    int status_code = 0;
    std::string reason_phrase;
};

// Parses a single request line, e.g. "GET /path HTTP/1.1", without the
// trailing CRLF. Does not validate that `method` is a registered method or
// that `target` is a well-formed request-target beyond basic shape; a
// request-target that looks like an absolute URL can be handed to
// parse_url separately.
bool parse_request_line(const std::string& line, RequestLine& out, std::string& error);

// Parses a single status line, e.g. "HTTP/1.1 404 Not Found", without the
// trailing CRLF. The reason phrase may be empty (HTTP/2 and later drop it).
bool parse_status_line(const std::string& line, StatusLine& out, std::string& error);

// Parses header fields starting at `input[pos]`, stopping at (and
// consuming) the blank line that terminates the header block. Tolerates
// both CRLF and bare LF line endings, and obs-fold continuation lines
// (a line starting with space or tab, appended to the previous header's
// value with a single space), since both appear in the wild even though
// obs-fold is deprecated. Advances `pos` past the blank line on success.
bool parse_headers(const std::string& input, size_t& pos, HttpHeaders& out, std::string& error);

// How a message body is delimited. UntilConnectionClose is part of this
// vocabulary so callers can express their own final decision using the
// same type, but determine_body_framing() below never returns it directly
// (see its comment for why).
enum class BodyFraming {
    None,                  // no body: no Content-Length, no chunked Transfer-Encoding
    ContentLength,         // fixed-size body; see content_length
    Chunked,               // Transfer-Encoding: chunked
    UntilConnectionClose,  // body continues until the connection closes
};

// Inspects Content-Length and Transfer-Encoding to decide how a message
// body is framed. Always returns None, ContentLength, or Chunked, never
// UntilConnectionClose: that determination additionally depends on
// whether this is a request or a response (a request with neither header
// has no body; a response with neither header, on some status codes, is
// read until connection close), which this function has no way to know
// on its own. A caller parsing a response should treat a None result as
// UntilConnectionClose once it has also checked that the status code and
// request method allow a body at all (HEAD responses and 1xx/204/304
// never do, regardless of headers).
//
// If Transfer-Encoding names "chunked" as its last encoding, that takes
// priority over any Content-Length present (per RFC 9112 section 6.3, a
// message must not have both; this function just prefers chunked rather
// than treating the combination as an error).
BodyFraming determine_body_framing(const HttpHeaders& headers, size_t& content_length);

// Decodes a chunked-transfer-encoded body (RFC 9112 section 7.1):
// "<hex-size>\r\n<data>\r\n" repeated, ending in a zero-size chunk.
// Chunk extensions (";name=value" after the size) are skipped, not
// exposed. Any trailer fields after the final chunk are skipped rather
// than parsed into a HttpHeaders; a caller that needs them should parse
// `input` itself starting right after the zero-size chunk's line. Content
// appearing after the trailer section's terminating blank line, if any,
// is not examined either way.
bool decode_chunked_body(const std::string& input, std::string& out, std::string& error);

}  // namespace archnet
