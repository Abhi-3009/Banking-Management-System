#define _GNU_SOURCE
#include "../include/banking.h"
#include "../include/locks.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static void now_str(char *buf, size_t n) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, n, "%Y-%m-%d %H:%M:%S", &tm);
}

// authenticate customer
int customer_auth(acc_id_t id, const char *pin) {
    FILE *fp = fopen(ACC_FILE, "r");
    if (!fp) return 0;
    int fd = fileno(fp);
    if (fd < 0) { fclose(fp); return 0; }
    if (lock_file(fd, 0) != 0) { fclose(fp); return 0; }
    char line[512];
    fgets(line, sizeof(line), fp);
    int ok = 0;
    while (fgets(line, sizeof(line), fp)) {
        Account a;
        if (sscanf(line, "%d,%49[^,],%15[^,],%lf,%d,%d",
                   &a.id, a.name, a.pin, &a.balance, &a.role, &a.active) == 6) {
            if (a.id == id && strcmp(a.pin, pin) == 0 && a.active) { ok = 1; break; }
        }
    }
    unlock_file(fd);
    fclose(fp);
    return ok;
}

// transaction log
static void log_transaction_csv(const Transaction *t) {
    FILE *fp = fopen(TXN_FILE, "a+");
    if (!fp) return;
    int fd = fileno(fp);
    if (fd < 0) { fclose(fp); return; }
    if (lock_file(fd,1) != 0) { fclose(fp); return; }

    int maxid = 9000;
    char line[512];
    rewind(fp);
    if (fgets(line,sizeof(line),fp)) {
        int id; if (sscanf(line, "%d,", &id) == 1 && id > maxid) maxid = id;
    }
    while (fgets(line,sizeof(line),fp)) {
        int id; if (sscanf(line, "%d,", &id) == 1 && id > maxid) maxid = id;
    }
    int nid = maxid + 1;

    fprintf(fp, "%d,%d,%s,%.2f,%d,%s\n",
            nid, t->acc_id, t->type, t->amount, (int)t->other_acc, t->timestamp);
    fflush(fp);

    unlock_file(fd);
    fclose(fp);
}

// read account
int read_account_by_id(acc_id_t id, Account *out) {
    FILE *fp = fopen(ACC_FILE, "r");
    if (!fp) return 0;
    int fd = fileno(fp);
    if (fd < 0) { fclose(fp); return 0; }
    if (lock_file(fd, 0) != 0) { fclose(fp); return 0; }

    char line[512];
    fgets(line, sizeof(line), fp);
    int found = 0;
    while (fgets(line, sizeof(line), fp)) {
        Account a;
        if (sscanf(line, "%d,%49[^,],%15[^,],%lf,%d,%d",
                   &a.id, a.name, a.pin, &a.balance, &a.role, &a.active) == 6) {
            if (a.id == id) { *out = a; found = 1; break; }
        }
    }

    unlock_file(fd);
    fclose(fp);
    return found;
}

