#include "../include/banking.h"
#include <time.h>

void now_iso8601(char *buf, size_t n) {
    time_t t = time(NULL);
    struct tm *tm = gmtime(&t);  // Portable and thread-unsafe version, fine here
    if (tm)
        strftime(buf, n, "%Y-%m-%dT%H:%M:%SZ", tm);
    else
        snprintf(buf, n, "UNKNOWN");
}