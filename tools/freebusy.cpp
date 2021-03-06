// SPDX-License-Identifier: GPL-2.0-only WITH linking exception
#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <libHX/string.h>
#include <gromox/defs.h>
#include <gromox/exmdb_rpc.hpp>
#include <gromox/mapidefs.h>
#include <gromox/scope.hpp>
#include <gromox/socket.h>
#include <gromox/paths.h>
#include <gromox/tpropval_array.hpp>
#include <gromox/endian_macro.hpp>
#include <gromox/double_list.hpp>
#include <gromox/ext_buffer.hpp>
#include <gromox/list_file.hpp>
#include <gromox/proptags.hpp>
#include <gromox/rop_util.hpp>
#include <gromox/ical.hpp>
#include <gromox/guid.hpp>
#include <gromox/util.hpp>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <csignal>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <cstdint>
#define TRY(expr) do { int klfdv = (expr); if (klfdv != EXT_ERR_SUCCESS) return klfdv; } while (false)
#define SOCKET_TIMEOUT								60

using cookie_jar = std::map<std::string, std::string, std::less<>>;

struct EXMDB_NODE : public EXMDB_ITEM {
	EXMDB_NODE(EXMDB_ITEM &&o) : EXMDB_ITEM(std::move(o)) {}
	int sockd = -1;
	time_t last_time{};
};

struct CONNECT_REQUEST {
	char *prefix;
	char *remote_id;
	BOOL b_private;
};

struct GET_NAMED_PROPIDS_REQUEST {
	const char *dir;
	BOOL b_create;
	const PROPNAME_ARRAY *ppropnames;
};

struct CHECK_FOLDER_PERMISSION_REQUEST {
	const char *dir;
	uint64_t folder_id;
	const char *username;
};

struct LOAD_CONTENT_TABLE_REQUEST {
	const char *dir;
	uint32_t cpid;
	uint64_t folder_id;
	const char *username;
	uint8_t table_flags;
	const RESTRICTION *prestriction;
	const SORTORDER_SET *psorts;
};

struct UNLOAD_TABLE_REQUEST {
	const char *dir;
	uint32_t table_id;
};

struct QUERY_TABLE_REQUEST {
	const char *dir;
	const char *username;
	uint32_t cpid;
	uint32_t table_id;
	const PROPTAG_ARRAY *pproptags;
	uint32_t start_pos;
	int32_t row_needed;
};

struct EVENT_NODE {
	DOUBLE_LIST_NODE node;
	time_t start_time;
	time_t end_time;
	EXCEPTIONINFO *pexception;
	EXTENDEDEXCEPTION *pex_exception;
};

using namespace gromox;

static time_t g_end_time;
static time_t g_start_time;
static const char *g_username;
static std::vector<EXMDB_NODE> g_exmdb_list;
static std::shared_ptr<ICAL_COMPONENT> g_tz_component;

static int exmdb_client_push_connect_request(
	EXT_PUSH *pext, const CONNECT_REQUEST *r)
{
	TRY(ext_buffer_push_string(pext, r->prefix));
	TRY(ext_buffer_push_string(pext, r->remote_id));
	return ext_buffer_push_bool(pext, r->b_private);
}

static int exmdb_client_push_get_named_propids(
	EXT_PUSH *pext, const GET_NAMED_PROPIDS_REQUEST *r)
{
	TRY(ext_buffer_push_string(pext, r->dir));
	TRY(ext_buffer_push_bool(pext, r->b_create));
	return ext_buffer_push_propname_array(pext, r->ppropnames);
}

static int exmdb_client_push_check_folder_permission_request(
	EXT_PUSH *pext, const CHECK_FOLDER_PERMISSION_REQUEST *r)
{
	TRY(ext_buffer_push_string(pext, r->dir));
	TRY(ext_buffer_push_uint64(pext, r->folder_id));
	return ext_buffer_push_string(pext, r->username);
}

static int exmdb_client_push_load_content_table_request(
	EXT_PUSH *pext, const LOAD_CONTENT_TABLE_REQUEST *r)
{
	TRY(ext_buffer_push_string(pext, r->dir));
	TRY(ext_buffer_push_uint32(pext, r->cpid));
	TRY(ext_buffer_push_uint64(pext, r->folder_id));
	if (NULL == r->username) {
		TRY(ext_buffer_push_uint8(pext, 0));
	} else {
		TRY(ext_buffer_push_uint8(pext, 1));
		TRY(ext_buffer_push_string(pext, r->username));
	}
	TRY(ext_buffer_push_uint8(pext, r->table_flags));
	if (NULL == r->prestriction) {
		TRY(ext_buffer_push_uint8(pext, 0));
	} else {
		TRY(ext_buffer_push_uint8(pext, 1));
		TRY(ext_buffer_push_restriction(pext, r->prestriction));
	}
	if (NULL == r->psorts) {
		return ext_buffer_push_uint8(pext, 0);
	}
	TRY(ext_buffer_push_uint8(pext, 1));
	return ext_buffer_push_sortorder_set(pext, r->psorts);
}

static int exmdb_client_push_unload_table_request(
	EXT_PUSH *pext, const UNLOAD_TABLE_REQUEST *r)
{
	TRY(ext_buffer_push_string(pext, r->dir));
	return ext_buffer_push_uint32(pext, r->table_id);
}

static int exmdb_client_push_query_table_request(
	EXT_PUSH *pext, const QUERY_TABLE_REQUEST *r)
{
	TRY(ext_buffer_push_string(pext, r->dir));
	if (NULL == r->username) {
		TRY(ext_buffer_push_uint8(pext, 0));
	} else {
		TRY(ext_buffer_push_uint8(pext, 1));
		TRY(ext_buffer_push_string(pext, r->username));
	}
	TRY(ext_buffer_push_uint32(pext, r->cpid));
	TRY(ext_buffer_push_uint32(pext, r->table_id));
	TRY(ext_buffer_push_proptag_array(pext, r->pproptags));
	TRY(ext_buffer_push_uint32(pext, r->start_pos));
	return ext_buffer_push_int32(pext, r->row_needed);
}

static int exmdb_client_push_request2(EXT_PUSH &ext_push, uint8_t call_id,
	void *prequest, BINARY *pbin_out)
{
	TRY(ext_buffer_push_advance(&ext_push, sizeof(uint32_t)));
	TRY(ext_buffer_push_uint8(&ext_push, call_id));
	switch (call_id) {
	case exmdb_callid::CONNECT:
		TRY(exmdb_client_push_connect_request(&ext_push, static_cast<CONNECT_REQUEST *>(prequest)));
		break;
	case exmdb_callid::GET_NAMED_PROPIDS:
		TRY(exmdb_client_push_get_named_propids(&ext_push, static_cast<GET_NAMED_PROPIDS_REQUEST *>(prequest)));
		break;
	case exmdb_callid::CHECK_FOLDER_PERMISSION:
		TRY(exmdb_client_push_check_folder_permission_request(&ext_push, static_cast<CHECK_FOLDER_PERMISSION_REQUEST *>(prequest)));
		break;
	case exmdb_callid::LOAD_CONTENT_TABLE:
		TRY(exmdb_client_push_load_content_table_request(&ext_push, static_cast<LOAD_CONTENT_TABLE_REQUEST *>(prequest)));
		break;
	case exmdb_callid::UNLOAD_TABLE:
		TRY(exmdb_client_push_unload_table_request(&ext_push, static_cast<UNLOAD_TABLE_REQUEST *>(prequest)));
		break;
	case exmdb_callid::QUERY_TABLE:
		TRY(exmdb_client_push_query_table_request(&ext_push, static_cast<QUERY_TABLE_REQUEST *>(prequest)));
		break;
	default:
		return EXT_ERR_BAD_SWITCH;
	}
	pbin_out->cb = ext_push.offset;
	ext_push.offset = 0;
	ext_buffer_push_uint32(&ext_push,
		pbin_out->cb - sizeof(uint32_t));
	/* memory referenced by ext_push.data will be freed outside */
	pbin_out->pb = ext_buffer_push_release(&ext_push);
	return EXT_ERR_SUCCESS;
}

static int exmdb_client_push_request(uint8_t call_id,
	void *prequest, BINARY *pbin_out)
{
	EXT_PUSH ext_push;
	if (!ext_buffer_push_init(&ext_push, nullptr, 0, EXT_FLAG_WCOUNT))
		return EXT_ERR_ALLOC;
	auto ret = exmdb_client_push_request2(ext_push, call_id, prequest, pbin_out);
	if (ret != 0)
		ext_buffer_push_free(&ext_push);
	return ret;
}

static BOOL exmdb_client_read_socket(int sockd, BINARY *pbin)
{
	fd_set myset;
	int read_len;
	uint32_t offset;
	struct timeval tv;
	uint8_t resp_buff[5];
	
	pbin->cb = 0;
	pbin->pb = NULL;
	while (TRUE) {
		tv.tv_usec = 0;
		tv.tv_sec = SOCKET_TIMEOUT;
		FD_ZERO(&myset);
		FD_SET(sockd, &myset);
		if (select(sockd + 1, &myset, NULL, NULL, &tv) <= 0) {
			if (NULL != pbin->pb) {
				free(pbin->pb);
				pbin->pb = NULL;
			}
			return FALSE;
		}
		if (0 == pbin->cb) {
			read_len = read(sockd, resp_buff, 5);
			if (1 == read_len) {
				pbin->cb = 1;
				pbin->pv = malloc(1);
				if (pbin->pv == nullptr)
					return FALSE;
				*(uint8_t*)pbin->pb = resp_buff[0];
				return TRUE;
			} else if (5 == read_len) {
				pbin->cb = *(uint32_t*)(resp_buff + 1) + 5;
				pbin->pv = malloc(pbin->cb);
				if (pbin->pv == nullptr)
					return FALSE;
				memcpy(pbin->pb, resp_buff, 5);
				offset = 5;
				if (offset == pbin->cb) {
					return TRUE;
				}
				continue;
			} else {
				return FALSE;
			}
		}
		read_len = read(sockd,
			pbin->pb + offset,
			pbin->cb - offset);
		if (read_len <= 0) {
			free(pbin->pb);
			pbin->pb = NULL;
			return FALSE;
		}
		offset += read_len;
		if (offset == pbin->cb) {
			return TRUE;
		}
	}
}

static BOOL exmdb_client_write_socket(
	int sockd, const BINARY *pbin)
{
	int written_len;
	uint32_t offset;
	
	offset = 0;
	while (TRUE) {
		written_len = write(sockd,
				pbin->pb + offset,
				pbin->cb - offset);
		if (written_len <= 0) {
			return FALSE;
		}
		offset += written_len;
		if (offset == pbin->cb) {
			return TRUE;
		}
	}
}

static BOOL exmdb_client_get_named_propids(int sockd, const char *dir,
	BOOL b_create, const PROPNAME_ARRAY *ppropnames, PROPID_ARRAY *ppropids)
{
	BINARY tmp_bin;
	EXT_PULL ext_pull;
	GET_NAMED_PROPIDS_REQUEST request;
	
	request.dir = dir;
	request.b_create = b_create;
	request.ppropnames = ppropnames;
	if (exmdb_client_push_request(exmdb_callid::GET_NAMED_PROPIDS,
	    &request, &tmp_bin) != EXT_ERR_SUCCESS)
		return FALSE;	
	if (FALSE == exmdb_client_write_socket(sockd, &tmp_bin)) {
		return FALSE;
	}
	if (FALSE == exmdb_client_read_socket(sockd, &tmp_bin)) {
		return FALSE;
	}
	if (tmp_bin.cb < 5 || tmp_bin.pb[0] != exmdb_response::SUCCESS)
		return FALSE;
	ext_buffer_pull_init(&ext_pull, tmp_bin.pb + 5,
		tmp_bin.cb - 5, malloc, EXT_FLAG_WCOUNT);
	if (EXT_ERR_SUCCESS != ext_buffer_pull_propid_array(
		&ext_pull, ppropids)) {
		return FALSE;
	}
	return TRUE;
}

