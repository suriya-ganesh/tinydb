#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <libc.h>
#include <sys/poll.h>


const size_t k_max_msg = 1096; //max message we read
const int k_msg_len = 4;

enum {
    STATE_REQ = 1,
    STATE_RES = 2,
    STATE_END = 3,
};

struct Conn {
    int fd = -1;
    uint32_t state = 0; //the state enum we've defined

    //read buffer
    size_t rbuf_size = 0;
    uint8_t rbuf[k_msg_len + k_max_msg];

    //write buffer
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    uint8_t wbuf[k_msg_len + k_max_msg];
};

void die(const char *msg, const char *pattern = "[%d] %s\n") {
    int err = errno;
    fprintf(stderr, pattern, err, msg);
    abort();
}

void msg(const char *msg, const char *pattern = "%s\n") {
    fprintf(stderr, pattern, msg);
}

//give a file descriptor and get a non blocking fd.
void fd_set_nb(int fd) {

    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0);
    if (errno) {
        die("fcntl get error");
        return;
    }

    flags |= O_NONBLOCK;

    errno = 0;
    fcntl(fd, F_SETFL, flags);
    if (errno) {
        die("fcntl set error");
        return;
    }
}

// flush the contents of the buffer into the listener
bool try_flush_buffer(Conn *conn) {
    size_t rv = 0;

    do {
        size_t remain = conn->wbuf_size - conn->wbuf_sent;
        rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
    } while (rv >= 0 && errno == EINTR);

    if (rv < 0 && errno == EAGAIN) { return false; }

    if (rv < 0) {
        msg("write() error");
        conn->state = STATE_END;
        return false;
    }

    conn->wbuf_sent += (size_t) rv;

    // On succesful message sent we reset connection to be ready for new request.
    if (conn->wbuf_sent == conn->wbuf_size) {
        conn->state = STATE_REQ;
        conn->wbuf_size = 0;
        conn->wbuf_sent = 0;
    }

}

void state_res(Conn *conn) {
    while (try_flush_buffer(conn)) {}
}

bool try_one_req(Conn *conn) {

    if (conn->rbuf_size < k_msg_len) { return false; } //unparsable data

    uint32_t len = 0;

    memcpy(&len, conn->rbuf, k_msg_len);
    if (len > k_max_msg) {
        msg("message too long");
        conn->state = STATE_END;
        return false;
    }

    fprintf(stderr, "client says %s\n", &conn->rbuf[4]); //changed

    // writing an echo message back according to the protocol
    memcpy(&conn->wbuf[0], &len, k_msg_len);
    memcpy(&conn->wbuf[k_msg_len], &conn->rbuf[k_msg_len], len);
    conn->wbuf_size = k_msg_len + len;
    //resetting the read pointer to the next message for next round
    size_t remain = conn->rbuf_size - k_msg_len - len;
    if (remain) {
        memmove(conn->rbuf, &conn->rbuf[k_msg_len + len], remain);
    }

    conn->rbuf_size = remain;
    conn->state = STATE_RES;
    state_res(conn);

    return (conn->state == STATE_REQ);
}


bool try_fill_buffer(Conn *conn) {
    assert(conn->rbuf_size <= sizeof(conn->rbuf));

    ssize_t rv = 0;

    // retry logic for system interrupt while reading using EINTR
    do {
        size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
        rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
    } while (rv < 0 && errno == EINTR);


    if (rv < 0 && errno == EAGAIN) { return false; }

    if (rv == 0) { // if we read 0 bytes
        if (conn->rbuf_size > 0) {
            msg("unexpected EOF");
        } else {
            msg("EOF");
        }

        conn->state = STATE_END;
        return false;
    }

    conn->rbuf_size += (size_t)rv;
    assert(conn->rbuf_size <= sizeof(conn->rbuf));

    while (try_one_req(conn)) {}

    return (conn->state == STATE_REQ);
}

void state_req(Conn *conn) {
    while (try_fill_buffer(conn)) {}
}


void connection_io(Conn *conn) {

    if (conn->state == STATE_REQ) {
        state_req(conn);
    } else if (conn->state == STATE_RES) {
        state_res(conn);
    }
}

int32_t readfull(int fd, char *buf, size_t size) {

    while (size > 0) {

        ssize_t readSize = read(fd, buf, size);
        if (readSize < 0) { return -1; }
        assert((size_t) readSize <= size);
        size -= (size_t) readSize;
        buf += readSize;
    }
    return 0;
}

