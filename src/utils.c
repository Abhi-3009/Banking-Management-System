#include "../include/banking.h"
#include <time.h>

void now_iso8601(char *buf, size_t n) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(buf, n, "%Y-%m-%dT%H:%M:%S%z", tm);
}