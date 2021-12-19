// SPDX-License-Identifier: AGPL-3.0-or-later WITH linking exception
// SPDX-FileCopyrightText: 2020–2021 grommunio GmbH
// This file is part of Gromox.
#ifdef HAVE_CONFIG_H
#	include "config.h"
#endif
#define PAM_SM_AUTH 1
#include <cstring>
#include <memory>
#include <mutex>
#include <typeinfo>
#include <libHX/misc.h>
#include <security/pam_modules.h>
#include <gromox/defs.h>
#include <gromox/fileio.h>
#include <gromox/paths.h>
#include <gromox/scope.hpp>
#include <gromox/tie.hpp>
#include <gromox/config_file.hpp>
#include <gromox/util.hpp>
#include "../exch/http/service.h"
#ifndef PAM_EXTERN
#	define PAM_EXTERN
#endif

using namespace gromox;

std::shared_ptr<CONFIG_FILE> g_config_file;
static std::mutex g_svc_once;
static constexpr const char *g_dfl_svc_plugins[] = {
	"libgxs_ldap_adaptor.so",
	"libgxs_mysql_adaptor.so",
	"libgxs_authmgr.so",
	nullptr,
};

static int converse(pam_handle_t *pamh, int nargs,
    const struct pam_message **message, struct pam_response **resp)
{
	*resp = nullptr;
	struct pam_conv *conv;
	auto ret = pam_get_item(pamh, PAM_CONV, const_cast<const void **>(reinterpret_cast<void **>(&conv)));

	if (ret == PAM_SUCCESS && conv != nullptr && conv->conv != nullptr)
		ret = conv->conv(nargs, message, resp, conv->appdata_ptr);
	if (*resp == nullptr || (*resp)->resp == nullptr)
		ret = PAM_AUTH_ERR;
	return ret;
}

static int read_password(pam_handle_t *pamh, const char *prompt, char **pass)
{
	struct pam_message msg;
	const struct pam_message *pmsg = &msg;
	struct pam_response *resp = nullptr;

	*pass = nullptr;
	msg.msg_style = PAM_PROMPT_ECHO_OFF;
	msg.msg = prompt != nullptr ? prompt : "Password: ";
	auto ret = converse(pamh, 1, &pmsg, &resp);
	if (ret == PAM_SUCCESS)
		*pass = strdup(resp->resp);
	return ret;
}

PAM_EXTERN GX_EXPORT int pam_sm_authenticate(pam_handle_t *pamh, int flags,
    int argc, const char **argv)
{
	auto cfg = g_config_file = config_file_prg(nullptr, "pam.cfg");
	if (g_config_file == nullptr)
		return PAM_AUTH_ERR;

	const char *service = nullptr, *username = nullptr;
	for (int i = 0; i < argc; ++i)
		if (strncmp(argv[i], "service=", 8) == 0)
			service = argv[i] + 8;
	auto ret = pam_get_user(pamh, &username, nullptr);
	if (ret != PAM_SUCCESS || username == nullptr)
		return PAM_AUTH_ERR;

	const void *authtok_v = nullptr;
	ret = pam_get_item(pamh, PAM_AUTHTOK, &authtok_v);
	if (ret != PAM_SUCCESS)
		return PAM_AUTH_ERR;
	std::unique_ptr<char[], stdlib_delete> authtok;
	if (authtok_v != nullptr) {
		authtok.reset(strdup(static_cast<const char *>(authtok_v)));
	} else {
		ret = read_password(pamh, cfg->get_value("pam_prompt"), &unique_tie(authtok));
		if (ret != PAM_SUCCESS)
			return ret;
	}

	auto svc_plugin_path = cfg->get_value("service_plugin_path");
	if (svc_plugin_path == nullptr)
		svc_plugin_path = PKGLIBDIR;
	struct strvecfree { void operator()(char **s) { HX_zvecfree(s); } };
	char **svc_plugin_list = nullptr;
	auto cl_0 = make_scope_exit([&]() { HX_zvecfree(svc_plugin_list); });
	auto val = cfg->get_value("service_plugin_list");
	if (val != nullptr) {
		svc_plugin_list = read_file_by_line(val);
		if (svc_plugin_list == nullptr)
			return PAM_AUTH_ERR;
	}

	bool svcplug_ignerr = parse_bool(cfg->get_value("service_plugin_ignore_errors"));
	auto config_dir = val = cfg->get_value("config_file_path");
	if (val == nullptr)
		config_dir = PKGSYSCONFDIR "/pam:" PKGSYSCONFDIR;

	std::lock_guard<std::mutex> holder(g_svc_once);
	service_init({svc_plugin_path, config_dir, "", "",
		svc_plugin_list != nullptr ? svc_plugin_list : g_dfl_svc_plugins,
		svcplug_ignerr, 1});
	if (service_run_early() != 0)
		return PAM_AUTH_ERR;
	if (service_run() != 0)
		return PAM_AUTH_ERR;
	auto cleanup_1 = make_scope_exit(service_stop);

	if (service == nullptr || strcmp(service, "smtp") == 0) {
		BOOL (*fptr_login)(const char *, const char *, char *, int);
		fptr_login = reinterpret_cast<decltype(fptr_login)>(service_query("auth_login_smtp", "system", typeid(decltype(*fptr_login))));
		if (fptr_login == nullptr)
			return PAM_AUTH_ERR;
		auto cleanup_2 = make_scope_exit([]() { service_release("auth_login_smtp", "system"); });
		char reason[256];
		return fptr_login(username, authtok.get(), reason, sizeof(reason)) ?
		       PAM_SUCCESS : PAM_AUTH_ERR;
	} else if (strcmp(service, "imap") == 0 || strcmp(service, "pop3") == 0) {
		BOOL (*fptr_login)(const char *, const char *, char *, char *, char *, int);
		fptr_login = reinterpret_cast<decltype(fptr_login)>(service_query("auth_login_pop3", "system", typeid(decltype(*fptr_login))));
		if (fptr_login == nullptr)
			return PAM_AUTH_ERR;
		auto cleanup_2 = make_scope_exit([]() { service_release("auth_login_pop3", "system"); });
		char maildir[256], lang[256], reason[256];
		return fptr_login(username, authtok.get(), maildir, lang, reason, sizeof(reason)) ?
		       PAM_SUCCESS : PAM_AUTH_ERR;
	} else if (strcmp(service, "exch") == 0) {
		BOOL (*fptr_login)(const char *, const char *, char *, char *, char *, int);
		fptr_login = reinterpret_cast<decltype(fptr_login)>(service_query("auth_login_exch", "system", typeid(decltype(*fptr_login))));
		if (fptr_login == nullptr)
			return PAM_AUTH_ERR;
		auto cleanup_2 = make_scope_exit([]() { service_release("auth_login_exch", "system"); });
		char maildir[256], lang[256], reason[256];
		return fptr_login(username, authtok.get(), maildir, lang, reason, sizeof(reason)) ?
		       PAM_SUCCESS : PAM_AUTH_ERR;
	}
	return PAM_AUTH_ERR;
}

PAM_EXTERN GX_EXPORT int pam_sm_setcred(pam_handle_t *pamh, int flags,
    int argc, const char **argv)
{
	return PAM_SUCCESS;
}
