#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdlib.h>

#define PORT 8080
#define BUFSZ 8192

static void readline(char *buf, size_t n) {
    if (!fgets(buf,n,stdin)) { buf[0]=0; return; }
    size_t L=strlen(buf); if (L && buf[L-1]=='\n') buf[L-1]=0;
}

static int send_and_recv(int sock, const char *msg, char *resp, size_t rn) {
    if (send(sock, msg, strlen(msg), 0) <= 0) return 0;
    if (send(sock, "\n", 1, 0) <= 0) return 0;
    ssize_t n = recv(sock, resp, rn-1, 0);
    if (n <= 0) return 0;
    resp[n]=0;
    return 1;
}

static int connect_server() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return -1; }
    struct sockaddr_in serv = {0};
    serv.sin_family = AF_INET;
    serv.sin_port = htons(PORT);
    serv.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(sock, (struct sockaddr*)&serv, sizeof(serv)) < 0) { perror("connect"); close(sock); return -1; }
    return sock;
}

static void show_main_menu() {
    puts("\n=== Banking Client ===");
    puts("1) Login as Admin");
    puts("2) Login as Manager");
    puts("3) Login as Employee");
    puts("4) Login as Customer");
    puts("5) Quit");
    printf("Choice: ");
}

static void admin_menu() {
    puts("\n--- Admin Menu ---");
    puts("1) Add Customer");
    puts("2) Add Employee");
    puts("3) List Accounts");
    puts("4) Delete Account");
    puts("5) Modify Account");
    puts("6) Search Account by ID");
    puts("7) Logout");
    printf("Choice: ");
}

static void manager_menu() {
    puts("\n--- Manager Menu ---");
    puts("1) List Loans");
    puts("2) Approve Loan");
    puts("3) Reject Loan");
    puts("4) Toggle Account Active/Inactive");
    puts("5) List Feedback");
    puts("6) View Account");
    puts("7) Logout");
    printf("Choice: ");
}

static void employee_menu() {
    puts("\n--- Employee Menu ---");
    puts("1) View Account");
    puts("2) List Customers");
    puts("3) List Loans");
    puts("4) Logout");
    printf("Choice: ");
}

static void customer_menu() {
    puts("\n--- Customer Menu ---");
    puts("1) Deposit");
    puts("2) Withdraw");
    puts("3) Balance");
    puts("4) History");
    puts("5) Transfer");
    puts("6) Change PIN");
    puts("7) Feedback");
    puts("8) Apply Loan");
    puts("9) Logout");
    printf("Choice: ");
}

