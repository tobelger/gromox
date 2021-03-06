#pragma once
#include <gromox/common_types.hpp>

using COMMAND_HANDLER = BOOL (*)(int argc, char **argv);

void console_server_init(const char* bind_ip, int port);
extern void console_server_free(void);
extern int console_server_run(void);
extern int console_server_stop(void);
int  console_server_reply_to_client(const char* format, ...);
extern void console_server_notify_main_stop(void);
extern BOOL console_server_register_command(const char *cmd, COMMAND_HANDLER);
