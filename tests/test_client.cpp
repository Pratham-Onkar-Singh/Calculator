// test_client.cpp - sends requests to the calculator server and prints the answers
//   ./test_client [port]
//   ./test_client [port] idle      only the idle timeout

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>

using namespace std;

int port = 8080;

class TestConn {
public:
    TestConn() {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        if (connect(fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
            cout << "cannot connect to 127.0.0.1:" << port << " - start ./server first\n";
            exit(1);
        }
        timeval tv = {3, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    ~TestConn() { close(fd); }

    void write(const string &data) { send(fd, data.data(), data.size(), 0); }

    // status code of the next response (-1 if none came), body goes into body
    int next_reply(string &body) {
        size_t end;
        while ((end = pending.find("\r\n\r\n")) == string::npos)
            if (!more()) return -1;
        string head = pending.substr(0, end);
        pending.erase(0, end + 4);

        size_t len = 0;
        size_t p = head.find("Content-Length: ");
        if (p != string::npos) len = atoi(head.c_str() + p + 16);
        while (pending.size() < len)
            if (!more()) return -1;
        body = pending.substr(0, len);
        pending.erase(0, len);
        return atoi(head.c_str() + 9);
    }

    // waits up to max_seconds, returns how long it took the server to close (-1 = it didn't)
    int seconds_until_closed(int max_seconds) {
        time_t start = time(NULL);
        pollfd p = {fd, POLLIN, 0};
        if (poll(&p, 1, max_seconds * 1000) <= 0) return -1;
        char c;
        if (recv(fd, &c, 1, MSG_PEEK) != 0) return -1;
        return (int)(time(NULL) - start);
    }

    // "True" if the server has not closed the connection
    string open() {
        pollfd p = {fd, POLLIN, 0};
        if (poll(&p, 1, 300) == 0) return "True";
        char c;
        return recv(fd, &c, 1, MSG_PEEK) > 0 ? "True" : "False";
    }

private:
    int fd;
    string pending;

    bool more() {
        char buf[2048];
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        pending.append(buf, n);
        return true;
    }
};

string GET(const string &target) {
    return "GET " + target + " HTTP/1.1\r\nHost: localhost\r\n\r\n";
}

// reads one reply and prints it, returns true if there was one
bool show(TestConn &c, const string &label) {
    string body;
    int code = c.next_reply(body);
    printf("  %-28s -> %d %s\n", label.c_str(), code, body.c_str());
    return code > 0;
}

int main(int argc, char **argv) {
    if (argc > 1) port = atoi(argv[1]);

    if (argc > 2 && string(argv[2]) == "idle") {
        cout << "== idle timeout ==" << endl;
        TestConn c;
        c.write(GET("/add?a=1&b=1"));
        show(c, "GET /add?a=1&b=1");
        cout << "  sending nothing now..." << endl;
        int secs = c.seconds_until_closed(120);
        if (secs < 0) cout << "  still open after 120 s" << endl;
        else cout << "  server closed the connection after about " << secs << " s" << endl;
        return 0;
    }

    cout << "== one connection, every request ==" << endl;
    {
        TestConn c;
        int n = 0;
        c.write(GET("/add?a=2&b=3"));   n += show(c, "GET /add?a=2&b=3");
        c.write(GET("/sub?a=10&b=4"));  n += show(c, "GET /sub?a=10&b=4");
        c.write(GET("/mul?a=6&b=7"));   n += show(c, "GET /mul?a=6&b=7");
        c.write(GET("/div?a=9&b=3"));   n += show(c, "GET /div?a=9&b=3");
        c.write(GET("/div?a=1&b=0"));   n += show(c, "GET /div?a=1&b=0");
        c.write(GET("/add?a=x&b=3"));   n += show(c, "GET /add?a=x&b=3");
        c.write(GET("/pow?a=2&b=8"));   n += show(c, "GET /pow?a=2&b=8");
        c.write("POST /add HTTP/1.1\r\nHost: localhost\r\n\r\n");
        n += show(c, "POST /add");
        c.write("GET /add?a=2&b=3 HTTP/1.1\r\n\r\n");
        n += show(c, "GET /add (no Host)");
        cout << "  socket still open: " << c.open() << endl;
        cout << "  1 TCP handshake, " << n << " responses" << endl;
    }

    cout << endl << "== body arrives later than the headers ==" << endl;
    {
        TestConn c;
        c.write("POST /div HTTP/1.1\r\nHost: localhost\r\nContent-Length: 8\r\n\r\n");
        usleep(300000);
        c.write("abcdefgh" + GET("/div?a=100&b=7"));
        show(c, "POST /div, 8 byte body");
        show(c, "GET /div?a=100&b=7");
        cout << "  socket still open: " << c.open() << endl;
    }

    cout << endl << "== chunked body ==" << endl;
    {
        TestConn c;
        c.write("POST /mul HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n"
                "3\r\nabc\r\nA\r\n0123456789\r\n0\r\n\r\n" + GET("/mul?a=-4&b=5"));
        show(c, "POST /mul, chunked");
        show(c, "GET /mul?a=-4&b=5");
        cout << "  socket still open: " << c.open() << endl;
    }

    cout << endl << "== pipelining: six requests in one write ==" << endl;
    {
        TestConn c;
        c.write(GET("/add?a=2&b=3") + GET("/sub?a=10&b=4") + GET("/mul?a=6&b=7") +
                GET("/div?a=1&b=0") + GET("/pow?a=2&b=8") +
                "POST /add HTTP/1.1\r\nHost: localhost\r\nContent-Length: 3\r\n\r\nxyz");
        show(c, "1st  GET /add?a=2&b=3");
        show(c, "2nd  GET /sub?a=10&b=4");
        show(c, "3rd  GET /mul?a=6&b=7");
        show(c, "4th  GET /div?a=1&b=0");
        show(c, "5th  GET /pow?a=2&b=8");
        show(c, "6th  POST /add");
        cout << "  socket still open: " << c.open() << endl;
    }

    cout << endl << "== the server closes when it should ==" << endl;
    {
        TestConn c;
        c.write("GET /sub?a=5&b=25 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
        show(c, "GET with Connection: close");
        cout << "  socket still open: " << c.open() << endl;
    }
    {
        TestConn c;
        c.write("HELLO\r\n\r\n");
        show(c, "garbage request line");
        cout << "  socket still open: " << c.open() << endl;
    }
    return 0;
}
