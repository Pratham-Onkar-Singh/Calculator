// calculator.h - the four operations and the checks around them
#ifndef CALCULATOR_H
#define CALCULATOR_H

#include <string>

#include "http_parser.h"

// Digits with an optional minus in front. Fails on anything else or on overflow.
bool read_int(const std::string &s, long long &out);

// Works out the status code and body for a request that parsed fine.
int handle(const Request &req, std::string &body);

#endif
