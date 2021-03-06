#define _GNU_SOURCE
#include <dlfcn.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "network.h"

#define REMOTE_MAX_OPEN 10
#define REMOTE_FD_SHIFT 2048

static int remote_fds[REMOTE_MAX_OPEN] = {0};
static int remote_errno;

static int local_paths_count;
static char* local_paths = NULL;
static char** local_paths_array = NULL;
static int* local_paths_len = NULL;

int (*orig_open)(const char *pathname, int flags);
int (*orig_fxstat)(int ver, int fildes, struct stat *buf);
int (*orig_lxstat)(int ver, const char *path, struct stat *buf);
int (*orig_close)(int fildes);
int (*orig_read)(int fildes, void *buf, size_t nbyte);
off_t (*orig_lseek)(int fildes, off_t offset, int whence);
int (*orig_faccessat)(int fd, const char *path, int amode, int flag);
int (*orig_getxattr)(const char *path, const char *name, void *value, size_t size);

static bool debug;

static void __attribute__((constructor)) initialise() {
    if (getenv("LIBREMOTEC_DEBUG"))
        debug = true;
    else
        debug = false;

    orig_open = dlsym(RTLD_NEXT, "open");
    orig_fxstat = dlsym(RTLD_NEXT, "__fxstat");
    orig_lxstat = dlsym(RTLD_NEXT, "__lxstat");
    orig_close = dlsym(RTLD_NEXT, "close");
    orig_read = dlsym(RTLD_NEXT, "read");
    orig_lseek = dlsym(RTLD_NEXT, "lseek");
    orig_faccessat = dlsym(RTLD_NEXT, "faccessat");
    orig_getxattr = dlsym(RTLD_NEXT, "getxattr");
}

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