static BOOL exmdb_client_check_folder_permission(int sockd,
	const char *dir, uint64_t folder_id, const char *username,
	uint32_t *ppermission)
{
	BINARY tmp_bin;
	CHECK_FOLDER_PERMISSION_REQUEST request;
	
	request.dir = dir;
	request.folder_id = folder_id;
	request.username = username;
	if (exmdb_client_push_request(exmdb_callid::CHECK_FOLDER_PERMISSION,
	    &request, &tmp_bin) != EXT_ERR_SUCCESS)
		return FALSE;
	if (FALSE == exmdb_client_write_socket(sockd, &tmp_bin)) {
		return FALSE;
	}
	if (FALSE == exmdb_client_read_socket(sockd, &tmp_bin)) {
		return FALSE;
	}
	if (tmp_bin.cb != 9 || tmp_bin.pb[0] != exmdb_response::SUCCESS)
		return FALSE;
	*ppermission = *(uint32_t*)(tmp_bin.pb + 5);
	return TRUE;
}

static BOOL exmdb_client_load_content_table(int sockd, const char *dir,
	uint32_t cpid, uint64_t folder_id, const char *username,
	uint8_t table_flags, const RESTRICTION *prestriction,
	const SORTORDER_SET *psorts, uint32_t *ptable_id, uint32_t *prow_count)
{
	BINARY tmp_bin;
	LOAD_CONTENT_TABLE_REQUEST request;
	
	request.dir = dir;
	request.cpid = cpid;
	request.folder_id = folder_id;
	request.username = username;
	request.table_flags = table_flags;
	request.prestriction = prestriction;
	request.psorts = psorts;
	if (exmdb_client_push_request(exmdb_callid::LOAD_CONTENT_TABLE,
	    &request, &tmp_bin) != EXT_ERR_SUCCESS)
		return FALSE;
	if (FALSE == exmdb_client_write_socket(sockd, &tmp_bin)) {
		return FALSE;
	}
	if (FALSE == exmdb_client_read_socket(sockd, &tmp_bin)) {
		return FALSE;
	}
	if (tmp_bin.cb != 13 || tmp_bin.pb[0] != exmdb_response::SUCCESS)
		return FALSE;
	*ptable_id = *(uint32_t*)(tmp_bin.pb + 5);
	*prow_count = *(uint32_t*)(tmp_bin.pb + 9);
	return TRUE;
}

static BOOL exmdb_client_unload_table(int sockd,
	const char *dir, uint32_t table_id)
{
	BINARY tmp_bin;
	UNLOAD_TABLE_REQUEST request;
	
	request.dir = dir;
	request.table_id = table_id;
	if (exmdb_client_push_request(exmdb_callid::UNLOAD_TABLE,
	    &request, &tmp_bin) != EXT_ERR_SUCCESS)
		return FALSE;
	if (FALSE == exmdb_client_write_socket(sockd, &tmp_bin)) {
		return FALSE;
	}
	if (FALSE == exmdb_client_read_socket(sockd, &tmp_bin)) {
		return FALSE;
	}
	if (tmp_bin.cb != 5 || tmp_bin.pb[0] != exmdb_response::SUCCESS)
		return FALSE;
	return TRUE;
}

static BOOL exmdb_client_query_table(int sockd, const char *dir,
	const char *username, uint32_t cpid, uint32_t table_id,
	const PROPTAG_ARRAY *pproptags, uint32_t start_pos,
	int32_t row_needed, TARRAY_SET *pset)
{
	BINARY tmp_bin;
	EXT_PULL ext_pull;
	QUERY_TABLE_REQUEST request;
	
	request.dir = dir;
	request.username = username;
	request.cpid = cpid;
	request.table_id = table_id;
	request.pproptags = pproptags;
	request.start_pos = start_pos;
	request.row_needed = row_needed;
	if (exmdb_client_push_request(exmdb_callid::QUERY_TABLE,
	    &request, &tmp_bin) != EXT_ERR_SUCCESS)
		return FALSE;	
	if (FALSE == exmdb_client_write_socket(sockd, &tmp_bin)) {
		return FALSE;
	}
	if (FALSE == exmdb_client_read_socket(sockd, &tmp_bin)) {
		return FALSE;
	}
	if (tmp_bin.pb[0] != exmdb_response::SUCCESS)
		return FALSE;
	ext_buffer_pull_init(&ext_pull, tmp_bin.pb + 5,
		tmp_bin.cb - 5, malloc, EXT_FLAG_WCOUNT);
	if (EXT_ERR_SUCCESS != ext_buffer_pull_tarray_set(
		&ext_pull, pset)) {
		return FALSE;
	}
	return TRUE;
}

static void cache_connection(const char *dir, int sockd)
{
	auto i = std::find_if(g_exmdb_list.begin(), g_exmdb_list.end(),
	         [&](const EXMDB_ITEM &s) { return strncmp(s.prefix.c_str(), dir, s.prefix.size()) == 0; });
	if (i == g_exmdb_list.end())
		return;
	i->sockd = sockd;
	time(&i->last_time);
}

static int connect_exmdb(const char *dir)
{
	int process_id;
	BINARY tmp_bin;
	char remote_id[128];
	char tmp_buff[1024];
	uint8_t response_code;
	CONNECT_REQUEST request;
	
	auto pexnode = std::find_if(g_exmdb_list.begin(), g_exmdb_list.end(),
	               [&](const EXMDB_ITEM &s) { return strncmp(s.prefix.c_str(), dir, s.prefix.size()) == 0; });
	if (pexnode == g_exmdb_list.end())
		return -1;
	if (-1 != pexnode->sockd) {
		if (time(NULL) - pexnode->last_time > SOCKET_TIMEOUT - 3) {
			close(pexnode->sockd);
			pexnode->sockd = -1;
		} else {
			return pexnode->sockd;
		}
	}
	int sockd = gx_inet_connect(pexnode->host.c_str(), pexnode->port, 0);
	if (sockd < 0)
	        return -1;
	process_id = getpid();
	sprintf(remote_id, "freebusy:%d", process_id);
	request.prefix    = deconst(pexnode->prefix.c_str());
	request.remote_id = remote_id;
	request.b_private = TRUE;
	if (exmdb_client_push_request(exmdb_callid::CONNECT, &request,
	    &tmp_bin) != EXT_ERR_SUCCESS) {
		close(sockd);
		return -1;
	}
	if (FALSE == exmdb_client_write_socket(sockd, &tmp_bin)) {
		close(sockd);
		return -1;
	}
	tmp_bin.pv = tmp_buff;
	if (FALSE == exmdb_client_read_socket(sockd, &tmp_bin)) {
		close(sockd);
		return -1;
	}
	response_code = tmp_bin.pb[0];
	if (response_code == exmdb_response::SUCCESS) {
		if (5 != tmp_bin.cb || 0 != *(uint32_t*)(tmp_bin.pb + 1)) {
			fprintf(stderr, "response format error during connect to "
				"[%s]:%hu/%s\n", pexnode->host.c_str(),
				pexnode->port, pexnode->prefix.c_str());
			close(sockd);
			return -1;
		}
		return sockd;
	}
	fprintf(stderr, "Failed to connect to [%s]:%hu/%s",
	        pexnode->host.c_str(), pexnode->port, pexnode->prefix.c_str());
	switch (response_code) {
	case exmdb_response::ACCESS_DENY:
		fprintf(stderr, ": access deniedn\n");
		break;
	case exmdb_response::MAX_REACHED:
		fprintf(stderr, ": maximum connections reached in server\n");
		break;
	case exmdb_response::LACK_MEMORY:
		fprintf(stderr, ": server out of memory\n");
		break;
	case exmdb_response::MISCONFIG_PREFIX:
		fprintf(stderr, ": prefix not served by server\n");
		break;
	case exmdb_response::MISCONFIG_MODE:
		fprintf(stderr, ": misconfigured prefix mode\n");
		break;
	default:
		fprintf(stderr, ": error code %d\n", response_code);
		break;
	}
	close(sockd);
	return -1;
}

