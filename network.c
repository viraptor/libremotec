#include "network.h"

#include <stdio.h>
#include <string.h>
#include <error.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <arpa/inet.h>

#define GEN_RECV_GENERIC(TYPE, NAME) \
    TYPE remote_recv_##NAME() { \
        TYPE res = 0; \
        int ret = recv_all(remote_conn, &res, sizeof(res), 0); \
        if (ret == -1) { \
            perror("receiving " #NAME " failed"); \
            exit(EXIT_FAILURE); \
        } \
        return res; \
    }

#define GEN_SEND_GENERIC(TYPE, NAME) \
    void remote_send_##NAME(TYPE arg) { \
        int ret = send(remote_conn, &arg, sizeof(arg), 0); \
        if (ret == -1) { \
            perror("sending " #NAME " argument failed"); \
            exit(EXIT_FAILURE); \
        } \
    }

static int remote_conn = -1;

static ssize_t recv_all(int socket, void *buffer, size_t length, int flags) {
    char *addressable = buffer;
    size_t total = 0;
    while (total < length) {
        int ret = recv(socket, addressable + total, length - total, flags);
        if (ret == -1) {
            return ret;
        } else if (ret == 0) {
            error(EXIT_FAILURE, 0, "connection closed");
        }
        total += ret;
    }
    return total;
}

GEN_SEND_GENERIC(rc_type, syscall)
GEN_SEND_GENERIC(int, int)
GEN_SEND_GENERIC(size_t, size_t)

GEN_RECV_GENERIC(rc_type, syscall)
GEN_RECV_GENERIC(size_t, size_t)
GEN_RECV_GENERIC(int, int)

void remote_ensure() {
    if (remote_conn != -1) {
        return;
    }

    remote_conn = socket(AF_INET, SOCK_STREAM, 0);
    if (remote_conn == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    const char *host = getenv("REMOTE_SERVER");
    if (!host) {
        error(1, 0, "fatal: REMOTE_SERVER variable not set");
    }
    struct sockaddr_in server_addr = {.sin_family = AF_INET, .sin_port = htons(12345)};
    inet_pton(AF_INET, host, &server_addr.sin_addr);
    if (connect(remote_conn, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
        perror("connect failed");
        exit(EXIT_FAILURE);
    }
}

void remote_listen() {
    if (remote_conn != -1) {
        printf("already listening - fail!\n");
        exit(EXIT_FAILURE);
    }

    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    const struct sockaddr_in server_addr = {.sin_family = AF_INET, .sin_port = htons(12345), .sin_addr = {.s_addr = htonl(INADDR_ANY)}};
    if (bind(listen_sock, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(listen_sock, 0) == -1) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    if ((remote_conn = accept(listen_sock, NULL, NULL)) == -1) {
        perror("accept failed");
        exit(EXIT_FAILURE);
    }
}

void remote_send_errno(int arg) {
    remote_send_int(arg);
}

void remote_send_string(const char *string) {
    size_t len = strlen(string);
    remote_send_size_t(len+1);

    int ret = send(remote_conn, string, len+1, 0);
    if (ret == -1) {
        perror("syscall string argument failed");
        exit(EXIT_FAILURE);
    }
}

void remote_send_data(void *src, size_t len) {
    int ret = send(remote_conn, src, len, 0);
    if (ret == -1) {
        perror("sending data failed");
        exit(EXIT_FAILURE);
    }
}

char* remote_recv_string() {
    size_t len = remote_recv_size_t();
    char *res = malloc(len);
    int ret = recv_all(remote_conn, res, len, 0);
    if (ret == -1) {
        free(res);
        perror("receiving string failed");
        exit(EXIT_FAILURE);
    }
    return res;
}

void remote_recv_data(void *dest, size_t len) {
    int ret = recv_all(remote_conn, dest, len, 0);
    if (ret == -1) {
        perror("receiving data failed");
        exit(EXIT_FAILURE);
    }
}

int remote_recv_errno() {
    return remote_recv_int();
}