// deposit amount
int customer_deposit(acc_id_t id, double amt, char *out, size_t out_n) {
    if (amt <= 0) { snprintf(out, out_n, "ERR|Invalid amount"); return 0; }

    FILE *fp = fopen(ACC_FILE, "r+");
    if (!fp) { snprintf(out, out_n, "ERR|Open failed"); return 0; }
    int fd = fileno(fp);
    if (fd < 0) { fclose(fp); snprintf(out, out_n, "ERR|fileno"); return 0; }

    if (lock_file(fd, 1) != 0) { fclose(fp); snprintf(out, out_n, "ERR|Lock failed"); return 0; }

    char line[512];
    if (!fgets(line, sizeof(line), fp)) {
        unlock_file(fd); fclose(fp); snprintf(out, out_n, "ERR|Read failed"); return 0;
    }

    Account *arr = NULL;
    size_t cnt = 0, cap = 0;
    int matched = 0;

    while (fgets(line, sizeof(line), fp)) {
        Account a = {0};
        size_t L = strlen(line); if (L && (line[L-1]=='\n' || line[L-1]=='\r')) line[L-1]=0;
        int got = sscanf(line, "%d,%49[^,],%15[^,],%lf,%d,%d",
                         &a.id, a.name, a.pin, &a.balance, &a.role, &a.active);
        if (got != 6) {
            continue;
        }
        if (cnt >= cap) { cap = cap ? cap * 2 : 32; arr = realloc(arr, cap * sizeof(Account)); }
        if (a.id == id) {
            if (!a.active) {
                free(arr);
                unlock_file(fd);
                fclose(fp);
                snprintf(out, out_n, "ERR|Inactive");
                return 0;
            }
            a.balance += amt;
            matched = 1;
        }
        arr[cnt++] = a;
    }

    if (!matched) {
        free(arr);
        unlock_file(fd);
        fclose(fp);
        snprintf(out, out_n, "ERR|Account %d not found", id);
        return 0;
    }

    char tmpname[256];
    snprintf(tmpname, sizeof(tmpname), "%s.tmp", ACC_FILE);
    FILE *tf = fopen(tmpname, "w");
    if (!tf) { free(arr); unlock_file(fd); fclose(fp); snprintf(out, out_n, "ERR|Temp open failed"); return 0; }

    fprintf(tf, "id,name,pin,balance,role,active\n");
    for (size_t i = 0; i < cnt; ++i) {
        fprintf(tf, "%d,%s,%s,%.2f,%d,%d\n",
                arr[i].id, arr[i].name, arr[i].pin, arr[i].balance, arr[i].role, arr[i].active);
    }
    fflush(tf);
    int tfd = fileno(tf);
    if (tfd >= 0) fsync(tfd);
    fclose(tf);
    unlock_file(fd);
    fclose(fp);

    if (rename(tmpname, ACC_FILE) != 0) {
        free(arr);
        snprintf(out, out_n, "ERR|Replace failed");
        return 0;
    }

    Transaction t;
    t.acc_id = id;
    strncpy(t.type, "DEPOSIT", sizeof(t.type)-1);
    t.type[sizeof(t.type)-1] = '\0';
    t.amount = amt;
    t.other_acc = 0;
    now_str(t.timestamp, sizeof(t.timestamp));
    log_transaction_csv(&t);

    free(arr);
    snprintf(out, out_n, "OK|Deposit done: +%.2f", amt);
    return 1;
}

// withdraw amount
int customer_withdraw(acc_id_t id, double amt, char *out, size_t out_n) {
    if (amt <= 0) {
        snprintf(out, out_n, "ERR|Invalid amount");
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
        snprintf(out, out_n, "ERR|fileno");
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
        snprintf(out, out_n, "ERR|Read failed");
        return 0;
    }

    Account *arr = NULL;
    size_t cnt = 0, cap = 0;
    int matched = 0;
    double current_balance = 0.0;

    while (fgets(line, sizeof(line), fp)) {
        Account a = {0};
        size_t L = strlen(line);
        if (L && (line[L - 1] == '\n' || line[L - 1] == '\r')) line[L - 1] = 0;

        int got = sscanf(line, "%d,%49[^,],%15[^,],%lf,%d,%d",
                         &a.id, a.name, a.pin, &a.balance, &a.role, &a.active);
        if (got != 6) continue;

        if (cnt >= cap) { cap = cap ? cap * 2 : 32; arr = realloc(arr, cap * sizeof(Account)); }

        if (a.id == id && a.active) {
            if (a.balance < amt) {
                free(arr);
                unlock_file(fd);
                fclose(fp);
                snprintf(out, out_n, "ERR|Insufficient balance (%.2f)", a.balance);
                return 0;
            }
            a.balance -= amt;
            current_balance = a.balance;
            matched = 1;
        }
        arr[cnt++] = a;
    }

    if (!matched) {
        free(arr);
        unlock_file(fd);
        fclose(fp);
        snprintf(out, out_n, "ERR|Account %d not found", id);
        return 0;
    }

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
    for (size_t i = 0; i < cnt; ++i) {
        fprintf(tf, "%d,%s,%s,%.2f,%d,%d\n",
                arr[i].id, arr[i].name, arr[i].pin, arr[i].balance,
                arr[i].role, arr[i].active);
    }
    fflush(tf);
    int tfd = fileno(tf);
    if (tfd >= 0) fsync(tfd);
    fclose(tf);

    unlock_file(fd);
    fclose(fp);

    if (rename(tmpname, ACC_FILE) != 0) {
        free(arr);
        snprintf(out, out_n, "ERR|Replace failed");
        return 0;
    }

    Transaction t;
    t.acc_id = id;
    strncpy(t.type, "WITHDRAW", sizeof(t.type) - 1);
    t.type[sizeof(t.type) - 1] = '\0';
    t.amount = amt;
    t.other_acc = 0;
    now_str(t.timestamp, sizeof(t.timestamp));
    log_transaction_csv(&t);

    free(arr);
    snprintf(out, out_n, "OK|Withdraw done: -%.2f | New balance: %.2f", amt, current_balance);
    return 1;
}

