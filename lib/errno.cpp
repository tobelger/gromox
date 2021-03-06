#include <gromox/defs.h>
#include <gromox/mapi_types.hpp>

unsigned int gxerr_to_hresult(gxerr_t e)
{
	switch (e) {
	case GXERR_SUCCESS: return ecSuccess;
	case GXERR_OVER_QUOTA: return EC_DISK_FULL;
	default: return ecError;
	}
}
