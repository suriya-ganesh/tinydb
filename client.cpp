#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <libc.h>

static void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

int main() {

    int fd = socket(AF_INET, SOCK_STREAM, 0);

    //Creating and populating socket address struct
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);
    addr.sin_port = ntohs(1234);

    //Connecting to said socket
    int connectResp = connect(fd, (const struct sockaddr *) &addr, sizeof(addr));
    if (connectResp) { die("connect()"); }

    //writing message to the socket
    char msg[] = "world";
    ssize_t writeResp = write(fd, msg, strlen(msg));
    if (writeResp < 0) { die("write()"); }

    //reading message back from the socket
    char readBuf[64] = {};
    ssize_t readResp = read(fd, readBuf, sizeof(readBuf) - 1);
    if (readResp < 0) { die("read()"); }

    printf("server response: %s\n", msg);
    close(fd);

    return 0;
}