// transfer amount
int customer_transfer(acc_id_t from, acc_id_t to, double amt, char *out, size_t out_n) {
    if (amt <= 0) { snprintf(out,out_n,"ERR|Bad amount"); return 0; }
    if (from == to) { snprintf(out,out_n,"ERR|Same account"); return 0; }

    FILE *fp = fopen(ACC_FILE, "r+");
    if (!fp) { snprintf(out,out_n,"ERR|Cannot open file"); return 0; }
    int fd = fileno(fp);
    if (fd < 0) { fclose(fp); snprintf(out,out_n,"ERR|fileno"); return 0; }
    if (lock_file(fd, 1) != 0) { fclose(fp); snprintf(out,out_n,"ERR|Lock failed"); return 0; }

    char line[512];
    if (!fgets(line, sizeof(line), fp)) { unlock_file(fd); fclose(fp); snprintf(out,out_n,"ERR|Read failed"); return 0; }

    Account *arr = NULL; size_t cap=0, cnt=0;
    while (fgets(line, sizeof(line), fp)) {
        Account a;
        if (sscanf(line, "%d,%49[^,],%15[^,],%lf,%d,%d",
                   &a.id, a.name, a.pin, &a.balance, &a.role, &a.active) == 6) {
            if (cnt >= cap) { cap = cap?cap*2:32; arr = realloc(arr, cap * sizeof(Account)); }
            arr[cnt++] = a;
        }
    }

    int idx_from_exists = 0, idx_to_exists = 0;
    for (size_t i=0;i<cnt;i++){
        if (arr[i].id == from) idx_from_exists = 1;
        if (arr[i].id == to) idx_to_exists = 1;
    }
    if (!idx_from_exists || !idx_to_exists) { free(arr); unlock_file(fd); fclose(fp); snprintf(out,out_n,"ERR|Account missing"); return 0; }

    double total_from_balance = 0.0;
    for (size_t i=0;i<cnt;i++) if (arr[i].id == from) total_from_balance += arr[i].balance;
    if (total_from_balance < amt) { free(arr); unlock_file(fd); fclose(fp); snprintf(out,out_n,"ERR|Insufficient"); return 0; }

    double remaining = amt;
    for (size_t i=0;i<cnt && remaining>0;i++){
        if (arr[i].id == from) {
            double take = (arr[i].balance >= remaining) ? remaining : arr[i].balance;
            arr[i].balance -= take;
            remaining -= take;
        }
    }
    if (remaining > 0.0001) { free(arr); unlock_file(fd); fclose(fp); snprintf(out,out_n,"ERR|Transfer failed"); return 0; }

    remaining = amt;
    for (size_t i=0;i<cnt && remaining>0;i++){
        if (arr[i].id == to) {
            arr[i].balance += remaining;
            remaining = 0;
        }
    }

    char tmpname[256]; snprintf(tmpname,sizeof(tmpname), "%s.tmp", ACC_FILE);
    FILE *tf = fopen(tmpname, "w");
    if (!tf) { free(arr); unlock_file(fd); fclose(fp); snprintf(out,out_n,"ERR|Temp open failed"); return 0; }
    fprintf(tf, "id,name,pin,balance,role,active\n");
    for (size_t i=0;i<cnt;i++) {
        fprintf(tf, "%d,%s,%s,%.2f,%d,%d\n",
                arr[i].id, arr[i].name, arr[i].pin, arr[i].balance, arr[i].role, arr[i].active);
    }
    fflush(tf);
    int tfd = fileno(tf); if (tfd >= 0) fsync(tfd); fclose(tf);

    if (rename(tmpname, ACC_FILE) != 0) { free(arr); unlock_file(fd); fclose(fp); snprintf(out,out_n,"ERR|Replace failed"); return 0; }

    unlock_file(fd);
    fclose(fp);

    Transaction t1; t1.acc_id = from; strncpy(t1.type,"TRANSFER",sizeof(t1.type)-1); t1.type[sizeof(t1.type)-1]=0;
    t1.amount = -amt; t1.other_acc = to; now_str(t1.timestamp,sizeof(t1.timestamp));
    Transaction t2; t2.acc_id = to;   strncpy(t2.type,"TRANSFER",sizeof(t2.type)-1); t2.type[sizeof(t2.type)-1]=0;
    t2.amount = amt;  t2.other_acc = from; now_str(t2.timestamp,sizeof(t2.timestamp));
    log_transaction_csv(&t1); log_transaction_csv(&t2);

    free(arr);
    snprintf(out,out_n,"OK|Transfer done");
    return 1;
}

