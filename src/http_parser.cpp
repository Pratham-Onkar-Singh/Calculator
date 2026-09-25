// http_parser.cpp
#include "http_parser.h"

#include <cctype>

using namespace std;

const size_t MAX_HEAD = 8192;
const size_t MAX_BODY = 1000000;

static string to_lower(string s) {
    for (char &ch : s) ch = tolower((unsigned char)ch);
    return s;
}

static string strip(const string &s) {
    size_t b = s.find_first_not_of(" \t");
    size_t e = s.find_last_not_of(" \t");
    if (b == string::npos) return "";
    return s.substr(b, e - b + 1);
}

// Content-Length value: digits only
static int read_length(const string &s, size_t &out) {
    if (s.empty()) return 400;
    for (char ch : s)
        if (ch < '0' || ch > '9') return 400;
    if (s.size() > 9) return 413;
    out = 0;
    for (char ch : s) out = out * 10 + (ch - '0');
    return out > MAX_BODY ? 413 : 0;
}

static int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

// Chunked body starting at buf[pos]: "<hex size>\r\n<data>\r\n" ... "0\r\n\r\n".
// Same return values as parse_request, end = first byte after the body.
static int read_chunks(const string &buf, size_t pos, string &body, size_t &end) {
    body.clear();
    while (true) {
        size_t eol = buf.find("\r\n", pos);
        if (eol == string::npos) return -1;
        string line = buf.substr(pos, eol - pos);
        line = strip(line.substr(0, line.find(';')));   // ignore chunk extensions
        if (line.empty() || line.size() > 8) return 400;
        size_t size = 0;
        for (char ch : line) {
            int v = hex_value(ch);
            if (v < 0) return 400;
            size = size * 16 + v;
        }
        pos = eol + 2;

        if (size == 0) {
            // trailer lines (usually none) and then an empty line
            while (true) {
                eol = buf.find("\r\n", pos);
                if (eol == string::npos) return -1;
                bool empty = (eol == pos);
                pos = eol + 2;
                if (empty) break;
            }
            end = pos;
            return 0;
        }

        if (body.size() + size > MAX_BODY) return 413;
        if (buf.size() < pos + size + 2) return -1;
        body += buf.substr(pos, size);
        pos += size;
        if (buf.compare(pos, 2, "\r\n") != 0) return 400;
        pos += 2;
    }
}

int parse_request(const string &buf, Request &req, size_t &used) {
    size_t start = 0;
    while (buf.compare(start, 2, "\r\n") == 0) start += 2;   // stray empty lines

    size_t head_end = buf.find("\r\n\r\n", start);
    if (head_end == string::npos) return buf.size() - start > MAX_HEAD ? 431 : -1;
    if (head_end - start > MAX_HEAD) return 431;

    // request line
    size_t eol = buf.find("\r\n", start);
    string line = buf.substr(start, eol - start);
    size_t s1 = line.find(' ');
    size_t s2 = line.find(' ', s1 + 1);
    if (s1 == string::npos || s2 == string::npos || line.find(' ', s2 + 1) != string::npos)
        return 400;
    req.method = line.substr(0, s1);
    req.target = line.substr(s1 + 1, s2 - s1 - 1);
    req.version = line.substr(s2 + 1);
    if (req.method.empty() || req.target.empty()) return 400;
    if (req.version != "HTTP/1.1" && req.version != "HTTP/1.0") return 400;

    size_t q = req.target.find('?');
    req.path = req.target.substr(0, q);
    req.query = (q == string::npos) ? "" : req.target.substr(q + 1);

    // headers
    size_t length = 0;
    bool chunked = false;
    size_t pos = eol + 2;
    while (pos < head_end + 2) {
        eol = buf.find("\r\n", pos);
        line = buf.substr(pos, eol - pos);
        pos = eol + 2;

        size_t colon = line.find(':');
        if (colon == string::npos) return 400;
        string name = to_lower(strip(line.substr(0, colon)));
        string value = strip(line.substr(colon + 1));
        if (name == "host") {
            req.has_host = true;
        } else if (name == "connection") {
            req.connection = to_lower(value);
        } else if (name == "content-length") {
            int err = read_length(value, length);
            if (err) return err;
        } else if (name == "transfer-encoding") {
            if (to_lower(value) != "chunked") return 501;
            chunked = true;
        }
    }

    size_t body_start = head_end + 4;
    if (chunked) return read_chunks(buf, body_start, req.body, used);

    // the body is exactly `length` bytes; whatever follows is the next request
    if (buf.size() < body_start + length) return -1;
    req.body = buf.substr(body_start, length);
    used = body_start + length;
    return 0;
}

string get_param(const string &query, const string &name, bool &found) {
    found = false;
    string value;
    size_t pos = 0;
    while (pos <= query.size()) {
        size_t amp = query.find('&', pos);
        if (amp == string::npos) amp = query.size();
        string pair = query.substr(pos, amp - pos);
        if (pair.compare(0, name.size() + 1, name + "=") == 0) {
            value = pair.substr(name.size() + 1);
            found = true;
        }
        pos = amp + 1;
    }
    return value;
}

bool should_close(const Request &req) {
    if (req.connection.find("close") != string::npos) return true;
    if (req.version == "HTTP/1.0") return req.connection.find("keep-alive") == string::npos;
    return false;
}
