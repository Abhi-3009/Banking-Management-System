#include "../include/locks.h"
#include <stdio.h>
#include <sys/file.h>
#include <unistd.h>

int lock_file(int fd, int write) {
    int op = write ? LOCK_EX : LOCK_SH;
    int ret = flock(fd, op);
    if (ret == 0) {
        printf("[LOCK] %s lock acquired on fd=%d (pid=%d)\n",
               write ? "WRITE" : "READ", fd, getpid());
        fflush(stdout);
    } else {
        perror("[LOCK] Failed to acquire lock");
    }
    return ret;
}

int unlock_file(int fd) {
    int ret = flock(fd, LOCK_UN);
    if (ret == 0) {
        printf("[LOCK] Lock released on fd=%d (pid=%d)\n", fd, getpid());
        fflush(stdout);
    } else {
        perror("[LOCK] Failed to release lock");
    }
    return ret;
}