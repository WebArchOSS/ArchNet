#include "archnet/http_message.h"

#include "micro_test.h"

using archnet::BodyFraming;
using archnet::decode_chunked_body;
using archnet::determine_body_framing;
using archnet::HttpHeaders;
using archnet::parse_headers;
using archnet::parse_request_line;
using archnet::parse_status_line;
using archnet::RequestLine;
using archnet::StatusLine;

namespace {

void test_request_line_basic() {
    RequestLine rl;
    std::string error;
    CHECK(parse_request_line("GET /index.html HTTP/1.1", rl, error));
    CHECK_EQ(rl.method, "GET");
    CHECK_EQ(rl.target, "/index.html");
    CHECK_EQ(rl.http_major, 1);
    CHECK_EQ(rl.http_minor, 1);
}

void test_request_line_post_with_query_target() {
    RequestLine rl;
    std::string error;
    CHECK(parse_request_line("POST /search?q=cats HTTP/1.0", rl, error));
    CHECK_EQ(rl.method, "POST");
    CHECK_EQ(rl.target, "/search?q=cats");
    CHECK_EQ(rl.http_minor, 0);
}

void test_request_line_malformed() {
    RequestLine rl;
    std::string error;
    CHECK(!parse_request_line("GET", rl, error));
    CHECK(!parse_request_line("GET /path", rl, error));
    CHECK(!parse_request_line("GET /path NOTHTTP", rl, error));
    CHECK(!parse_request_line("GET /path HTTP/abc", rl, error));
}

void test_request_line_bare_major_version_is_tolerated() {
    // Not real wire traffic (requests are always HTTP/1.0 or HTTP/1.1 in
    // practice), but harmless to accept since parse_version_numbers is
    // shared with parse_status_line, where this form is genuinely useful.
    RequestLine rl;
    std::string error;
    CHECK(parse_request_line("GET /path HTTP/1", rl, error));
    CHECK_EQ(rl.http_major, 1);
    CHECK_EQ(rl.http_minor, 0);
}

void test_status_line_basic() {
    StatusLine sl;
    std::string error;
    CHECK(parse_status_line("HTTP/1.1 404 Not Found", sl, error));
    CHECK_EQ(sl.http_major, 1);
    CHECK_EQ(sl.http_minor, 1);
    CHECK_EQ(sl.status_code, 404);
    CHECK_EQ(sl.reason_phrase, "Not Found");
}

void test_status_line_empty_reason_phrase() {
    StatusLine sl;
    std::string error;
    CHECK(parse_status_line("HTTP/2 200 ", sl, error));
    CHECK_EQ(sl.status_code, 200);
    CHECK_EQ(sl.reason_phrase, "");
}

void test_status_line_malformed() {
    StatusLine sl;
    std::string error;
    CHECK(!parse_status_line("HTTP/1.1 4041 Not Found", sl, error));  // 4 digits
    CHECK(!parse_status_line("HTTP/1.1 abc Not Found", sl, error));
    CHECK(!parse_status_line("NOTHTTP 200 OK", sl, error));
}

void test_headers_basic() {
    std::string input = "Host: example.com\r\nContent-Type: text/html\r\n\r\nBODY";
    HttpHeaders headers;
    std::string error;
    size_t pos = 0;
    CHECK(parse_headers(input, pos, headers, error));
    CHECK_EQ(headers.get("host").value_or("?"), "example.com");
    CHECK_EQ(headers.get("Content-Type").value_or("?"), "text/html");
    CHECK_EQ(input.substr(pos), "BODY");
}

void test_headers_lookup_is_case_insensitive() {
    std::string input = "X-Custom-Header: value\r\n\r\n";
    HttpHeaders headers;
    std::string error;
    size_t pos = 0;
    CHECK(parse_headers(input, pos, headers, error));
    CHECK_EQ(headers.get("x-custom-header").value_or("?"), "value");
    CHECK_EQ(headers.get("X-CUSTOM-HEADER").value_or("?"), "value");
}

void test_headers_duplicate_names_preserved_in_order() {
    std::string input = "Set-Cookie: a=1\r\nSet-Cookie: b=2\r\n\r\n";
    HttpHeaders headers;
    std::string error;
    size_t pos = 0;
    CHECK(parse_headers(input, pos, headers, error));
    auto all = headers.get_all("set-cookie");
    REQUIRE_EQ(all.size(), size_t(2));
    CHECK_EQ(all[0], "a=1");
    CHECK_EQ(all[1], "b=2");
}

void test_headers_obs_fold_continuation() {
    std::string input = "X-Long: part1\r\n part2\r\n\tpart3\r\n\r\n";
    HttpHeaders headers;
    std::string error;
    size_t pos = 0;
    CHECK(parse_headers(input, pos, headers, error));
    CHECK_EQ(headers.get("x-long").value_or("?"), "part1 part2 part3");
}

void test_headers_tolerates_bare_lf() {
    std::string input = "Host: example.com\nContent-Type: text/html\n\nBODY";
    HttpHeaders headers;
    std::string error;
    size_t pos = 0;
    CHECK(parse_headers(input, pos, headers, error));
    CHECK_EQ(headers.get("host").value_or("?"), "example.com");
    CHECK_EQ(input.substr(pos), "BODY");
}

void test_headers_missing_colon_is_an_error() {
    std::string input = "not-a-header-line\r\n\r\n";
    HttpHeaders headers;
    std::string error;
    size_t pos = 0;
    CHECK(!parse_headers(input, pos, headers, error));
}

void test_headers_incomplete_block_is_an_error() {
    std::string input = "Host: example.com\r\nContent-Type: text/html\r\n";  // no blank line
    HttpHeaders headers;
    std::string error;
    size_t pos = 0;
    CHECK(!parse_headers(input, pos, headers, error));
}

void test_body_framing_content_length() {
    HttpHeaders headers;
    headers.add("Content-Length", "42");
    size_t content_length = 0;
    CHECK(determine_body_framing(headers, content_length) == BodyFraming::ContentLength);
    CHECK_EQ(content_length, size_t(42));
}

void test_body_framing_chunked() {
    HttpHeaders headers;
    headers.add("Transfer-Encoding", "chunked");
    size_t content_length = 0;
    CHECK(determine_body_framing(headers, content_length) == BodyFraming::Chunked);
}

void test_body_framing_chunked_takes_priority_over_content_length() {
    HttpHeaders headers;
    headers.add("Content-Length", "10");
    headers.add("Transfer-Encoding", "gzip, chunked");
    size_t content_length = 0;
    CHECK(determine_body_framing(headers, content_length) == BodyFraming::Chunked);
}

void test_body_framing_none_when_neither_header_present() {
    HttpHeaders headers;
    size_t content_length = 0;
    CHECK(determine_body_framing(headers, content_length) == BodyFraming::None);
}

void test_body_framing_transfer_encoding_not_ending_in_chunked_falls_back() {
    // Not a realistic header value, but exercises "last encoding wins".
    HttpHeaders headers;
    headers.add("Transfer-Encoding", "chunked, gzip");
    headers.add("Content-Length", "10");
    size_t content_length = 0;
    CHECK(determine_body_framing(headers, content_length) == BodyFraming::ContentLength);
}

void test_chunked_decode_wikipedia_example() {
    std::string chunked = "4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n";
    std::string out, error;
    CHECK(decode_chunked_body(chunked, out, error));
    CHECK_EQ(out, "Wikipedia");
}

void test_chunked_decode_with_extension_and_trailer() {
    std::string chunked = "4;ext=1\r\nWiki\r\n0\r\nX-Trailer: value\r\n\r\n";
    std::string out, error;
    CHECK(decode_chunked_body(chunked, out, error));
    CHECK_EQ(out, "Wiki");
}

void test_chunked_decode_empty_body() {
    std::string chunked = "0\r\n\r\n";
    std::string out, error;
    CHECK(decode_chunked_body(chunked, out, error));
    CHECK_EQ(out, "");
}

void test_chunked_decode_missing_terminator_is_an_error() {
    std::string chunked = "4\r\nWiki";  // no terminating CRLF, no zero chunk
    std::string out, error;
    CHECK(!decode_chunked_body(chunked, out, error));
}

void test_chunked_decode_size_larger_than_data_is_an_error() {
    std::string chunked = "10\r\nshort\r\n0\r\n\r\n";  // claims 16 bytes, has fewer
    std::string out, error;
    CHECK(!decode_chunked_body(chunked, out, error));
}

}  // namespace

