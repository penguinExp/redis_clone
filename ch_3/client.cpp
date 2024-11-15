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

static void die(const char *msg)
{
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
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

    char msg[] = "hello";
    write(fd, msg, strlen(msg));

    char r_buf[64] = {};
    ssize_t n = read(fd, r_buf, sizeof(r_buf) - 1); // -1 to ensure space for null-terminator `\0`

    if (n < 0)
    {
        die("read()");
    }

    printf("Server says: %s\n", r_buf);
    close(fd);

    return 0;
}