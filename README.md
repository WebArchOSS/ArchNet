# ArchNet

Project Arch's networking layer: URL parsing/serialization and HTTP/1.1 message parsing. Part of [Project Arch](https://github.com/WebArchOSS/Arch), imported there as a git submodule at `engine/network`.

## What's here

- `include/archnet/url.h`, `src/url.cpp`: a URL parser and serializer. Scheme, userinfo, host (including bracketed IPv6 literals), port (with well-known defaults per scheme), path, query, and fragment, plus percent-encode/decode helpers with three encode sets (`UserInfo`, `PathLike`, `QueryParam`).
- `include/archnet/http_message.h`, `src/http_message.cpp`: HTTP/1.1 request/status line parsing, headers (case-insensitive lookup, duplicate preservation, obs-fold tolerance), body-framing detection (Content-Length vs. chunked vs. neither), and a chunked-transfer-encoding decoder.
- No actual transport (sockets, TLS): this is pure parsing and framing logic. See "Known simplifications" below.

## Building and testing standalone

```
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
```

## Known simplifications

- `parse_url` is not a full WHATWG URL Standard implementation: no IDNA/punycode for non-ASCII hosts, no relative-URL resolution, no scheme-specific special-casing.
- `parse_headers` tolerates obs-fold continuation lines for compatibility, which is a known HTTP request-smuggling vector (RFC 9112 section 5.2) if this ever sits in front of another HTTP implementation that interprets folding differently.
- `determine_body_framing` only looks at headers: it can't apply the "HEAD/1xx/204/304 responses never have a body" rule, since that needs to know the request method and status code too.

See `tests/` for the behavior this is actually verified against.

## License

GPL-3.0. See `LICENSE`.
