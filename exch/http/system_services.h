#pragma once
#include <gromox/common_types.hpp>
#include <gromox/mem_file.hpp>
#include <gromox/xarray.hpp>
#include <gromox/double_list.hpp>
#include <gromox/single_list.hpp>

#ifdef __cplusplus
extern "C" {
#endif

extern int system_services_run(void);
extern int system_services_stop(void);

extern BOOL (*system_services_judge_ip)(const char*);
extern BOOL (*system_services_container_add_ip)(const char*);
extern BOOL (*system_services_container_remove_ip)(const char*);
extern BOOL (*system_services_judge_user)(const char*);
extern int (*system_services_add_user_into_temp_list)(const char*, int);
extern BOOL (*system_services_auth_login)(const char*, const char*, char*, char*, char*, int);
extern const char* (*system_services_extension_to_mime)(const char*);
extern void (*system_services_log_info)(int, const char *, ...);

#ifdef __cplusplus
} /* extern "C" */
#endif
