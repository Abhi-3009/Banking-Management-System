#ifndef BANKING_H
#define BANKING_H

#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef int acc_id_t;

typedef struct {
    acc_id_t id;
    char name[50];
    char pin[16];
    double balance;
    int active;
    int role;       // 0 customer, 1 admin, 2 manager, 3 employee
} Account;

typedef struct {
    int loan_id;
    acc_id_t acc_id;
    double amount;
    double interest;
    int months;
    int status;     // 0 pending, 1 approved, 2 rejected
    char created_at[32];
} Loan;

typedef struct {
    int txn_id;
    acc_id_t acc_id;
    char type[16];
    double amount;
    acc_id_t other_acc;
    char timestamp[32];
} Transaction;

typedef struct {
    int fb_id;
    acc_id_t acc_id;
    char text[256];
    char timestamp[32];
} Feedback;

#define ACC_FILE "data/accounts.csv"
#define TXN_FILE "data/transactions.csv"
#define LOAN_FILE "data/loans.csv"
#define FB_FILE  "data/feedback.csv"

void now_iso8601(char *buf, size_t n);

int loan_apply(acc_id_t acc_id, double amount, double interest, int months, char *resp, size_t n);
int loan_list_all(char *resp, size_t n);

int customer_balance(acc_id_t, char*, size_t);
int customer_deposit(acc_id_t, double, char*, size_t);
int customer_withdraw(acc_id_t, double, char*, size_t);
int customer_transfer(acc_id_t, acc_id_t, double, char*, size_t);
int customer_change_pin(acc_id_t, const char*, const char*, char*, size_t);
int customer_feedback(acc_id_t, const char*, char*, size_t);
int customer_auth(acc_id_t, const char*);

int loan_assign(int loan_id, int emp_id, char *resp, size_t n);
int loan_approve_by_employee(int loan_id, int emp_id, char *resp, size_t n);
int loan_reject_by_employee(int loan_id, int emp_id, char *resp, size_t n);
int loan_list_all(char *resp, size_t n);

#endif