int32_t writeall(int fd, const char *buf, size_t size) {
    while (size > 0) {
        ssize_t writeLength = write(fd, buf, size);
        if (writeLength < 0) { return -1; }
        size -= (size_t) writeLength;
        buf += writeLength;
    }
    return 0;
}

//insert a conn into the map
void conn_put(std::vector<Conn *> &fd2conn, Conn *conn) {

    if (fd2conn.size() <= (size_t) conn->fd) {
        fd2conn.resize(conn->fd + 1);
    }
    fd2conn[conn->fd] = conn;
}

int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd) {

    struct sockaddr_in client_add = {};
    socklen_t socklen = sizeof(client_add);
    int connfd = accept(fd, (sockaddr *) &client_add, &socklen);
    if (connfd < 0) {
        msg("accept() error");
        return -1;
    }

    fd_set_nb(connfd); //setting the client connection to be non-blocking

    auto *conn = (struct Conn *) malloc(sizeof(struct Conn));

    if (!conn) {
        close(connfd);
        return -1;
    }

    conn->fd = connfd;
    conn->state = STATE_REQ;
    conn->rbuf_size = 0;
    conn->wbuf_size = 0;
    conn->wbuf_sent = 0;
    conn_put(fd2conn, conn);

    return 0;
}


static int32_t one_request(int connfd) {

    char rbuf[k_msg_len + k_max_msg + 1]; //we will fill this buffer with one message while reading from socket
    errno = 0;

    int32_t err = readfull(connfd, rbuf, k_msg_len);
    if (err) {
        if (errno == 0) { msg("EOF"); }
        else { msg("read() error"); }
        return err;
    }

    uint32_t len = 0;
    memcpy(&len, rbuf, k_msg_len); //copy msg_len_value onto len
    if (len > k_max_msg) {
        msg("req too long");
        return -1;
    }

    //reading request body
    err = readfull(connfd, &rbuf[k_msg_len], len); //copy the rest of the bytes
    if (err) {
        msg("read() error");
        return err;
    }

    rbuf[k_msg_len + len] = '\0'; //end message signifier
    msg(&rbuf[k_msg_len], "client says %s\n");

    const char reply[] = "world";
    char wbuf[k_msg_len + strlen(reply)];
    len = (uint32_t) strlen(reply);
    memcpy(wbuf, &len, k_msg_len);
    memcpy(&wbuf[k_msg_len], reply, len);
    return writeall(connfd, wbuf, len + k_msg_len);
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0); // opening a tcp  socket filedescriptor with
    //                       ^^^^^^^^^^ TCP

    //set socket options see https://pubs.opengroup.org/onlinepubs/7908799/xns/getsockopt.html
    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    sockaddr_in addr = {};

    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234); //converting network to host byte order
    addr.sin_addr.s_addr = ntohl(0); //short form for 0.0.0.0

    // bind associates the address with the socket see man pages
    int bindfail = bind(fd, (const sockaddr *) &addr, sizeof(addr));
    if (bindfail) { die("bind()"); }

    int listenfail = listen(fd, SOMAXCONN);
    if (listenfail) { die("listen()"); }

    //map of clients with fd as key
    std::vector<Conn *> fd2conn;

    fd_set_nb(fd);

    std::vector<struct pollfd> poll_args;
    // Now that we have a working socket and listening in it.
    // We can read from it
    while (true) {

        poll_args.clear();

        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd); // first fd is the listening fd

        // iterating through every connection that we alreadyhave and pushing to the polling list
        for (Conn *conn: fd2conn) {
            if (!conn) { continue; }

            struct pollfd pfd = {conn->fd};
            pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
            pfd.events |= POLLERR;

            poll_args.push_back(pfd);
        }


        // Waiting for some event to happen on a fd
        // in our case it is a write/read from our client
        int rv = poll(poll_args.data(), (nfds_t) poll_args.size(), 1000);
        if (rv < 0) { die("poll"); }

        for (size_t i = 1; i < poll_args.size(); ++i) {
            if (poll_args[i].revents) {
                Conn *conn = fd2conn[poll_args[i].fd];
                connection_io(conn);
                if (conn->state == STATE_END) {
                    fd2conn[conn->fd] = nullptr;
                    close(conn->fd);
                    free(conn);
                }
            }
        }

        if (poll_args[0].revents) {
            accept_new_conn(fd2conn, fd);
        }

    }


    return 0;
}