int main() {
    char tmp[256], resp[BUFSZ];
    while (1) {
        show_main_menu();
        readline(tmp, sizeof(tmp));
        int choice = atoi(tmp);
        if (choice == 5) break;
        if (choice < 1 || choice > 4) { puts("Invalid"); continue; }

        char role[16];
        if (choice == 1) strcpy(role,"ADMIN");
        else if (choice == 2) strcpy(role,"MANAGER");
        else if (choice == 3) strcpy(role,"EMPLOYEE");
        else strcpy(role,"CUST");

        char idbuf[64], pin[64], cmd[BUFSZ];
        printf("ID: "); readline(idbuf,sizeof(idbuf));
        printf("PIN: "); readline(pin,sizeof(pin));
        if (!idbuf[0] || !pin[0]) { puts("ID and PIN required"); continue; }

        int sock = connect_server();
        if (sock < 0) continue;

        snprintf(cmd, sizeof(cmd), "LOGIN|%s|%s|%s", role, idbuf, pin);
        if (!send_and_recv(sock, cmd, resp, sizeof(resp))) { puts("Server error"); close(sock); continue; }
        printf("SERVER: %s\n", resp);
        if (strncmp(resp, "OK|ROLE", 7) != 0) { puts("Login failed"); close(sock); continue; }

        if (strcmp(role,"ADMIN")==0) {
            while (1) {
                admin_menu(); readline(tmp,sizeof(tmp)); int c = atoi(tmp);
                if (c == 1) {
                    char name[128], npin[64];
                    printf("Customer name: "); readline(name,sizeof(name));
                    printf("PIN: "); readline(npin,sizeof(npin));
                    snprintf(cmd,sizeof(cmd),"ADD_ACCOUNT|%s|%s", name, npin);
                    send_and_recv(sock, cmd, resp, sizeof(resp)); printf("%s\n", resp);
                } else if (c == 2) {
                    char name[128], npin[64];
                    printf("Employee name: "); readline(name,sizeof(name));
                    printf("PIN: "); readline(npin,sizeof(npin));
                    snprintf(cmd,sizeof(cmd),"ADD_EMPLOYEE|%s|%s", name, npin);
                    send_and_recv(sock, cmd, resp, sizeof(resp)); printf("%s\n", resp);
                } else if (c == 3) {
                    snprintf(cmd,sizeof(cmd),"LIST_ACCOUNTS|"); send_and_recv(sock, cmd, resp, sizeof(resp)); printf("%s\n", resp);
                } else if (c == 4) {
                    char aid[32];
                    printf("Account ID to delete: "); readline(aid, sizeof(aid));
                    snprintf(cmd, sizeof(cmd), "DELETE_ACCOUNT|%s", aid);
                    send_and_recv(sock, cmd, resp, sizeof(resp)); printf("%s\n", resp);
                }  else if (c == 5) {
                    char aid[32], newname[128], newpin[64];
                    printf("Account ID to modify: "); readline(aid, sizeof(aid));
                    printf("New Name: "); readline(newname, sizeof(newname));
                    printf("New PIN: "); readline(newpin, sizeof(newpin));
                    snprintf(cmd, sizeof(cmd), "MODIFY_ACCOUNT|%s|%s|%s", aid, newname, newpin);
                    send_and_recv(sock, cmd, resp, sizeof(resp)); printf("%s\n", resp);
                } else if (c == 6) {
                    char aid[32]; printf("Account ID: "); readline(aid,sizeof(aid));
                    snprintf(cmd,sizeof(cmd),"SEARCH_ACCOUNT|%s", aid); send_and_recv(sock, cmd, resp, sizeof(resp)); printf("%s\n", resp);
                } else { snprintf(cmd,sizeof(cmd),"LOGOUT|"); send_and_recv(sock, cmd, resp, sizeof(resp)); printf("%s\n", resp); break; }
            }
        } else if (strcmp(role,"MANAGER")==0) {
            while (1) {
                manager_menu(); readline(tmp,sizeof(tmp)); int c = atoi(tmp);
                if (c == 1) { snprintf(cmd,sizeof(cmd),"LIST_LOANS|"); send_and_recv(sock,cmd,resp,sizeof(resp)); printf("%s\n",resp); }
                else if (c == 2) { char lid[32]; printf("Loan ID: "); readline(lid,sizeof(lid)); snprintf(cmd,sizeof(cmd),"APPROVE_LOAN|%s",lid); send_and_recv(sock,cmd,resp,sizeof(resp)); printf("%s\n",resp); }
                else if (c == 3) { char lid[32]; printf("Loan ID: "); readline(lid,sizeof(lid)); snprintf(cmd,sizeof(cmd),"REJECT_LOAN|%s",lid); send_and_recv(sock,cmd,resp,sizeof(resp)); printf("%s\n",resp); }
                else if (c == 4) { char aid[32], val[8]; printf("Account ID: "); readline(aid,sizeof(aid)); printf("Active? (1/0): "); readline(val,sizeof(val)); snprintf(cmd,sizeof(cmd),"TOGGLE_ACCOUNT|%s|%s",aid,val); send_and_recv(sock,cmd,resp,sizeof(resp)); printf("%s\n",resp); }
                else if (c == 5) { snprintf(cmd,sizeof(cmd),"LIST_FEEDBACK|"); send_and_recv(sock,cmd,resp,sizeof(resp)); printf("%s\n",resp); }
                else if (c == 6) { char aid[32]; printf("Account ID: "); readline(aid,sizeof(aid)); snprintf(cmd,sizeof(cmd),"VIEW_ACCOUNT|%s",aid); send_and_recv(sock,cmd,resp,sizeof(resp)); printf("%s\n",resp); }
                else { snprintf(cmd,sizeof(cmd),"LOGOUT|"); send_and_recv(sock,cmd,resp,sizeof(resp)); printf("%s\n",resp); break; }
            }
        } else if (strcmp(role,"EMPLOYEE")==0) {
            while (1) {
                employee_menu(); readline(tmp,sizeof(tmp)); int c = atoi(tmp);
                if (c == 1) { char aid[32]; printf("Account ID: "); readline(aid,sizeof(aid)); snprintf(cmd,sizeof(cmd),"VIEW_ACCOUNT|%s",aid); send_and_recv(sock,cmd,resp,sizeof(resp)); printf("%s\n",resp); }
                else if (c == 2) { snprintf(cmd,sizeof(cmd),"LIST_CUSTOMERS|"); send_and_recv(sock,cmd,resp,sizeof(resp)); printf("%s\n",resp); }
                else if (c == 3) { snprintf(cmd,sizeof(cmd),"LIST_LOANS|"); send_and_recv(sock,cmd,resp,sizeof(resp)); printf("%s\n",resp); }
                else { snprintf(cmd,sizeof(cmd),"LOGOUT|"); send_and_recv(sock,cmd,resp,sizeof(resp)); printf("%s\n",resp); break; }
            }
        } else {
            while (1) {
                customer_menu();
                readline(tmp, sizeof(tmp));
                int c = atoi(tmp);

                if (c == 1) {
                    char amt[64];
                    printf("Amount: ");
                    readline(amt, sizeof(amt));
                    snprintf(cmd, sizeof(cmd), "DEPOSIT|%s", amt);
                    send_and_recv(sock, cmd, resp, sizeof(resp));
                    printf("%s\n", resp);
                }

                else if (c == 2) {  // Withdraw
                    char amt[64];
                    printf("Amount: ");
                    readline(amt, sizeof(amt));
                    snprintf(cmd, sizeof(cmd), "WITHDRAW|%s", amt);
                    send_and_recv(sock, cmd, resp, sizeof(resp));
                    printf("%s\n", resp);
                }

                else if (c == 3) {  // Balance
                    snprintf(cmd, sizeof(cmd), "BALANCE");
                    send_and_recv(sock, cmd, resp, sizeof(resp));
                    printf("%s\n", resp);
                }

                else if (c == 4) {  // History
                    snprintf(cmd, sizeof(cmd), "HISTORY|");
                    send_and_recv(sock, cmd, resp, sizeof(resp));
                    printf("%s\n", resp);
                }

                else if (c == 5) {  // Transfer
                    char to[32], amt[64];
                    printf("Transfer to (Account ID): ");
                    readline(to, sizeof(to));
                    printf("Amount: ");
                    readline(amt, sizeof(amt));
                    snprintf(cmd, sizeof(cmd), "TRANSFER|%s|%s", to, amt);
                    send_and_recv(sock, cmd, resp, sizeof(resp));
                    printf("%s\n", resp);
                }

                else if (c == 6) {  // Change PIN
                    char oldp[32], newp[32];
                    printf("Old PIN: ");
                    readline(oldp, sizeof(oldp));
                    printf("New PIN: ");
                    readline(newp, sizeof(newp));
                    snprintf(cmd, sizeof(cmd), "CHANGEPIN|%s|%s", oldp, newp);
                    send_and_recv(sock, cmd, resp, sizeof(resp));
                    printf("%s\n", resp);
                }

                else if (c == 7) {  // Feedback
                    char txt[256];
                    printf("Feedback: ");
                    readline(txt, sizeof(txt));
                    snprintf(cmd, sizeof(cmd), "FEEDBACK|%s", txt);
                    send_and_recv(sock, cmd, resp, sizeof(resp));
                    printf("%s\n", resp);
                }

                else if (c == 8) {  // Apply Loan
                    char amt[64], intr[32], months[8];
                    printf("Amount: ");
                    readline(amt, sizeof(amt));
                    printf("Interest: ");
                    readline(intr, sizeof(intr));
                    printf("Months: ");
                    readline(months, sizeof(months));
                    snprintf(cmd, sizeof(cmd), "APPLYLOAN|%s|%s|%s", amt, intr, months);
                    send_and_recv(sock, cmd, resp, sizeof(resp));
                    printf("%s\n", resp);
                }

                else {  // Logout
                    snprintf(cmd, sizeof(cmd), "LOGOUT|");
                    send_and_recv(sock, cmd, resp, sizeof(resp));
                    printf("%s\n", resp);
                    break;
                }
            }
        }

        close(sock);
    }

    puts("Client exiting.");
    return 0;
}