// Compile w/ [g++ -Wall -Wextra -O2 -g (n).cpp -o (n)]

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <vector>

const size_t k_max_msg = 4096; // 4 Kib

enum
{
    STATE_REQ = 0,
    STATE_RES = 1,
    STATE_END = 2, // mark the connection for deletion
};

struct Conn
{
    int fd = -1;
    uint32_t state = 0; // either [STATE_REQ] or [STATE_RES]

    // buffer for reading
    size_t r_buf_size = 0;
    uint8_t r_buf[4 + k_max_msg];

    // buffer for writing
    size_t w_buf_size = 0;
    size_t w_buf_sent = 0;
    uint8_t w_buf[4 + k_max_msg];
};

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

static void fd_set_nb(int fd)
{
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0);

    if (errno)
    {
        die("fcntl() error");
        return;
    }

    flags |= O_NONBLOCK;

    errno = 0;

    (void)fcntl(fd, F_SETFL, flags);

    if (errno)
    {
        die("fcntl() error");
    }
}

static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn)
{
    if (fd2conn.size() <= (size_t)conn->fd)
    {
        fd2conn.resize(conn->fd + 1);
    }

    fd2conn[conn->fd] = conn;
}

static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd)
{
    // accept
    struct sockaddr_in client_addr = {};
    socklen_t socklen = sizeof(client_addr);

    int conn_fd = accept(fd, (struct sockaddr *)&client_addr, &socklen);

    if (conn_fd < 0)
    {
        msg("accept() error");
        return -1; // error
    }

    // set the new connection fd to non-blocking mode
    fd_set_nb(conn_fd);

    // creating the struct conn
    struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));

    if (!conn)
    {
        close(conn_fd);
        return -1;
    }

    conn->fd = conn_fd;
    conn->state = STATE_REQ;
    conn->r_buf_size = 0;
    conn->w_buf_size = 0;
    conn->w_buf_sent = 0;

    conn_put(fd2conn, conn);

    return 0;
}

static void state_req(Conn *conn);
static void state_res(Conn *conn);

static bool try_one_request(Conn *conn)
{
    // try to parse the request from the buffer
    if (conn->r_buf_size < 4)
    {
        // not enough data in the buffer
        // will have to retry in the next iteration
        return false;
    }

    uint32_t len = 0;
    memcpy(&len, &conn->r_buf[0], 4);

    if (len > k_max_msg)
    {
        msg("too long");
        conn->state = STATE_END;
        return false;
    }

    if (4 + len > conn->r_buf_size)
    {
        // not enough data in the buffer
        // will have to retry in the next iteration
        return false;
    }

    // got one request, do something with it
    printf("client says: %.*s\n", len, &conn->r_buf[4]);

    // generating an echoing response
    memcpy(&conn->w_buf[0], &len, 4);
    memcpy(&conn->w_buf[4], &conn->r_buf[4], len);

    conn->w_buf_size = 4 + len;

    // removing the request from the buffer
    size_t remain = conn->r_buf_size - 4 - len;

    if (remain)
    {
        memmove(conn->r_buf, &conn->r_buf[4 + len], remain);
    }

    conn->r_buf_size = remain;

    // change state
    conn->state = STATE_RES;
    state_res(conn);

    // continue the outer loop if the request was successfully processed
    return (conn->state == STATE_REQ);
}

static bool try_fill_buffer(Conn *conn)
{
    // try to fill the buffer
    assert(conn->r_buf_size < sizeof(conn->r_buf));

    ssize_t rv = 0;

    do
    {
        size_t cap = sizeof(conn->r_buf) - conn->r_buf_size;
        rv = read(conn->fd, &conn->r_buf[conn->r_buf_size], cap);
    } while (rv < 0 && errno == EINTR);

    if (rv < 0 && errno == EAGAIN)
    {
        // got EAGAIN, stop.
        return false;
    }

    if (rv < 0)
    {
        msg("read() error");
        conn->state = STATE_END;
        return false;
    }

    if (rv == 0)
    {
        if (conn->r_buf_size > 0)
        {
            msg("unexpected EOF");
        }
        else
        {
            msg("EOF");
        }

        conn->state = STATE_END;

        return false;
    }

    conn->r_buf_size += (size_t)rv;
    assert(conn->r_buf_size <= sizeof(conn->r_buf));

    // try to process requests one by one
    while (try_one_request(conn))
    {
    }

    return (conn->state == STATE_REQ);
}

static void state_req(Conn *conn)
{
    while (try_fill_buffer(conn))
    {
    }
}

static bool try_flush_buffer(Conn *conn)
{
    ssize_t rv = 0;

    do
    {
        size_t remain = conn->w_buf_size - conn->w_buf_sent;
        rv = write(conn->fd, &conn->w_buf[conn->w_buf_sent], remain);
    } while (rv < 0 && errno == EAGAIN);

    if (rv < 0 && errno == EAGAIN)
    {
        // got EAGAIN, stop.
        return false;
    }

    if (rv < 0)
    {
        msg("write() error");
        conn->state = STATE_END;

        return false;
    }

    conn->w_buf_sent += (size_t)rv;

    assert(conn->w_buf_sent <= conn->w_buf_size);

    if (conn->w_buf_sent == conn->w_buf_size)
    {
        // response was fully sent,
        // change the state back
        conn->state = STATE_REQ;
        conn->w_buf_sent = 0;
        conn->w_buf_size = 0;

        return false;
    }

    // still got some data in wbuf, could try to write again
    return true;
}

static void state_res(Conn *conn)
{
    while (try_flush_buffer(conn))
    {
    }
}

static void connection_io(Conn *conn)
{
    if (conn->state == STATE_REQ)
    {
        state_req(conn);
    }
    else if (conn->state == STATE_RES)
    {
        state_res(conn);
    }
    else
    {
        assert(0); // not expected
    }
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

    // a map of all client connections, keyed by fd
    std::vector<Conn *> fd2conn;

    // set the listen fd to non-blocking mode
    fd_set_nb(fd);

    // the event loop
    std::vector<struct pollfd> poll_args;

    while (true)
    {
        // prepare the arguments for the [poll()]
        poll_args.clear();

        // for convience, the listing fd is put in the first position
        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd);

        // connection fds
        for (Conn *conn : fd2conn)
        {
            if (!conn)
            {
                continue;
            }

            struct pollfd pfd = {};

            pfd.fd = conn->fd;
            pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
            pfd.events = pfd.events | POLLERR;

            poll_args.push_back(pfd);
        }

        // poll for active fds
        // the timeout argument doesn't matter here
        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);

        if (rv < 0)
        {
            die("poll");
        }

        // process active connections
        for (size_t i = 1; i < poll_args.size(); ++i)
        {
            if (poll_args[i].revents)
            {
                Conn *conn = fd2conn[poll_args[i].fd];
                connection_io(conn);

                if (conn->state == STATE_END)
                {
                    // client closed normally, or something bad happened.
                    // destroy the connection
                    fd2conn[conn->fd] = NULL;
                    (void)close(conn->fd);
                    free(conn);
                }
            }
        }

        // try to accept a new connection if the listening fd is active
        if (poll_args[0].revents)
        {
            (void)accept_new_conn(fd2conn, fd);
        }
    }

    return 0;
}