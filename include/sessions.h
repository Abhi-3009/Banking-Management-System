#ifndef SESSIONS_H
#define SESSIONS_H

#include "banking.h"

int session_is_active(acc_id_t id, const char *role);
int session_set_active(acc_id_t id, const char *role);
int session_set_inactive(acc_id_t id, const char *role);

#endif