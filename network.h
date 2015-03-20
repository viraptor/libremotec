#include <errno.h>
#include <sys/types.h>

typedef enum {
    RC_OPEN,
    RC_FSTAT,
    RC_CLOSE,
    RC_READ,
    RC_LSEEK,
} rc_type;

void remote_ensure();
void remote_listen();

void remote_send_syscall(rc_type);
void remote_send_string(const char *);
void remote_send_size_t(size_t);
void remote_send_off_t(off_t);
void remote_send_int(int);
void remote_send_data(void *, size_t);
void remote_send_errno(int);

rc_type remote_recv_syscall();
size_t remote_recv_size_t();
off_t remote_recv_off_t();
int remote_recv_int();
char* remote_recv_string();
void remote_recv_data(void *, size_t);
int remote_recv_errno();
