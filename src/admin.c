#define _GNU_SOURCE
#include "../include/banking.h"
#include "../include/locks.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>

/* Add account: writes a new CSV line with incremental id */
int admin_add_account(const char *name, const char *pin, int role, char *out, size_t out_n) {
    if (!name || !pin) {
        snprintf(out, out_n, "ERR|Invalid input");
        return 0;
    }

    FILE *fp = fopen(ACC_FILE, "r+");
    if (!fp) {
        snprintf(out, out_n, "ERR|Open failed");
        return 0;
    }

    int fd = fileno(fp);
    if (fd < 0) {
        fclose(fp);
        snprintf(out, out_n, "ERR|fileno failed");
        return 0;
    }

    if (lock_file(fd, 1) != 0) {
        fclose(fp);
        snprintf(out, out_n, "ERR|Lock failed");
        return 0;
    }

    char line[512];
    if (!fgets(line, sizeof(line), fp)) {
        unlock_file(fd);
        fclose(fp);
        snprintf(out, out_n, "ERR|Read header failed");
        return 0;
    }

    Account *arr = NULL;
    size_t cap = 0, cnt = 0;
    int maxid = 10000; // base starting id for customers

    while (fgets(line, sizeof(line), fp)) {
        Account a;
        if (sscanf(line, "%d,%49[^,],%15[^,],%lf,%d,%d",
                   &a.id, a.name, a.pin, &a.balance, &a.role, &a.active) == 6) {
            if (cnt >= cap) {
                cap = cap ? cap * 2 : 32;
                arr = realloc(arr, cap * sizeof(Account));
            }
            arr[cnt++] = a;
            if (a.id > maxid) maxid = a.id;
            if (strcmp(a.name, name) == 0) {
                free(arr);
                unlock_file(fd);
                fclose(fp);
                snprintf(out, out_n, "ERR|Duplicate name");
                return 0;
            }
        }
    }
    
    int new_id = maxid + 1;
    Account newacc = {
        .id = new_id,
        .role = role,
        .active = 1,
        .balance = 0.0
    };
    strncpy(newacc.name, name, sizeof(newacc.name)-1);
    strncpy(newacc.pin, pin, sizeof(newacc.pin)-1);
    newacc.name[sizeof(newacc.name)-1] = '\0';
    newacc.pin[sizeof(newacc.pin)-1] = '\0';

    // write to tmp file
    char tmpname[256];
    snprintf(tmpname, sizeof(tmpname), "%s.tmp", ACC_FILE);
    FILE *tf = fopen(tmpname, "w");
    if (!tf) {
        free(arr);
        unlock_file(fd);
        fclose(fp);
        snprintf(out, out_n, "ERR|Temp open failed");
        return 0;
    }

    fprintf(tf, "id,name,pin,balance,role,active\n");
    for (size_t i = 0; i < cnt; i++) {
        fprintf(tf, "%d,%s,%s,%.2f,%d,%d\n",
                arr[i].id, arr[i].name, arr[i].pin,
                arr[i].balance, arr[i].role, arr[i].active);
    }
    fprintf(tf, "%d,%s,%s,%.2f,%d,%d\n",
            newacc.id, newacc.name, newacc.pin,
            newacc.balance, newacc.role, newacc.active);

    fflush(tf);
    int tfd = fileno(tf);
    if (tfd >= 0) fsync(tfd);
    fclose(tf);

    if (rename(tmpname, ACC_FILE) != 0) {
        free(arr);
        unlock_file(fd);
        fclose(fp);
        snprintf(out, out_n, "ERR|Replace failed");
        return 0;
    }
    unlock_file(fd);
    fclose(fp);
    free(arr);

    snprintf(out, out_n, "OK|Account created id=%d", new_id);
    return 1;
}

int admin_add_employee(const char *name, const char *pin, char *out, size_t out_n) {
    return admin_add_account(name, pin, 3, out, out_n);
}

int admin_list_accounts(char *out, size_t out_n) {
    FILE *fp = fopen(ACC_FILE, "r");
    if (!fp) { snprintf(out,out_n,"ERR|Open accounts failed"); return 0; }
    int fd = fileno(fp);
    if (fd < 0) { fclose(fp); snprintf(out,out_n,"ERR|fileno failed"); return 0; }

    if (lock_file(fd, 0) != 0) { fclose(fp); snprintf(out,out_n,"ERR|Lock failed"); return 0; }

    char line[512];
    size_t pos = 0;
    while (fgets(line, sizeof(line), fp)) {
        pos += snprintf(out+pos, (out_n>pos?out_n-pos:0), "%s", line);
        if (pos > out_n - 200) break;
    }

    unlock_file(fd);
    fclose(fp);
    return 1;
}

