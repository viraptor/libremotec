#define _GNU_SOURCE
#include <dlfcn.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "network.h"

#define REMOTE_MAX_OPEN 10
#define REMOTE_FD_SHIFT 2048

static int remote_fds[REMOTE_MAX_OPEN] = {0};
static int remote_errno;

static int local_paths_count;
static char* local_paths = NULL;
static char** local_paths_array = NULL;
static int* local_paths_len = NULL;

typedef int (*orig_open_f_type)(const char *pathname, int flags);
typedef int (*orig_fxstat_f_type)(int ver, int fildes, struct stat *buf);
typedef int (*orig_close_f_type)(int fildes);
typedef int (*orig_read_f_type)(int fildes, void *buf, size_t nbyte);

static int find_free_remote_fd() {
    for (int i=0; i<REMOTE_MAX_OPEN; i++) {
        if (!remote_fds[i]) {
            return i;
        }
    }
    return -1;
}

static int call_remote_open(const char *pathname, int flags) {
    remote_ensure();
    remote_send_syscall(RC_OPEN);
    remote_send_string(pathname);
    remote_send_int(flags);
    int fd = remote_recv_int();
    if (fd == -1) {
        remote_errno = remote_recv_errno();
    }
    return fd;
}

static int call_remote_fstat(int fildes, struct stat *buf) {
    remote_ensure();
    remote_send_syscall(RC_FSTAT);
    remote_send_int(fildes);
    int ret = remote_recv_int();
    if (ret == -1) {
        remote_errno = remote_recv_errno();
    } else {
        remote_recv_data(buf, sizeof(*buf));
    }
    return ret;
}

static int call_remote_close(int fildes) {
    remote_ensure();
    remote_send_syscall(RC_CLOSE);
    remote_send_int(fildes);
    int ret = remote_recv_int();
    if (ret == -1) {
        remote_errno = remote_recv_errno();
    }
    return ret;
}

static int call_remote_read(int fildes, void *buf, size_t nbyte) {
    remote_ensure();
    remote_send_syscall(RC_READ);
    remote_send_int(fildes);
    remote_send_size_t(nbyte);
    int ret = remote_recv_int();
    if (ret == -1) {
        remote_errno = remote_recv_errno();
    } else if (ret > 0) {
        remote_recv_data(buf, ret);
    }
    return ret;
}

void prepare_local_paths() {
    char *paths = getenv("LOCAL_PATHS");

    local_paths_count = 0;
    if (!paths)
        return;

    local_paths = strdup(paths);
    size_t len = strlen(local_paths);

    local_paths_count++;
    for (size_t i=0; i<len; i++) {
        if (local_paths[i] == ':') {
            local_paths_count++;
        }
    }

    local_paths_array = calloc(local_paths_count, sizeof(*local_paths_array));
    local_paths_len = calloc(local_paths_count, sizeof(*local_paths_len));

    int path_num = 1;
    local_paths_array[0] = local_paths;
    for (size_t i=0; i<len; i++) {
        if (local_paths[i] == ':') {
            local_paths[i] = 0;
            i++;
            if (local_paths[i] != 0) {
                local_paths_array[path_num] = local_paths + i;
                local_paths_len[path_num-1] = local_paths_array[path_num] - local_paths_array[path_num-1] -1;
                path_num++;
            }
        }
    }
    local_paths_len[path_num-1] = local_paths + len - local_paths_array[path_num-1];
}

static bool is_local_path(const char* pathname) {
printf("opening: %s\n", pathname);
    for (int i=0; i<local_paths_count; i++) {
        if (strncmp(pathname, local_paths_array[i], local_paths_len[i]) == 0 && (
                local_paths_array[i][local_paths_len[i]] == '/' ||
                local_paths_array[i][local_paths_len[i]] == 0)) {
            printf("it's local\n");
            return true;
        }
    }
    printf("it's remote\n");
    return false;
}

int open(const char *pathname, int flags, ...) {
    static orig_open_f_type orig_open = NULL;
    if (orig_open == NULL) {
        orig_open = (orig_open_f_type)dlsym(RTLD_NEXT, "open");
        prepare_local_paths();
    }

    if (is_local_path(pathname)) {
        // local part
        return orig_open(pathname, flags);
    } else {
        // remote part
        int remote_fd_idx = find_free_remote_fd();
        if (remote_fd_idx == -1) {
            errno = ENFILE;
            return -1;
        }

        int remote_fd = call_remote_open(pathname, flags);
        if (remote_fd == -1) {
            errno = remote_errno;
            return -1;
        }

        remote_fds[remote_fd_idx] = remote_fd;
        return remote_fd_idx + REMOTE_FD_SHIFT;
    }
}

int __fxstat(int ver, int fildes, struct stat *buf) {
    static orig_fxstat_f_type orig_fxstat = NULL;
    if (orig_fxstat == NULL) {
        orig_fxstat = (orig_fxstat_f_type)dlsym(RTLD_NEXT, "__fxstat");
    }

    if (fildes < REMOTE_FD_SHIFT) {
        // local part
        return orig_fxstat(ver, fildes, buf);
    } else {
        // remote part
        int remote_fd = remote_fds[fildes - REMOTE_FD_SHIFT];
        int ret = call_remote_fstat(remote_fd, buf);
        if (ret == -1) {
            errno = remote_errno;
        }
        return ret;
    }
}

int close(int fildes) {
    static orig_close_f_type orig_close = NULL;
    if (orig_close == NULL) {
        orig_close = (orig_close_f_type)dlsym(RTLD_NEXT, "close");
    }

    if (fildes < REMOTE_FD_SHIFT) {
        // local part
        return orig_close(fildes);
    } else {
        // remote part
        int remote_fd = remote_fds[fildes - REMOTE_FD_SHIFT];
        int ret = call_remote_close(remote_fd);
        if (ret == -1) {
            errno = remote_errno;
        } else {
            remote_fds[fildes - REMOTE_FD_SHIFT] = 0;
        }
        return ret;
    }
}

ssize_t read(int fildes, void *buf, size_t nbyte) {
    static orig_read_f_type orig_read = NULL;
    if (orig_read == NULL) {
        orig_read = (orig_read_f_type)dlsym(RTLD_NEXT, "read");
    }

    if (fildes < REMOTE_FD_SHIFT) {
        // local part
        return orig_read(fildes, buf, nbyte);
    } else {
        // remote part
        int remote_fd = remote_fds[fildes - REMOTE_FD_SHIFT];
        int ret = call_remote_read(remote_fd, buf, nbyte);
        if (ret == -1) {
            errno = remote_errno;
        }
        return ret;
    }
}