static std::shared_ptr<ICAL_COMPONENT> tzstruct_to_vtimezone(int year,
	const char *tzid, TIMEZONESTRUCT *ptzstruct)
{
	int day;
	int order;
	int utc_offset;
	std::shared_ptr<ICAL_VALUE> pivalue;
	char tmp_buff[1024];
	
	auto pcomponent = ical_new_component("VTIMEZONE");
	if (NULL == pcomponent) {
		return NULL;
	}
	auto piline = ical_new_simple_line("TZID", tzid);
	if (NULL == piline) {
		return NULL;
	}
	if (pcomponent->append_line(piline) < 0)
		return nullptr;
	/* STANDARD component */
	auto pcomponent1 = ical_new_component("STANDARD");
	if (NULL == pcomponent1) {
		return NULL;
	}
	if (pcomponent->append_comp(pcomponent1) < 0)
		return nullptr;
	if (0 == ptzstruct->daylightdate.month) {
		strcpy(tmp_buff, "16010101T000000");
	} else {
		if (0 == ptzstruct->standarddate.year) {
			day = ical_get_dayofmonth(year,
				ptzstruct->standarddate.month,
				ptzstruct->standarddate.day,
				ptzstruct->standarddate.dayofweek);
			sprintf(tmp_buff, "%04d%02d%02dT%02d%02d%02d",
				year, (int)ptzstruct->standarddate.month,
				day, (int)ptzstruct->standarddate.hour,
				(int)ptzstruct->standarddate.minute,
				(int)ptzstruct->standarddate.second);
		} else if (1 == ptzstruct->standarddate.year) {
			sprintf(tmp_buff, "%04d%02d%02dT%02d%02d%02d",
				year, (int)ptzstruct->standarddate.month,
				(int)ptzstruct->standarddate.day,
				(int)ptzstruct->standarddate.hour,
				(int)ptzstruct->standarddate.minute,
				(int)ptzstruct->standarddate.second);
		} else {
			return NULL;
		}
	}
	piline = ical_new_simple_line("DTSTART", tmp_buff);
	if (NULL == piline) {
		return NULL;
	}
	if (pcomponent1->append_line(piline) < 0)
		return nullptr;
	if (0 != ptzstruct->daylightdate.month) {
		if (0 == ptzstruct->standarddate.year) {
			piline = ical_new_line("RRULE");
			if (NULL == piline) {
				return NULL;
			}
			if (pcomponent1->append_line(piline) < 0)
				return nullptr;
			pivalue = ical_new_value("FREQ");
			if (NULL == pivalue) {
				return NULL;
			}
			if (piline->append_value(pivalue) < 0)
				return nullptr;
			if (!pivalue->append_subval("YEARLY"))
				return NULL;
			pivalue = ical_new_value("BYDAY");
			if (NULL == pivalue) {
				return NULL;
			}
			if (piline->append_value(pivalue) < 0)
				return nullptr;
			order = ptzstruct->standarddate.day;
			if (5 == order) {
				order = -1;
			}
			switch (ptzstruct->standarddate.dayofweek) {
			case 0:
				sprintf(tmp_buff, "%dSU", order);
				break;
			case 1:
				sprintf(tmp_buff, "%dMO", order);
				break;
			case 2:
				sprintf(tmp_buff, "%dTU", order);
				break;
			case 3:
				sprintf(tmp_buff, "%dWE", order);
				break;
			case 4:
				sprintf(tmp_buff, "%dTH", order);
				break;
			case 5:
				sprintf(tmp_buff, "%dFR", order);
				break;
			case 6:
				sprintf(tmp_buff, "%dSA", order);
				break;
			default:
				return NULL;
			}
			if (!pivalue->append_subval(tmp_buff))
				return NULL;
			pivalue = ical_new_value("BYMONTH");
			if (NULL == pivalue) {
				return NULL;
			}
			if (piline->append_value(pivalue) < 0)
				return nullptr;
			sprintf(tmp_buff, "%d", (int)ptzstruct->standarddate.month);
			if (!pivalue->append_subval(tmp_buff))
				return NULL;
		} else if (1 == ptzstruct->standarddate.year) {
			piline = ical_new_line("RRULE");
			if (NULL == piline) {
				return NULL;
			}
			if (pcomponent1->append_line(piline) < 0)
				return nullptr;
			pivalue = ical_new_value("FREQ");
			if (NULL == pivalue) {
				return NULL;
			}
			if (piline->append_value(pivalue) < 0)
				return nullptr;
			if (!pivalue->append_subval("YEARLY"))
				return NULL;
			pivalue = ical_new_value("BYMONTHDAY");
			if (NULL == pivalue) {
				return NULL;
			}
			if (piline->append_value(pivalue) < 0)
				return nullptr;
			sprintf(tmp_buff, "%d", (int)ptzstruct->standarddate.day);
			pivalue = ical_new_value("BYMONTH");
			if (NULL == pivalue) {
				return NULL;
			}
			if (piline->append_value(pivalue) < 0)
				return nullptr;
			sprintf(tmp_buff, "%d", (int)ptzstruct->standarddate.month);
			if (!pivalue->append_subval(tmp_buff))
				return NULL;
		}
	}
	utc_offset = (-1)*(ptzstruct->bias + ptzstruct->daylightbias);
	if (utc_offset >= 0) {
		tmp_buff[0] = '+';
	} else {
		tmp_buff[0] = '-';
	}
	utc_offset = abs(utc_offset);
	sprintf(tmp_buff + 1, "%02d%02d", utc_offset/60, utc_offset%60);
	piline = ical_new_simple_line("TZOFFSETFROM", tmp_buff);
	if (piline == nullptr)
		return nullptr;
	if (pcomponent1->append_line(piline) < 0)
		return nullptr;
	utc_offset = (-1)*(ptzstruct->bias + ptzstruct->standardbias);
	if (utc_offset >= 0) {
		tmp_buff[0] = '+';
	} else {
		tmp_buff[0] = '-';
	}
	utc_offset = abs(utc_offset);
	sprintf(tmp_buff + 1, "%02d%02d", utc_offset/60, utc_offset%60);
	piline = ical_new_simple_line("TZOFFSETTO", tmp_buff);
	if (piline == nullptr)
		return nullptr;
	if (pcomponent1->append_line(piline) < 0)
		return nullptr;
	if (0 == ptzstruct->daylightdate.month) {
		return pcomponent;
	}
	/* DAYLIGHT component */
	pcomponent1 = ical_new_component("DAYLIGHT");
	if (NULL == pcomponent1) {
		return NULL;
	}
	if (pcomponent->append_comp(pcomponent1) < 0)
		return nullptr;
	if (0 == ptzstruct->daylightdate.year) {
		day = ical_get_dayofmonth(year,
			ptzstruct->daylightdate.month,
			ptzstruct->daylightdate.day,
			ptzstruct->daylightdate.dayofweek);
		sprintf(tmp_buff, "%04d%02d%02dT%02d%02d%02d",
			year, (int)ptzstruct->daylightdate.month,
			day, (int)ptzstruct->daylightdate.hour,
			(int)ptzstruct->daylightdate.minute,
			(int)ptzstruct->daylightdate.second);
	} else if (1 == ptzstruct->daylightdate.year) {
		sprintf(tmp_buff, "%04d%02d%02dT%02d%02d%02d",
			year, (int)ptzstruct->daylightdate.month,
			(int)ptzstruct->daylightdate.day,
			(int)ptzstruct->daylightdate.hour,
			(int)ptzstruct->daylightdate.minute,
			(int)ptzstruct->daylightdate.second);
	} else {
		return NULL;
	}
	piline = ical_new_simple_line("DTSTART", tmp_buff);
	if (NULL == piline) {
		return NULL;
	}
	if (pcomponent1->append_line(piline) < 0)
		return nullptr;
	if (0 == ptzstruct->daylightdate.year) {
		piline = ical_new_line("RRULE");
		if (NULL == piline) {
			return NULL;
		}
		if (pcomponent1->append_line(piline) < 0)
			return nullptr;
		pivalue = ical_new_value("FREQ");
		if (NULL == pivalue) {
			return NULL;
		}
		if (piline->append_value(pivalue) < 0)
			return nullptr;
		if (!pivalue->append_subval("YEARLY"))
			return NULL;
		pivalue = ical_new_value("BYDAY");
		if (NULL == pivalue) {
			return NULL;
		}
		if (piline->append_value(pivalue) < 0)
			return nullptr;
		order = ptzstruct->daylightdate.day;
		if (5 == order) {
			order = -1;
		}
		switch (ptzstruct->daylightdate.dayofweek) {
		case 0:
			sprintf(tmp_buff, "%dSU", order);
			break;
		case 1:
			sprintf(tmp_buff, "%dMO", order);
			break;
		case 2:
			sprintf(tmp_buff, "%dTU", order);
			break;
		case 3:
			sprintf(tmp_buff, "%dWE", order);
			break;
		case 4:
			sprintf(tmp_buff, "%dTH", order);
			break;
		case 5:
			sprintf(tmp_buff, "%dFR", order);
			break;
		case 6:
			sprintf(tmp_buff, "%dSA", order);
			break;
		default:
			return NULL;
		}
		if (!pivalue->append_subval(tmp_buff))
			return NULL;
		pivalue = ical_new_value("BYMONTH");
		if (NULL == pivalue) {
			return NULL;
		}
		if (piline->append_value(pivalue) < 0)
			return nullptr;
		sprintf(tmp_buff, "%d", (int)ptzstruct->daylightdate.month);
		if (!pivalue->append_subval(tmp_buff))
			return NULL;
	} else if (1 == ptzstruct->daylightdate.year) {
		piline = ical_new_line("RRULE");
		if (NULL == piline) {
			return NULL;
		}
		if (pcomponent1->append_line(piline) < 0)
			return nullptr;
		pivalue = ical_new_value("FREQ");
		if (NULL == pivalue) {
			return NULL;
		}
		if (piline->append_value(pivalue) < 0)
			return nullptr;
		if (!pivalue->append_subval("YEARLY"))
			return NULL;
		pivalue = ical_new_value("BYMONTHDAY");
		if (NULL == pivalue) {
			return NULL;
		}
		if (piline->append_value(pivalue) < 0)
			return nullptr;
		sprintf(tmp_buff, "%d", (int)ptzstruct->daylightdate.day);
		pivalue = ical_new_value("BYMONTH");
		if (NULL == pivalue) {
			return NULL;
		}
		if (piline->append_value(pivalue) < 0)
			return nullptr;
		sprintf(tmp_buff, "%d", (int)ptzstruct->daylightdate.month);
		if (!pivalue->append_subval(tmp_buff))
			return NULL;
	}
	utc_offset = (-1)*(ptzstruct->bias + ptzstruct->standardbias);
	if (utc_offset >= 0) {
		tmp_buff[0] = '+';
	} else {
		tmp_buff[0] = '-';
	}
	utc_offset = abs(utc_offset);
	sprintf(tmp_buff + 1, "%02d%02d", utc_offset/60, utc_offset%60);
	piline = ical_new_simple_line("TZOFFSETFROM", tmp_buff);
	if (piline == nullptr)
		return nullptr;
	if (pcomponent1->append_line(piline) < 0)
		return nullptr;
	utc_offset = (-1)*(ptzstruct->bias + ptzstruct->daylightbias);
	if (utc_offset >= 0) {
		tmp_buff[0] = '+';
	} else {
		tmp_buff[0] = '-';
	}
	utc_offset = abs(utc_offset);
	sprintf(tmp_buff + 1, "%02d%02d", utc_offset/60, utc_offset%60);
	piline = ical_new_simple_line("TZOFFSETTO", tmp_buff);
	if (piline == nullptr)
		return nullptr;
	if (pcomponent1->append_line(piline) < 0)
		return nullptr;
	return pcomponent;
}

