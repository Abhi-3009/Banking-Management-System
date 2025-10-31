#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

#include "include/banking.h"

/* Demo credentials (kept for quick login) */
#define ADMIN_ID 9999
#define ADMIN_PIN "admin123"
#define MANAGER_ID 8888
#define MANAGER_PIN "manager123"
#define EMPLOYEE_ID 7777
#define EMPLOYEE_PIN "emp123"

#define PORT 8080
#define BACKLOG 8
#define BUFSZ 8192

/* --- Function declarations--- */

/* ==== Admin functions ==== */
int admin_add_account(const char *name, const char *pin, int role, char *resp, size_t n);
int admin_add_employee(const char *name, const char *pin, char *resp, size_t n);
int admin_list_accounts(char *resp, size_t n);
int admin_delete_account(acc_id_t id, char *resp, size_t n);
int admin_modify_account(acc_id_t id, const char *new_name, const char *new_pin, char *resp, size_t n);
int admin_search_account(acc_id_t id, char *resp, size_t n);

/* ==== Customer functions ==== */
int customer_auth(acc_id_t id, const char *pin);
int customer_balance(acc_id_t id, char *resp, size_t n);
int customer_deposit(acc_id_t id, double amt, char *resp, size_t n);
int customer_withdraw(acc_id_t id, double amt, char *resp, size_t n);
int customer_transfer(acc_id_t from, acc_id_t to, double amt, char *resp, size_t n);
int customer_change_pin(acc_id_t id, const char *oldpin, const char *newpin, char *resp, size_t n);
int customer_view_history(acc_id_t id, char *resp, size_t n);
int customer_feedback(acc_id_t id, const char *text, char *resp, size_t n);

/* ==== Employee functions ==== */
int employee_auth(acc_id_t id, const char *pin);
int employee_view_account(acc_id_t id, char *resp, size_t n);
int employee_list_customers(char *resp, size_t n);
int employee_list_loans(char *resp, size_t n);

/* ==== Manager functions ==== */
int manager_auth(acc_id_t id, const char *pin);
int manager_approve_loan(int loan_id, char *resp, size_t n);
int manager_reject_loan(int loan_id, char *resp, size_t n);
int manager_view_account(acc_id_t id, char *resp, size_t n);
int manager_list_customers(char *resp, size_t n);
int manager_list_loans(char *resp, size_t n);
int manager_toggle_account(acc_id_t id, int active, char *resp, size_t n);
int manager_list_feedback(char *resp, size_t n);

/* ==== Loan functions ==== */
int loan_apply(acc_id_t id, double amount, double interest, int months, char *resp, size_t n);
int loan_list_all(char *resp, size_t n);
int loan_approve(int loan_id, char *resp, size_t n);
int loan_reject(int loan_id, char *resp, size_t n);

/* Helper - safe send helper to avoid partial sends */
static ssize_t safe_send(int fd, const char *buf, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        ssize_t s = send(fd, buf + sent, n - sent, 0);
        if (s <= 0) {
            if (s < 0 && errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)s;
    }
    return (ssize_t)sent;
}

