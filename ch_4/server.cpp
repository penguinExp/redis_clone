// Compile w/ [g++ -Wall -Wextra -O2 -g (n).cpp -o (n)]

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <cassert>

const size_t k_max_msg = 4096; // 4 Kib

static void msg(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
}

static void die(const char *msg)
{
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

static int32_t read_full(int fd, char *buf, size_t n)
{

    while (n > 0)
    {
        ssize_t rv = read(fd, buf, n);

        if (rv <= 0)
        {
            return -1; // error, or unexpected EOF
        }

        assert((ssize_t)rv <= n);

        n -= (ssize_t)rv;
        buf += rv;
    }

    return 0;
}

static int32_t write_all(int fd, const char *buf, ssize_t n)
{
    while (n > 0)
    {
        ssize_t rv = write(fd, buf, n);

        if (rv <= 0)
        {
            return -1; // error
        }

        assert((ssize_t)rv <= n);

        n -= (ssize_t)rv;
        buf += rv;
    }

    return 0;
}

static int32_t one_request(int conn_fd)
{
    errno = 0;

    // 4 bytes header
    char r_buf[4 + k_max_msg + 1]; // len + msg + null_terminator
    int32_t err = read_full(conn_fd, r_buf, 4);

    if (err)
    {
        if (errno == 0)
        {
            msg("EOF");
        }
        else
        {
            msg("read() error");
        }

        return err;
    }

    uint32_t len = 0;

    // copy 4 bytes from [r_buf] to [len];
    memcpy(&len, r_buf, 4); // assume little endian

    if (len > k_max_msg)
    {
        msg("too long");
        return -1;
    }

    // request body
    err = read_full(conn_fd, &r_buf[4], len);

    if (err)
    {
        msg("read() error");
        return err;
    }

    // do something
    r_buf[4 + len] = '\0';
    printf("client says: %s\n", &r_buf[4]);

    // reply using the same protocol
    const char reply[] = "world";
    char w_buf[4 + sizeof(reply)];
    len = (uint32_t)strlen(reply);

    memcpy(w_buf, &len, 4);
    memcpy(&w_buf[4], reply, len);

    return write_all(conn_fd, w_buf, 4 + len);
}

int main()
{
    // create a socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0)
    {
        die("socket()");
    }

    // set options
    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    // bind
    struct sockaddr_in addr = {};

    /*
    👉 Note:
        - ntohs - Network to host short
        - ntohl - Network to host long

    Network protocols use `big-endian` formats. Therefore, when sending data over a
    network, applications must convert their data from the host's byte order to
    network byte order
    */

    addr.sin_family = AF_INET; // IPv4
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(0); // wildcard address 0.0.0.0

    int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr));

    if (rv)
    {
        die("bind()");
    }

    // listen
    rv = listen(fd, SOMAXCONN); // SOMAXCONN is `128` on linux

    if (rv)
    {
        die("listen()");
    }

    // server loop
    while (true)
    {
        // accept
        struct sockaddr_in client_addr = {};
        socklen_t socklen = sizeof(client_addr);

        int conn_fd = accept(fd, (struct sockaddr *)&client_addr, &socklen);

        // error
        if (conn_fd < 0)
        {
            continue;
        }

        while (true)
        {
            int32_t err = one_request(conn_fd);

            if (err)
            {
                break;
            }
        }

        close(conn_fd);
    }

    return 0;
}