static BOOL recurrencepattern_to_rrule(std::shared_ptr<ICAL_COMPONENT> ptz_component,
	time_t whole_start_time, const APPOINTMENTRECURRENCEPATTERN *papprecurr,
	ICAL_RRULE *pirrule)
{
	ICAL_TIME itime;
	time_t unix_time;
	uint64_t nt_time;
	std::shared_ptr<ICAL_VALUE> pivalue;
	char tmp_buff[1024];
	
	auto piline = ical_new_line("RRULE");
	if (NULL == piline) {
		return FALSE;
	}
	switch (papprecurr->recurrencepattern.patterntype) {
	case PATTERNTYPE_DAY:
		pivalue = ical_new_value("FREQ");
		if (NULL == pivalue) {
			return FALSE;
		}
		if (piline->append_value(pivalue) < 0)
			return false;
		if (!pivalue->append_subval("DAILY"))
			return FALSE;
		sprintf(tmp_buff, "%u",
			papprecurr->recurrencepattern.period/1440);
		pivalue = ical_new_value("INTERVAL");
		if (NULL == pivalue) {
			return FALSE;
		}
		if (piline->append_value(pivalue) < 0)
			return false;
		if (!pivalue->append_subval(tmp_buff))
			return FALSE;
		break;
	case PATTERNTYPE_WEEK:
		pivalue = ical_new_value("FREQ");
		if (NULL == pivalue) {
			return FALSE;
		}
		if (piline->append_value(pivalue) < 0)
			return false;
		if (!pivalue->append_subval("WEEKLY"))
			return FALSE;
		sprintf(tmp_buff, "%u",
			papprecurr->recurrencepattern.period);
		pivalue = ical_new_value("INTERVAL");
		if (NULL == pivalue) {
			return FALSE;
		}
		if (piline->append_value(pivalue) < 0)
			return false;
		if (!pivalue->append_subval(tmp_buff))
			return FALSE;
		pivalue = ical_new_value("BYDAY");
		if (NULL == pivalue) {
			return FALSE;
		}
		if (piline->append_value(pivalue) < 0)
			return false;
		if (WEEKRECURRENCEPATTERN_SU&
			papprecurr->recurrencepattern.
			patterntypespecific.weekrecurrence) {
			if (!pivalue->append_subval("SU"))
				return FALSE;
		}
		if (WEEKRECURRENCEPATTERN_M&
			papprecurr->recurrencepattern.
			patterntypespecific.weekrecurrence) {
			if (!pivalue->append_subval("MO"))
				return FALSE;
		}
		if (WEEKRECURRENCEPATTERN_TU&
			papprecurr->recurrencepattern.
			patterntypespecific.weekrecurrence) {
			if (!pivalue->append_subval("TU"))
				return FALSE;
		}
		if (WEEKRECURRENCEPATTERN_W&
			papprecurr->recurrencepattern.
			patterntypespecific.weekrecurrence) {
			if (!pivalue->append_subval("WE"))
				return FALSE;
		}
		if (WEEKRECURRENCEPATTERN_TH&
			papprecurr->recurrencepattern.
			patterntypespecific.weekrecurrence) {
			if (!pivalue->append_subval("TH"))
				return FALSE;
		}
		if (WEEKRECURRENCEPATTERN_F&
			papprecurr->recurrencepattern.
			patterntypespecific.weekrecurrence) {
			if (!pivalue->append_subval("FR"))
				return FALSE;
		}
		if (WEEKRECURRENCEPATTERN_SA&
			papprecurr->recurrencepattern.
			patterntypespecific.weekrecurrence) {
			if (!pivalue->append_subval("SA"))
				return FALSE;
		}
		break;
	case PATTERNTYPE_MONTH:
	case PATTERNTYPE_HJMONTH:
		pivalue = ical_new_value("FREQ");
		if (NULL == pivalue) {
			return FALSE;
		}
		if (piline->append_value(pivalue) < 0)
			return false;
		if (0 != papprecurr->recurrencepattern.period%12) {
			if (!pivalue->append_subval("MONTHLY"))
				return FALSE;
			sprintf(tmp_buff, "%u",
				papprecurr->recurrencepattern.period);
			pivalue = ical_new_value("INTERVAL");
			if (NULL == pivalue) {
				return FALSE;
			}
			if (piline->append_value(pivalue) < 0)
				return false;
			if (!pivalue->append_subval(tmp_buff))
				return FALSE;
			pivalue = ical_new_value("BYMONTHDAY");
			if (NULL == pivalue) {
				return FALSE;
			}
			if (piline->append_value(pivalue) < 0)
				return false;
			if (31 == papprecurr->recurrencepattern.
				patterntypespecific.dayofmonth) {
				strcpy(tmp_buff, "-1");
			} else {
				sprintf(tmp_buff, "%u",
					papprecurr->recurrencepattern.
					patterntypespecific.dayofmonth);
			}
			if (!pivalue->append_subval(tmp_buff))
				return FALSE;
		} else {
			if (!pivalue->append_subval("YEARLY"))
				return FALSE;
			sprintf(tmp_buff, "%u",
				papprecurr->recurrencepattern.period/12);
			pivalue = ical_new_value("INTERVAL");
			if (NULL == pivalue) {
				return FALSE;
			}
			if (piline->append_value(pivalue) < 0)
				return false;
			if (!pivalue->append_subval(tmp_buff))
				return FALSE;
			pivalue = ical_new_value("BYMONTHDAY");
			if (NULL == pivalue) {
				return FALSE;
			}
			if (piline->append_value(pivalue) < 0)
				return false;
			if (31 == papprecurr->recurrencepattern.
				patterntypespecific.dayofmonth) {
				strcpy(tmp_buff, "-1");
			} else {
				sprintf(tmp_buff, "%u",
					papprecurr->recurrencepattern.
					patterntypespecific.dayofmonth);
			}
			if (!pivalue->append_subval(tmp_buff))
				return FALSE;
			pivalue = ical_new_value("BYMONTH");
			if (NULL == pivalue) {
				return FALSE;
			}
			if (piline->append_value(pivalue) < 0)
				return false;
			ical_get_itime_from_yearday(1601, 
				papprecurr->recurrencepattern.firstdatetime/
				1440 + 1, &itime);
			sprintf(tmp_buff, "%u", itime.month);
			if (!pivalue->append_subval(tmp_buff))
				return FALSE;
		}
		break;
	case PATTERNTYPE_MONTHNTH:
	case PATTERNTYPE_HJMONTHNTH:
		pivalue = ical_new_value("FREQ");
		if (NULL == pivalue) {
			return FALSE;
		}
		if (piline->append_value(pivalue) < 0)
			return false;
		if (0 != papprecurr->recurrencepattern.period%12) {
			if (!pivalue->append_subval("MONTHLY"))
				return FALSE;
			sprintf(tmp_buff, "%u",
				papprecurr->recurrencepattern.period);
			pivalue = ical_new_value("INTERVAL");
			if (NULL == pivalue) {
				return FALSE;
			}
			if (piline->append_value(pivalue) < 0)
				return false;
			if (!pivalue->append_subval(tmp_buff))
				return FALSE;
			pivalue = ical_new_value("BYDAY");
			if (NULL == pivalue) {
				return FALSE;
			}
			if (piline->append_value(pivalue) < 0)
				return false;
			if (WEEKRECURRENCEPATTERN_SU&papprecurr->recurrencepattern.
				patterntypespecific.monthnth.weekrecurrence) {
				if (!pivalue->append_subval("SU"))
					return FALSE;
			}
			if (WEEKRECURRENCEPATTERN_M&papprecurr->recurrencepattern.
				patterntypespecific.monthnth.weekrecurrence) {
				if (!pivalue->append_subval("MO"))
					return FALSE;
			}
			if (WEEKRECURRENCEPATTERN_TU&papprecurr->recurrencepattern.
				patterntypespecific.monthnth.weekrecurrence) {
				if (!pivalue->append_subval("TU"))
					return FALSE;
			}
			if (WEEKRECURRENCEPATTERN_W&papprecurr->recurrencepattern.
				patterntypespecific.monthnth.weekrecurrence) {
				if (!pivalue->append_subval("WE"))
					return FALSE;
			}
			if (WEEKRECURRENCEPATTERN_TH&papprecurr->recurrencepattern.
				patterntypespecific.monthnth.weekrecurrence) {
				if (!pivalue->append_subval("TH"))
					return FALSE;
			}
			if (WEEKRECURRENCEPATTERN_F&papprecurr->recurrencepattern.
				patterntypespecific.monthnth.weekrecurrence) {
				if (!pivalue->append_subval("FR"))
					return FALSE;
			}
			if (WEEKRECURRENCEPATTERN_SA&papprecurr->recurrencepattern.
				patterntypespecific.monthnth.weekrecurrence) {
				if (!pivalue->append_subval("SA"))
					return FALSE;
			}
			pivalue = ical_new_value("BYSETPOS");
			if (NULL == pivalue) {
				return FALSE;
			}
			if (piline->append_value(pivalue) < 0)
				return false;
			if (5 == papprecurr->recurrencepattern.
				patterntypespecific.monthnth.recurrencenum) {
				strcpy(tmp_buff, "-1");
			} else {
				sprintf(tmp_buff, "%u",
					papprecurr->recurrencepattern.
					patterntypespecific.monthnth.recurrencenum);
			}
			if (!pivalue->append_subval(tmp_buff))
				return FALSE;
		} else {
			if (!pivalue->append_subval("YEARLY"))
				return FALSE;
			sprintf(tmp_buff, "%u",
				papprecurr->recurrencepattern.period/12);
			pivalue = ical_new_value("INTERVAL");
			if (NULL == pivalue) {
				return FALSE;
			}
			if (piline->append_value(pivalue) < 0)
				return false;
			if (!pivalue->append_subval(tmp_buff))
				return FALSE;
			pivalue = ical_new_value("BYDAY");
			if (NULL == pivalue) {
				return FALSE;
			}
			if (piline->append_value(pivalue) < 0)
				return false;
			if (WEEKRECURRENCEPATTERN_SU&papprecurr->recurrencepattern.
				patterntypespecific.monthnth.weekrecurrence) {
				if (!pivalue->append_subval("SU"))
					return FALSE;
			}
			if (WEEKRECURRENCEPATTERN_M&papprecurr->recurrencepattern.
				patterntypespecific.monthnth.weekrecurrence) {
				if (!pivalue->append_subval("MO"))
					return FALSE;
			}
			if (WEEKRECURRENCEPATTERN_TU&papprecurr->recurrencepattern.
				patterntypespecific.monthnth.weekrecurrence) {
				if (!pivalue->append_subval("TU"))
					return FALSE;
			}
			if (WEEKRECURRENCEPATTERN_W&papprecurr->recurrencepattern.
				patterntypespecific.monthnth.weekrecurrence) {
				if (!pivalue->append_subval("WE"))
					return FALSE;
			}
			if (WEEKRECURRENCEPATTERN_TH&papprecurr->recurrencepattern.
				patterntypespecific.monthnth.weekrecurrence) {
				if (!pivalue->append_subval("TH"))
					return FALSE;
			}
			if (WEEKRECURRENCEPATTERN_F&papprecurr->recurrencepattern.
				patterntypespecific.monthnth.weekrecurrence) {
				if (!pivalue->append_subval("FR"))
					return FALSE;
			}
			if (WEEKRECURRENCEPATTERN_SA&papprecurr->recurrencepattern.
				patterntypespecific.monthnth.weekrecurrence) {
				if (!pivalue->append_subval("SA"))
					return FALSE;
			}
			pivalue = ical_new_value("BYSETPOS");
			if (NULL == pivalue) {
				return FALSE;
			}
			if (piline->append_value(pivalue) < 0)
				return false;
			if (5 == papprecurr->recurrencepattern.
				patterntypespecific.monthnth.recurrencenum) {
				strcpy(tmp_buff, "-1");
			} else {
				sprintf(tmp_buff, "%u",
					papprecurr->recurrencepattern.
					patterntypespecific.monthnth.recurrencenum);
			}
			if (!pivalue->append_subval(tmp_buff))
				return FALSE;
			pivalue = ical_new_value("BYMONTH");
			if (NULL == pivalue) {
				return FALSE;
			}
			if (piline->append_value(pivalue) < 0)
				return false;
			sprintf(tmp_buff, "%u",
				papprecurr->recurrencepattern.firstdatetime);
			if (!pivalue->append_subval(tmp_buff))
				return FALSE;
		}
		break;
	default:
		return FALSE;
	}
	if (ENDTYPE_AFTER_N_OCCURRENCES ==
		papprecurr->recurrencepattern.endtype) {
		sprintf(tmp_buff, "%u",
			papprecurr->recurrencepattern.occurrencecount);
		pivalue = ical_new_value("COUNT");
		if (NULL == pivalue) {
			return FALSE;
		}
		if (piline->append_value(pivalue) < 0)
			return false;
		if (!pivalue->append_subval(tmp_buff))
			return FALSE;
	} else if (ENDTYPE_AFTER_DATE ==
		papprecurr->recurrencepattern.endtype) {
		nt_time = papprecurr->recurrencepattern.enddate
						+ papprecurr->starttimeoffset;
		nt_time *= 600000000;
		unix_time = rop_util_nttime_to_unix(nt_time);
		ical_utc_to_datetime(ptz_component, unix_time, &itime);
		sprintf(tmp_buff, "%04d%02d%02dT%02d%02d%02dZ",
			itime.year, itime.month, itime.day,
			itime.hour, itime.minute, itime.second);
		pivalue = ical_new_value("UNTIL");
		if (NULL == pivalue) {
			return FALSE;
		}
		if (piline->append_value(pivalue) < 0)
			return false;
		if (!pivalue->append_subval(tmp_buff))
			return FALSE;
	}
	if (PATTERNTYPE_WEEK == papprecurr->recurrencepattern.patterntype) {
		pivalue = ical_new_value("WKST");
		if (NULL == pivalue) {
			return FALSE;
		}
		if (piline->append_value(pivalue) < 0)
			return false;
		switch (papprecurr->recurrencepattern.firstdow) {
		case 0:
			if (!pivalue->append_subval("SU"))
				return FALSE;
			break;
		case 1:
			if (!pivalue->append_subval("MO"))
				return FALSE;
			break;
		case 2:
			if (!pivalue->append_subval("TU"))
				return FALSE;
			break;
		case 3:
			if (!pivalue->append_subval("WE"))
				return FALSE;
			break;
		case 4:
			if (!pivalue->append_subval("TH"))
				return FALSE;
			break;
		case 5:
			if (!pivalue->append_subval("FR"))
				return FALSE;
			break;
		case 6:
			if (!pivalue->append_subval("SA"))
				return FALSE;
			break;
		default:
			return FALSE;
		}
	}
	return ical_parse_rrule(
		ptz_component, whole_start_time,
		&piline->value_list, pirrule) ? TRUE : false;
}