static int call_remote_lstat(const char *path, struct stat *buf) {
    remote_ensure();
    remote_send_syscall(RC_LSTAT);
    remote_send_string(path);
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

static off_t call_remote_lseek(int fildes, off_t offset, int whence) {
    remote_ensure();
    remote_send_syscall(RC_LSEEK);
    remote_send_int(fildes);
    remote_send_off_t(offset);
    remote_send_int(whence);
    off_t ret = remote_recv_off_t();
    if (ret == -1) {
        remote_errno = remote_recv_errno();
    }
    return ret;
}

static int call_remote_faccessat(int fd, const char *path, int amode, int flag) {
    remote_ensure();
    remote_send_syscall(RC_FACCESSAT);
    remote_send_int(fd);
    remote_send_string(path);
    remote_send_int(amode);
    remote_send_int(flag);
    int ret = remote_recv_int();
    if (ret == -1) {
        remote_errno = remote_recv_errno();
    }
    return ret;
}

static int call_remote_getxattr(const char *path, const char *name, void *value, size_t size) {
    remote_ensure();
    remote_send_syscall(RC_GETXATTR);
    remote_send_string(path);
    remote_send_string(name);
    remote_send_size_t(size);
    int ret = remote_recv_int();
    if (ret == -1) {
        remote_errno = remote_recv_errno();
    } else {
        remote_recv_data(value, size);
    }
    return ret;
}

void prepare_local_paths() {
    // initialise only once
    if (local_paths_array)
        return;

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
    prepare_local_paths();

    for (int i=0; i<local_paths_count; i++) {
        if (strncmp(pathname, local_paths_array[i], local_paths_len[i]) == 0 && (
                local_paths_array[i][local_paths_len[i]] == '/' ||
                local_paths_array[i][local_paths_len[i]] == 0)) {
            return true;
        }
    }
    return false;
}

int open(const char *pathname, int flags, ...) {
    if (is_local_path(pathname)) {
        // local part
        return orig_open(pathname, flags);
    } else {
        if (debug) {
            fprintf(stderr, "open() on remote file %s\n", pathname);
        }

        // remote part
        int remote_fd_idx = find_free_remote_fd();
        if (remote_fd_idx == -1) {
            errno = ENFILE;
            return -1;
        }

        int remote_fd = call_remote_open(pathname, flags);
        if (remote_fd == -1) {
            if (debug) {
                fprintf(stderr, "failed opening %s\n", pathname);
            }
            errno = remote_errno;
            return -1;
        }

        if (debug) {
            fprintf(stderr, "opened %s, local %i, remote %i\n", pathname, remote_fd_idx + REMOTE_FD_SHIFT, remote_fd);
        }
        remote_fds[remote_fd_idx] = remote_fd;
        return remote_fd_idx + REMOTE_FD_SHIFT;
    }
}

int __fxstat(int ver, int fildes, struct stat *buf) {
    if (fildes < REMOTE_FD_SHIFT) {
        // local part
        return orig_fxstat(ver, fildes, buf);
    } else {
        if (debug) {
            fprintf(stderr, "fstat() on remote fd %i\n", fildes);
        }
        // remote part
        int remote_fd = remote_fds[fildes - REMOTE_FD_SHIFT];
        int ret = call_remote_fstat(remote_fd, buf);
        if (ret == -1) {
            errno = remote_errno;
        }
        return ret;
    }
}

int __lxstat(int ver, const char *path, struct stat *buf) {
    if (is_local_path(path)) {
        // local part
        return orig_lxstat(ver, path, buf);
    } else {
        if (debug) {
            fprintf(stderr, "lstat() on remote file %s\n", path);
        }
        // remote part
        int ret = call_remote_lstat(path, buf);
        if (ret == -1) {
            errno = remote_errno;
        }
        return ret;
    }
}

int close(int fildes) {
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
    if (fildes < REMOTE_FD_SHIFT) {
        // local part
        return orig_read(fildes, buf, nbyte);
    } else {
        if (debug) {
            fprintf(stderr, "read() %zi bytes on remote fd %i\n", nbyte, fildes);
        }
        // remote part
        int remote_fd = remote_fds[fildes - REMOTE_FD_SHIFT];
        int ret = call_remote_read(remote_fd, buf, nbyte);
        if (ret == -1) {
            errno = remote_errno;
        }
        return ret;
    }
}

off_t lseek(int fildes, off_t offset, int whence) {
    if (fildes < REMOTE_FD_SHIFT) {
        // local part
        return orig_lseek(fildes, offset, whence);
    } else {
        if (debug) {
            fprintf(stderr, "lseek() offset %zi from %i\n", offset, whence);
        }
        // remote part
        int remote_fd = remote_fds[fildes - REMOTE_FD_SHIFT];
        off_t ret = call_remote_lseek(remote_fd, offset, whence);
        if (ret == -1) {
            errno = remote_errno;
        }
        return ret;
    }
}

int faccessat(int fd, const char *path, int amode, int flag) {
    // this may be wrong in some scenarios, because we never know which
    // side's AT_FDCWD is expected
    // TODO: actually we can figure that out by making the path absolute...
    if ((fd != AT_FDCWD && fd < REMOTE_FD_SHIFT) || (fd == AT_FDCWD && is_local_path(path))) {
        // local part
        return orig_faccessat(fd, path, amode, flag);
    } else {
        if (debug) {
            fprintf(stderr, "faccessat() fd %i path %s\n", fd, path);
        }
        // remote part
        int remote_fd;
        if (fd != AT_FDCWD) {
            remote_fd = remote_fds[fd - REMOTE_FD_SHIFT];
        } else {
            remote_fd = AT_FDCWD;
        }
        int ret = call_remote_faccessat(remote_fd, path, amode, flag);
        if (ret == -1) {
            errno = remote_errno;
        }
        return ret;
    }
}

int getxattr(const char *path, const char *name, void *value, size_t size) {
    if (is_local_path(path)) {
        // local part
        return orig_getxattr(path, name, value, size);
    } else {
        if (debug) {
            fprintf(stderr, "getxattr() on remote file %s\n", path);
        }
        // remote part
        int ret = call_remote_getxattr(path, name, value, size);
        if (ret == -1) {
            errno = remote_errno;
        }
        return ret;
    }
}