int main() {
    RUN_TEST(test_request_line_basic);
    RUN_TEST(test_request_line_post_with_query_target);
    RUN_TEST(test_request_line_malformed);
    RUN_TEST(test_request_line_bare_major_version_is_tolerated);
    RUN_TEST(test_status_line_basic);
    RUN_TEST(test_status_line_empty_reason_phrase);
    RUN_TEST(test_status_line_malformed);
    RUN_TEST(test_headers_basic);
    RUN_TEST(test_headers_lookup_is_case_insensitive);
    RUN_TEST(test_headers_duplicate_names_preserved_in_order);
    RUN_TEST(test_headers_obs_fold_continuation);
    RUN_TEST(test_headers_tolerates_bare_lf);
    RUN_TEST(test_headers_missing_colon_is_an_error);
    RUN_TEST(test_headers_incomplete_block_is_an_error);
    RUN_TEST(test_body_framing_content_length);
    RUN_TEST(test_body_framing_chunked);
    RUN_TEST(test_body_framing_chunked_takes_priority_over_content_length);
    RUN_TEST(test_body_framing_none_when_neither_header_present);
    RUN_TEST(test_body_framing_transfer_encoding_not_ending_in_chunked_falls_back);
    RUN_TEST(test_chunked_decode_wikipedia_example);
    RUN_TEST(test_chunked_decode_with_extension_and_trailer);
    RUN_TEST(test_chunked_decode_empty_body);
    RUN_TEST(test_chunked_decode_missing_terminator_is_an_error);
    RUN_TEST(test_chunked_decode_size_larger_than_data_is_an_error);

    int failures = archtest::failure_count();
    if (failures == 0) {
        std::fprintf(stderr, "All tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d check(s) failed.\n", failures);
    return 1;
}
