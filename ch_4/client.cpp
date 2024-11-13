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

const size_t k_max_msg = 4096;

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

static int32_t query(int fd, const char *text)
{
    uint32_t len = (uint32_t)strlen(text);

    if (len > k_max_msg)
    {
        return -1;
    }

    // send request
    char w_buf[4 + k_max_msg];

    memcpy(w_buf, &len, 4);
    memcpy(&w_buf[4], text, len);

    if (int32_t err = write_all(fd, w_buf, 4 + len))
    {
        return err;
    }

    // 4 bytes header
    char r_buf[4 + k_max_msg + 1];
    errno = 0;
    int32_t err = read_full(fd, r_buf, 4);

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

    memcpy(&len, r_buf, 4); // assume little endian

    if (len > k_max_msg)
    {
        msg("too long");
        return -1;
    }

    // reply body
    err = read_full(fd, &r_buf[4], len);

    if (err)
    {
        msg("read() error");
        return err;
    }

    // do something
    r_buf[4 + len] = '\0';
    printf("Server says: %s\n", &r_buf[4]);

    return 0;
}

int main()
{

    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0)
    {
        die("socket()");
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK); // 127.0.0.1

    int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));

    if (rv)
    {
        die("connect()");
    }

    // multiple requests
    int32_t err = query(fd, "hello1");

    if (err)
    {
        goto L_DONE;
    }

    err = query(fd, "hello2");

    if (err)
    {
        goto L_DONE;
    }

    err = query(fd, "hello3");

    if (err)
    {
        goto L_DONE;
    }

L_DONE:
    close(fd);
    return 0;
}