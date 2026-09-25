# Http-Calculator-Server

**Name:** Pratham Onkar Singh

**Roll No.:** 24BCS10136

A calculator for HTTP/1.1, written in C++ on top of raw TCP sockets. The connection stays open
between requests, so one client can send everything over a single socket.

## What it answers

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

## How it is built

`main()` in `server.cpp` accepts connections and starts one `std::thread` per client
(`serve()`). Each client thread has its own `buffer` string:

1. `recv()` appends new bytes to the buffer.
2. `parse_request()` (in `http_parser.cpp`) looks at the front of the buffer. It returns `-1`
   when the request isn't complete yet (no empty line after the headers, or fewer body bytes
   than `Content-Length`), `0` with `used` = the exact size of the request when it is
   complete, or an error status when it is broken.
3. Only `used` bytes are erased. If the buffer still holds another complete request
   (pipelining) it is parsed and answered right away, so the answers go out in the same order
   as the requests.

`handle()` (in `calculator.cpp`) checks, in this order: `Host` present (HTTP/1.1), known path
(`404`), method is `GET` (`405`, with `Allow: GET`), `a` and `b` are integers (`400`). Numbers
are read digit by digit in `read_int()`, so `x`, `2.5`, an empty value or a number too big for
`long long` are all `400`. Add, sub and mul use the compiler's overflow builtins, division by
zero is `400`, and division is integer division.

Every response has `Content-Length`. HTTP/1.1 connections are persistent by default, so
`Connection: close` is only sent when the server is about to close.

Stretch parts:

- **Connection: close** from the client, or HTTP/1.0 without keep-alive, closes the connection
  after the reply.
- **Chunked bodies** (`Transfer-Encoding: chunked`) are decoded; the request that follows the
  last chunk is read normally.
- **Pipelining** works as described above.
- **Idle timeout**, 20 s by default. Before each `recv()` the thread waits in `select()` with
  the timeout; if nothing arrives the socket is closed and the thread ends. Each open
  connection costs a thread, so connections nobody uses shouldn't stay around, and 20 s is
  still far more than the pause between requests of a real client.

A request that can't be parsed (bad request line, header without `:`, bad `Content-Length`)
gets `400` and the connection is closed, because after it the server can't know where the next
request starts. Headers over 8 KB get `431`, bodies over 1 MB get `413`, other transfer
encodings get `501`.

## Layout

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

## Compile

```
make
```

or by hand:

```
g++ -std=c++11 -Wall -O2 -pthread -o server src/server.cpp src/http_parser.cpp src/calculator.cpp
g++ -std=c++11 -Wall -O2 -o test_client tests/test_client.cpp
```

## Start the server

```
./server              # port 8080, idle timeout 20 s
./server 8080 5       # idle timeout 5 s
./server 9090         # port 9090
```

The terminal shows every connection and request, for example
`[fd 4] GET /add?a=2&b=3 -> 200`. `Ctrl+C` stops the server.

## Connect

In a second terminal:

```
curl -i "http://localhost:8080/sub?a=10&b=4"
```

To type requests by hand, use netcat with CRLF line endings:

```
nc -c localhost 8080      # macOS
nc -C localhost 8080      # Linux
```

```
GET /sub?a=10&b=4 HTTP/1.1
Host: localhost

```

Press Enter on an empty line to finish a request. The connection stays open for the next one.

## Test everything at once

```
./test_client            # port 8080
./test_client 9090       # other port
```

The client prints the status and body of every answer for: all requests from the table on one
connection, a body sent later than its headers, a chunked body, six pipelined requests,
`Connection: close`, and a garbage request line.

Idle timeout (run the server with a small timeout first, e.g. `./server 8080 5`):

```
./test_client 8080 idle
```

Same nine requests on one keep-alive connection using curl:

```
curl -s -w ' (HTTP %{http_code})\n' \
  "http://localhost:8080/add?a=2&b=3" "http://localhost:8080/sub?a=10&b=4" \
  "http://localhost:8080/mul?a=6&b=7" "http://localhost:8080/div?a=9&b=3" \
  "http://localhost:8080/div?a=1&b=0" "http://localhost:8080/add?a=x&b=3" \
  "http://localhost:8080/pow?a=2&b=8" \
  --next -s -w ' (HTTP %{http_code})\n' -X POST "http://localhost:8080/add" \
  --next -s -w ' (HTTP %{http_code})\n' -H "Host:" "http://localhost:8080/add?a=2&b=3"
```

## Test each request separately

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

`-H "Host:"` removes the Host header curl would normally add.

Keep-alive (curl says `Re-using existing connection` for the second URL):

```
curl -v "http://localhost:8080/add?a=2&b=3" "http://localhost:8080/mul?a=6&b=7"
```

Pipelining, six requests in one write:

```
printf 'GET /add?a=2&b=3 HTTP/1.1\r\nHost: localhost\r\n\r\nGET /sub?a=10&b=4 HTTP/1.1\r\nHost: localhost\r\n\r\nGET /mul?a=6&b=7 HTTP/1.1\r\nHost: localhost\r\n\r\nGET /div?a=1&b=0 HTTP/1.1\r\nHost: localhost\r\n\r\nGET /pow?a=2&b=8 HTTP/1.1\r\nHost: localhost\r\n\r\nPOST /add HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' | nc localhost 8080
```

A body of 8 bytes, then the next request:

```
printf 'POST /div HTTP/1.1\r\nHost: localhost\r\nContent-Length: 8\r\n\r\nabcdefghGET /div?a=100&b=7 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' | nc localhost 8080
```

Chunked body, then the next request:

```
printf 'POST /mul HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\nA\r\n0123456789\r\n0\r\n\r\nGET /mul?a=-4&b=5 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n' | nc localhost 8080
```

Server closes after the reply:

```
curl -i -H "Connection: close" "http://localhost:8080/add?a=1&b=1"
curl -i --http1.0 "http://localhost:8080/add?a=1&b=1"
```
