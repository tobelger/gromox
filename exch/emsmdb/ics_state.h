#pragma once
#include <memory>
#include <gromox/mapi_types.hpp>

struct logon_object;

enum {
	ICS_STATE_CONTENTS_DOWN,
	ICS_STATE_CONTENTS_UP,
	ICS_STATE_HIERARCHY_DOWN,
	ICS_STATE_HIERARCHY_UP
};

struct ICS_STATE {
	ICS_STATE() = default;
	~ICS_STATE();
	NOMOVE(ICS_STATE);
	static std::unique_ptr<ICS_STATE> create(logon_object *, int type);
	static std::shared_ptr<ICS_STATE> create_shared(logon_object *, int type);
	BOOL append_idset(uint32_t state_property, IDSET *);
	TPROPVAL_ARRAY *serialize();

	int type = 0;
	IDSET *pgiven = nullptr, *pseen = nullptr, *pseen_fai = nullptr;
	IDSET *pread = nullptr;
};
using ics_state = ICS_STATE;