/* Serve a single connected client (forked child) */
void serve_client(int connfd) {
    char sbuf[BUFSZ];
    char reply[BUFSZ];

    acc_id_t logged_in_id = -1;
    char current_role[16] = "NONE"; /* "ADMIN", "MANAGER", "EMPLOYEE", "CUST", "NONE" */

    while (1) {
        memset(sbuf, 0, sizeof(sbuf));
        ssize_t n = recv(connfd, sbuf, sizeof(sbuf) - 1, 0);
        if (n <= 0) break;
        sbuf[n] = '\0';

        /* trim trailing CR/LF */
        while (n > 0 && (sbuf[n-1] == '\n' || sbuf[n-1] == '\r')) { sbuf[n-1] = '\0'; n--; }

        if (sbuf[0] == '\0') continue;

        /* tokenize a copy (we may need original) */
        char copy[BUFSZ];
        strncpy(copy, sbuf, sizeof(copy)-1);
        char *cmd = strtok(copy, "|");
        if (!cmd) { safe_send(connfd, "ERR|Bad cmd\n", 12); continue; }

        printf("DEBUG: role=[%s], cmd=[%s]\n", current_role, cmd);
        fflush(stdout);

        /* ---------------- LOGIN ---------------- */
        if (strcmp(cmd, "LOGIN") == 0) {
            char *role = strtok(NULL, "|");
            if (role) role[strcspn(role, "\r\n")] = '\0';
            char *id_s = strtok(NULL, "|");
            char *pin = strtok(NULL, "|");

            if (!role || !id_s || !pin) {
                safe_send(connfd, "ERR|LOGIN bad format\n", 20);
                continue;
            }

            acc_id_t id = (acc_id_t)atoi(id_s);

            if (strcmp(role, "ADMIN") == 0) {
                if (id == ADMIN_ID && strcmp(pin, ADMIN_PIN) == 0) {
                    strcpy(current_role, "ADMIN");
                    current_role[strcspn(current_role, "\r\n")] = '\0';
                    logged_in_id = (acc_id_t)ADMIN_ID;
                    safe_send(connfd, "OK|ROLE:ADMIN\n", 13);
                } else {
                    safe_send(connfd, "ERR|Login failed\n", 17);
                }
            } else if (strcmp(role, "MANAGER") == 0 || strcmp(role, "MGR") == 0) {
                if (id == MANAGER_ID && strcmp(pin, MANAGER_PIN) == 0) {
                    strcpy(current_role, "MANAGER");
                    current_role[strcspn(current_role, "\r\n")] = '\0';
                    logged_in_id = (acc_id_t)MANAGER_ID;
                    safe_send(connfd, "OK|ROLE:MANAGER\n", 15);
                } else {
                    safe_send(connfd, "ERR|Login failed\n", 17);
                }
            } else if (strcmp(role, "EMPLOYEE") == 0 || strcmp(role, "EMP") == 0) {
                if (id == EMPLOYEE_ID && strcmp(pin, EMPLOYEE_PIN) == 0) {
                    strcpy(current_role, "EMPLOYEE");
                    current_role[strcspn(current_role, "\r\n")] = '\0';
                    logged_in_id = (acc_id_t)EMPLOYEE_ID;
                    safe_send(connfd, "OK|ROLE:EMPLOYEE\n", 16);
                } else {
                    safe_send(connfd, "ERR|Login failed\n", 17);
                }
            } else if (strcmp(role, "CUST") == 0 || strcmp(role, "CUSTOMER") == 0) {
                if (customer_auth(id, pin)) {
                    strcpy(current_role, "CUST");
                    current_role[strcspn(current_role, "\r\n")] = '\0';
                    logged_in_id = id;
                    safe_send(connfd, "OK|ROLE:CUST\n", 13);
                } else {
                    safe_send(connfd, "ERR|Login failed\n", 17);
                }
            } else {
                safe_send(connfd, "ERR|Invalid role\n", 17);
            }
            continue;
        }

        /* ---------------- LOGOUT/QUIT ---------------- */
        if (strcmp(cmd, "LOGOUT") == 0 || strcmp(cmd, "QUIT") == 0) {
            logged_in_id = -1;
            strcpy(current_role, "NONE");
            safe_send(connfd, "BYE\n", 4);
            continue;
        }

        /* Any other command requires role/session validation in places */
        /* ---------------- ADMIN commands ---------------- */
        if (strcmp(cmd, "ADD_ACCOUNT") == 0 && strcmp(current_role, "ADMIN") == 0) {
            char *name = strtok(NULL, "|");
            char *pin2 = strtok(NULL, "|");
            if (!name || !pin2) { safe_send(connfd, "ERR|args\n", 9); continue; }
            admin_add_account(name, pin2, 0, reply, sizeof(reply));
            size_t L = strlen(reply); if (L && reply[L-1] != '\n') strcat(reply, "\n");
            safe_send(connfd, reply, strlen(reply));
            continue;
        } else if (strcmp(cmd, "ADD_EMPLOYEE") == 0 && strcmp(current_role, "ADMIN") == 0) {
            char *name = strtok(NULL, "|");
            char *pin2 = strtok(NULL, "|");
            if (!name || !pin2) { safe_send(connfd, "ERR|args\n", 9); continue; }
            admin_add_employee(name, pin2, reply, sizeof(reply));
            size_t L = strlen(reply); if (L && reply[L-1] != '\n') strcat(reply, "\n");
            safe_send(connfd, reply, strlen(reply));
            continue;
        } else if (strcmp(cmd, "LIST_ACCOUNTS") == 0 && strcmp(current_role, "ADMIN") == 0) {
            admin_list_accounts(reply, sizeof(reply));
            size_t L = strlen(reply); if (L && reply[L-1] != '\n') strcat(reply, "\n");
            safe_send(connfd, reply, strlen(reply));
            continue;
        } else if (strcmp(cmd, "DELETE_ACCOUNT") == 0 && strcmp(current_role, "ADMIN") == 0) {
            char *id_s = strtok(NULL, "|");
            if (!id_s) { safe_send(connfd, "ERR|Missing id\n", 15); continue; }
            acc_id_t id = (acc_id_t)atoi(id_s);
            admin_delete_account(id, reply, sizeof(reply));
            size_t L = strlen(reply); if (L && reply[L-1] != '\n') strcat(reply, "\n");
            safe_send(connfd, reply, strlen(reply));
            continue;
        } else if (strcmp(cmd, "MODIFY_ACCOUNT") == 0 && strcmp(current_role, "ADMIN") == 0) {
            char *id_s = strtok(NULL, "|");
            char *new_name = strtok(NULL, "|");
            char *new_pin = strtok(NULL, "|");
            if (!id_s || !new_name || !new_pin) { safe_send(connfd, "ERR|Missing args\n", 17); continue; }
            acc_id_t id = (acc_id_t)atoi(id_s);
            admin_modify_account(id, new_name, new_pin, reply, sizeof(reply));
            size_t L = strlen(reply); if (L && reply[L-1] != '\n') strcat(reply, "\n");
            safe_send(connfd, reply, strlen(reply));
            continue;
        } else if (strcmp(cmd, "SEARCH_ACCOUNT") == 0 && strcmp(current_role, "ADMIN") == 0) {
            char *id_s = strtok(NULL, "|");
            if (!id_s) { safe_send(connfd, "ERR|Missing id\n", 15); continue; }
            acc_id_t id = (acc_id_t)atoi(id_s);
            admin_search_account(id, reply, sizeof(reply));
            size_t L = strlen(reply); if (L && reply[L-1] != '\n') strcat(reply, "\n");
            safe_send(connfd, reply, strlen(reply));
            continue;
        }

        /* ---------------- CUSTOMER commands (use logged_in_id only) ---------------- */
        if (strcmp(cmd, "DEPOSIT") == 0) {
            char *am = strtok(NULL, "|");
            double amount = atof(am);
            customer_deposit(logged_in_id, amount, reply, sizeof(reply));
            send(connfd, reply, strlen(reply), 0);
        }
        else if (strcmp(cmd, "WITHDRAW") == 0) {
            char *am = strtok(NULL, "|");
            double amount = atof(am);
            customer_withdraw(logged_in_id, amount, reply, sizeof(reply));
            send(connfd, reply, strlen(reply), 0);
        } else if (strcmp(cmd, "BALANCE") == 0 && strcmp(current_role, "CUST") == 0) {
            customer_balance(logged_in_id, reply, sizeof(reply));
            size_t L = strlen(reply); if (L && reply[L-1] != '\n') strcat(reply, "\n");
            safe_send(connfd, reply, strlen(reply));
            continue;
        } else if (strcmp(cmd, "HISTORY") == 0 && strcmp(current_role, "CUST") == 0) {
            customer_view_history(logged_in_id, reply, sizeof(reply));
            size_t L = strlen(reply); if (L && reply[L-1] != '\n') strcat(reply, "\n");
            safe_send(connfd, reply, strlen(reply));
            continue;
        } else if (strcmp(cmd, "TRANSFER") == 0 && strcmp(current_role, "CUST") == 0) {
            char *to_s = strtok(NULL, "|");
            char *am = strtok(NULL, "|");
            if (!to_s || !am) { safe_send(connfd, "ERR|Missing args\n", 17); continue; }
            acc_id_t to = (acc_id_t)atoi(to_s); double amount = atof(am);
            customer_transfer(logged_in_id, to, amount, reply, sizeof(reply));
            size_t L = strlen(reply); if (L && reply[L-1] != '\n') strcat(reply, "\n");
            safe_send(connfd, reply, strlen(reply));
            continue;
        } else if (strcmp(cmd, "CHANGEPIN") == 0 && strcmp(current_role, "CUST") == 0) {
            char *oldp = strtok(NULL, "|");
            char *newp = strtok(NULL, "|");
            if (!oldp || !newp) { safe_send(connfd, "ERR|Missing args\n", 17); continue; }
            customer_change_pin(logged_in_id, oldp, newp, reply, sizeof(reply));
            size_t L = strlen(reply); if (L && reply[L-1] != '\n') strcat(reply, "\n");
            safe_send(connfd, reply, strlen(reply));
            continue;
        } else if (strcmp(cmd, "FEEDBACK") == 0 && strcmp(current_role, "CUST") == 0) {
            char *txt = strtok(NULL, "|");
            if (!txt) { safe_send(connfd, "ERR|Missing text\n", 17); continue; }
            customer_feedback(logged_in_id, txt, reply, sizeof(reply));
            size_t L = strlen(reply); if (L && reply[L-1] != '\n') strcat(reply, "\n");
            safe_send(connfd, reply, strlen(reply));
            continue;
        } else if (strcmp(cmd, "APPLYLOAN") == 0 && strcmp(current_role, "CUST") == 0) {
            char *am = strtok(NULL, "|");
            if (!am) { snprintf(reply, sizeof(reply), "ERR|Missing amount"); send(connfd, reply, strlen(reply), 0); continue; }
            double amount = atof(am);
            char *it = strtok(NULL, "|");
            char *mn = strtok(NULL, "|");
            double interest = it ? atof(it) : 0.0;
            int months = mn ? atoi(mn) : 0;
            loan_apply(logged_in_id, amount, interest, months, reply, sizeof(reply));
            send(connfd, reply, strlen(reply), 0);
            continue;
        }

        /* ---------------- EMPLOYEE / MANAGER / ADMIN shared commands ---------------- */
        if (strcmp(cmd, "VIEW_ACCOUNT") == 0 &&
            (strcmp(current_role, "EMPLOYEE") == 0 || strcmp(current_role, "MANAGER") == 0 || strcmp(current_role, "ADMIN") == 0)) {
            char *id_s = strtok(NULL, "|");
            if (!id_s) { safe_send(connfd, "ERR|Missing id\n", 15); continue; }
            acc_id_t id = (acc_id_t)atoi(id_s);
            if (strcmp(current_role, "EMPLOYEE") == 0) employee_view_account(id, reply, sizeof(reply));
            else if (strcmp(current_role, "MANAGER") == 0) manager_view_account(id, reply, sizeof(reply));
            else admin_search_account(id, reply, sizeof(reply));
            size_t L = strlen(reply); if (L && reply[L-1] != '\n') strcat(reply, "\n");
            safe_send(connfd, reply, strlen(reply));
            continue;
        } else if (strcmp(cmd, "LIST_CUSTOMERS") == 0 &&
                   (strcmp(current_role, "EMPLOYEE") == 0 || strcmp(current_role, "MANAGER") == 0 || strcmp(current_role, "ADMIN") == 0)) {
            if (strcmp(current_role, "EMPLOYEE") == 0) employee_list_customers(reply, sizeof(reply));
            else if (strcmp(current_role, "MANAGER") == 0) manager_list_customers(reply, sizeof(reply));
            else admin_list_accounts(reply, sizeof(reply));
            size_t L = strlen(reply); if (L && reply[L-1] != '\n') strcat(reply, "\n");
            safe_send(connfd, reply, strlen(reply));
            continue;
        } else if (strcmp(cmd, "LIST_LOANS") == 0 &&
                   (strcmp(current_role, "EMPLOYEE") == 0 || strcmp(current_role, "MANAGER") == 0 || strcmp(current_role, "ADMIN") == 0)) {
            if (strcmp(current_role, "EMPLOYEE") == 0) employee_list_loans(reply, sizeof(reply));
            else manager_list_loans(reply, sizeof(reply));
            size_t L = strlen(reply); if (L && reply[L-1] != '\n') strcat(reply, "\n");
            safe_send(connfd, reply, strlen(reply));
            continue;
        }

        /* ---------------- MANAGER actions ---------------- */
        if (strcmp(cmd, "APPROVE_LOAN") == 0 && strcmp(current_role, "MANAGER") == 0) {
            char *lid_s = strtok(NULL, "|");
            if (!lid_s) { safe_send(connfd, "ERR|Missing id\n", 15); continue; }
            int lid = atoi(lid_s);
            manager_approve_loan(lid, reply, sizeof(reply));
            size_t L = strlen(reply); if (L && reply[L-1] != '\n') strcat(reply, "\n");
            safe_send(connfd, reply, strlen(reply));
            continue;
        } else if (strcmp(cmd, "REJECT_LOAN") == 0 && strcmp(current_role, "MANAGER") == 0) {
            char *lid_s = strtok(NULL, "|");
            if (!lid_s) { safe_send(connfd, "ERR|Missing id\n", 15); continue; }
            int lid = atoi(lid_s);
            manager_reject_loan(lid, reply, sizeof(reply));
            size_t L = strlen(reply); if (L && reply[L-1] != '\n') strcat(reply, "\n");
            safe_send(connfd, reply, strlen(reply));
            continue;
        } else if (strcmp(cmd, "TOGGLE_ACCOUNT") == 0 && strcmp(current_role, "MANAGER") == 0) {
            char *id_s = strtok(NULL, "|");
            char *val_s = strtok(NULL, "|");
            if (!id_s || !val_s) { safe_send(connfd, "ERR|Missing args\n", 17); continue; }
            acc_id_t id = (acc_id_t)atoi(id_s);
            int act = atoi(val_s);
            manager_toggle_account(id, act, reply, sizeof(reply));
            size_t L = strlen(reply); if (L && reply[L-1] != '\n') strcat(reply, "\n");
            safe_send(connfd, reply, strlen(reply));
            continue;
        } else if (strcmp(cmd, "LIST_FEEDBACK") == 0 && strcmp(current_role, "MANAGER") == 0) {
            manager_list_feedback(reply, sizeof(reply));
            size_t L = strlen(reply); if (L && reply[L-1] != '\n') strcat(reply, "\n");
            safe_send(connfd, reply, strlen(reply));
            continue;
        }

        /* unknown command or permission denied */
        safe_send(connfd, "ERR|Unknown or permission denied\n", 33);
    }

    close(connfd);
}

