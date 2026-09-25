// server.cpp - HTTP/1.1 calculator that keeps connections open.
// main() accepts connections and starts one std::thread for each client. The thread
// adds everything it receives to the client's buffer and answers every complete request
// in it, in order, until the client leaves, asks to close, or is idle for too long.
//
//   ./server [port] [idle_timeout_seconds]

#include <netinet/in.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>

#include "calculator.h"
#include "http_parser.h"

using namespace std;

int idle_timeout = 20;
atomic<int> active(0);
mutex out_lock;

const char *status_text(int code) {
    if (code == 200) return "OK";
    if (code == 400) return "Bad Request";
    if (code == 404) return "Not Found";
    if (code == 405) return "Method Not Allowed";
    if (code == 413) return "Payload Too Large";
    if (code == 431) return "Request Header Fields Too Large";
    if (code == 501) return "Not Implemented";
    return "Error";
}

// HTTP/1.1 is keep-alive by default, so a Connection header is only sent when closing.
string make_response(int code, const string &body, bool closing) {
    string r = "HTTP/1.1 " + to_string(code) + " " + status_text(code) + "\r\n";
    r += "Content-Type: text/plain\r\n";
    r += "Content-Length: " + to_string(body.size()) + "\r\n";
    if (code == 405) r += "Allow: GET\r\n";
    if (closing) r += "Connection: close\r\n";
    r += "\r\n";
    return r + body;
}

bool send_all(int fd, const string &data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.c_str() + sent, data.size() - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

void say(int fd, const string &msg) {
    lock_guard<mutex> guard(out_lock);
    printf("[fd %d] %s\n", fd, msg.c_str());
    fflush(stdout);
}

// true when there is something to read before the idle timeout runs out
bool wait_for_data(int fd) {
    while (true) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd, &set);
        timeval wait = {idle_timeout, 0};
        int r = select(fd + 1, &set, NULL, NULL, &wait);
        if (r < 0 && errno == EINTR) continue;
        return r > 0;
    }
}

void serve(int fd) {
    say(fd, "connected, " + to_string(++active) + " open connection(s)");
    string buffer;   // received but not handled yet

    bool done = false;
    while (!done) {
        // there can be several requests in the buffer (pipelining), answer them in order
        while (true) {
            Request req;
            size_t used = 0;
            int r = parse_request(buffer, req, used);
            if (r == -1) break;
            if (r > 0) {
                // can't tell where the next request would start, so give up on this connection
                send_all(fd, make_response(r, status_text(r), true));
                say(fd, "bad request, answered " + to_string(r));
                done = true;
                break;
            }
            buffer.erase(0, used);

            string body;
            int code = handle(req, body);
            bool closing = should_close(req);
            say(fd, req.method + " " + req.target + " -> " + to_string(code));
            if (!send_all(fd, make_response(code, body, closing)) || closing) {
                done = true;
                break;
            }
        }
        if (done) break;

        if (!wait_for_data(fd)) {
            say(fd, "idle for " + to_string(idle_timeout) + "s");
            break;
        }
        char tmp[4096];
        ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        buffer.append(tmp, n);
    }

    say(fd, "closed, " + to_string(--active) + " open connection(s)");
    close(fd);
}

int main(int argc, char **argv) {
    int port = argc > 1 ? atoi(argv[1]) : 8080;
    if (argc > 2) idle_timeout = atoi(argv[2]);
    if (port <= 0 || port > 65535 || idle_timeout <= 0) {
        fprintf(stderr, "usage: %s [port] [idle_timeout_seconds]\n", argv[0]);
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(server_fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    listen(server_fd, 20);
    printf("calculator listening on port %d, idle timeout %d s\n", port, idle_timeout);
    fflush(stdout);

    while (true) {
        int fd = accept(server_fd, NULL, NULL);
        if (fd < 0) continue;
        if (fd >= FD_SETSIZE) {   // select() can't watch it
            close(fd);
            continue;
        }
        thread(serve, fd).detach();
    }
}
