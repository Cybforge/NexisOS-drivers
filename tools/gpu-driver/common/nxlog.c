#include "nxlog.h"
#include <stdarg.h>

static const nexis_gpu_services *log_services;

void nx_log_attach(const nexis_gpu_services *services) {
    log_services = (services && services->size >= NEXIS_GPU_SERVICES_LOG_BYTES && services->log) ? services : 0;
}

#define LINE 200u

static void put(char *line, unsigned *n, char c) {
    if (*n < LINE - 1) line[(*n)++] = c;
}

static void put_number(char *line, unsigned *n, unsigned long long v, unsigned base, bool upper, bool negative,
                       unsigned width, bool zero) {
    char digits[24];
    unsigned count = 0;
    do {
        unsigned d = (unsigned)(v % base);
        digits[count++] = (char)(d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10);
        v /= base;
    } while (v && count < sizeof(digits));
    unsigned total = count + (negative ? 1u : 0u);
    if (negative && zero) put(line, n, '-');
    for (; total < width; total++) put(line, n, zero ? '0' : ' ');
    if (negative && !zero) put(line, n, '-');
    while (count) put(line, n, digits[--count]);
}

void nx_logf(const char *format, ...) {
    if (!log_services || !format) return;
    char line[LINE];
    unsigned n = 0;
    va_list ap;
    va_start(ap, format);
    for (const char *p = format; *p; p++) {
        if (*p != '%') { put(line, &n, *p); continue; }
        p++;
        bool zero = false;
        unsigned width = 0, longs = 0;
        if (*p == '0') { zero = true; p++; }
        while (*p >= '0' && *p <= '9') width = width * 10 + (unsigned)(*p++ - '0');
        while (*p == 'l' || *p == 'z') { longs++; p++; }
        switch (*p) {
            case 'u': put_number(line, &n, longs ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned), 10, false, false, width, zero); break;
            case 'd': {
                long long v = longs ? va_arg(ap, long long) : va_arg(ap, int);
                bool neg = v < 0;
                put_number(line, &n, neg ? (unsigned long long)(-(v + 1)) + 1u : (unsigned long long)v, 10, false, neg, width, zero);
                break;
            }
            case 'x': put_number(line, &n, longs ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned), 16, false, false, width, zero); break;
            case 'X': put_number(line, &n, longs ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned), 16, true, false, width, zero); break;
            case 'c': put(line, &n, (char)va_arg(ap, int)); break;
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                while (*s) put(line, &n, *s++);
                break;
            }
            case '%': put(line, &n, '%'); break;
            case 0: p--; break;
            default: put(line, &n, '?'); break;
        }
    }
    va_end(ap);
    line[n] = 0;
    log_services->log(log_services->service_context, line);
}
