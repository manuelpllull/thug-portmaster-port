#ifndef VITA_LOG_H
#define VITA_LOG_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

void vita_log_init(void);
void vita_log_shutdown(void);
void vita_log_start_watchdog(void);
void vita_log_flush(void);
void vita_log_printf(const char *sys, const char *fmt, ...);
int vita_log_ring_read(unsigned int *pos, char *out, int max);

#define VLOG(sys, ...) vita_log_printf(sys, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // VITA_LOG_H
