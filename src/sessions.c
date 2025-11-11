#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "../include/banking.h"
#include "../include/locks.h"

static const char *SESS_FILE = "data/sessions.csv";

int session_is_active(acc_id_t id, const char *role) {
    FILE *fp = fopen(SESS_FILE, "r");
    if (!fp) return 0;
    int fd = fileno(fp);
    if (fd < 0) { fclose(fp); return 0; }
    if (lock_file(fd, 0) != 0) { fclose(fp); return 0; }

    char line[256]; int found = 0;
    while (fgets(line, sizeof(line), fp)) {
        int rid = -1;
        char rrole[64] = {0};
        if (sscanf(line, "%d,%63[^\n]", &rid, rrole) >= 1) {
            if (rid == id && strcmp(rrole, role) == 0) { found = 1; break; }
        }
    }
    unlock_file(fd);
    fclose(fp);
    return found;
}

int session_set_active(acc_id_t id, const char *role) {
    if (session_is_active(id, role)) return 1;

    FILE *fp = fopen(SESS_FILE, "a");
    if (!fp) {
        fp = fopen(SESS_FILE, "w");
        if (!fp) return 0;
    }
    int fd = fileno(fp);
    if (fd < 0) { fclose(fp); return 0; }
    if (lock_file(fd, 1) != 0) { fclose(fp); return 0; }

    fprintf(fp, "%d,%s\n", id, role);
    fflush(fp);

    unlock_file(fd);
    fclose(fp);
    return 1;
}

int session_set_inactive(acc_id_t id, const char *role) {
    FILE *fp = fopen(SESS_FILE, "r");
    if (!fp) return 0;
    int fd = fileno(fp);
    if (fd < 0) { fclose(fp); return 0; }
    if (lock_file(fd, 1) != 0) { fclose(fp); return 0; }

    FILE *tmp = fopen("data/temp_sessions.csv", "w");
    if (!tmp) { unlock_file(fd); fclose(fp); return 0; }

    char line[256]; int found = 0;
    while (fgets(line, sizeof(line), fp)) {
        int rid = -1;
        char rrole[64] = {0};
        if (sscanf(line, "%d,%63[^\n]", &rid, rrole) >= 1) {
            if (rid == id && strcmp(rrole, role) == 0) {
                found = 1;
            } else {
                fputs(line, tmp);
            }
        }
    }

    fflush(tmp);
    fclose(tmp);
    unlock_file(fd);
    fclose(fp);

    if (rename("data/temp_sessions.csv", SESS_FILE) != 0) {}
    return found;
}