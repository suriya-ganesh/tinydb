#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

static void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

void dosomething(int connfd) {

    char readBuf[64] = {};

    ssize_t readResp = read(connfd, readBuf, sizeof(readBuf) - 1);
    if (readResp < 0) { die("read()"); }

    printf("read from client: %s\n", readBuf);

    char writeBuf[] = "world";
    write(connfd, writeBuf, strlen(writeBuf));
}


int32_t readfull(int fd, char *buf, size_t size) {

    while (size > 0) {

        ssize_t readSize = read(fd, buf, size);
        if (readSize < 0) { return -1; }
        assert((size_t) readSize <= size);
        size -= (size_t)readSize;
        buf += readSize;
    }
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
