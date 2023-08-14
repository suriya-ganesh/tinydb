#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <libc.h>

const size_t k_max_msg = 1096;
const int msg_size = 4;


void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

void msg(const char *msg, const char *pattern = "%s\n") {
    fprintf(stderr, pattern, msg);
}


int32_t readfull(int fd, char *buf, size_t size) {

    while (size > 0) {
        ssize_t readsize = read(fd, buf, size);
        if (readsize < 0) { return -1; }
        assert((size_t) readsize <= size);
        size -= (size_t) readsize;
        buf += readsize;
    }

    return 0;
}

int32_t writeall(int fd, char *buf, size_t size) {

    while (size > 0) {
        ssize_t writeLength = write(fd, buf, size);
        if (writeLength < 0) { return -1; }
        size -= (size_t) writeLength;
        buf += writeLength;
    }
    return 0;
}

int32_t query(int fd, const char *text) {
    auto len = (uint32_t) strlen(text);
    if (len > k_max_msg) { return -1; }

    //writing query to server
    char wbuf[msg_size + len];
    memcpy(wbuf, &len, msg_size);
    memcpy(&wbuf[msg_size], text, len);
    int32_t err = writeall(fd, wbuf, msg_size + len);
    if (err) { return err; }

    //reading response from server
    char rbuf[msg_size + k_max_msg + 1];
    errno = 0;
    err = readfull(fd, rbuf, 4); //reading header
    if (err) {
        if (errno == 0) { msg("EOF"); }
        else { msg("read() err"); }
        return err;
    }

    memcpy(&len, rbuf, 4);

    err = readfull(fd, &rbuf[4], len);
    if (err) {
        msg("read() error");
    }
    rbuf[4 + len] = '\0';
    msg(&rbuf[4], "server says: %s\n");

    return 0;
}

static int32_t send_req(int fd, const char *text) {
    uint32_t len = (uint32_t) strlen(text);
    if (len > k_max_msg) {
        return -1;
    }

    char wbuf[4 + k_max_msg];
    memcpy(wbuf, &len, 4);  // assume little endian
    memcpy(&wbuf[4], text, len);
    return writeall(fd, wbuf, 4 + len);
}


static int32_t read_res(int fd) {
    // 4 bytes header
    char rbuf[4 + k_max_msg + 1];
    errno = 0;
    int32_t err = readfull(fd, rbuf, 4);
    if (err) {
        if (errno == 0) {
            msg("EOF");
        } else {
            msg("read() error");
        }
        return err;
    }

    uint32_t len = 0;
    memcpy(&len, rbuf, 4);  // assume little endian
    if (len > k_max_msg) {
        msg("too long");
        return -1;
    }

    // reply body
    err = readfull(fd, &rbuf[4], len);
    if (err) {
        msg("read() error");
        return err;
    }

    // do something
    rbuf[4 + len] = '\0';
    printf("server says: %s\n", &rbuf[4]);
    return 0;
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        die("socket()");
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);  // 127.0.0.1
    int rv = connect(fd, (const struct sockaddr *) &addr, sizeof(addr));
    if (rv) {
        die("connect");
    }

    // multiple pipelined requests
    const char *query_list[] = {"hello1", "hello2", "hello3"};
    for (auto & i : query_list) {
        int32_t err = send_req(fd, i);
        if (err) {
            goto L_DONE;
        }
    }
    for (size_t i = 0; i < 3; ++i) {
        int32_t err = read_res(fd);
        if (err) {
            goto L_DONE;
        }
    }

    L_DONE:
    close(fd);
    return 0;
}
