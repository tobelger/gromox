#pragma once
#ifndef __cplusplus
#	include <stdbool.h>
#endif
#include "common_types.h"
#include "plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void service_init(const char *prog_id, int context_num, const char *plugin_path, const char *config_path, const char *data_path, const char *const *plugins, bool ignerr);
extern int service_run(void);
extern int service_stop(void);
int service_load_library(const char *path);

int service_unload_library(const char *path);

void* service_query(const char *service_name, const char *module);

void service_release(const char *service_name, const char *module);

int service_console_talk(int argc, char **argv, char *reason, int len);

void service_enum_plugins(ENUM_PLUGINS enum_func);

void service_enum_dependency(const char *plugin, ENUM_PLUGINS enum_func);

void service_enum_reference(const char *module, ENUM_PLUGINS enum_func);
extern BOOL service_register_service(const char *func_name, void *addr);

#ifdef __cplusplus
} /* extern "C" */
#endif
