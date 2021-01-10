#pragma once
#include <gromox/defs.h>
#include <gromox/common_types.hpp>
#define NOTIFY_SMTP			0x00000001
#define NOTIFY_DELIVERY		0x00000002

void gateway_control_init(const char *path);
extern int gateway_control_run(void);
void gateway_control_notify(const char *command, int control_mask);
extern int gateway_control_stop(void);
extern void gateway_control_free(void);
