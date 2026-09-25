# HTTP Calculator Server

**Name:** Pratham Onkar Singh

**Roll No.:** 24BCS10136

A simple HTTP/1.1 calculator written in C++ using raw TCP sockets. Connections remain open
between requests, allowing a client to send multiple requests through one socket.

## Supported requests

| request | status | body |
| --- | --- | --- |
| `GET /add?a=2&b=3` | 200 | `5` |
| `GET /sub?a=10&b=4` | 200 | `6` |
| `GET /mul?a=6&b=7` | 200 | `42` |
| `GET /div?a=9&b=3` | 200 | `3` |
| `GET /div?a=1&b=0` | 400 | cannot divide by zero |
| `GET /add?a=x&b=3` | 400 | a and b must both be given as integers |
| `GET /pow?a=2&b=8` | 404 | no such operation: /pow |
| `POST /add` | 405 | POST is not allowed, use GET |
| `GET /add` with no `Host` header | 400 | missing Host header |

## How it works

`main()` in `server.cpp` accepts connections and creates one `std::thread` for each client
using `serve()`. Every client thread has its own `buffer` string:

1. `recv()` adds newly received bytes to the buffer.
2. `parse_request()` in `http_parser.cpp` checks the beginning of the buffer. It returns `-1`
   if the request is incomplete (there is no empty line after the headers, or the body has
   fewer bytes than `Content-Length`). It returns `0` and sets `used` to the exact request
   size when complete, or returns an error status for an invalid request.
3. Only the `used` bytes are removed. If another complete request remains in the buffer
   (pipelining), it is handled immediately. Responses are therefore sent in request order.

`handle()` in `calculator.cpp` checks the following in order: the `Host` header is present for
HTTP/1.1, the path is known (`404`), the method is `GET` (`405`, with `Allow: GET`), and `a`
and `b` are integers (`400`). `read_int()` reads numbers one digit at a time, so `x`, `2.5`,
an empty value, or a value too large for `long long` all return `400`. Addition, subtraction,
and multiplication use the compiler's overflow built-ins. Division by zero returns `400`,
and division uses integer division.

Every response includes `Content-Length`. Because HTTP/1.1 connections are persistent by
default, `Connection: close` is only included when the server is going to close the connection.

Extra features:

- **Connection: close:** A client request with this header, or an HTTP/1.0 request without
  keep-alive, closes the connection after the response.
- **Chunked bodies:** Bodies using `Transfer-Encoding: chunked` are decoded, and any request
  after the final chunk is read normally.
- **Pipelining:** Multiple requests are handled as described above.
- **Idle timeout:** The default timeout is 20 seconds. Before every `recv()`, the thread waits
  in `select()` for the configured time. If no data arrives, the socket is closed and the
  thread ends. Each open connection uses a thread, so unused connections should not remain
  open. Twenty seconds is still much longer than a normal pause between client requests.

An unparseable request, such as a bad request line, a header without `:`, or an invalid
`Content-Length`, receives `400` and the connection is closed. After such an error, the server
cannot reliably find where the next request begins. Headers larger than 8 KB receive `431`,
bodies larger than 1 MB receive `413`, and unsupported transfer encodings receive `501`.

## Project structure

```
Folder_4/
├── src/
│   ├── server.cpp        # socket setup, accept loop, one thread per client
│   ├── http_parser.h
│   ├── http_parser.cpp   # parse_request(), chunked decoding, query parameters
│   ├── calculator.h
│   └── calculator.cpp    # handle() and read_int()
├── tests/
│   └── test_client.cpp   # client that prints the server's answers
├── Makefile
├── README.md
└── .gitignore
```

## Build

```
make
```

Or compile manually:

```
g++ -std=c++11 -Wall -O2 -pthread -o server src/server.cpp src/http_parser.cpp src/calculator.cpp
g++ -std=c++11 -Wall -O2 -o test_client tests/test_client.cpp
```

## Run the server