// view transaction history
int customer_view_history(acc_id_t id, char *out, size_t out_n) {
    FILE *fp = fopen(TXN_FILE, "r");
    if (!fp) { snprintf(out,out_n,"No history\n"); return 0; }
    int fd = fileno(fp);
    if (fd < 0) { fclose(fp); snprintf(out,out_n,"ERR|fileno"); return 0; }
    if (lock_file(fd,0) != 0) { fclose(fp); snprintf(out,out_n,"ERR|Lock failed"); return 0; }

    char line[512]; size_t pos=0;
    while (fgets(line,sizeof(line),fp)) {
        int tid, aid; char type[32]; double amt; int other; char ts[64];
        if (sscanf(line, "%d,%d,%31[^,],%lf,%d,%31[^\n]",
                   &tid, &aid, type, &amt, &other, ts) >= 5) {
            if (aid == id) {
                pos += snprintf(out+pos, out_n>pos?out_n-pos:0, "%s", line);
                if (pos > out_n - 200) break;
            }
        }
    }

    unlock_file(fd);
    fclose(fp);
    if (pos == 0) { snprintf(out,out_n,"No transactions found\n"); }
    return 1;
}

// change PIN
int customer_change_pin(acc_id_t id, const char *oldpin, const char *newpin, char *out, size_t out_n) {
    if (!oldpin || !newpin) { snprintf(out,out_n,"ERR|Bad args"); return 0; }

    FILE *fp = fopen(ACC_FILE, "r+");
    if (!fp) { snprintf(out,out_n,"ERR|Open failed"); return 0; }
    int fd = fileno(fp);
    if (fd < 0) { fclose(fp); snprintf(out,out_n,"ERR|fileno"); return 0; }
    if (lock_file(fd, 1) != 0) { fclose(fp); snprintf(out,out_n,"ERR|Lock failed"); return 0; }

    char line[512];
    if (!fgets(line, sizeof(line), fp)) { unlock_file(fd); fclose(fp); snprintf(out,out_n,"ERR|Read failed"); return 0; }

    Account *arr = NULL; size_t cap=0,cnt=0;
    while (fgets(line,sizeof(line),fp)) {
        Account a;
        if (sscanf(line, "%d,%49[^,],%15[^,],%lf,%d,%d",
                   &a.id, a.name, a.pin, &a.balance, &a.role, &a.active) == 6) {
            if (cnt >= cap) { cap = cap?cap*2:32; arr = realloc(arr, cap * sizeof(Account)); }
            arr[cnt++] = a;
        }
    }

    int changed = 0;
    for (size_t i=0;i<cnt;i++) {
        if (arr[i].id == id) {
            if (strcmp(arr[i].pin, oldpin) != 0) { free(arr); unlock_file(fd); fclose(fp); snprintf(out,out_n,"ERR|Old PIN mismatch"); return 0; }
            strncpy(arr[i].pin, newpin, sizeof(arr[i].pin)-1);
            arr[i].pin[sizeof(arr[i].pin)-1] = '\0';
            changed = 1;
        }
    }
    if (!changed) { free(arr); unlock_file(fd); fclose(fp); snprintf(out,out_n,"ERR|Not found"); return 0; }

    char tmpname[256]; snprintf(tmpname,sizeof(tmpname), "%s.tmp", ACC_FILE);
    FILE *tf = fopen(tmpname, "w");
    if (!tf) { free(arr); unlock_file(fd); fclose(fp); snprintf(out,out_n,"ERR|Temp write failed"); return 0; }
    fprintf(tf, "id,name,pin,balance,role,active\n");
    for (size_t i=0;i<cnt;i++) {
        fprintf(tf, "%d,%s,%s,%.2f,%d,%d\n",
                arr[i].id, arr[i].name, arr[i].pin, arr[i].balance, arr[i].role, arr[i].active);
    }
    fflush(tf);
    int tfd = fileno(tf); if (tfd >= 0) fsync(tfd); fclose(tf);

    if (rename(tmpname, ACC_FILE) != 0) { free(arr); unlock_file(fd); fclose(fp); snprintf(out,out_n,"ERR|Replace failed"); return 0; }

    unlock_file(fd);
    fclose(fp);
    free(arr);

    snprintf(out,out_n,"OK|PIN changed");
    return 1;
}