static BOOL find_recurrence_times(std::shared_ptr<ICAL_COMPONENT> ptz_component,
	time_t whole_start_time, const APPOINTMENTRECURRENCEPATTERN *papprecurr,
	time_t start_time, time_t end_time, DOUBLE_LIST *plist)
{
	int i;
	ICAL_TIME itime;
	time_t tmp_time;
	time_t tmp_time1;
	uint64_t nt_time;
	ICAL_RRULE irrule;
	EVENT_NODE *pevnode;
	
	if (FALSE == recurrencepattern_to_rrule(
		ptz_component, whole_start_time,
		papprecurr, &irrule)) {
		return FALSE;	
	}
	double_list_init(plist);
	do {
		itime = ical_rrule_instance_itime(&irrule);
		ical_itime_to_utc(ptz_component, itime, &tmp_time);
		if (tmp_time < start_time) {
			continue;
		}
		ical_itime_to_utc(NULL, itime, &tmp_time1);
		for (i=0; i<papprecurr->exceptioncount; i++) {
			nt_time = papprecurr->pexceptioninfo[i].originalstartdate;
			nt_time *= 600000000;
			if (tmp_time1 == rop_util_nttime_to_unix(nt_time)) {
				break;
			}
		}
		if (i < papprecurr->exceptioncount) {
			continue;
		}
		pevnode = static_cast<EVENT_NODE *>(malloc(sizeof(EVENT_NODE)));
		pevnode->node.pdata = pevnode;
		pevnode->start_time = tmp_time;
		pevnode->end_time = tmp_time + (papprecurr->endtimeoffset
								- papprecurr->starttimeoffset)*60;
		pevnode->pexception = NULL;
		pevnode->pex_exception = NULL;
		double_list_append_as_tail(plist, &pevnode->node);
		if (tmp_time >= end_time) {
			break;
		}
	} while (ical_rrule_iterate(&irrule));
	for (i=0; i<papprecurr->exceptioncount; i++) {
		nt_time = papprecurr->pexceptioninfo[i].startdatetime;
		nt_time *= 600000000;
		tmp_time = rop_util_nttime_to_unix(nt_time);
		ical_utc_to_datetime(NULL, tmp_time, &itime);
		ical_itime_to_utc(ptz_component, itime, &tmp_time);
		if (tmp_time >= start_time && tmp_time <= end_time) {
			pevnode = static_cast<EVENT_NODE *>(malloc(sizeof(EVENT_NODE)));
			pevnode->node.pdata = pevnode;
			pevnode->start_time = tmp_time;
			nt_time = papprecurr->pexceptioninfo[i].enddatetime;
			nt_time *= 600000000;
			tmp_time = rop_util_nttime_to_unix(nt_time);
			ical_utc_to_datetime(NULL, tmp_time, &itime);
			ical_itime_to_utc(ptz_component, itime, &tmp_time);
			pevnode->end_time = tmp_time;
			pevnode->pexception = papprecurr->pexceptioninfo + i;
			pevnode->pex_exception = papprecurr->pextendedexception + i;
			double_list_append_as_tail(plist, &pevnode->node);
		}
	}
	return TRUE;
}

static BOOL make_ical_uid(BINARY *pglobal_obj, char *uid_buff)
{
	GUID guid;
	time_t cur_time;
	EXT_PULL ext_pull;
	EXT_PUSH ext_push;
	char tmp_buff[256];
	char tmp_buff1[256];
	GLOBALOBJECTID globalobjectid;
	
	if (NULL != pglobal_obj) {
		ext_buffer_pull_init(&ext_pull,
			pglobal_obj->pb, pglobal_obj->cb, malloc, 0);
		if (EXT_ERR_SUCCESS != ext_buffer_pull_globalobjectid(
			&ext_pull, &globalobjectid)) {
			return FALSE;
		}
		if (0 == memcmp(globalobjectid.data.pb,
			"\x76\x43\x61\x6c\x2d\x55\x69\x64\x01\x00\x00\x00", 12)) {
			if (globalobjectid.data.cb - 12 > sizeof(tmp_buff) - 1) {
				memcpy(tmp_buff, globalobjectid.data.pb + 12,
									sizeof(tmp_buff) - 1);
				tmp_buff[sizeof(tmp_buff) - 1] = '\0';
			} else {
				memcpy(tmp_buff, globalobjectid.data.pb + 12,
								globalobjectid.data.cb - 12);
				tmp_buff[globalobjectid.data.cb - 12] = '\0';
			}
			strcpy(uid_buff, tmp_buff);
		} else {
			globalobjectid.year = 0;
			globalobjectid.month = 0;
			globalobjectid.day = 0;
			ext_buffer_push_init(&ext_push, tmp_buff, sizeof(tmp_buff), 0);
			if (EXT_ERR_SUCCESS != ext_buffer_push_globalobjectid(
				&ext_push, &globalobjectid)) {
				return FALSE;
			}
			if (FALSE == encode_hex_binary(tmp_buff,
				ext_push.offset, tmp_buff1, sizeof(tmp_buff1))) {
				return FALSE;
			}
			HX_strupper(tmp_buff1);
			strcpy(uid_buff, tmp_buff1);
		}
	} else {
		time(&cur_time);
		memset(&globalobjectid, 0, sizeof(GLOBALOBJECTID));
		memcpy(globalobjectid.arrayid,
			"\x04\x00\x00\x00\x82\x00\xE0\x00"
			"\x74\xC5\xB7\x10\x1A\x82\xE0\x08", 16);
		globalobjectid.creationtime = rop_util_unix_to_nttime(cur_time);
		globalobjectid.data.cb = 16;
		globalobjectid.data.pv = tmp_buff1;
		guid = guid_random_new();
		ext_buffer_push_init(&ext_push, tmp_buff1, 16, 0);
		ext_buffer_push_guid(&ext_push, &guid);
		ext_buffer_push_init(&ext_push, tmp_buff, sizeof(tmp_buff), 0);
		if (EXT_ERR_SUCCESS != ext_buffer_push_globalobjectid(
			&ext_push, &globalobjectid)) {
			return FALSE;
		}
		if (FALSE == encode_hex_binary(tmp_buff,
			ext_push.offset, tmp_buff1, sizeof(tmp_buff1))) {
			return FALSE;
		}
		HX_strupper(tmp_buff1);
		strcpy(uid_buff, tmp_buff1);
	}
	return TRUE;
}

static void output_event(time_t start_time, time_t end_time,
	uint32_t busy_type, const char *uid, const char *subject,
	const char *location, BOOL b_meeting, BOOL b_recurring,
	BOOL b_exception, BOOL b_reminder, BOOL b_private)
{
	size_t tmp_len;
	ICAL_TIME itime;
	char tmp_buff[4096];
	
	if (NULL == g_tz_component) {
		printf("{\"StartTime\":%lu, ", start_time);
		printf("\"EndTime\":%lu, ", end_time);
	} else {
		ical_utc_to_datetime(g_tz_component, start_time, &itime);
		printf("{\"StartTime\":\"%d-%02d-%02dT%02d:%02d:%02d\", ",
					itime.year, itime.month, itime.day, itime.hour,
					itime.minute, itime.second);
		ical_utc_to_datetime(g_tz_component, end_time, &itime);
		printf("\"EndTime\":\"%d-%02d-%02dT%02d:%02d:%02d\", ",
				itime.year, itime.month, itime.day, itime.hour,
				itime.minute, itime.second);
	}
	switch (busy_type) {
	case 0x00000000:
		strcpy(tmp_buff, "Free");
		break;
	case 0x00000001:
		strcpy(tmp_buff, "Tentative");
		break;
	case 0x00000002:
		strcpy(tmp_buff, "Busy");
		break;
	case 0x00000003:
		strcpy(tmp_buff, "OOF");
		break;
	case 0x00000004:
		strcpy(tmp_buff, "WorkingElsewhere");
		break;
	default:
		strcpy(tmp_buff, "NoData");
		break;
	}
	printf("\"BusyType\":\"%s\", ", tmp_buff);
	printf("\"ID\":\"%s\", ", uid);
	if (subject != nullptr) {
		encode64(subject, strlen(subject),
		         tmp_buff, sizeof(tmp_buff), &tmp_len);
		printf("\"Subject\":\"%s\", ", tmp_buff);
	}
	if (location != nullptr) {
		encode64(location, strlen(location),
		         tmp_buff, sizeof(tmp_buff), &tmp_len);
		printf("\"Location\":\"%s\", ", tmp_buff);
	}
	if (TRUE == b_meeting) {
		printf("\"IsMeeting\":true, ");
	} else {
		printf("\"IsMeeting\":false, ");
	}
	if (TRUE == b_recurring) {
		printf("\"IsRecurring\":true, ");
	} else {
		printf("\"IsRecurring\":false, ");
	}
	if (TRUE == b_exception) {
		printf("\"IsException\":true, ");
	} else {
		printf("\"IsException\":false, ");
	}
	if (TRUE == b_reminder) {
		printf("\"IsReminderSet\":true, ");
	} else {
		printf("\"IsReminderSet\":false, ");
	}
	if (TRUE == b_private) {
		printf("\"IsPrivate\":true}");
	} else {
		printf("\"IsPrivate\":false}");
	}
}

