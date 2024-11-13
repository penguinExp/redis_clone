#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>

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

static void do_something(int conn_fd)
{
    char r_buf[64] = {};
    ssize_t n = read(conn_fd, r_buf, sizeof(r_buf) - 1);

    if (n < 0)
    {
        msg("read() error");
        return;
    }

    printf("Client says: %s\n", r_buf);

    char w_buf[] = "world";
    write(conn_fd, w_buf, strlen(w_buf));
}

int main()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0)
    {
        die("socket()");
    }

    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(0);

    int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr));
    if (rv)
    {
        die("bind()");
    }

    rv = listen(fd, SOMAXCONN);
    if (rv)
    {
        die("bind()");
    }

    while (true)
    {
        struct sockaddr_in client_addr = {};
        socklen_t socklen = sizeof(client_addr);

        int conn_fd = accept(fd, (struct sockaddr *)&client_addr, &socklen);

        if (conn_fd < 0)
        {
            continue;
        }

        do_something(conn_fd);
        close(conn_fd);
    }

    return 0;
}
