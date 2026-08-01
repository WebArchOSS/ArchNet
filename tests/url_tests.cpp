#include "archnet/url.h"

#include "micro_test.h"

using archnet::EncodeSet;
using archnet::parse_url;
using archnet::percent_decode;
using archnet::percent_encode;
using archnet::Url;

namespace {

Url parse_ok(const std::string& input) {
    Url url;
    std::string error;
    CHECK(parse_url(input, url, error));
    return url;
}

void test_full_url_with_all_components() {
    Url url = parse_ok("https://user:pass@example.com:8443/a/b?x=1&y=2#frag");
    CHECK_EQ(url.scheme, "https");
    CHECK_EQ(url.username, "user");
    CHECK_EQ(url.password, "pass");
    CHECK_EQ(url.host, "example.com");
    REQUIRE_EQ(url.port.has_value(), true);
    CHECK_EQ(*url.port, 8443);
    CHECK_EQ(url.path, "/a/b");
    REQUIRE_EQ(url.query.has_value(), true);
    CHECK_EQ(*url.query, "x=1&y=2");
    REQUIRE_EQ(url.fragment.has_value(), true);
    CHECK_EQ(*url.fragment, "frag");
}

void test_default_port_used_when_absent() {
    Url http = parse_ok("http://example.com/");
    CHECK(!http.port.has_value());
    REQUIRE_EQ(http.effective_port().has_value(), true);
    CHECK_EQ(*http.effective_port(), 80);

    Url https = parse_ok("https://example.com/");
    CHECK_EQ(*https.effective_port(), 443);
}

void test_explicit_port_overrides_default() {
    Url url = parse_ok("https://example.com:8080/");
    REQUIRE_EQ(url.port.has_value(), true);
    CHECK_EQ(*url.port, 8080);
    CHECK_EQ(*url.effective_port(), 8080);
}

void test_scheme_with_no_default_port() {
    Url url = parse_ok("mailto:a@b.com");
    CHECK(!url.effective_port().has_value());
}

void test_scheme_is_lowercased() {
    Url url = parse_ok("HTTPS://Example.com/");
    CHECK_EQ(url.scheme, "https");
    CHECK_EQ(url.host, "example.com");
}

void test_no_authority_form() {
    Url url = parse_ok("mailto:a@b.com");
    CHECK_EQ(url.scheme, "mailto");
    CHECK(url.host.empty());
    CHECK_EQ(url.path, "a@b.com");
}

void test_ipv6_host_keeps_brackets_and_parses_port() {
    Url url = parse_ok("https://[::1]:9000/path");
    CHECK_EQ(url.host, "[::1]");
    REQUIRE_EQ(url.port.has_value(), true);
    CHECK_EQ(*url.port, 9000);
}

void test_ipv6_host_without_port() {
    Url url = parse_ok("https://[2001:db8::1]/");
    CHECK_EQ(url.host, "[2001:db8::1]");
    CHECK(!url.port.has_value());
}

void test_path_is_percent_decoded_but_query_is_not() {
    Url url = parse_ok("https://example.com/a%20b?q=%2Fslash");
    CHECK_EQ(url.path, "/a b");
    REQUIRE_EQ(url.query.has_value(), true);
    CHECK_EQ(*url.query, "q=%2Fslash");
}

void test_userinfo_is_percent_decoded() {
    Url url = parse_ok("https://us%20er:pa%40ss@example.com/");
    CHECK_EQ(url.username, "us er");
    CHECK_EQ(url.password, "pa@ss");
}

void test_missing_scheme_is_an_error() {
    Url url;
    std::string error;
    CHECK(!parse_url("not a url", url, error));
    CHECK(!error.empty());
}

void test_empty_host_is_an_error() {
    Url url;
    std::string error;
    CHECK(!parse_url("https://", url, error));
}

void test_invalid_port_is_an_error() {
    Url url;
    std::string error;
    CHECK(!parse_url("https://example.com:notaport/", url, error));

    Url url2;
    std::string error2;
    CHECK(!parse_url("https://example.com:99999999/", url2, error2));  // too many digits
}

void test_port_out_of_range_is_an_error() {
    Url url;
    std::string error;
    CHECK(!parse_url("https://example.com:70000/", url, error));  // fits in 5 digits, > 65535
}

void test_unterminated_ipv6_literal_is_an_error() {
    Url url;
    std::string error;
    CHECK(!parse_url("https://[::1/path", url, error));
}

void test_percent_decode_passes_through_malformed_sequences() {
    CHECK_EQ(percent_decode("100%"), "100%");
    CHECK_EQ(percent_decode("100%2"), "100%2");
    CHECK_EQ(percent_decode("100%zz"), "100%zz");
    CHECK_EQ(percent_decode("100%25"), "100%");
}

void test_percent_encode_userinfo_vs_path_like_sets() {
    // '/' is allowed unescaped in PathLike but not in UserInfo.
    CHECK_EQ(percent_encode("a/b", EncodeSet::PathLike), "a/b");
    CHECK_EQ(percent_encode("a/b", EncodeSet::UserInfo), "a%2Fb");
    CHECK_EQ(percent_encode("a b", EncodeSet::PathLike), "a%20b");
}

void test_percent_encode_query_param_escapes_structural_characters() {
    // Unlike PathLike, QueryParam must escape '&', '=', and '+': these are
    // structurally significant inside a query string, so an unescaped one
    // in a value would corrupt the query (e.g. searching for "AT&T").
    CHECK_EQ(percent_encode("AT&T", EncodeSet::QueryParam), "AT%26T");
    CHECK_EQ(percent_encode("a=b", EncodeSet::QueryParam), "a%3Db");
    CHECK_EQ(percent_encode("a+b", EncodeSet::QueryParam), "a%2Bb");
    CHECK_EQ(percent_encode("a b", EncodeSet::QueryParam), "a%20b");
    CHECK_EQ(percent_encode("safe-chars_here.txt~1", EncodeSet::QueryParam), "safe-chars_here.txt~1");
}

void test_username_without_password() {
    Url url = parse_ok("ftp://anonymous@ftp.example.com/pub/file.txt");
    CHECK_EQ(url.username, "anonymous");
    CHECK(url.password.empty());
    CHECK_EQ(url.host, "ftp.example.com");
}

void test_to_string_round_trips_well_formed_urls() {
    const char* examples[] = {
        "https://user:pass@example.com:8443/a/b?x=1&y=2#frag",
        "http://example.com/",
        "https://example.com",
        "mailto:a@b.com",
        "ftp://anonymous@ftp.example.com/pub/file.txt",
        "https://[::1]:9000/path",
    };
    for (const char* example : examples) {
        Url url = parse_ok(example);
        CHECK_EQ(archnet::to_string(url), std::string(example));
    }
}

}  // namespace

