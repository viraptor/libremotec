#include "network.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/xattr.h>

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

static void handle_lstat() {
    struct stat buf;
    char *path = remote_recv_string();
    int ret = lstat(path, &buf);
    free(path);
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

static void handle_lseek() {
    int fd = remote_recv_int();
    off_t offset = remote_recv_off_t();
    int whence = remote_recv_int();
    off_t ret = lseek(fd, offset, whence);
    remote_send_off_t(ret);
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

static void handle_faccessat() {
    int fd = remote_recv_int();
    char *path = remote_recv_string();
    int amode = remote_recv_int();
    int flag = remote_recv_int();
    int ret = faccessat(fd, path, amode, flag);
    remote_send_int(ret);
    if (ret == -1) {
        remote_send_errno(errno);
    }
}

static void handle_getxattr() {
    char *path = remote_recv_string();
    char *name = remote_recv_string();
    size_t size = remote_recv_size_t();
    void *value = NULL;
    if (size > 0)
        value = malloc(size);
    int ret = getxattr(path, name, value, size);
    free(path);
    free(name);
    remote_send_int(ret);
    if (ret == -1) {
        remote_send_errno(errno);
    } else {
        remote_send_data(value, size);
    }
    free(value);
}

typedef void (*handler_t)(void);
static handler_t handlers[TOTAL_RC_COUNT] = {
    [RC_OPEN] = &handle_open,
    [RC_FSTAT] = &handle_fstat,
    [RC_LSTAT] = &handle_lstat,
    [RC_CLOSE] = &handle_close,
    [RC_READ] = &handle_read,
    [RC_LSEEK] = &handle_lseek,
    [RC_FACCESSAT] = &handle_faccessat,
    [RC_GETXATTR] = &handle_getxattr,
};

int main() {
    remote_listen();
    while (true) {
        rc_type call = remote_recv_syscall();
        if (call >= TOTAL_RC_COUNT) {
            printf("Unknown syscall\n");
            exit(1);
        }
        handlers[call]();
    }
}
