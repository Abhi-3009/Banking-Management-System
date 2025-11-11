#define _GNU_SOURCE
#include "../include/banking.h"
#include "../include/locks.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>

// view account details
int employee_view_account(acc_id_t id, char *out, size_t out_n) {
    FILE *fp = fopen(ACC_FILE, "r");
    if (!fp) { snprintf(out,out_n,"ERR|Open failed"); return 0; }
    int fd = fileno(fp);
    if (fd < 0) { fclose(fp); snprintf(out,out_n,"ERR|fileno"); return 0; }
    if (lock_file(fd,0) != 0) { fclose(fp); snprintf(out,out_n,"ERR|Lock failed"); return 0; }

    char line[512];
    fgets(line,sizeof(line),fp);
    int found=0;
    while (fgets(line,sizeof(line),fp)) {
        Account a;
        if (sscanf(line, "%d,%49[^,],%15[^,],%lf,%d,%d",
                   &a.id, a.name, a.pin, &a.balance, &a.role, &a.active) == 6) {
            if (a.id == id) {
                snprintf(out,out_n,"OK|ID:%d Name:%s Bal:%.2f Active:%d Role:%d",
                         a.id, a.name, a.balance, a.active, a.role);
                found = 1; break;
            }
        }
    }

    unlock_file(fd);
    fclose(fp);
    if (!found) { snprintf(out,out_n,"ERR|Not found"); return 0; }
    return 1;
}

// list all customers
int employee_list_customers(char *out, size_t out_n) {
    FILE *fp = fopen(ACC_FILE, "r");
    if (!fp) { snprintf(out,out_n,"ERR|Open failed"); return 0; }
    int fd = fileno(fp);
    if (fd < 0) { fclose(fp); snprintf(out,out_n,"ERR|fileno"); return 0; }
    if (lock_file(fd,0) != 0) { fclose(fp); snprintf(out,out_n,"ERR|Lock failed"); return 0; }

    char line[512]; size_t pos=0;
    while (fgets(line,sizeof(line),fp)) {
        pos += snprintf(out+pos, out_n>pos?out_n-pos:0, "%s", line);
        if (pos > out_n - 200) break;
    }

    unlock_file(fd);
    fclose(fp);
    return 1;
}

// list all loans
int employee_list_loans(char *out, size_t out_n) {
    extern int loan_list_all(char*, size_t);
    return loan_list_all(out, out_n);
}