int main() {
    RUN_TEST(test_full_url_with_all_components);
    RUN_TEST(test_default_port_used_when_absent);
    RUN_TEST(test_explicit_port_overrides_default);
    RUN_TEST(test_scheme_with_no_default_port);
    RUN_TEST(test_scheme_is_lowercased);
    RUN_TEST(test_no_authority_form);
    RUN_TEST(test_ipv6_host_keeps_brackets_and_parses_port);
    RUN_TEST(test_ipv6_host_without_port);
    RUN_TEST(test_path_is_percent_decoded_but_query_is_not);
    RUN_TEST(test_userinfo_is_percent_decoded);
    RUN_TEST(test_missing_scheme_is_an_error);
    RUN_TEST(test_empty_host_is_an_error);
    RUN_TEST(test_invalid_port_is_an_error);
    RUN_TEST(test_port_out_of_range_is_an_error);
    RUN_TEST(test_unterminated_ipv6_literal_is_an_error);
    RUN_TEST(test_percent_decode_passes_through_malformed_sequences);
    RUN_TEST(test_percent_encode_userinfo_vs_path_like_sets);
    RUN_TEST(test_percent_encode_query_param_escapes_structural_characters);
    RUN_TEST(test_username_without_password);
    RUN_TEST(test_to_string_round_trips_well_formed_urls);

    int failures = archtest::failure_count();
    if (failures == 0) {
        std::fprintf(stderr, "All tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d check(s) failed.\n", failures);
    return 1;
}