/* Ensure data dir and default files exist */
void ensure_default_records() {
    mkdir("data", 0755);

    /* Accounts */
    FILE *fa = fopen(ACC_FILE, "a+");
    FILE *ft = fopen(TXN_FILE, "a+");
    FILE *fl = fopen(LOAN_FILE, "a+");
    FILE *ff = fopen(FB_FILE, "a+");
    if (!fa || !ft || !fl || !ff) {
        perror("[INIT] Cannot create data files");
        if (fa) fclose(fa);
        if (ft) fclose(ft);
        if (fl) fclose(fl);
        if (ff) fclose(ff);
        return;
    }

    /* If accounts is empty, write header + defaults */
    fseek(fa, 0, SEEK_END);
    if (ftell(fa) == 0) {
        fprintf(fa, "id,name,pin,balance,role,active\n");
        /* roles: 1 ADMIN,2 MANAGER,3 EMPLOYEE,0 CUST */
        fprintf(fa, "9999,ADMIN,admin123,0.00,1,1\n");
        fprintf(fa, "8888,MANAGER,manager123,0.00,2,1\n");
        fprintf(fa, "7777,EMPLOYEE,emp123,0.00,3,1\n");
        fprintf(fa, "1001,Alice,1111,5000.00,0,1\n");
        fprintf(fa, "1002,Bob,2222,3000.00,0,1\n");
        printf("[INIT] Default accounts created in %s\n", ACC_FILE);
    }

    fseek(ft,0,SEEK_END);
    if (ftell(ft) == 0) fprintf(ft, "txn_id,account_id,type,amount,other_account,timestamp\n");

    fseek(fl,0,SEEK_END);
    if (ftell(fl) == 0) fprintf(fl, "loan_id,account_id,amount,interest,months,status,created_at\n");

    fseek(ff,0,SEEK_END);
    if (ftell(ff) == 0) fprintf(ff, "fb_id,account_id,feedback,timestamp\n");

    fclose(fa); fclose(ft); fclose(fl); fclose(ff);
}

int main(void) {
    ensure_default_records();

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) { perror("socket"); return 1; }

    struct sockaddr_in serv = {0};
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = INADDR_ANY;
    serv.sin_port = htons(PORT);

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(listenfd, (struct sockaddr*)&serv, sizeof(serv)) < 0) { perror("bind"); return 1; }
    if (listen(listenfd, BACKLOG) < 0) { perror("listen"); return 1; }

    printf("Multi-client Banking Server listening on port %d\n", PORT);
    signal(SIGCHLD, SIG_IGN);

    while (1) {
        struct sockaddr_in cli; socklen_t clilen = sizeof(cli);
        int connfd = accept(listenfd, (struct sockaddr*)&cli, &clilen);
        if (connfd < 0) { perror("accept"); continue; }

        pid_t pid = fork();
        if (pid == 0) {
            close(listenfd);
            serve_client(connfd);
            exit(0);
        } else if (pid > 0) {
            close(connfd);
        } else {
            perror("fork");
        }
    }

    close(listenfd);
    return 0;
}