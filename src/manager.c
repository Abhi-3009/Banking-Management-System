#define _GNU_SOURCE
#include "../include/banking.h"
#include "../include/locks.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>

extern int loan_list_all(char*, size_t);
extern int employee_list_customers(char*, size_t);
extern int employee_view_account(acc_id_t, char*, size_t);
extern int loan_assign(int, int, char*, size_t);

int manager_approve_loan(int loan_id, char *out, size_t out_n) {
    (void)loan_id;
    snprintf(out, out_n, "ERR|Managers cannot approve loans; assign to employee");
    return 0;
}
int manager_reject_loan(int loan_id, char *out, size_t out_n) {
    (void)loan_id;
    snprintf(out, out_n, "ERR|Managers cannot reject loans; assign to employee");
    return 0;
}

int manager_list_loans(char *out, size_t out_n) { return loan_list_all(out, out_n); }
int manager_list_customers(char *out, size_t out_n) { return employee_list_customers(out, out_n); }
int manager_view_account(acc_id_t id, char *out, size_t out_n) { return employee_view_account(id, out, out_n); }

// assign loan to employee
int manager_assign_loan(int loan_id, int emp_id, char *out, size_t out_n) {
    return loan_assign(loan_id, emp_id, out, out_n);
}

// toggle account active/inactive
int manager_toggle_account(acc_id_t id, int active, char *out, size_t out_n) {
    FILE *fp = fopen(ACC_FILE, "r");
    if (!fp) { snprintf(out,out_n,"ERR|Open failed"); return 0; }
    int fd = fileno(fp);
    if (fd < 0) { fclose(fp); snprintf(out,out_n,"ERR|fileno"); return 0; }
    if (lock_file(fd,1) != 0) { fclose(fp); snprintf(out,out_n,"ERR|Lock failed"); return 0; }

    char line[512];
    if (!fgets(line, sizeof(line), fp)) { unlock_file(fd); fclose(fp); snprintf(out,out_n,"ERR|Read failed"); return 0; }

    Account *arr=NULL; size_t cap=0,cnt=0;
    while (fgets(line,sizeof(line),fp)) {
        Account a;
        if (sscanf(line, "%d,%49[^,],%15[^,],%lf,%d,%d",
                   &a.id, a.name, a.pin, &a.balance, &a.role, &a.active) == 6) {
            if (cnt>=cap) { cap = cap?cap*2:32; arr = realloc(arr, cap*sizeof(Account)); }
            arr[cnt++] = a;
        }
    }

    int found=0;
    for (size_t i=0;i<cnt;i++) {
        if (arr[i].id == id) { arr[i].active = active; found = 1; break; }
    }

    char tmp[256]; snprintf(tmp,sizeof(tmp), "%s.tmp", ACC_FILE);
    FILE *tf = fopen(tmp,"w");
    if (!tf) { free(arr); unlock_file(fd); fclose(fp); snprintf(out,out_n,"ERR|Temp write failed"); return 0; }
    fprintf(tf, "id,name,pin,balance,role,active\n");
    for (size_t i=0;i<cnt;i++) {
        fprintf(tf, "%d,%s,%s,%.2f,%d,%d\n",
                arr[i].id, arr[i].name, arr[i].pin, arr[i].balance, arr[i].role, arr[i].active);
    }
    fflush(tf); fclose(tf);

    unlock_file(fd);
    fclose(fp);
    free(arr);
    if (rename(tmp, ACC_FILE) != 0) { snprintf(out,out_n,"ERR|Replace failed"); return 0; }

    if (!found) { snprintf(out,out_n,"ERR|Not found"); return 0; }
    snprintf(out,out_n,"OK|Account %d set active=%d", id, active);
    return 1;
}

// list feedback
int manager_list_feedback(char *out, size_t out_n) {
    FILE *fp = fopen(FB_FILE, "r");
    if (!fp) { snprintf(out,out_n,"No feedbacks\n"); return 0; }
    int fd = fileno(fp);
    if (fd < 0) { fclose(fp); snprintf(out,out_n,"ERR|fileno"); return 0; }
    if (lock_file(fd,0) != 0) { fclose(fp); snprintf(out,out_n,"ERR|Lock failed"); return 0; }

    char line[512]; size_t pos=0;
    while (fgets(line,sizeof(line),fp)) {
        pos += snprintf(out+pos, out_n>pos?out_n-pos:0, "%s", line);
        if (pos > out_n - 200) break;
    }
    unlock_file(fd); fclose(fp);
    return 1;
}