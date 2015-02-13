#include "network.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

static void handle_open() {
    char *path = remote_recv_string();
    int flags = remote_recv_int();
    int ret = open(path, flags);
    free(path);
    remote_send_int(ret);
    if (ret == -1) {
        remote_send_errno(errno);
    }
}

static void handle_fstat() {
    struct stat buf;
    int fd = remote_recv_int();
    int ret = fstat(fd, &buf);
    remote_send_int(ret);
    if (ret == -1) {
        remote_send_errno(errno);
    } else {
        remote_send_data(&buf, sizeof(buf));
    }
}

static void handle_close() {
    int fd = remote_recv_int();
    int ret = close(fd);
    remote_send_int(ret);
    if (ret == -1) {
        remote_send_errno(errno);
    }
}

static void handle_read() {
    int fd = remote_recv_int();
    int len = remote_recv_size_t();
    char *data = malloc(len);
    int ret = read(fd, data, len);
    remote_send_int(ret);
    if (ret == -1) {
        remote_send_errno(errno);
    } else if (ret > 0) {
        remote_send_data(data, ret);
    }
    free(data);
}

int main() {
    remote_listen();
    while (true) {
        switch(remote_recv_syscall()) {
            case RC_OPEN:
                handle_open();
                break;
            case RC_FSTAT:
                handle_fstat();
                break;
            case RC_CLOSE:
                handle_close();
                break;
            case RC_READ:
                handle_read();
                break;
            default:
                printf("Unknown syscall\n");
                exit(1);
        }
    }
}
