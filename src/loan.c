#define _GNU_SOURCE
#include "../include/banking.h"
#include "../include/locks.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/file.h>

static const char *LOAN_ASSIGN_FILE = "data/loan_assignments.csv";

int loan_assign(int loan_id, int emp_id, char *resp, size_t n);
int loan_approve_by_employee(int loan_id, int emp_id, char *resp, size_t n);
int loan_reject_by_employee(int loan_id, int emp_id, char *resp, size_t n);

static int loan_approve_core(int loan_id, char *resp, size_t n);
static int loan_reject_core(int loan_id, char *resp, size_t n);

static int next_txn_id() {
    FILE *fp = fopen(TXN_FILE, "r");
    if (!fp) return 1000;
    char line[512]; int last = 999;
    while (fgets(line, sizeof(line), fp)) {
        int tid = 0;
        if (sscanf(line, "%d,", &tid) == 1)
            if (tid > last) last = tid;
    }
    fclose(fp);
    return last + 1;
}

static int append_txn_record(int acc_id, const char *type, double amount, int other_acc) {
    FILE *fp = fopen(TXN_FILE, "a");
    if (!fp) {
        return 0;
    }
    int fd = fileno(fp);
    if (fd < 0) { fclose(fp); return 0; }
    if (lock_file(fd, 1) != 0) { fclose(fp); return 0; }

    int tid = next_txn_id();
    char timestr[64];
    now_iso8601(timestr, sizeof(timestr));
    fprintf(fp, "%d,%d,%s,%.2f,%d,%s\n", tid, acc_id, type, amount, other_acc, timestr);
    fflush(fp);

    unlock_file(fd);
    fclose(fp);
    return 1;
}

static int update_account_balance(int acc_id, double delta) {
    FILE *fp = fopen(ACC_FILE, "r");
    if (!fp) return 0;
    int fd = fileno(fp);
    if (fd < 0) { fclose(fp); return 0; }
    if (lock_file(fd, 1) != 0) { fclose(fp); return 0; }

    char line[512];
    if (!fgets(line, sizeof(line), fp)) { unlock_file(fd); fclose(fp); return 0; }

    Account *arr = NULL;
    size_t cap = 0, cnt = 0;
    while (fgets(line, sizeof(line), fp)) {
        Account a;
        if (sscanf(line, "%d,%49[^,],%15[^,],%lf,%d,%d",
                   &a.id, a.name, a.pin, &a.balance, &a.role, &a.active) == 6) {
            if (cnt >= cap) { cap = cap ? cap * 2 : 64; arr = realloc(arr, cap * sizeof(Account)); }
            arr[cnt++] = a;
        }
    }

    int found = 0;
    for (size_t i = 0; i < cnt; ++i) {
        if (arr[i].id == acc_id) {
            arr[i].balance += delta;
            found = 1;
            break;
        }
    }

    char tmp[256]; snprintf(tmp, sizeof(tmp), "%s.tmp", ACC_FILE);
    FILE *tf = fopen(tmp, "w");
    if (!tf) { free(arr); unlock_file(fd); fclose(fp); return 0; }
    fprintf(tf, "id,name,pin,balance,role,active\n");
    for (size_t i = 0; i < cnt; ++i) {
        fprintf(tf, "%d,%s,%s,%.2f,%d,%d\n",
                arr[i].id, arr[i].name, arr[i].pin, arr[i].balance, arr[i].role, arr[i].active);
    }
    fflush(tf); fclose(tf);
    unlock_file(fd);
    fclose(fp);
    free(arr);

    if (rename(tmp, ACC_FILE) != 0) return 0;
    return found ? 1 : 0;
}


int loan_assign(int loan_id, int emp_id, char *resp, size_t n) {
    FILE *fp = fopen(LOAN_ASSIGN_FILE, "a");
    if (!fp) {
        fp = fopen(LOAN_ASSIGN_FILE, "w");
        if (!fp) { snprintf(resp,n,"ERR|Cannot open assignments file"); return 0; }
    }
    flock(fileno(fp), LOCK_EX);
    fprintf(fp, "%d,%d\n", loan_id, emp_id);
    fflush(fp);
    flock(fileno(fp), LOCK_UN);
    fclose(fp);
    snprintf(resp, n, "OK|Loan %d assigned to employee %d", loan_id, emp_id);
    return 1;
}

static int loan_is_assigned_to(int loan_id, int emp_id) {
    FILE *fp = fopen(LOAN_ASSIGN_FILE, "r");
    if (!fp) return 0;
    flock(fileno(fp), LOCK_SH);

    char line[256]; int found = 0;
    while (fgets(line, sizeof(line), fp)) {
        int lid=-1, eid=-1;
        if (sscanf(line, "%d,%d", &lid, &eid) == 2) {
            if (lid == loan_id && eid == emp_id) { found = 1; break; }
        }
    }
    flock(fileno(fp), LOCK_UN);
    fclose(fp);
    return found;
}

int loan_approve_by_employee(int loan_id, int emp_id, char *resp, size_t n) {
    if (!loan_is_assigned_to(loan_id, emp_id)) {
        snprintf(resp, n, "ERR|Loan %d not assigned to employee %d", loan_id, emp_id);
        return 0;
    }
    return loan_approve_core(loan_id, resp, n);
}

int loan_reject_by_employee(int loan_id, int emp_id, char *resp, size_t n) {
    if (!loan_is_assigned_to(loan_id, emp_id)) {
        snprintf(resp, n, "ERR|Loan %d not assigned to employee %d", loan_id, emp_id);
        return 0;
    }
    return loan_reject_core(loan_id, resp, n);
}