static BOOL get_freebusy(const char *dir)
{
	int i;
	int sockd;
	void *pvalue;
	BOOL b_first;
	BOOL b_private;
	BOOL b_meeting;
	char *psubject;
	BOOL b_private1 = false, b_meeting1;
	char *psubject1;
	BOOL b_reminder;
	char *plocation;
	BOOL b_reminder1;
	char *plocation1;
	uint8_t tmp_true;
	uint32_t lids[13];
	uint32_t table_id;
	EXT_PULL ext_pull;
	char uid_buff[256];
	uint32_t row_count;
	TARRAY_SET tmp_set;
	uint32_t busy_type;
	uint32_t busy_type1;
	EVENT_NODE *pevnode;
	uint64_t end_nttime;
	uint32_t permission;
	PROPID_ARRAY propids;
	DOUBLE_LIST tmp_list;
	time_t whole_end_time;
	uint64_t start_nttime;
	PROPTAG_ARRAY proptags;
	uint32_t pidlidclipend;
	uint32_t pidlidprivate;
	TIMEZONESTRUCT tzstruct;
	DOUBLE_LIST_NODE *pnode;
	time_t whole_start_time;
	uint32_t pidlidlocation;
	RESTRICTION restriction;
	uint32_t pidlidrecurring;
	PROPNAME_ARRAY propnames;
	uint32_t tmp_proptags[13];
	uint32_t pidlidbusystatus;
	RESTRICTION *prestriction;
	RESTRICTION *prestriction1;
	RESTRICTION *prestriction2;
	RESTRICTION *prestriction3;
	uint32_t pidlidreminderset;
	std::shared_ptr<ICAL_COMPONENT> ptz_component;
	uint32_t pidlidtimezonestruct;
	uint32_t pidlidglobalobjectid;
	uint32_t pidlidappointmentrecur;
	PROPERTY_NAME tmp_propnames[13];
	uint32_t pidlidappointmentsubtype;
	uint32_t pidlidappointmentendwhole;
	uint32_t pidlidappointmentstateflags;
	uint32_t pidlidappointmentstartwhole;
	APPOINTMENTRECURRENCEPATTERN apprecurr;
	
	
	start_nttime = rop_util_unix_to_nttime(g_start_time);
	end_nttime = rop_util_unix_to_nttime(g_end_time);
	propnames.count = 13;
	propnames.ppropname = tmp_propnames;
	/* PidLidAppointmentStartWhole */
	rop_util_get_common_pset(PSETID_APPOINTMENT, &tmp_propnames[0].guid);
	lids[0] = 0x0000820D;
	tmp_propnames[0].kind = MNID_ID;
	tmp_propnames[0].plid = &lids[0];
	/* PidLidAppointmentEndWhole */
	rop_util_get_common_pset(PSETID_APPOINTMENT, &tmp_propnames[1].guid);
	lids[1] = 0x0000820E;
	tmp_propnames[1].kind = MNID_ID;
	tmp_propnames[1].plid = &lids[1];
	/* PidLidBusyStatus */
	rop_util_get_common_pset(PSETID_APPOINTMENT, &tmp_propnames[2].guid);
	lids[2] = 0x00008205;
	tmp_propnames[2].kind = MNID_ID;
	tmp_propnames[2].plid = &lids[2];
	/* PidLidRecurring */
	rop_util_get_common_pset(PSETID_APPOINTMENT, &tmp_propnames[3].guid);
	lids[3] = 0x00008223;
	tmp_propnames[3].kind = MNID_ID;
	tmp_propnames[3].plid = &lids[3];
	/* PidLidAppointmentRecur */
	rop_util_get_common_pset(PSETID_APPOINTMENT, &tmp_propnames[4].guid);
	lids[4] = 0x00008216;
	tmp_propnames[4].kind = MNID_ID;
	tmp_propnames[4].plid = &lids[4];
	/* PidLidAppointmentSubType */
	rop_util_get_common_pset(PSETID_APPOINTMENT, &tmp_propnames[5].guid);
	lids[5] = 0x00008215;
	tmp_propnames[5].kind = MNID_ID;
	tmp_propnames[5].plid = &lids[5];
	/* PidLidPrivate */
	rop_util_get_common_pset(PSETID_COMMON, &tmp_propnames[6].guid);
	lids[6] = 0x00008506;
	tmp_propnames[6].kind = MNID_ID;
	tmp_propnames[6].plid = &lids[6];
	/* PidLidAppointmentStateFlags */
	rop_util_get_common_pset(PSETID_APPOINTMENT, &tmp_propnames[7].guid);
	lids[7] = 0x00008217;
	tmp_propnames[7].kind = MNID_ID;
	tmp_propnames[7].plid = &lids[7];
	/* PidLidClipEnd */
	rop_util_get_common_pset(PSETID_APPOINTMENT, &tmp_propnames[8].guid);
	lids[8] = 0x00008236;
	tmp_propnames[8].kind = MNID_ID;
	tmp_propnames[8].plid = &lids[8];
	/* PidLidLocation */
	rop_util_get_common_pset(PSETID_APPOINTMENT, &tmp_propnames[9].guid);
	lids[9] = 0x00008208;
	tmp_propnames[9].kind = MNID_ID;
	tmp_propnames[9].plid = &lids[9];
	/* PidLidReminderSet */
	rop_util_get_common_pset(PSETID_COMMON, &tmp_propnames[10].guid);
	lids[10] = 0x00008503;
	tmp_propnames[10].kind = MNID_ID;
	tmp_propnames[10].plid = &lids[10];
	/* PidLidGlobalObjectId */
	rop_util_get_common_pset(PSETID_MEETING, &tmp_propnames[11].guid);
	lids[11] = 0x00000003;
	tmp_propnames[11].kind = MNID_ID;
	tmp_propnames[11].plid = &lids[11];
	/* PidLidTimeZoneStruct */
	rop_util_get_common_pset(PSETID_APPOINTMENT, &tmp_propnames[12].guid);
	lids[12] = 0x00008233;
	tmp_propnames[12].kind = MNID_ID;
	tmp_propnames[12].plid = &lids[12];
	
	sockd = connect_exmdb(dir);
	if (-1 == sockd) {
		return FALSE;
	}
	if (FALSE == exmdb_client_get_named_propids(
		sockd, dir, FALSE, &propnames, &propids)) {
		return FALSE;
	}
	if (propids.count != propnames.count) {
		return FALSE;
	}
	pidlidappointmentstartwhole = PROP_TAG(PT_SYSTIME, propids.ppropid[0]);
	pidlidappointmentendwhole = PROP_TAG(PT_SYSTIME, propids.ppropid[1]);
	pidlidbusystatus = PROP_TAG(PT_LONG, propids.ppropid[2]);
	pidlidrecurring = PROP_TAG(PT_BOOLEAN, propids.ppropid[3]);
	pidlidappointmentrecur = PROP_TAG(PT_BINARY, propids.ppropid[4]);
	pidlidappointmentsubtype = PROP_TAG(PT_BOOLEAN, propids.ppropid[5]);
	pidlidprivate = PROP_TAG(PT_BOOLEAN, propids.ppropid[6]);
	pidlidappointmentstateflags = PROP_TAG(PT_LONG, propids.ppropid[7]);
	pidlidclipend = PROP_TAG(PT_SYSTIME, propids.ppropid[8]);
	pidlidlocation = PROP_TAG(PT_UNICODE, propids.ppropid[9]);
	pidlidreminderset = PROP_TAG(PT_BOOLEAN, propids.ppropid[10]);
	pidlidglobalobjectid = PROP_TAG(PT_BINARY, propids.ppropid[11]);
	pidlidtimezonestruct = PROP_TAG(PT_BINARY, propids.ppropid[12]);
	
	if (NULL != g_username) {
		if (FALSE == exmdb_client_check_folder_permission(
			sockd, dir, rop_util_make_eid_ex(1, PRIVATE_FID_CALENDAR),
			g_username, &permission)) {
			close(sockd);
			cache_connection(dir, -1);
			return FALSE;
		}
		if (0 == (permission&PERMISSION_FREEBUSYSIMPLE) &&
			0 == (permission&PERMISSION_FREEBUSYDETAILED)
			&& 0 == (permission&PERMISSION_READANY)) {
			printf("{\"dir\":\"%s\", \"permission\":\"none\"}\n", dir);
			cache_connection(dir, sockd);
			return TRUE;
		}
	} else {
		permission = PERMISSION_FREEBUSYDETAILED | PERMISSION_READANY;
	}
	tmp_true = 1;
	restriction.rt = RES_OR;
	restriction.pres = malloc(sizeof(RESTRICTION_AND_OR));
	auto andor = restriction.andor;
	andor->count = 4;
	prestriction = static_cast<RESTRICTION *>(malloc(4 * sizeof(RESTRICTION)));
	andor->pres = prestriction;
	/*OR (pidlidappointmentstartwhole >= start
		&& pidlidappointmentstartwhole <= end) */
	prestriction[0].rt = RES_AND;
	prestriction[0].pres = malloc(sizeof(RESTRICTION_AND_OR));
	andor = prestriction[0].andor;
	prestriction1 = static_cast<RESTRICTION *>(malloc(2 * sizeof(RESTRICTION)));
	andor->count = 2;
	andor->pres = prestriction1;
	prestriction1[0].rt = RES_PROPERTY;
	prestriction1[0].pres = malloc(sizeof(RESTRICTION_PROPERTY));
	auto rprop = prestriction1[0].prop;
	rprop->relop = RELOP_GE;
	rprop->proptag = pidlidappointmentstartwhole;
	rprop->propval.proptag = pidlidappointmentstartwhole;
	rprop->propval.pvalue = &start_nttime;
	prestriction1[1].rt = RES_PROPERTY;
	prestriction1[1].pres = malloc(sizeof(RESTRICTION_PROPERTY));
	rprop = prestriction1[1].prop;
	rprop->relop = RELOP_LE;
	rprop->proptag = pidlidappointmentstartwhole;
	rprop->propval.proptag = pidlidappointmentstartwhole;
	rprop->propval.pvalue = &end_nttime;
	/* OR (pidlidappointmentendwhole >= start
		&& pidlidappointmentendwhole <= end) */
	prestriction[1].rt = RES_AND;
	prestriction[1].pres = malloc(sizeof(RESTRICTION_AND_OR));
	andor = prestriction[1].andor;
	prestriction1 = static_cast<RESTRICTION *>(malloc(2 * sizeof(RESTRICTION)));
	andor->count = 2;
	andor->pres = prestriction1;
	prestriction1[0].rt = RES_PROPERTY;
	prestriction1[0].pres = malloc(sizeof(RESTRICTION_PROPERTY));
	rprop = prestriction1[0].prop;
	rprop->relop = RELOP_GE;
	rprop->proptag = pidlidappointmentendwhole;
	rprop->propval.proptag = pidlidappointmentendwhole;
	rprop->propval.pvalue = &start_nttime;
	prestriction1[1].rt = RES_PROPERTY;
	prestriction1[1].pres = malloc(sizeof(RESTRICTION_PROPERTY));
	rprop = prestriction1[1].prop;
	rprop->relop = RELOP_LE;
	rprop->proptag = pidlidappointmentendwhole;
	rprop->propval.proptag = pidlidappointmentendwhole;
	rprop->propval.pvalue = &end_nttime;
	/* OR (pidlidappointmentstartwhole < start
		&& pidlidappointmentendwhole > end) */
	prestriction[2].rt = RES_AND;
	prestriction[2].pres = malloc(sizeof(RESTRICTION_AND_OR));
	andor = prestriction[2].andor;
	prestriction1 = static_cast<RESTRICTION *>(malloc(2 * sizeof(RESTRICTION)));
	andor->count = 2;
	andor->pres = prestriction1;
	prestriction1[0].rt = RES_PROPERTY;
	prestriction1[0].pres = malloc(sizeof(RESTRICTION_PROPERTY));
	rprop = prestriction1[0].prop;
	rprop->relop = RELOP_LT;
	rprop->proptag = pidlidappointmentstartwhole;
	rprop->propval.proptag = pidlidappointmentstartwhole;
	rprop->propval.pvalue = &start_nttime;
	prestriction1[1].rt = RES_PROPERTY;
	prestriction1[1].pres = malloc(sizeof(RESTRICTION_PROPERTY));
	rprop = prestriction1[1].prop;
	rprop->relop = RELOP_GT;
	rprop->proptag = pidlidappointmentendwhole;
	rprop->propval.proptag = pidlidappointmentendwhole;
	rprop->propval.pvalue = &end_nttime;
	/* OR */
	prestriction[3].rt = RES_OR;
	prestriction[3].pres = malloc(sizeof(RESTRICTION_AND_OR));
	andor = prestriction[3].andor;
	prestriction1 = static_cast<RESTRICTION *>(malloc(2 * sizeof(RESTRICTION)));
	andor->count = 2;
	andor->pres = prestriction1;
	/* OR (EXIST(pidlidclipend) &&
		pidlidrecurring == true &&
		pidlidclipend >= start) */
	prestriction1[0].rt = RES_AND;
	prestriction1[0].pres = malloc(sizeof(RESTRICTION_AND_OR));
	andor = prestriction1[0].andor;
	andor->count = 3;
	prestriction2 = static_cast<RESTRICTION *>(malloc(3 * sizeof(RESTRICTION)));
	andor->pres = prestriction2;
	prestriction2[0].rt = RES_EXIST;
	prestriction2[0].pres = malloc(sizeof(RESTRICTION_EXIST));
	auto rex = prestriction2[0].exist;
	rex->proptag = pidlidclipend;
	prestriction2[1].rt = RES_PROPERTY;
	prestriction2[1].pres = malloc(sizeof(RESTRICTION_PROPERTY));
	rprop = prestriction2[1].prop;
	rprop->relop = RELOP_EQ;
	rprop->proptag = pidlidrecurring;
	rprop->propval.proptag = pidlidrecurring;
	rprop->propval.pvalue = &tmp_true;
	prestriction2[2].rt = RES_PROPERTY;
	prestriction2[2].pres = malloc(sizeof(RESTRICTION_PROPERTY));
	rprop = prestriction2[2].prop;
	rprop->relop = RELOP_GE;
	rprop->proptag = pidlidclipend;
	rprop->propval.proptag = pidlidclipend;
	rprop->propval.pvalue = &start_nttime;
	/* OR (!EXIST(pidlidclipend) &&
		pidlidrecurring == true &&
		pidlidappointmentstartwhole <= end) */
	prestriction1[1].rt = RES_AND;
	prestriction1[1].pres = malloc(sizeof(RESTRICTION_AND_OR));
	andor = prestriction1[1].andor;
	andor->count = 3;
	prestriction2 = static_cast<RESTRICTION *>(malloc(3 * sizeof(RESTRICTION)));
	andor->pres = prestriction2;
	prestriction2[0].rt = RES_NOT;
	prestriction3 = static_cast<RESTRICTION *>(malloc(sizeof(RESTRICTION)));
	prestriction2[0].pres = prestriction3;
	prestriction3->rt = RES_EXIST;
	prestriction3->pres = malloc(sizeof(RESTRICTION_EXIST));
	rex = prestriction3->exist;
	rex->proptag = pidlidclipend;
	prestriction2[1].rt = RES_PROPERTY;
	prestriction2[1].pres = malloc(sizeof(RESTRICTION_PROPERTY));
	rprop = prestriction2[1].prop;
	rprop->relop = RELOP_EQ;
	rprop->proptag = pidlidrecurring;
	rprop->propval.proptag = pidlidrecurring;
	rprop->propval.pvalue = &tmp_true;
	prestriction2[2].rt = RES_PROPERTY;
	prestriction2[2].pres = malloc(sizeof(RESTRICTION_PROPERTY));
	rprop = prestriction2[2].prop;
	rprop->relop = RELOP_LE;
	rprop->proptag = pidlidappointmentstartwhole;
	rprop->propval.proptag = pidlidappointmentstartwhole;
	rprop->propval.pvalue = &end_nttime;
	/* end of OR */
	
	if (FALSE == exmdb_client_load_content_table(sockd, dir,
		0, rop_util_make_eid_ex(1, PRIVATE_FID_CALENDAR),
		NULL, TABLE_FLAG_NONOTIFICATIONS, &restriction, NULL,
		&table_id, &row_count)) {
		close(sockd);
		cache_connection(dir, -1);
		return FALSE;
	}
	proptags.count = 13;
	proptags.pproptag = tmp_proptags;
	tmp_proptags[0] = pidlidappointmentstartwhole;
	tmp_proptags[1] = pidlidappointmentendwhole;
	tmp_proptags[2] = pidlidbusystatus;
	tmp_proptags[3] = pidlidrecurring;
	tmp_proptags[4] = pidlidappointmentrecur;
	tmp_proptags[5] = pidlidappointmentsubtype;
	tmp_proptags[6] = pidlidprivate;
	tmp_proptags[7] = pidlidappointmentstateflags;
	tmp_proptags[8] = pidlidlocation;
	tmp_proptags[9] = pidlidreminderset;
	tmp_proptags[10] = pidlidglobalobjectid;
	tmp_proptags[11] = pidlidtimezonestruct;
	tmp_proptags[12] = PROP_TAG_SUBJECT;
	if (FALSE == exmdb_client_query_table(sockd, dir, NULL,
		0, table_id, &proptags, 0, row_count, &tmp_set)) {
		close(sockd);
		cache_connection(dir, -1);
		return FALSE;	
	}
	printf("{\"dir\":\"%s\", \"permission\":", dir);
	if ((permission & PERMISSION_FREEBUSYDETAILED) ||
		(permission & PERMISSION_READANY)) {
		printf("\"detailed\", ");
	} else {
		printf("\"simple\", ");
	}
	printf("\"events\":[");
	b_first = FALSE;
	for (i=0; i<tmp_set.count; i++) {
		pvalue = tpropval_array_get_propval(
			tmp_set.pparray[i], pidlidappointmentstartwhole);
		if (NULL == pvalue) {
			continue;
		}
		whole_start_time = rop_util_nttime_to_unix(*(uint64_t*)pvalue);
		pvalue = tpropval_array_get_propval(
			tmp_set.pparray[i], pidlidappointmentendwhole);
		if (NULL == pvalue) {
			continue;
		}
		whole_end_time = rop_util_nttime_to_unix(*(uint64_t*)pvalue);
		pvalue = tpropval_array_get_propval(
			tmp_set.pparray[i], pidlidglobalobjectid);
		if (!make_ical_uid(static_cast<BINARY *>(pvalue), uid_buff))
			continue;
		psubject = static_cast<char *>(tpropval_array_get_propval(
		           tmp_set.pparray[i], PROP_TAG_SUBJECT));
		plocation = static_cast<char *>(tpropval_array_get_propval(
		            tmp_set.pparray[i], pidlidlocation));
		pvalue = tpropval_array_get_propval(
			tmp_set.pparray[i], pidlidreminderset);
		if (NULL == pvalue || 0 == *(uint8_t*)pvalue) {
			b_reminder = FALSE;
		} else {
			b_reminder = TRUE;
		}
		pvalue = tpropval_array_get_propval(
			tmp_set.pparray[i], pidlidprivate);
		if (NULL == pvalue || 0 == *(uint8_t*)pvalue) {
			b_private = FALSE;
		} else {
			b_private = TRUE;
		}
		pvalue = tpropval_array_get_propval(
			tmp_set.pparray[i], pidlidbusystatus);
		if (NULL == pvalue) {
			busy_type = 0;
		} else {
			busy_type = *(uint32_t*)pvalue;
			if (busy_type > 4) {
				busy_type = 0;
			}
		}
		pvalue = tpropval_array_get_propval(
			tmp_set.pparray[i], pidlidappointmentstateflags);
		if (NULL == pvalue) {
			b_meeting = FALSE;
		} else {
			if ((*(uint32_t*)pvalue) & 0x00000001) {
				b_meeting = TRUE;
			} else {
				b_meeting = FALSE;
			}
		}
		pvalue = tpropval_array_get_propval(
			tmp_set.pparray[i], pidlidrecurring);
		if (NULL != pvalue && 0 != *(uint8_t*)pvalue) {
			pvalue = tpropval_array_get_propval(
				tmp_set.pparray[i], pidlidtimezonestruct);
			if (NULL == pvalue) {
				ptz_component = NULL;
			} else {
				ext_buffer_pull_init(&ext_pull, ((BINARY*)pvalue)->pb,
					((BINARY*)pvalue)->cb, malloc, EXT_FLAG_UTF16);
				if (EXT_ERR_SUCCESS != ext_buffer_pull_timezonestruct(
					&ext_pull, &tzstruct)) {
					continue;	
				}
				ptz_component = tzstruct_to_vtimezone(
						1600, "timezone", &tzstruct);
				if (NULL == ptz_component) {
					continue;
				}
			}
			pvalue = tpropval_array_get_propval(
				tmp_set.pparray[i], pidlidappointmentrecur);
			if (NULL == pvalue) {
				continue;
			}
			ext_buffer_pull_init(&ext_pull, ((BINARY*)pvalue)->pb,
				((BINARY*)pvalue)->cb, malloc, EXT_FLAG_UTF16);
			if (EXT_ERR_SUCCESS !=
				ext_buffer_pull_appointmentrecurrencepattern(
				&ext_pull, &apprecurr)) {
				continue;
			}
			if (FALSE == find_recurrence_times(ptz_component,
				whole_start_time, &apprecurr, g_start_time,
				g_end_time, &tmp_list)) {
				continue;	
			}
			while ((pnode = double_list_pop_front(&tmp_list)) != nullptr) {
				pevnode = (EVENT_NODE*)pnode->pdata;
				if (NULL != pevnode->pexception &&
					NULL != pevnode->pex_exception) {
					if (pevnode->pexception->overrideflags &
						OVERRIDEFLAG_MEETINGTYPE) {
						if (pevnode->pexception->meetingtype & 0x00000001) {
							b_meeting1 = TRUE;
						} else {
							b_meeting1 = FALSE;
						}
					} else {
						b_meeting1 = b_meeting;
					}
					if (pevnode->pexception->overrideflags &
						OVERRIDEFLAG_REMINDER) {
						if (0 == pevnode->pexception->reminderset) {
							b_reminder1 = FALSE;
						} else {
							b_reminder1 = TRUE;
						}
					} else {
						b_reminder1 = b_reminder;
					}
					if (pevnode->pexception->overrideflags &
						OVERRIDEFLAG_BUSYSTATUS) {
						busy_type1 = pevnode->pexception->busystatus;
					} else {
						busy_type1 = busy_type;
					}
					if (pevnode->pexception->overrideflags &
						OVERRIDEFLAG_SUBJECT) {
						psubject1 = pevnode->pex_exception->subject;	
					} else {
						psubject1 = psubject;
					}
					if (pevnode->pexception->overrideflags &
						OVERRIDEFLAG_LOCATION) {
						plocation1 = pevnode->pex_exception->location;	
					} else {
						plocation1 = plocation;
					}
					if (TRUE == b_first) {
						printf(",");
					}
					b_first = TRUE;
					output_event(pevnode->start_time, pevnode->end_time,
						busy_type1, uid_buff, psubject1, plocation1,
						b_meeting1, TRUE, TRUE, b_reminder1, b_private1);
				} else {
					if (TRUE == b_first) {
						printf(",");
					}
					b_first = TRUE;
					output_event(pevnode->start_time, pevnode->end_time,
						busy_type, uid_buff, psubject, plocation,
						b_meeting, TRUE, FALSE, b_reminder, b_private);
				}
			}
		} else {
			if (TRUE == b_first) {
				printf(",");
			}
			b_first = TRUE;
			output_event(whole_start_time, whole_end_time,
				busy_type, uid_buff, psubject, plocation,
				b_meeting, FALSE, FALSE, b_reminder, b_private);
		}
	}
	printf("]}\n");
	if (FALSE == exmdb_client_unload_table(sockd, dir, table_id)) {
		close(sockd);
		cache_connection(dir, -1);
		return FALSE;
	}
	cache_connection(dir, sockd);
	return TRUE;
}