int admin_delete_account(acc_id_t id, char *resp, size_t n) {
    FILE *fp = fopen(ACC_FILE, "r");
    if (!fp) { snprintf(resp, n, "ERR|Unable to open account file"); return 0; }
    int fd = fileno(fp);
    if (fd < 0) { fclose(fp); snprintf(resp,n,"ERR|fileno"); return 0; }
    if (lock_file(fd, 0) != 0) { fclose(fp); snprintf(resp,n,"ERR|Lock failed"); return 0; }

    char tmpname[] = "data/tmp_accounts.csv";
    FILE *tmp = fopen(tmpname, "w");
    if (!tmp) { unlock_file(fd); fclose(fp); snprintf(resp, n, "ERR|Temp file error"); return 0; }

    char line[512];
    int found = 0;

    /* copy header and rows except deleted id */
    while (fgets(line, sizeof(line), fp)) {
        acc_id_t cid; char name[50], pin[16]; double bal; int role, active;
        if (sscanf(line, "%d,%49[^,],%15[^,],%lf,%d,%d",
                   &cid, name, pin, &bal, &role, &active) == 6) {
            if (cid == id) { found = 1; continue; }
            fprintf(tmp, "%d,%s,%s,%.2f,%d,%d\n", cid, name, pin, bal, role, active);
        } else {
            /* write header or malformed line as-is */
            fputs(line, tmp);
        }
    }

    fflush(tmp);
    fclose(tmp);
    unlock_file(fd);
    fclose(fp);

    /* rename safely */
    if (rename(tmpname, ACC_FILE) != 0) {
        snprintf(resp, n, "ERR|Replace failed");
        return 0;
    }

    if (found)
        snprintf(resp, n, "OK|Account %d deleted", id);
    else
        snprintf(resp, n, "ERR|Account %d not found", id);
    return found;
}

int admin_modify_account(acc_id_t id, const char *new_name, const char *new_pin, char *resp, size_t n) {
    FILE *fp = fopen(ACC_FILE, "r");
    if (!fp) { snprintf(resp, n, "ERR|Unable to open account file"); return 0; }
    int fd = fileno(fp);
    if (fd < 0) { fclose(fp); snprintf(resp,n,"ERR|fileno"); return 0; }
    if (lock_file(fd, 0) != 0) { fclose(fp); snprintf(resp,n,"ERR|Lock failed"); return 0; }

    char tmpname[] = "data/tmp_accounts.csv";
    FILE *tmp = fopen(tmpname, "w");
    if (!tmp) { unlock_file(fd); fclose(fp); snprintf(resp, n, "ERR|Temp file error"); return 0; }

    char line[512];
    int found = 0;

    while (fgets(line, sizeof(line), fp)) {
        acc_id_t cid; char name[50], pin[16]; double bal; int role, active;
        if (sscanf(line, "%d,%49[^,],%15[^,],%lf,%d,%d",
                   &cid, name, pin, &bal, &role, &active) == 6) {
            if (cid == id) {
                found = 1;
                fprintf(tmp, "%d,%s,%s,%.2f,%d,%d\n", cid, new_name, new_pin, bal, role, active);
            } else {
                fprintf(tmp, "%d,%s,%s,%.2f,%d,%d\n", cid, name, pin, bal, role, active);
            }
        } else {
            fputs(line, tmp);
        }
    }

    fflush(tmp);
    fclose(tmp);
    unlock_file(fd);
    fclose(fp);

    if (rename(tmpname, ACC_FILE) != 0) {
        snprintf(resp, n, "ERR|Replace failed");
        return 0;
    }

    if (found)
        snprintf(resp, n, "OK|Account %d modified", id);
    else
        snprintf(resp, n, "ERR|Account %d not found", id);
    return found;
}

int admin_search_account(acc_id_t id, char *out, size_t out_n) {
    FILE *fp = fopen(ACC_FILE, "r");
    if (!fp) { snprintf(out,out_n,"ERR|Open failed"); return 0; }
    int fd = fileno(fp);
    if (fd < 0) { fclose(fp); snprintf(out,out_n,"ERR|fileno failed"); return 0; }
    if (lock_file(fd, 0) != 0) { fclose(fp); snprintf(out,out_n,"ERR|Lock failed"); return 0; }

    char line[512];
    /* skip header if present */
    fgets(line, sizeof(line), fp);
    while (fgets(line, sizeof(line), fp)) {
        Account a;
        if (sscanf(line, "%d,%49[^,],%15[^,],%lf,%d,%d",
                   &a.id, a.name, a.pin, &a.balance, &a.role, &a.active) == 6) {
            if (a.id == id) {
                snprintf(out,out_n,"OK|ID:%d Name:%s Bal:%.2f Active:%d Role:%d",
                         a.id, a.name, a.balance, a.active, a.role);
                unlock_file(fd);
                fclose(fp);
                return 1;
            }
        }
    }

    unlock_file(fd);
    fclose(fp);
    snprintf(out,out_n,"ERR|Not found");
    return 0;
}