static void append_loan_record(Loan *l) {
    FILE *fp = fopen(LOAN_FILE, "a");
    if (!fp) return;
    flock(fileno(fp), LOCK_EX);
    fprintf(fp, "%d,%d,%.2f,%.2f,%d,%d,%s\n",
            l->loan_id, l->acc_id, l->amount, l->interest,
            l->months, l->status, l->created_at);
    flock(fileno(fp), LOCK_UN);
    fclose(fp);
}

static int next_loan_id() {
    FILE *fp = fopen(LOAN_FILE, "r");
    if (!fp) return 7001;
    char line[256]; int last = 7000;
    while (fgets(line, sizeof(line), fp)) {
        int lid = 0;
        if (sscanf(line, "%d,", &lid) == 1)
            if (lid > last) last = lid;
    }
    fclose(fp);
    return last + 1;
}

int loan_apply(acc_id_t acc_id, double amount, double interest, int months, char *resp, size_t n) {
    if (amount <= 0 || months <= 0) {
        snprintf(resp, n, "ERR|Invalid loan details");
        return 0;
    }

    Loan l = {0};
    l.loan_id = next_loan_id();
    l.acc_id = acc_id;
    l.amount = amount;
    l.interest = interest;
    l.months = months;
    l.status = 0; // pending

    now_iso8601(l.created_at, sizeof(l.created_at));
    append_loan_record(&l);

    snprintf(resp, n, "OK|Loan %d applied successfully", l.loan_id);
    return 1;
}

int loan_list_all(char *resp, size_t n) {
    FILE *fp = fopen(LOAN_FILE, "r");
    if (!fp) {
        snprintf(resp, n, "ERR|Cannot open %s", LOAN_FILE);
        return 0;
    }

    char line[256];
    size_t used = snprintf(resp, n, "Loan_ID,Account_ID,Amount,Interest,Months,Status,Created_At\n");

    while (fgets(line, sizeof(line), fp)) {
        if (used + strlen(line) >= n) break;
        used += snprintf(resp + used, n - used, "%s", line);
    }

    fclose(fp);
    return 1;
}

static int loan_approve_core(int loan_id, char *resp, size_t n) {
    FILE *fp = fopen(LOAN_FILE, "r+");
    if (!fp) { snprintf(resp, n, "ERR|Cannot open file"); return 0; }

    FILE *temp = fopen("data/temp_loans.csv", "w");
    if (!temp) { fclose(fp); snprintf(resp, n, "ERR|Temp create fail"); return 0; }

    char line[256]; int found = 0;
    int target_acc = -1;
    double target_amt = 0.0;

    flock(fileno(fp), LOCK_EX);

    while (fgets(line, sizeof(line), fp)) {
        int id, acc, months, status;
        double amt, intr;
        char created[64];

        if (sscanf(line, "%d,%d,%lf,%lf,%d,%d,%63[^\n]",
                   &id, &acc, &amt, &intr, &months, &status, created) == 7) {
            if (id == loan_id) {
                found = 1;
                target_acc = acc;
                target_amt = amt;
                fprintf(temp, "%d,%d,%.2f,%.2f,%d,1,%s\n", id, acc, amt, intr, months, created);
            } else {
                fputs(line, temp);
            }
        } else {
            fputs(line, temp);
        }
    }

    flock(fileno(fp), LOCK_UN);
    fclose(fp);
    fclose(temp);
    rename("data/temp_loans.csv", LOAN_FILE);

    if (!found) {
        snprintf(resp, n, "ERR|Loan not found");
        return 0;
    }
    int acc_ok = update_account_balance(target_acc, target_amt);
    int tx_ok = append_txn_record(target_acc, "LOAN_CREDIT", target_amt, 0);

    if (!acc_ok || !tx_ok) {
        if (!acc_ok && !tx_ok)
            snprintf(resp, n, "OK|Loan %d approved|WARN|Account credit & txn failed", loan_id);
        else if (!acc_ok)
            snprintf(resp, n, "OK|Loan %d approved|WARN|Account credit failed", loan_id);
        else
            snprintf(resp, n, "OK|Loan %d approved|WARN|Txn record failed", loan_id);
        return 1;
    }

    snprintf(resp, n, "OK|Loan %d approved", loan_id);
    return 1;
}

static int loan_reject_core(int loan_id, char *resp, size_t n) {
    FILE *fp = fopen(LOAN_FILE, "r+");
    if (!fp) { snprintf(resp, n, "ERR|Cannot open file"); return 0; }

    FILE *temp = fopen("data/temp_loans.csv", "w");
    if (!temp) { fclose(fp); snprintf(resp, n, "ERR|Temp create fail"); return 0; }

    char line[256]; int found = 0;
    flock(fileno(fp), LOCK_EX);

    while (fgets(line, sizeof(line), fp)) {
        int id, acc, months, status;
        double amt, intr;
        char created[64];

        if (sscanf(line, "%d,%d,%lf,%lf,%d,%d,%63[^\n]", &id, &acc, &amt, &intr, &months, &status, created) == 7) {
            if (id == loan_id) {
                found = 1;
                fprintf(temp, "%d,%d,%.2f,%.2f,%d,2,%s\n", id, acc, amt, intr, months, created);
            } else {
                fputs(line, temp);
            }
        }
    }

    flock(fileno(fp), LOCK_UN);
    fclose(fp);
    fclose(temp);
    rename("data/temp_loans.csv", LOAN_FILE);

    snprintf(resp, n, found ? "OK|Loan %d rejected" : "ERR|Loan not found", loan_id);
    return found;
}