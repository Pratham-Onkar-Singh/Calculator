// http_parser.h - finds one HTTP request at the front of a buffer
#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <string>

struct Request {
    std::string method, target, version;
    std::string path, query;     // target split at '?'
    std::string connection;      // Connection header, lowercase
    bool has_host = false;
    std::string body;
};

// Looks for one whole request at the start of buf.
// Returns -1 if more data is needed, 0 if req was filled in (used = how many bytes of
// buf it took), or an HTTP status code if the request is broken.
int parse_request(const std::string &buf, Request &req, size_t &used);

// Value of name=... in a query string like "a=2&b=3".
std::string get_param(const std::string &query, const std::string &name, bool &found);

// Connection: close, or HTTP/1.0 without keep-alive
bool should_close(const Request &req);

#endif
