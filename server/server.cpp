#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <libc.h>


const size_t k_max_msg = 1096; //max message we read
const int msg_size = 4;


void die(const char *msg, const char *pattern = "[%d] %s\n") {
    int err = errno;
    fprintf(stderr, pattern, err, msg);
    abort();
}

void msg(const char *msg, const char *pattern = "%s\n") {
    fprintf(stderr, pattern, msg);
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


static int32_t one_request(int connfd) {

    char rbuf[msg_size + k_max_msg + 1]; //we will fill this buffer with one message while reading from socket
    errno = 0;

    int32_t err = readfull(connfd, rbuf, msg_size);
    if (err) {
        if (errno == 0) { msg("EOF"); }
        else { msg("read() error"); }
        return err;
    }

    uint32_t len = 0;
    memcpy(&len, rbuf, msg_size); //copy msg_size_value onto len
    if (len > k_max_msg) {
        msg("req too long");
        return -1;
    }

    //reading request body
    err = readfull(connfd, &rbuf[msg_size], len); //copy the rest of the bytes
    if (err) {
        msg("read() error");
        return err;
    }

    rbuf[ msg_size + len] = '\0'; //end message signifier
    msg(&rbuf[msg_size], "client says %s\n");

    const char reply[] = "world";
    char wbuf[msg_size + strlen(reply)];
    len = (uint32_t) strlen(reply);
    memcpy(wbuf, &len, msg_size);
    memcpy(&wbuf[msg_size], reply, len);
    return writeall(connfd, wbuf, len + msg_size);
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

    // Now that we have a working socket and listening in it.
    // We can read from it
    while (true) {
        sockaddr_in client_addr = {};
        socklen_t socklen = sizeof(client_addr);
        int connfd = accept(fd, (struct sockaddr *) &client_addr, &socklen);
        if (connfd < 0) { continue; }

        while (true) {
            int32_t err = one_request(connfd);
            if (err) { break; }
        }
        close(connfd);

    }
    return 0;
}