// check balance
int customer_balance(acc_id_t id, char *out, size_t out_n) {
    FILE *fp = fopen(ACC_FILE, "r");
    if (!fp) {
        snprintf(out, out_n, "ERR|Open failed");
        return 0;
    }

    int fd = fileno(fp);
    if (fd < 0) {
        fclose(fp);
        snprintf(out, out_n, "ERR|fileno");
        return 0;
    }

    if (lock_file(fd, 0) != 0) {
        fclose(fp);
        snprintf(out, out_n, "ERR|Lock failed");
        return 0;
    }

    char line[512];
    fgets(line, sizeof(line), fp); // skip header

    int found = 0;
    double balance = 0.0;

    while (fgets(line, sizeof(line), fp)) {
        Account a;
        if (sscanf(line, "%d,%49[^,],%15[^,],%lf,%d,%d",
                   &a.id, a.name, a.pin, &a.balance, &a.role, &a.active) == 6) {
            if (a.id == id && a.active) {
                found = 1;
                balance = a.balance;
                break;
            }
        }
    }

    unlock_file(fd);
    fclose(fp);

    if (!found) {
        snprintf(out, out_n, "ERR|Account not found");
        return 0;
    }

    snprintf(out, out_n, "OK|Bal:%.2f", balance);
    return 1;
}

// submit feedback
int customer_feedback(acc_id_t id, const char *text, char *out, size_t out_n) {
    if (!text) { snprintf(out,out_n,"ERR|No text"); return 0; }
    FILE *fp = fopen(FB_FILE, "a+");
    if (!fp) { snprintf(out,out_n,"ERR|Open failed"); return 0; }
    int fd = fileno(fp);
    if (fd < 0) { fclose(fp); snprintf(out,out_n,"ERR|fileno"); return 0; }
    if (lock_file(fd,1) != 0) { fclose(fp); snprintf(out,out_n,"ERR|Lock failed"); return 0; }

    int maxid = 0;
    char line[512];
    rewind(fp);
    while (fgets(line,sizeof(line),fp)) {
        int idd; if (sscanf(line, "%d,", &idd) == 1 && idd > maxid) maxid = idd;
    }
    int nid = maxid + 1;
    char ts[64]; now_str(ts,sizeof(ts));
    fprintf(fp, "%d,%d,%s,%s\n", nid, id, text, ts);
    fflush(fp);

    unlock_file(fd);
    fclose(fp);
    snprintf(out,out_n,"OK|Feedback submitted id=%d", nid);
    return 1;
}