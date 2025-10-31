#ifndef LOCKS_H
#define LOCKS_H

#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>

int lock_file(int fd, int write);
int unlock_file(int fd);

#endif