#define _GNU_SOURCE
#include "../include/banking.h"
#include "../include/locks.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h> 
#include <sys/file.h> 

/* Lock-safe CSV append for loans */
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

/* Get next loan ID */
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

/* Loan Application (Customer) */
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

/* List all loans (Manager/Employee) */
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

/* Approve Loan (Manager) */
int loan_approve(int loan_id, char *resp, size_t n) {
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
                fprintf(temp, "%d,%d,%.2f,%.2f,%d,1,%s\n", id, acc, amt, intr, months, created);
            } else {
                fputs(line, temp);
            }
        }
    }

    flock(fileno(fp), LOCK_UN);
    fclose(fp);
    fclose(temp);
    rename("data/temp_loans.csv", LOAN_FILE);

    snprintf(resp, n, found ? "OK|Loan %d approved" : "ERR|Loan not found", loan_id);
    return found;
}

/* Reject Loan (Manager) */
int loan_reject(int loan_id, char *resp, size_t n) {
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