// SPDX-License-Identifier: AGPL-3.0-or-later, OR GPL-2.0-or-later WITH linking exception
// SPDX-FileCopyrightText: 2021 grammm GmbH
// This file is part of Gromox.
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <gromox/database.h>
#include <gromox/defs.h>
#include <gromox/proptags.hpp>
#include "mysql_adaptor.h"
#include "sql2.hpp"

using namespace gromox;
using aliasmap_t = std::multimap<std::string, std::string, std::less<>>;
using propmap_t  = std::multimap<unsigned int, std::pair<unsigned int, std::string>>;
struct sqlconnpool g_sqlconn_pool;

static std::vector<std::string>
aliasmap_extract(aliasmap_t &amap, const char *username)
{
	std::vector<std::string> v;
	auto stop = amap.upper_bound(username);
	for (auto it = amap.lower_bound(username); it != stop; ) {
		auto next = std::next(it);
		auto node = amap.extract(it);
		v.push_back(std::move(node.mapped()));
		it = next;
	}
	return v;
}

static bool aliasmap_load(sqlconn &conn, const char *query, aliasmap_t &out)
{
	if (!conn.query(query))
		return false;
	DB_RESULT res = mysql_store_result(conn.get());
	if (res == nullptr)
		return false;
	DB_ROW row;
	while ((row = res.fetch_row()) != nullptr)
		out.emplace(row[0], row[1]);
	return true;
}

static std::map<unsigned int, std::string>
propmap_extract(propmap_t &pmap, unsigned int user_id)
{
	std::map<unsigned int, std::string> v;
	auto stop = pmap.upper_bound(user_id);
	for (auto it = pmap.lower_bound(user_id); it != stop; ) {
		auto next = std::next(it);
		auto node = pmap.extract(it);
		v.emplace(node.mapped().first, std::move(node.mapped().second));
		it = next;
	}
	return v;
}

static bool propmap_load(sqlconn &conn, const char *query, propmap_t &out)
{
	if (!conn.query(query))
		return false;
	DB_RESULT res = mysql_store_result(conn.get());
	if (res == nullptr)
		return false;
	DB_ROW row;
	while ((row = res.fetch_row()) != nullptr) {
		if (row[2] == nullptr && row[3] == nullptr)
			continue;
		auto len = res.row_lengths();
		unsigned int id = strtoul(row[0], nullptr, 0);
		unsigned int pt = strtoul(row[1], nullptr, 0);
		auto data = row[2] != nullptr ? std::string(row[2], len[2]) : std::string(row[3]);
		out.emplace(id, std::make_pair(pt, std::move(data)));
	}
	return true;
}

static int userlist_parse(sqlconn &conn, const char *query,
    aliasmap_t &amap, propmap_t &pmap, std::vector<sql_user> &pfile)
{
	if (!conn.query(query))
		return false;
	DB_RESULT result = mysql_store_result(conn.get());
	if (result == nullptr)
		return false;

	for (size_t i = 0; i < result.num_rows(); ++i) {
		auto row = result.fetch_row();
		auto adrtype = strtoul(row[2], nullptr, 0);
		auto subtype = strtoul(row[3], nullptr, 0);
		if (adrtype == ADDRESS_TYPE_NORMAL && subtype == SUB_TYPE_ROOM)
			adrtype = ADDRESS_TYPE_ROOM;
		else if (adrtype == ADDRESS_TYPE_NORMAL && subtype == SUB_TYPE_EQUIPMENT)
			adrtype = ADDRESS_TYPE_EQUIPMENT;

		sql_user u;
		u.addr_type = adrtype;
		u.id = strtoul(row[0], nullptr, 0);
		u.username = row[1];
		u.aliases = aliasmap_extract(amap, row[1]);
		u.propvals = propmap_extract(pmap, u.id);
		u.maildir = row[4];
		if (adrtype == ADDRESS_TYPE_MLIST) {
			u.list_type = strtoul(z_null(row[5]), nullptr, 0);
			u.list_priv = strtoul(z_null(row[6]), nullptr, 0);
			/* no overwrite of propval is intended */
			if (u.list_type == MLIST_TYPE_CLASS && row[7] != nullptr)
				u.propvals.emplace(PROP_TAG_DISPLAYNAME, row[7]);
			else if (u.list_type == MLIST_TYPE_GROUP && row[8] != nullptr)
				u.propvals.emplace(PROP_TAG_DISPLAYNAME, row[8]);
		}
		pfile.push_back(std::move(u));
	}
	return pfile.size();
}