static void cookie_parser_unencode(const char *src, char *dest)
{
	int code;
	const char *last;
	
	last = src + strlen(src);
	for (; src != last; src++, dest++) {
		if (*src == '+') {
			*dest = ' ';
		} else if (*src == '%') {
			if (sscanf(src+1, "%2x", &code) != 1) {
				code = '?';
			}
			*dest = code;
			src +=2;
		} else {
			*dest = *src;
		}
	}
	*dest = '\0';
}

static cookie_jar cookie_parser_init(const char *cookie_string)
{
	int len;
	char *ptr;
	char *ptr1;
	char *ptoken;
	char *last_ptr;
	cookie_jar jar;
	char *decoded_string;
	
	len = strlen(cookie_string);
	decoded_string = (char*)malloc(len + 2);
	if (NULL == decoded_string) {
		return jar;
	}
	cookie_parser_unencode(cookie_string, decoded_string);
	len = strlen(decoded_string);
	if (len > 0 && '\n' == decoded_string[len - 1]) {
		len --;
		decoded_string[len] = '\0';
	}
	
	if (len > 0) {
		decoded_string[len] = ';';
		len ++;
		decoded_string[len] = '\0';
	}
	
	ptr = decoded_string;
	last_ptr = decoded_string;
	
	while ('\0' != *ptr) {
		if (';' == *ptr) {
			/* check if the ';' is only a character of the value */
			ptr1 = strchr(ptr + 1, ';');
			if (NULL != ptr1 && NULL == memchr(
				ptr + 1, '=', ptr1 - ptr - 1)) {
				ptr ++;
				continue;
			}
			*ptr = '\0';
			ptoken = strchr(last_ptr, '=');
			if (NULL != ptoken) {
				*ptoken = '\0';
				ptoken ++;
				try {
					std::string pparam = ptoken;
					while (' ' == *last_ptr && '\0' != *last_ptr) {
						last_ptr ++;
					}
					if ('\0' != *last_ptr) {
						jar.emplace(last_ptr, std::move(pparam));
					}
				} catch (...) {
				}
			}
			last_ptr = ptr + 1;
		}
		ptr ++;
	}
	
	free(decoded_string);
	return jar;
}