```
./server              # port 8080, idle timeout 20 s
./server 8080 5       # idle timeout 5 s
./server 9090         # port 9090
```

The terminal displays each connection and request, for example:
`[fd 4] GET /add?a=2&b=3 -> 200`. Press `Ctrl+C` to stop the server.

## Send a request

In another terminal, run:

```
curl -i "http://localhost:8080/sub?a=10&b=4"
```

To enter requests manually, use netcat with CRLF line endings:

```
nc -c localhost 8080      # macOS
nc -C localhost 8080      # Linux
```

```
GET /sub?a=10&b=4 HTTP/1.1
Host: localhost

```

Press Enter on an empty line to complete the request. The connection remains open for another
request.

## Run all tests

```
./test_client            # port 8080
./test_client 9090       # other port
```

The client prints the status and body of each response. It tests all requests in the table on
one connection, a body sent after its headers, a chunked body, six pipelined requests,
`Connection: close`, and an invalid request line.

To test the idle timeout, first run the server with a short timeout such as `./server 8080 5`:

```
./test_client 8080 idle
```

To send the same nine requests over one keep-alive connection with curl:

```
curl -s -w ' (HTTP %{http_code})\n' \
  "http://localhost:8080/add?a=2&b=3" "http://localhost:8080/sub?a=10&b=4" \
  "http://localhost:8080/mul?a=6&b=7" "http://localhost:8080/div?a=9&b=3" \
  "http://localhost:8080/div?a=1&b=0" "http://localhost:8080/add?a=x&b=3" \
  "http://localhost:8080/pow?a=2&b=8" \
  --next -s -w ' (HTTP %{http_code})\n' -X POST "http://localhost:8080/add" \
  --next -s -w ' (HTTP %{http_code})\n' -H "Host:" "http://localhost:8080/add?a=2&b=3"
```

## Test requests separately

```
curl -i "http://localhost:8080/add?a=2&b=3"
curl -i "http://localhost:8080/sub?a=10&b=4"
curl -i "http://localhost:8080/mul?a=6&b=7"
curl -i "http://localhost:8080/div?a=9&b=3"
curl -i "http://localhost:8080/div?a=1&b=0"
curl -i "http://localhost:8080/add?a=x&b=3"
curl -i "http://localhost:8080/pow?a=2&b=8"
curl -i -X POST "http://localhost:8080/add"
curl -i -H "Host:" "http://localhost:8080/add?a=2&b=3"
```

`-H "Host:"` removes the `Host` header that curl normally adds.

Keep-alive test (curl prints `Re-using existing connection` for the second URL):

```
curl -v "http://localhost:8080/add?a=2&b=3" "http://localhost:8080/mul?a=6&b=7"
```

Pipelining test with six requests in one write:

```
printf 'GET /add?a=2&b=3 HTTP/1.1\r\nHost: localhost\r\n\r\nGET /sub?a=10&b=4 HTTP/1.1\r\nHost: localhost\r\n\r\nGET /mul?a=6&b=7 HTTP/1.1\r\nHost: localhost\r\n\r\nGET /div?a=1&b=0 HTTP/1.1\r\nHost: localhost\r\n\r\nGET /pow?a=2&b=8 HTTP/1.1\r\nHost: localhost\r\n\r\nPOST /add HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' | nc localhost 8080
```

An 8-byte body followed by another request:

```
printf 'POST /div HTTP/1.1\r\nHost: localhost\r\nContent-Length: 8\r\n\r\nabcdefghGET /div?a=100&b=7 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' | nc localhost 8080
```

A chunked body followed by another request:

```
printf 'POST /mul HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\nA\r\n0123456789\r\n0\r\n\r\nGET /mul?a=-4&b=5 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' | nc localhost 8080
```

Requests where the server closes the connection after responding:

```
curl -i -H "Connection: close" "http://localhost:8080/add?a=1&b=1"
curl -i --http1.0 "http://localhost:8080/add?a=1&b=1"
```
