// calculator.cpp
#include "calculator.h"

#include <climits>

using namespace std;

bool read_int(const string &s, long long &out) {
    size_t i = 0;
    bool neg = false;
    if (!s.empty() && s[0] == '-') {
        neg = true;
        i = 1;
    }
    if (i == s.size()) return false;
    long long v = 0;
    for (; i < s.size(); i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        int d = s[i] - '0';
        if (v > (LLONG_MAX - d) / 10) return false;
        v = v * 10 + d;
    }
    out = neg ? -v : v;
    return true;
}

int handle(const Request &req, string &body) {
    if (req.version == "HTTP/1.1" && !req.has_host) {
        body = "missing Host header";
        return 400;
    }

    const string &path = req.path;
    if (path != "/add" && path != "/sub" && path != "/mul" && path != "/div") {
        body = "no such operation: " + path;
        return 404;
    }
    if (req.method != "GET") {
        body = req.method + " is not allowed, use GET";
        return 405;
    }

    bool has_a, has_b;
    string sa = get_param(req.query, "a", has_a);
    string sb = get_param(req.query, "b", has_b);
    long long a, b, result = 0;
    if (!has_a || !has_b || !read_int(sa, a) || !read_int(sb, b)) {
        body = "a and b must both be given as integers";
        return 400;
    }

    bool overflow = false;
    if (path == "/add") overflow = __builtin_add_overflow(a, b, &result);
    else if (path == "/sub") overflow = __builtin_sub_overflow(a, b, &result);
    else if (path == "/mul") overflow = __builtin_mul_overflow(a, b, &result);
    else if (b == 0) {
        body = "cannot divide by zero";
        return 400;
    } else {
        result = a / b;   // a is never LLONG_MIN here because read_int can't produce it
    }
    if (overflow) {
        body = "result is too large";
        return 400;
    }

    body = to_string(result);
    return 200;
}