int mysql_adaptor_get_class_users(int class_id, std::vector<sql_user> &pfile) try
{
	char query[1024];

	auto conn = g_sqlconn_pool.get_wait();
	if (conn.res == nullptr)
		return false;
	snprintf(query, GX_ARRAY_SIZE(query),
	         "SELECT u.username, a.aliasname FROM users AS u "
	         "INNER JOIN aliases AS a ON u.username=a.mainname "
	         "INNER JOIN members AS m ON m.class_id=%d AND m.username=u.username", class_id);
	aliasmap_t amap;
	aliasmap_load(conn.res, query, amap);

	snprintf(query, GX_ARRAY_SIZE(query),
	         "SELECT u.id, p.proptag, p.propval_bin, p.propval_str FROM users AS u "
	         "INNER JOIN user_properties AS p ON u.id=p.user_id "
	         "INNER JOIN members AS m ON m.class_id=%d AND m.username=u.username", class_id);
	propmap_t pmap;
	propmap_load(conn.res, query, pmap);

	snprintf(query, GX_ARRAY_SIZE(query),
	         "SELECT u.id, u.username, u.address_type, u.sub_type, "
	         "u.maildir, z.list_type, z.list_privilege, "
	         "cl.classname, gr.title FROM users AS u "
	         "INNER JOIN members AS m ON m.class_id=%d AND m.username=u.username "
	         "LEFT JOIN mlists AS z ON u.username=z.listname "
	         "LEFT JOIN classes AS cl ON u.username=cl.listname "
	         "LEFT JOIN groups AS gr ON u.username=gr.groupname", class_id);
	return userlist_parse(conn.res, query, amap, pmap, pfile);
} catch (const std::exception &e) {
	printf("[mysql_adaptor]: %s %s\n", __func__, e.what());
	return false;
}

int mysql_adaptor_get_domain_users(int domain_id, std::vector<sql_user> &pfile) try
{
	char query[1024];

	auto conn = g_sqlconn_pool.get_wait();
	if (conn.res == nullptr)
		return false;
	snprintf(query, GX_ARRAY_SIZE(query),
	         "SELECT u.username, a.aliasname FROM users AS u "
	         "INNER JOIN aliases AS a ON u.domain_id=%d AND u.username=a.mainname", domain_id);
	aliasmap_t amap;
	aliasmap_load(conn.res, query, amap);

	snprintf(query, GX_ARRAY_SIZE(query),
	         "SELECT u.id, p.proptag, p.propval_bin, p.propval_str FROM users AS u "
	         "INNER JOIN user_properties AS p ON u.domain_id=%d AND u.id=p.user_id", domain_id);
	propmap_t pmap;
	propmap_load(conn.res, query, pmap);

	snprintf(query, GX_ARRAY_SIZE(query),
	         "SELECT u.id, u.username, u.address_type, u.sub_type, "
	         "u.maildir, z.list_type, z.list_privilege, "
	         "cl.classname, gr.title FROM users AS u "
	         "LEFT JOIN mlists AS z ON u.username=z.listname "
	         "LEFT JOIN classes AS cl ON u.username=cl.listname "
	         "LEFT JOIN groups AS gr ON u.username=gr.groupname "
	         "WHERE u.domain_id=%u AND u.group_id=0", domain_id);
	return userlist_parse(conn.res, query, amap, pmap, pfile);
} catch (const std::exception &e) {
	printf("[mysql_adaptor]: %s %s\n", __func__, e.what());
	return false;
}

int mysql_adaptor_get_group_users(int group_id, std::vector<sql_user> &pfile) try
{
	char query[1024];

	auto conn = g_sqlconn_pool.get_wait();
	if (conn.res == nullptr)
		return false;
	snprintf(query, GX_ARRAY_SIZE(query),
	         "SELECT u.username, a.aliasname FROM users AS u "
	         "INNER JOIN aliases AS a ON u.username=a.mainname "
	         "WHERE u.group_id=%d AND (SELECT COUNT(*) AS num "
	         "FROM members AS m WHERE u.username=m.username)=0",
	         group_id);
	aliasmap_t amap;
	aliasmap_load(conn.res, query, amap);

	snprintf(query, GX_ARRAY_SIZE(query),
	         "SELECT u.id, p.proptag, p.propval_bin, p.propval_str FROM users AS u "
	         "INNER JOIN user_properties AS p ON u.group_id=%d AND u.id=p.user_id "
	         "WHERE (SELECT COUNT(*) AS num FROM members AS m WHERE u.username=m.username)=0",
	         group_id);
	propmap_t pmap;
	propmap_load(conn.res, query, pmap);

	snprintf(query, GX_ARRAY_SIZE(query),
	         "SELECT u.id, u.username, u.address_type, u.sub_type, "
	         "u.maildir, z.list_type, z.list_privilege, "
	         "cl.classname, gr.title FROM users AS u "
	         "LEFT JOIN mlists AS z ON u.username=z.listname "
	         "LEFT JOIN classes AS cl ON u.username=cl.listname "
	         "LEFT JOIN groups AS gr ON u.username=gr.groupname "
	         "WHERE u.group_id=%d AND (SELECT COUNT(*) AS num "
	         "FROM members AS m WHERE u.username=m.username)=0", group_id);
	return userlist_parse(conn.res, query, amap, pmap, pfile);
} catch (const std::exception &e) {
	printf("[mysql_adaptor]: %s %s\n", __func__, e.what());
	return false;
}