static inline const char *
cookie_parser_get(const cookie_jar &jar, const char *name)
{
	auto i = jar.find(name);
	return i != jar.cend() ? i->second.c_str() : nullptr;
}

int main(int argc, const char **argv)
{
	char *line;
	size_t len;
	int dir_num;
	char *ptoken;
	char *ptoken1;
	const char *pdir;
	const char *pdirs;
	const char *pbias;
	char tmp_buff[128];
	ICAL_TIME itime_end;
	const char *pstdbias;
	const char *pstdtime;
	const char *pdtlbias;
	const char *pdtltime;
	const char *pendtime;
	const char *pstdyear;
	const char *pdtlyear;
	ICAL_TIME itime_start;
	const char *pstdmonth;
	const char *pdtlmonth;
	const char *pstarttime;
	TIMEZONESTRUCT tzstruct;
	const char *pstddayorder;
	const char *pdtldayorder;
	const char *pstddayofweek;
	const char *pdtldayofweek;
	
	setvbuf(stdout, nullptr, _IOLBF, 0);
	std::vector<EXMDB_ITEM> xmlist;
	auto ret = list_file_read_exmdb("exmdb_list.txt", PKGSYSCONFDIR, xmlist);
	if (ret < 0) {
		fprintf(stderr, "list_file_read_exmdb: %s\n", strerror(-ret));
		exit(1);
	}
	for (auto &&item : xmlist) try {
		if (item.type != EXMDB_ITEM::EXMDB_PRIVATE)
			continue;
		auto &n = g_exmdb_list.emplace_back(std::move(item));
		n.sockd = -1;
	} catch (const std::bad_alloc &) {
		return -ENOMEM;
	}
	
	line = NULL;
	if (-1 == getline(&line, &len, stdin)) {
		fprintf(stderr, "fail to read parameters from stdin\n");
		exit(2);
	}
	auto pparser = cookie_parser_init(line);
	g_username = cookie_parser_get(pparser, "username");
	pstarttime = cookie_parser_get(pparser, "starttime");
	if (NULL == pstarttime) {
		fprintf(stderr, "fail to get \"starttime\" from stdin\n");
		exit(4);
	}
	pendtime = cookie_parser_get(pparser, "endtime");
	if (NULL == pendtime) {
		fprintf(stderr, "fail to get \"endtime\" from stdin\n");
		exit(5);
	}
	if (NULL == strchr(pstarttime, 'T') && NULL == strchr(pendtime, 'T')) {
		g_start_time = atol(pstarttime);
		g_end_time = atol(pendtime);
		g_tz_component = NULL;
		goto GET_FREEBUSY_DATA;
	}
	if (6 != sscanf(pstarttime, "%d-%d-%dT%d:%d:%d",
		&itime_start.year, &itime_start.month, &itime_start.day,
		&itime_start.hour, &itime_start.minute, &itime_start.second)) {
		fprintf(stderr, "fail to parse \"starttime\" from stdin\n");
		exit(4);	
	}
	if (6 != sscanf(pendtime, "%d-%d-%dT%d:%d:%d",
		&itime_end.year, &itime_end.month, &itime_end.day,
		&itime_end.hour, &itime_end.minute, &itime_end.second)) {
		fprintf(stderr, "fail to parse \"endtime\" from stdin\n");
		exit(5);	
	}
	itime_start.leap_second = 0;
	itime_end.leap_second = 0;
	pbias = cookie_parser_get(pparser, "bias");
	if (NULL == pbias) {
		fprintf(stderr, "fail to get \"bias\" from stdin\n");
		exit(6);
	}
	pstdbias = cookie_parser_get(pparser, "stdbias");
	if (NULL == pstdbias) {
		fprintf(stderr, "fail to get \"stdbias\" from stdin\n");
		exit(7);
	}
	pstdtime = cookie_parser_get(pparser, "stdtime");
	if (NULL == pstdtime) {
		fprintf(stderr, "fail to get \"stdtime\" from stdin\n");
		exit(8);
	}
	pstddayorder = cookie_parser_get(pparser, "stddayorder");
	if (NULL == pstddayorder) {
		fprintf(stderr, "fail to get \"stddayorder\" from stdin\n");
		exit(9);
	}
	pstdmonth = cookie_parser_get(pparser, "stdmonth");
	if (NULL == pstdmonth) {
		fprintf(stderr, "fail to get \"stdmonth\" from stdin\n");
		exit(10);
	}
	pstdyear = cookie_parser_get(pparser, "stdyear");
	pstddayofweek = cookie_parser_get(pparser, "stddayofweek");
	if (NULL == pstddayofweek) {
		fprintf(stderr, "fail to get \"stddayofweek\" from stdin\n");
		exit(11);
	}
	pdtlbias = cookie_parser_get(pparser, "dtlbias");
	if (NULL == pdtlbias) {
		fprintf(stderr, "fail to get \"dtlbias\" from stdin\n");
		exit(12);
	}
	pdtltime = cookie_parser_get(pparser, "dtltime");
	if (NULL == pdtltime) {
		fprintf(stderr, "fail to get \"dtltime\" from stdin\n");
		exit(13);
	}
	pdtldayorder = cookie_parser_get(pparser, "dtldayorder");
	if (NULL == pdtldayorder) {
		fprintf(stderr, "fail to get \"dtldayorder\" from stdin\n");
		exit(14);
	}
	pdtlmonth = cookie_parser_get(pparser, "dtlmonth");
	if (NULL == pdtlmonth) {
		fprintf(stderr, "fail to get \"dtlmonth\" from stdin\n");
		exit(10);
	}
	pdtlyear = cookie_parser_get(pparser, "dtlyear");
	pdtldayofweek = cookie_parser_get(pparser, "dtldayofweek");
	if (NULL == pdtldayofweek) {
		fprintf(stderr, "fail to get \"dtldayofweek\" from stdin\n");
		exit(11);
	}
	tzstruct.bias = atoi(pbias);
	tzstruct.standardbias = atoi(pstdbias);
	tzstruct.daylightbias = atoi(pdtlbias);
	if (NULL == pstdyear) {
		tzstruct.standarddate.year = 0;
	} else {
		tzstruct.standarddate.year = atoi(pstdyear);
	}
	tzstruct.standardyear = tzstruct.standarddate.year;
	tzstruct.standarddate.month = atoi(pstdmonth);
	if (0 == strcasecmp(pstddayofweek, "Sunday")) {
		tzstruct.standarddate.dayofweek = 0;
	} else if (0 == strcasecmp(pstddayofweek, "Monday")) {
		tzstruct.standarddate.dayofweek = 1;
	} else if (0 == strcasecmp(pstddayofweek, "Tuesday")) {
		tzstruct.standarddate.dayofweek = 2;
	} else if (0 == strcasecmp(pstddayofweek, "Wednesday")) {
		tzstruct.standarddate.dayofweek = 3;
	} else if (0 == strcasecmp(pstddayofweek, "Thursday")) {
		tzstruct.standarddate.dayofweek = 4;
	} else if (0 == strcasecmp(pstddayofweek, "Friday")) {
		tzstruct.standarddate.dayofweek = 5;
	} else if (0 == strcasecmp(pstddayofweek, "Saturday")) {
		tzstruct.standarddate.dayofweek = 6;
	}
	tzstruct.standarddate.day = atoi(pstddayorder);
	HX_strlcpy(tmp_buff, pstdtime, GX_ARRAY_SIZE(tmp_buff));
	ptoken = strchr(tmp_buff, ':');
	if (NULL == ptoken) {
		fprintf(stderr, "\"stdtime\" format error\n");
		exit(12);
	}
	*ptoken = '\0';
	ptoken ++;
	ptoken1 = strchr(ptoken, ':');
	if (NULL == ptoken1) {
		fprintf(stderr, "\"stdtime\" format error\n");
		exit(12);
	}
	*ptoken1 = '\0';
	ptoken1 ++;
	tzstruct.standarddate.hour = atoi(tmp_buff);
	tzstruct.standarddate.minute = atoi(ptoken);
	tzstruct.standarddate.second = atoi(ptoken1);
	if (NULL == pdtlyear) {
		tzstruct.daylightdate.year = 0;
	} else {
		tzstruct.daylightdate.year = atoi(pdtlyear);
	}
	tzstruct.daylightyear = tzstruct.daylightdate.year;
	tzstruct.daylightdate.month = atoi(pdtlmonth);
	if (0 == strcasecmp(pdtldayofweek, "Sunday")) {
		tzstruct.daylightdate.dayofweek = 0;
	} else if (0 == strcasecmp(pdtldayofweek, "Monday")) {
		tzstruct.daylightdate.dayofweek = 1;
	} else if (0 == strcasecmp(pdtldayofweek, "Tuesday")) {
		tzstruct.daylightdate.dayofweek = 2;
	} else if (0 == strcasecmp(pdtldayofweek, "Wednesday")) {
		tzstruct.daylightdate.dayofweek = 3;
	} else if (0 == strcasecmp(pdtldayofweek, "Thursday")) {
		tzstruct.daylightdate.dayofweek = 4;
	} else if (0 == strcasecmp(pdtldayofweek, "Friday")) {
		tzstruct.daylightdate.dayofweek = 5;
	} else if (0 == strcasecmp(pdtldayofweek, "Saturday")) {
		tzstruct.daylightdate.dayofweek = 6;
	}
	tzstruct.daylightdate.day = atoi(pdtldayorder);
	HX_strlcpy(tmp_buff, pdtltime, GX_ARRAY_SIZE(tmp_buff));
	ptoken = strchr(tmp_buff, ':');
	if (NULL == ptoken) {
		fprintf(stderr, "\"dtltime\" format error\n");
		exit(13);
	}
	*ptoken = '\0';
	ptoken ++;
	ptoken1 = strchr(ptoken, ':');
	if (NULL == ptoken1) {
		fprintf(stderr, "\"dtltime\" format error\n");
		exit(13);
	}
	*ptoken1 = '\0';
	ptoken1 ++;
	tzstruct.daylightdate.hour = atoi(tmp_buff);
	tzstruct.daylightdate.minute = atoi(ptoken);
	tzstruct.daylightdate.second = atoi(ptoken1);
	g_tz_component = tzstruct_to_vtimezone(
				1600, "timezone", &tzstruct);
	if (NULL == g_tz_component) {
		fprintf(stderr, "fail to produce vtimezone component\n");
		exit(14);
	}
	ical_itime_to_utc(g_tz_component, itime_start, &g_start_time);
	ical_itime_to_utc(g_tz_component, itime_end, &g_end_time);
 GET_FREEBUSY_DATA:
	pdirs = cookie_parser_get(pparser, "dirs");
	if (NULL == pdirs) {
		fprintf(stderr, "fail to get \"dirs\" from stdin\n");
		exit(15);
	}
	dir_num = atoi(pdirs);
	for (decltype(dir_num) i = 0; i < dir_num; ++i) {
		sprintf(tmp_buff, "dir%d", i);
		pdir = cookie_parser_get(pparser, tmp_buff);
		if (NULL != pdir) {
			get_freebusy(pdir);
		}
	}
	exit(0);
}

