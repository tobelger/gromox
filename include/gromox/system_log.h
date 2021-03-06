#pragma once
#include <gromox/defs.h>

void system_log_init(const char *path);
extern int system_log_run(void);
extern void system_log_info(const char *format, ...);
extern int system_log_stop(void);
extern void system_log_free(void);
