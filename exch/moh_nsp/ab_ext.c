#include "ab_ext.h"

static int ab_ext_pull_string_array(EXT_PULL *pext, STRING_ARRAY *r)
{
	int i;
	int status;
	uint8_t tmp_byte;
	
	status = ext_buffer_pull_uint32(pext, &r->count);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == r->count) {
		r->ppstr = NULL;
		return EXT_ERR_SUCCESS;
	}
	r->ppstr = pext->alloc(sizeof(char*)*r->count);
	if (NULL == r->ppstr) {
		return EXT_ERR_ALLOC;
	}
	for (i=0; i<r->count; i++) {
		status = ext_buffer_pull_uint8(pext, &tmp_byte);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		if (0 == tmp_byte) {
			r->ppstr[i] = NULL;
		} else {
			status = ext_buffer_pull_string(pext, &r->ppstr[i]);
			if (EXT_ERR_SUCCESS != status) {
				return status;
			}
		}
	}
	return EXT_ERR_SUCCESS;
}

static int ab_ext_pull_wstring_array(EXT_PULL *pext, STRING_ARRAY *r)
{
	int i;
	int status;
	uint8_t tmp_byte;
	
	status = ext_buffer_pull_uint32(pext, &r->count);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == r->count) {
		r->ppstr = NULL;
		return EXT_ERR_SUCCESS;
	}
	r->ppstr = pext->alloc(sizeof(char*)*r->count);
	if (NULL == r->ppstr) {
		return EXT_ERR_ALLOC;
	}
	for (i=0; i<r->count; i++) {
		status = ext_buffer_pull_uint8(pext, &tmp_byte);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		if (0 == tmp_byte) {
			r->ppstr[i] = NULL;
		} else {
			status = ext_buffer_pull_wstring(pext, &r->ppstr[i]);
			if (EXT_ERR_SUCCESS != status) {
				return status;
			}
		}
	}
	return EXT_ERR_SUCCESS;
}

static int ab_ext_pull_binary_array(EXT_PULL *pext, BINARY_ARRAY *r)
{
	int i;
	int status;
	uint8_t tmp_byte;
	
	status = ext_buffer_pull_uint32(pext, &r->count);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == r->count) {
		r->pbin = NULL;
		return EXT_ERR_SUCCESS;
	}
	r->pbin = pext->alloc(sizeof(BINARY)*r->count);
	if (NULL == r->pbin) {
		return EXT_ERR_ALLOC;
	}
	for (i=0; i<r->count; i++) {
		status = ext_buffer_pull_uint8(pext, &tmp_byte);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		if (0 == tmp_byte) {
			r->pbin[i].cb = 0;
			r->pbin[i].pb = NULL;
		} else {
			status = ext_buffer_pull_binary(pext, &r->pbin[i]);
			if (EXT_ERR_SUCCESS != status) {
				return status;
			}
		}
	}
	return EXT_ERR_SUCCESS;
}

static int ab_ext_pull_multiple_val(
	EXT_PULL *pext, uint16_t type, void **ppval)
{
	switch (type) {
	case PROPVAL_TYPE_SHORT_ARRAY:
		*ppval = pext->alloc(sizeof(SHORT_ARRAY));
		if (NULL == (*ppval)) {
			return EXT_ERR_ALLOC;
		}
		return ext_buffer_pull_short_array(pext, *ppval);
	case PROPVAL_TYPE_LONG_ARRAY:
		*ppval = pext->alloc(sizeof(LONG_ARRAY));
		if (NULL == (*ppval)) {
			return EXT_ERR_ALLOC;
		}
		return ext_buffer_pull_long_array(pext, *ppval);
	case PROPVAL_TYPE_LONGLONG_ARRAY:
		*ppval = pext->alloc(sizeof(LONGLONG_ARRAY));
		if (NULL == (*ppval)) {
			return EXT_ERR_ALLOC;
		}
		return ext_buffer_pull_longlong_array(pext, *ppval);
	case PROPVAL_TYPE_STRING_ARRAY:
		*ppval = pext->alloc(sizeof(STRING_ARRAY));
		if (NULL == (*ppval)) {
			return EXT_ERR_ALLOC;
		}
		return ab_ext_pull_string_array(pext, *ppval);
	case PROPVAL_TYPE_WSTRING_ARRAY:
		*ppval = pext->alloc(sizeof(STRING_ARRAY));
		if (NULL == (*ppval)) {
			return EXT_ERR_ALLOC;
		}
		return ab_ext_pull_wstring_array(pext, *ppval);
	case PROPVAL_TYPE_GUID_ARRAY:
		*ppval = pext->alloc(sizeof(GUID_ARRAY));
		if (NULL == (*ppval)) {
			return EXT_ERR_ALLOC;
		}
		return ext_buffer_pull_guid_array(pext, *ppval);
	case PROPVAL_TYPE_BINARY_ARRAY:
		*ppval = pext->alloc(sizeof(BINARY_ARRAY));
		if (NULL == (*ppval)) {
			return EXT_ERR_ALLOC;
		}
		return ab_ext_pull_binary_array(pext, *ppval);
	}
	return EXT_ERR_FORMAT;
}

static int ab_ext_pull_addressbook_propval(
	EXT_PULL *pext, uint16_t type, void **ppval)
{
	int status;
	uint8_t tmp_byte;
	
	if (PROPVAL_TYPE_STRING == type ||
		PROPVAL_TYPE_WSTRING == type ||
		PROPVAL_TYPE_BINARY == type ||
		type & 0x1000) {
		status = ext_buffer_pull_uint8(pext, &tmp_byte);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		if (0xFF == tmp_byte) {
			if (type & 0x1000) {
				return ab_ext_pull_multiple_val(pext, type, ppval);
			} else {
				return ext_buffer_pull_propval(pext, type, ppval);
			}
		} else if (0 == tmp_byte) {
			*ppval = NULL;
			return EXT_ERR_SUCCESS;
		} else {
			return EXT_ERR_FORMAT;
		}
	}
	return ext_buffer_pull_propval(pext, type, ppval);
}

static int ab_ext_pull_addressbook_tapropval(
	EXT_PULL *pext, ADDRESSBOOK_TAPROPVAL *ppropval)
{
	int status;
	
	status = ext_buffer_pull_uint16(pext, &ppropval->type);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint16(pext, &ppropval->propid);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	return ab_ext_pull_addressbook_propval(pext,
			ppropval->type, &ppropval->pvalue);	
}

static int ab_ext_pull_addressbook_proplist(
	EXT_PULL *pext, ADDRESSBOOK_PROPLIST *pproplist)
{
	int i;
	int status;
	
	status = ext_buffer_pull_uint32(pext, &pproplist->count);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == pproplist->count) {
		pproplist->ppropval = NULL;
		return EXT_ERR_SUCCESS;
	}
	pproplist->ppropval = pext->alloc(pproplist->count*
						sizeof(ADDRESSBOOK_TAPROPVAL));
	if (NULL == pproplist->ppropval) {
		return EXT_ERR_ALLOC;
	}
	for (i=0; i<pproplist->count; i++) {
		status = ab_ext_pull_addressbook_tapropval(
					pext, pproplist->ppropval + i);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	return EXT_ERR_SUCCESS;
}

static int ab_ext_pull_addressbook_typropval(
	EXT_PULL *pext, ADDRESSBOOK_TYPROPVAL *ppropval)
{
	int status;
	
	status = ext_buffer_pull_uint16(pext, &ppropval->type);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	return ab_ext_pull_addressbook_propval(pext,
			ppropval->type, &ppropval->pvalue);	
}

static int ab_ext_pull_addressbook_fpropval(EXT_PULL *pext,
	uint16_t type, ADDRESSBOOK_FPROPVAL *ppropval)
{
	int status;
	
	status = ext_buffer_pull_uint8(pext, &ppropval->flag);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	switch (ppropval->flag) {
	case ADDRESSBOOK_FLAG_AVAILABLE:
		return ab_ext_pull_addressbook_propval(
				pext, type, &ppropval->pvalue);
	case ADDRESSBOOK_FLAG_UNAVAILABLE:
		ppropval->pvalue = NULL;
		return EXT_ERR_SUCCESS;
	case ADDRESSBOOK_FLAG_ERROR:
		ppropval->pvalue = pext->alloc(sizeof(uint32_t));
		if (NULL == ppropval->pvalue) {
			return EXT_ERR_ALLOC;
		}
		return ext_buffer_pull_uint32(pext, ppropval->pvalue);
	}
	return EXT_ERR_FORMAT;
}

static int ab_ext_pull_addressbook_tfpropval(EXT_PULL *pext,
	ADDRESSBOOK_TFPROPVAL *ppropval)
{
	int status;
	
	status = ext_buffer_pull_uint16(pext, &ppropval->type);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint8(pext, &ppropval->flag);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	switch (ppropval->flag) {
	case ADDRESSBOOK_FLAG_AVAILABLE:
		return ab_ext_pull_addressbook_propval(pext,
				ppropval->type, &ppropval->pvalue);
	case ADDRESSBOOK_FLAG_UNAVAILABLE:
		ppropval->pvalue = NULL;
		return EXT_ERR_SUCCESS;
	case ADDRESSBOOK_FLAG_ERROR:
		ppropval->pvalue = pext->alloc(sizeof(uint32_t));
		if (NULL == ppropval->pvalue) {
			return EXT_ERR_ALLOC;
		}
		return ext_buffer_pull_uint32(pext, ppropval->pvalue);
	}
	return EXT_ERR_FORMAT;
}

static int ab_ext_pull_addressbook_proprow(EXT_PULL *pext,
	const LPROPTAG_ARRAY *pcolumns, ADDRESSBOOK_PROPROW *prow)
{
	int i;
	int status;
	
	status = ext_buffer_pull_uint8(pext, &prow->flag);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	prow->ppvalue = pext->alloc(sizeof(void*)*pcolumns->count);
	if (NULL == prow->ppvalue) {
		return EXT_ERR_ALLOC;
	}
	if (PROPROW_FLAG_NORMAL == prow->flag) {
		for (i=0; i<pcolumns->count; i++) {
			if (PROPVAL_TYPE_UNSPECIFIED ==
				pcolumns->pproptag[i] & 0xFFFF) {
				prow->ppvalue[i] = pext->alloc(
					sizeof(ADDRESSBOOK_TYPROPVAL));
				if (NULL == prow->ppvalue[i]) {
					return EXT_ERR_ALLOC;
				}
				status = ab_ext_pull_addressbook_typropval(
									pext, prow->ppvalue[i]);
			} else {
				status = ab_ext_pull_addressbook_propval(pext,
					pcolumns->pproptag[i]&0xFFFF, prow->ppvalue + i);
			}
			if (EXT_ERR_SUCCESS != status) {
				return status;
			}
		}
	} else if (PROPROW_FLAG_FLAGGED == prow->flag) {
		for (i=0; i<pcolumns->count; i++) {
			if (PROPVAL_TYPE_UNSPECIFIED ==
				pcolumns->pproptag[i] & 0xFFFF) {
				prow->ppvalue[i] = pext->alloc(
					sizeof(ADDRESSBOOK_TFPROPVAL));
				if (NULL == prow->ppvalue[i]) {
					return EXT_ERR_ALLOC;
				}
				status = ab_ext_pull_addressbook_tfpropval(
									pext, prow->ppvalue[i]);
			} else {
				prow->ppvalue[i] = pext->alloc(
					sizeof(ADDRESSBOOK_FPROPVAL));
				if (NULL == prow->ppvalue[i]) {
					return EXT_ERR_ALLOC;
				}
				status = ab_ext_pull_addressbook_fpropval(pext,
					pcolumns->pproptag[i]&0xFFFF, prow->ppvalue[i]);
			}
			if (EXT_ERR_SUCCESS != status) {
				return status;
			}
		}
	} else {
		return EXT_ERR_FORMAT;
	}
	return EXT_ERR_SUCCESS;
}

static int ab_ext_pull_lproptag_array(EXT_PULL *pext,
	LPROPTAG_ARRAY *pproptags)
{
	int i;
	int status;
	
	status = ext_buffer_pull_uint32(pext, &pproptags->count);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	pproptags->pproptag = pext->alloc(sizeof(uint32_t)*pproptags->count);
	if (NULL == pproptags->pproptag) {
		return EXT_ERR_ALLOC;
	}
	for (i=0; i<pproptags->count; i++) {
		status = ext_buffer_pull_uint32(pext, pproptags->pproptag + i);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	return EXT_ERR_SUCCESS;
}

static int ab_ext_pull_mid_array(EXT_PULL *pext,
	MID_ARRAY *pmids)
{
	int i;
	int status;
	
	status = ext_buffer_pull_uint32(pext, &pmids->count);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == pmids->count) {
		pmids->pmid = NULL;
	} else {
		pmids->pmid = pext->alloc(sizeof(uint32_t)*pmids->count);
		if (NULL == pmids->pmid) {
			return EXT_ERR_ALLOC;
		}
	}
	for (i=0; i<pmids->count; i++) {
		status = ext_buffer_pull_uint32(pext, pmids->pmid + i);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	return EXT_ERR_SUCCESS;
}

static int ab_ext_pull_stat(EXT_PULL *pext, STAT *pstat)
{
	int status;
	
	status = ext_buffer_pull_uint32(pext, &pstat->sort_type);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &pstat->container_id);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &pstat->cur_rec);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_int32(pext, &pstat->delta);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &pstat->num_pos);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &pstat->total_rec);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &pstat->codepage);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &pstat->template_locale);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	return ext_buffer_pull_uint32(pext, &pstat->sort_locale);
}

static int ab_ext_pull_addressbook_propname(
	EXT_PULL *pext, ADDRESSBOOK_PROPNAME *ppropname)
{
	int status;
	
	status = ext_buffer_pull_guid(pext, &ppropname->guid);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	return ext_buffer_pull_uint32(pext, &ppropname->id);
}

static int ab_ext_pull_addressbook_colrow(
	EXT_PULL *pext, ADDRESSBOOK_COLROW *pcolrow)
{
	int i;
	int status;
	
	status = ab_ext_pull_lproptag_array(pext, &pcolrow->columns);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &pcolrow->row_count);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	pcolrow->prows = pext->alloc(pcolrow->row_count*
						sizeof(ADDRESSBOOK_PROPROW));
	if (NULL == pcolrow->prows) {
		return EXT_ERR_ALLOC;
	}
	for (i=0; i<pcolrow->row_count; i++) {
		status = ab_ext_pull_addressbook_proprow(pext,
				&pcolrow->columns, pcolrow->prows + i);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	return EXT_ERR_SUCCESS;
}

static int ab_ext_pull_nsp_entryid(EXT_PULL *pext, NSP_ENTRYID *pentryid)
{
	int status;
	
	status = ext_buffer_pull_uint8(pext, &pentryid->id_type);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0x87 != pentryid->id_type && 0x0 != pentryid->id_type) {
		return EXT_ERR_FORMAT;
	}
	status = ext_buffer_pull_uint8(pext, &pentryid->r1);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint8(pext, &pentryid->r2);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint8(pext, &pentryid->r3);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_guid(pext, &pentryid->provider_uid);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &pentryid->display_type);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == pentryid->id_type) {
		return ext_buffer_pull_string(pext, &pentryid->payload.pdn);
	} else {
		return ext_buffer_pull_uint32(pext, &pentryid->payload.mid);
	}
}

static int ab_ext_pull_nsp_entryids(EXT_PULL *pext, NSP_ENTRYIDS *pentryids)
{
	int i;
	int status;
	
	status = ext_buffer_pull_uint32(pext, &pentryids->count);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	pentryids->pentryid = pext->alloc(sizeof(NSP_ENTRYID)*pentryids->count);
	if (NULL == pentryids->pentryid) {
		return EXT_ERR_ALLOC;
	}
	for (i=0; i<pentryids->count; i++) {
		status = ab_ext_pull_nsp_entryid(pext, pentryids->pentryid + i);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	return EXT_ERR_SUCCESS;
}

int ab_ext_pull_bind_request(EXT_PULL *pext, BIND_REQUEST *prequest)
{
	int status;
	uint8_t tmp_byte;
	
	status = ext_buffer_pull_uint32(pext, &prequest->flags);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pstat = NULL;
	} else {
		prequest->pstat = pext->alloc(sizeof(STAT));
		if (NULL == prequest->pstat) {
			return EXT_ERR_ALLOC;
		}
		status = ab_ext_pull_stat(pext, prequest->pstat);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint32(pext, &prequest->cb_auxin);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == prequest->cb_auxin) {
		prequest->pauxin = NULL;
		return EXT_ERR_SUCCESS;
	}
	prequest->pauxin = pext->alloc(prequest->cb_auxin);
	if (NULL == prequest->pauxin) {
		return EXT_ERR_ALLOC;
	}
	return ext_buffer_pull_bytes(pext,
		prequest->pauxin, prequest->cb_auxin);
}

int ab_ext_pull_unbind_request(EXT_PULL *pext, UNBIND_REQUEST *prequest)
{
	int status;
	
	status = ext_buffer_pull_uint32(pext, &prequest->reserved);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &prequest->cb_auxin);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == prequest->cb_auxin) {
		prequest->pauxin = NULL;
		return EXT_ERR_SUCCESS;
	}
	prequest->pauxin = pext->alloc(prequest->cb_auxin);
	if (NULL == prequest->pauxin) {
		return EXT_ERR_ALLOC;
	}
	return ext_buffer_pull_bytes(pext,
		prequest->pauxin, prequest->cb_auxin);
}

int ab_ext_pull_comparemids_request(EXT_PULL *pext,
	COMPAREMIDS_REQUEST *prequest)
{
	int status;
	uint8_t tmp_byte;
	
	status = ext_buffer_pull_uint32(pext, &prequest->reserved);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pstat = NULL;
	} else {
		prequest->pstat = pext->alloc(sizeof(STAT));
		if (NULL == prequest->pstat) {
			return EXT_ERR_ALLOC;
		}
		status = ab_ext_pull_stat(pext, prequest->pstat);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint32(pext, &prequest->mid1);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &prequest->mid2);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &prequest->cb_auxin);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == prequest->cb_auxin) {
		prequest->pauxin = NULL;
		return EXT_ERR_SUCCESS;
	}
	prequest->pauxin = pext->alloc(prequest->cb_auxin);
	if (NULL == prequest->pauxin) {
		return EXT_ERR_ALLOC;
	}
	return ext_buffer_pull_bytes(pext,
		prequest->pauxin, prequest->cb_auxin);
}

int ab_ext_pull_dntomid_request(EXT_PULL *pext, DNTOMID_REQUEST *prequest)
{
	int status;
	uint8_t tmp_byte;
	
	status = ext_buffer_pull_uint32(pext, &prequest->reserved);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pnames = NULL;
	} else {
		prequest->pnames = pext->alloc(sizeof(STRING_ARRAY));
		if (NULL == prequest->pnames) {
			return EXT_ERR_ALLOC;
		}
		status = ext_buffer_pull_string_array(pext, prequest->pnames);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint32(pext, &prequest->cb_auxin);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == prequest->cb_auxin) {
		prequest->pauxin = NULL;
		return EXT_ERR_SUCCESS;
	}
	prequest->pauxin = pext->alloc(prequest->cb_auxin);
	if (NULL == prequest->pauxin) {
		return EXT_ERR_ALLOC;
	}
	return ext_buffer_pull_bytes(pext,
		prequest->pauxin, prequest->cb_auxin);
}

int ab_ext_pull_getmatches_request(
	EXT_PULL *pext, GETMATCHES_REQUEST *prequest)
{
	int status;
	uint8_t tmp_byte;
	
	status = ext_buffer_pull_uint32(pext, &prequest->reserved1);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pstat = NULL;
	} else {
		prequest->pstat = pext->alloc(sizeof(STAT));
		if (NULL == prequest->pstat) {
			return EXT_ERR_ALLOC;
		}
		status = ab_ext_pull_stat(pext, prequest->pstat);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pinmids = NULL;
	} else {
		prequest->pinmids = pext->alloc(sizeof(MID_ARRAY));
		if (NULL == prequest->pinmids) {
			return EXT_ERR_ALLOC;
		}
		status = ab_ext_pull_mid_array(pext, prequest->pinmids);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint32(pext, &prequest->reserved2);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pfilter = NULL;
	} else {
		prequest->pfilter = pext->alloc(sizeof(RESTRICTION));
		if (NULL == prequest->pfilter) {
			return EXT_ERR_ALLOC;
		}
		status = ext_buffer_pull_restriction(pext, prequest->pfilter);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->ppropname = NULL;
	} else {
		prequest->ppropname = pext->alloc(sizeof(ADDRESSBOOK_PROPNAME));
		if (NULL == prequest->ppropname) {
			return EXT_ERR_ALLOC;
		}
		status = ab_ext_pull_addressbook_propname(
						pext, prequest->ppropname);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint32(pext, &prequest->row_count);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pcolumns = NULL;
	} else {
		prequest->pcolumns = pext->alloc(sizeof(LPROPTAG_ARRAY));
		if (NULL == prequest->pcolumns) {
			return EXT_ERR_ALLOC;
		}
		status = ab_ext_pull_lproptag_array(pext, prequest->pcolumns);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint32(pext, &prequest->cb_auxin);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == prequest->cb_auxin) {
		prequest->pauxin = NULL;
		return EXT_ERR_SUCCESS;
	}
	prequest->pauxin = pext->alloc(prequest->cb_auxin);
	if (NULL == prequest->pauxin) {
		return EXT_ERR_ALLOC;
	}
	return ext_buffer_pull_bytes(pext,
		prequest->pauxin, prequest->cb_auxin);
}

int ab_ext_pull_getproplist_request(EXT_PULL *pext,
	GETPROPLIST_REQUEST *prequest)
{
	int status;
	
	status = ext_buffer_pull_uint32(pext, &prequest->flags);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &prequest->mid);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &prequest->codepage);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &prequest->cb_auxin);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == prequest->cb_auxin) {
		prequest->pauxin = NULL;
		return EXT_ERR_SUCCESS;
	}
	prequest->pauxin = pext->alloc(prequest->cb_auxin);
	if (NULL == prequest->pauxin) {
		return EXT_ERR_ALLOC;
	}
	return ext_buffer_pull_bytes(pext,
		prequest->pauxin, prequest->cb_auxin);
}

int ab_ext_pull_getprops_request(EXT_PULL *pext, GETPROPS_REQUEST *prequest)
{
	int status;
	uint8_t tmp_byte;
	
	status = ext_buffer_pull_uint32(pext, &prequest->flags);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pstat = NULL;
	} else {
		prequest->pstat = pext->alloc(sizeof(STAT));
		if (NULL == prequest->pstat) {
			return EXT_ERR_ALLOC;
		}
		status = ab_ext_pull_stat(pext, prequest->pstat);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pproptags = NULL;
	} else {
		prequest->pproptags = pext->alloc(sizeof(LPROPTAG_ARRAY));
		if (NULL == prequest->pproptags) {
			return EXT_ERR_ALLOC;
		}
		status = ab_ext_pull_lproptag_array(pext, prequest->pproptags);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint32(pext, &prequest->cb_auxin);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == prequest->cb_auxin) {
		prequest->pauxin = NULL;
		return EXT_ERR_SUCCESS;
	}
	prequest->pauxin = pext->alloc(prequest->cb_auxin);
	if (NULL == prequest->pauxin) {
		return EXT_ERR_ALLOC;
	}
	return ext_buffer_pull_bytes(pext,
		prequest->pauxin, prequest->cb_auxin);
}

int ab_ext_pull_getspecialtable_request(EXT_PULL *pext,
	GETSPECIALTABLE_REQUEST *prequest)
{
	int status;
	uint8_t tmp_byte;
	
	status = ext_buffer_pull_uint32(pext, &prequest->flags);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pstat = NULL;
	} else {
		prequest->pstat = pext->alloc(sizeof(STAT));
		if (NULL == prequest->pstat) {
			return EXT_ERR_ALLOC;
		}
		status = ab_ext_pull_stat(pext, prequest->pstat);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pversion = NULL;
	} else {
		prequest->pversion = pext->alloc(sizeof(uint32_t));
		if (NULL == prequest->pversion) {
			return EXT_ERR_ALLOC;
		}
		status = ext_buffer_pull_uint32(pext, prequest->pversion);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint32(pext, &prequest->cb_auxin);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == prequest->cb_auxin) {
		prequest->pauxin = NULL;
		return EXT_ERR_SUCCESS;
	}
	prequest->pauxin = pext->alloc(prequest->cb_auxin);
	if (NULL == prequest->pauxin) {
		return EXT_ERR_ALLOC;
	}
	return ext_buffer_pull_bytes(pext,
		prequest->pauxin, prequest->cb_auxin);
}

int ab_ext_pull_gettemplateinfo_request(EXT_PULL *pext,
	GETTEMPLATEINFO_REQUEST *prequest)
{
	int status;
	uint8_t tmp_byte;
	
	status = ext_buffer_pull_uint32(pext, &prequest->flags);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status - ext_buffer_pull_uint32(pext, &prequest->type);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pdn = NULL;
	} else {
		status = ext_buffer_pull_string(pext, &prequest->pdn);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint32(pext, &prequest->codepage);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &prequest->locale_id);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &prequest->cb_auxin);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == prequest->cb_auxin) {
		prequest->pauxin = NULL;
		return EXT_ERR_SUCCESS;
	}
	prequest->pauxin = pext->alloc(prequest->cb_auxin);
	if (NULL == prequest->pauxin) {
		return EXT_ERR_ALLOC;
	}
	return ext_buffer_pull_bytes(pext,
		prequest->pauxin, prequest->cb_auxin);
}

int ab_ext_pull_modlinkatt_request(EXT_PULL *pext,
	MODLINKATT_REQUEST *prequest)
{
	int i;
	int status;
	uint8_t tmp_byte;
	
	status = ext_buffer_pull_uint32(pext, &prequest->flags);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &prequest->proptag);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &prequest->mid);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->entryids.count = 0;
		prequest->entryids.pentryid = NULL;
	} else {
		status = ab_ext_pull_nsp_entryids(pext, &prequest->entryids);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint32(pext, &prequest->cb_auxin);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == prequest->cb_auxin) {
		prequest->pauxin = NULL;
		return EXT_ERR_SUCCESS;
	}
	prequest->pauxin = pext->alloc(prequest->cb_auxin);
	if (NULL == prequest->pauxin) {
		return EXT_ERR_ALLOC;
	}
	return ext_buffer_pull_bytes(pext,
		prequest->pauxin, prequest->cb_auxin);
}

int ab_ext_pull_modprops_request(EXT_PULL *pext, MODPROPS_REQUEST *prequest)
{
	int status;
	uint8_t tmp_byte;
	
	status = ext_buffer_pull_uint32(pext, &prequest->reserved);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pstat = NULL;
	} else {
		prequest->pstat = pext->alloc(sizeof(STAT));
		if (NULL == prequest->pstat) {
			return EXT_ERR_ALLOC;
		}
		status = ab_ext_pull_stat(pext, prequest->pstat);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pproptags = NULL;
	} else {
		prequest->pproptags = pext->alloc(sizeof(LPROPTAG_ARRAY));
		if (NULL == prequest->pproptags) {
			return EXT_ERR_ALLOC;
		}
		status = ab_ext_pull_lproptag_array(pext, prequest->pproptags);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pvalues = NULL;
	} else {
		prequest->pvalues = pext->alloc(sizeof(ADDRESSBOOK_PROPLIST));
		if (NULL == prequest->pvalues) {
			return EXT_ERR_ALLOC;
		}
		status = ab_ext_pull_addressbook_proplist(pext, prequest->pvalues);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint32(pext, &prequest->cb_auxin);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == prequest->cb_auxin) {
		prequest->pauxin = NULL;
		return EXT_ERR_SUCCESS;
	}
	prequest->pauxin = pext->alloc(prequest->cb_auxin);
	if (NULL == prequest->pauxin) {
		return EXT_ERR_ALLOC;
	}
	return ext_buffer_pull_bytes(pext,
		prequest->pauxin, prequest->cb_auxin);
}

int ab_ext_pull_queryrows_request(EXT_PULL *pext,
	QUERYROWS_REQUEST *prequest)
{
	int status;
	uint8_t tmp_byte;
	
	status = ext_buffer_pull_uint32(pext, &prequest->flags);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pstat = NULL;
	} else {
		prequest->pstat = pext->alloc(sizeof(STAT));
		if (NULL == prequest->pstat) {
			return EXT_ERR_ALLOC;
		}
		status = ab_ext_pull_stat(pext, prequest->pstat);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ab_ext_pull_mid_array(pext, &prequest->explicit_table);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &prequest->count);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pcolumns = NULL;
	} else {
		prequest->pcolumns = pext->alloc(sizeof(LPROPTAG_ARRAY));
		if (NULL == prequest->pcolumns) {
			return EXT_ERR_ALLOC;
		}
		status = ab_ext_pull_lproptag_array(pext, prequest->pcolumns);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint32(pext, &prequest->cb_auxin);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == prequest->cb_auxin) {
		prequest->pauxin = NULL;
		return EXT_ERR_SUCCESS;
	}
	prequest->pauxin = pext->alloc(prequest->cb_auxin);
	if (NULL == prequest->pauxin) {
		return EXT_ERR_ALLOC;
	}
	return ext_buffer_pull_bytes(pext,
		prequest->pauxin, prequest->cb_auxin);
}

int ab_ext_pull_querycolumns_request(EXT_PULL *pext,
	QUERYCOLUMNS_REQUEST *prequest)
{
	int status;
	
	status = ext_buffer_pull_uint32(pext, &prequest->reserved);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &prequest->flags);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &prequest->cb_auxin);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == prequest->cb_auxin) {
		prequest->pauxin = NULL;
		return EXT_ERR_SUCCESS;
	}
	prequest->pauxin = pext->alloc(prequest->cb_auxin);
	if (NULL == prequest->pauxin) {
		return EXT_ERR_ALLOC;
	}
	return ext_buffer_pull_bytes(pext,
		prequest->pauxin, prequest->cb_auxin);
}

int ab_ext_pull_resolvenames_request(
	EXT_PULL *pext, RESOLVENAMES_REQUEST *prequest)
{
	int status;
	uint8_t tmp_byte;
	
	status = ext_buffer_pull_uint32(pext, &prequest->reserved);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pstat = NULL;
	} else {
		prequest->pstat = pext->alloc(sizeof(STAT));
		if (NULL == prequest->pstat) {
			return EXT_ERR_ALLOC;
		}
		status = ab_ext_pull_stat(pext, prequest->pstat);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pproptags = NULL;
	} else {
		prequest->pproptags = pext->alloc(sizeof(LPROPTAG_ARRAY));
		if (NULL == prequest->pproptags) {
			return EXT_ERR_ALLOC;
		}
		status = ab_ext_pull_lproptag_array(pext, prequest->pproptags);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pnames = NULL;
	} else {
		prequest->pnames = pext->alloc(sizeof(STRING_ARRAY));
		if (NULL == prequest->pnames) {
			return EXT_ERR_ALLOC;
		}
		status = ext_buffer_pull_wstring_array(pext, prequest->pnames);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint32(pext, &prequest->cb_auxin);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == prequest->cb_auxin) {
		prequest->pauxin = NULL;
		return EXT_ERR_SUCCESS;
	}
	prequest->pauxin = pext->alloc(prequest->cb_auxin);
	if (NULL == prequest->pauxin) {
		return EXT_ERR_ALLOC;
	}
	return ext_buffer_pull_bytes(pext,
		prequest->pauxin, prequest->cb_auxin);
}

int ab_ext_pull_resortrestriction_request(EXT_PULL *pext,
	RESORTRESTRICTION_REQUEST *prequest)
{
	int status;
	uint8_t tmp_byte;
	
	status = ext_buffer_pull_uint32(pext, &prequest->reserved);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pstat = NULL;
	} else {
		prequest->pstat = pext->alloc(sizeof(STAT));
		if (NULL == prequest->pstat) {
			return EXT_ERR_ALLOC;
		}
		status = ab_ext_pull_stat(pext, prequest->pstat);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pinmids = NULL;
	} else {
		prequest->pinmids = pext->alloc(sizeof(MID_ARRAY));
		if (NULL == prequest->pinmids) {
			return EXT_ERR_ALLOC;
		}
		status = ab_ext_pull_mid_array(pext, prequest->pinmids);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint32(pext, &prequest->cb_auxin);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == prequest->cb_auxin) {
		prequest->pauxin = NULL;
		return EXT_ERR_SUCCESS;
	}
	prequest->pauxin = pext->alloc(prequest->cb_auxin);
	if (NULL == prequest->pauxin) {
		return EXT_ERR_ALLOC;
	}
	return ext_buffer_pull_bytes(pext,
		prequest->pauxin, prequest->cb_auxin);
}

int ab_ext_pull_seekentries_request(EXT_PULL *pext,
	SEEKENTRIES_REQUEST *prequest)
{
	int status;
	uint8_t tmp_byte;
	
	status = ext_buffer_pull_uint32(pext, &prequest->reserved);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pstat = NULL;
	} else {
		prequest->pstat = pext->alloc(sizeof(STAT));
		if (NULL == prequest->pstat) {
			return EXT_ERR_ALLOC;
		}
		status = ab_ext_pull_stat(pext, prequest->pstat);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->ptarget = NULL;
	} else {
		prequest->ptarget = pext->alloc(sizeof(ADDRESSBOOK_TAPROPVAL));
		if (NULL == prequest->ptarget) {
			return EXT_ERR_ALLOC;
		}
		status = ab_ext_pull_addressbook_tapropval(pext, prequest->ptarget);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pexplicit_table = NULL;
	} else {
		prequest->pexplicit_table = pext->alloc(sizeof(MID_ARRAY));
		if (NULL == prequest->pexplicit_table) {
			return EXT_ERR_ALLOC;
		}
		status = ab_ext_pull_mid_array(pext, prequest->pexplicit_table);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pcolumns = NULL;
	} else {
		prequest->pcolumns = pext->alloc(sizeof(LPROPTAG_ARRAY));
		if (NULL == prequest->pcolumns) {
			return EXT_ERR_ALLOC;
		}
		status = ab_ext_pull_lproptag_array(pext, prequest->pcolumns);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint32(pext, &prequest->cb_auxin);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == prequest->cb_auxin) {
		prequest->pauxin = NULL;
		return EXT_ERR_SUCCESS;
	}
	prequest->pauxin = pext->alloc(prequest->cb_auxin);
	if (NULL == prequest->pauxin) {
		return EXT_ERR_ALLOC;
	}
	return ext_buffer_pull_bytes(pext,
		prequest->pauxin, prequest->cb_auxin);
}

int ab_ext_pull_updatestat_request(EXT_PULL *pext,
	UPDATESTAT_REQUEST *prequest)
{
	int status;
	uint8_t tmp_byte;
	
	status = ext_buffer_pull_uint32(pext, &prequest->reserved);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint8(pext, &tmp_byte);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == tmp_byte) {
		prequest->pstat = NULL;
	} else {
		prequest->pstat = pext->alloc(sizeof(STAT));
		if (NULL == prequest->pstat) {
			return EXT_ERR_ALLOC;
		}
		status = ab_ext_pull_stat(pext, prequest->pstat);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	status = ext_buffer_pull_uint8(pext, &prequest->delta_requested);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &prequest->cb_auxin);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == prequest->cb_auxin) {
		prequest->pauxin = NULL;
		return EXT_ERR_SUCCESS;
	}
	prequest->pauxin = pext->alloc(prequest->cb_auxin);
	if (NULL == prequest->pauxin) {
		return EXT_ERR_ALLOC;
	}
	return ext_buffer_pull_bytes(pext,
		prequest->pauxin, prequest->cb_auxin);
}

int ab_ext_pull_getmailboxurl_request(EXT_PULL *pext,
	GETMAILBOXURL_REQUEST *prequest)
{
	int status;
	
	status = ext_buffer_pull_uint32(pext, &prequest->flags);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_wstring(pext, &prequest->puser_dn);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &prequest->cb_auxin);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == prequest->cb_auxin) {
		prequest->pauxin = NULL;
		return EXT_ERR_SUCCESS;
	}
	prequest->pauxin = pext->alloc(prequest->cb_auxin);
	if (NULL == prequest->pauxin) {
		return EXT_ERR_ALLOC;
	}
	return ext_buffer_pull_bytes(pext,
		prequest->pauxin, prequest->cb_auxin);
}

int ab_ext_pull_getaddressbookurl_request(EXT_PULL *pext,
	GETADDRESSBOOKURL_REQUEST *prequest)
{
	int status;
	
	status = ext_buffer_pull_uint32(pext, &prequest->flags);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_wstring(pext, &prequest->puser_dn);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_pull_uint32(pext, &prequest->cb_auxin);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == prequest->cb_auxin) {
		prequest->pauxin = NULL;
		return EXT_ERR_SUCCESS;
	}
	prequest->pauxin = pext->alloc(prequest->cb_auxin);
	if (NULL == prequest->pauxin) {
		return EXT_ERR_ALLOC;
	}
	return ext_buffer_pull_bytes(pext,
		prequest->pauxin, prequest->cb_auxin);
}

/*---------------------------------------------------------------------------*/

static int ab_ext_push_string_array(EXT_PUSH *pext, const STRING_ARRAY *r)
{
	int i;
	int status;
	
	status = ext_buffer_push_uint32(pext, r->count);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	for (i=0; i<r->count; i++) {
		if (NULL == r->ppstr[i]) {
			status = ext_buffer_push_uint8(pext, 0);
		} else {
			status = ext_buffer_push_uint8(pext, 0xFF);
			if (EXT_ERR_SUCCESS != status) {
				return status;
			}
			status = ext_buffer_push_string(pext, r->ppstr[i]);
		}
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	return EXT_ERR_SUCCESS;
}

static int ab_ext_push_wstring_array(EXT_PUSH *pext, const STRING_ARRAY *r)
{
	int i;
	int status;
	
	status = ext_buffer_push_uint32(pext, r->count);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	for (i=0; i<r->count; i++) {
		if (NULL == r->ppstr[i]) {
			status = ext_buffer_push_uint8(pext, 0);
		} else {
			status = ext_buffer_push_uint8(pext, 0xFF);
			if (EXT_ERR_SUCCESS != status) {
				return status;
			}
			status = ext_buffer_push_wstring(pext, r->ppstr[i]);
		}
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	return EXT_ERR_SUCCESS;
}

static int ab_ext_push_binary_array(EXT_PUSH *pext, const BINARY_ARRAY *r)
{
	int i;
	int status;
	
	status = ext_buffer_push_uint32(pext, r->count);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	for (i=0; i<r->count; i++) {
		if (0 == r->pbin[i].cb) {
			status = ext_buffer_push_uint8(pext, 0);
		} else {
			status = ext_buffer_push_uint8(pext, 0xFF);
			if (EXT_ERR_SUCCESS != status) {
				return status;
			}
			status = ext_buffer_push_binary(pext, &r->pbin[i]);
		}
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	return EXT_ERR_SUCCESS;
}

static int ab_ext_push_multiple_val(EXT_PUSH *pext,
	uint16_t type, const void *pval)
{
	switch(type) {
	case PROPVAL_TYPE_SHORT_ARRAY:
		return ext_buffer_push_short_array(pext, pval);
	case PROPVAL_TYPE_LONG_ARRAY:
		return ext_buffer_push_long_array(pext, pval);
	case PROPVAL_TYPE_LONGLONG_ARRAY:
		return ext_buffer_push_longlong_array(pext, pval);
	case PROPVAL_TYPE_STRING_ARRAY:
		return ab_ext_push_string_array(pext, pval);
	case PROPVAL_TYPE_WSTRING_ARRAY:
		return ab_ext_push_wstring_array(pext, pval);
	case PROPVAL_TYPE_GUID_ARRAY:
		return ext_buffer_push_guid_array(pext, pval);
	case PROPVAL_TYPE_BINARY_ARRAY:
		return ab_ext_push_binary_array(pext, pval);
	}
	return EXT_ERR_FORMAT;
}

static int ab_ext_push_addressbook_propval(
	EXT_PUSH *pext, uint16_t type, const void *pval)
{
	int status;
	
	if (PROPVAL_TYPE_STRING == type ||
		PROPVAL_TYPE_WSTRING == type ||
		PROPVAL_TYPE_BINARY == type ||
		type & 0x1000) {
		if (NULL == pval) {
			return ext_buffer_push_uint8(pext, 0);
		}
		status = ext_buffer_push_uint8(pext, 0xFF);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		if (type & 0x1000) {
			return ab_ext_push_multiple_val(pext, type, pval);
		}
	}
	return ext_buffer_push_propval(pext, type, pval);
}

static int ab_ext_push_addressbook_tapropval(
	EXT_PUSH *pext, const ADDRESSBOOK_TAPROPVAL *ppropval)
{
	int status;
	
	status = ext_buffer_push_uint16(pext, ppropval->type);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint16(pext, ppropval->propid);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	return ab_ext_push_addressbook_propval(pext,
			ppropval->type, ppropval->pvalue);	
}

static int ab_ext_push_addressbook_proplist(
	EXT_PUSH *pext, const ADDRESSBOOK_PROPLIST *pproplist)
{
	int i;
	int status;
	
	status = ext_buffer_push_uint32(pext, pproplist->count);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	for (i=0; i<pproplist->count; i++) {
		status = ab_ext_push_addressbook_tapropval(
					pext, pproplist->ppropval + i);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	return EXT_ERR_SUCCESS;
}

static int ab_ext_push_addressbook_typropval(
	EXT_PUSH *pext, const ADDRESSBOOK_TYPROPVAL *ppropval)
{
	int status;
	
	status = ext_buffer_push_uint16(pext, ppropval->type);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	return ab_ext_push_addressbook_propval(pext,
			ppropval->type, ppropval->pvalue);	
}

static int ab_ext_push_addressbook_fpropval(EXT_PUSH *pext,
	uint16_t type, const ADDRESSBOOK_FPROPVAL *ppropval)
{
	int status;
	
	status = ext_buffer_push_uint8(pext, ppropval->flag);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	switch (ppropval->flag) {
	case ADDRESSBOOK_FLAG_AVAILABLE:
		return ab_ext_push_addressbook_propval(
				pext, type, ppropval->pvalue);
	case ADDRESSBOOK_FLAG_UNAVAILABLE:
		return EXT_ERR_SUCCESS;
	case ADDRESSBOOK_FLAG_ERROR:
		return ext_buffer_push_uint32(pext, *(uint32_t*)ppropval->pvalue);
	}
	return EXT_ERR_FORMAT;
}

static int ab_ext_push_addressbook_tfpropval(EXT_PUSH *pext,
	const ADDRESSBOOK_TFPROPVAL *ppropval)
{
	int status;
	
	status = ext_buffer_push_uint16(pext, ppropval->type);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint8(pext, ppropval->flag);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	switch (ppropval->flag) {
	case ADDRESSBOOK_FLAG_AVAILABLE:
		return ab_ext_push_addressbook_propval(pext,
				ppropval->type, ppropval->pvalue);
	case ADDRESSBOOK_FLAG_UNAVAILABLE:
		return EXT_ERR_SUCCESS;
	case ADDRESSBOOK_FLAG_ERROR:
		return ext_buffer_push_uint32(pext, *(uint32_t*)ppropval->pvalue);
	}
	return EXT_ERR_FORMAT;
}

static int ab_ext_push_addressbook_proprow(EXT_PUSH *pext,
	const LPROPTAG_ARRAY *pcolumns, const ADDRESSBOOK_PROPROW *prow)
{
	int i;
	int status;
	
	status = ext_buffer_push_uint8(pext, prow->flag);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (PROPROW_FLAG_NORMAL == prow->flag) {
		for (i=0; i<pcolumns->count; i++) {
			if (PROPVAL_TYPE_UNSPECIFIED ==
				pcolumns->pproptag[i] & 0xFFFF) {
				status = ab_ext_push_addressbook_typropval(
									pext, prow->ppvalue[i]);
			} else {
				status = ab_ext_push_addressbook_propval(pext,
					pcolumns->pproptag[i]&0xFFFF, prow->ppvalue[i]);
			}
			if (EXT_ERR_SUCCESS != status) {
				return status;
			}
		}
	} else if (PROPROW_FLAG_FLAGGED == prow->flag) {
		for (i=0; i<pcolumns->count; i++) {
			if (PROPVAL_TYPE_UNSPECIFIED ==
				pcolumns->pproptag[i] & 0xFFFF) {
				status = ab_ext_push_addressbook_tfpropval(
									pext, prow->ppvalue[i]);
			} else {
				status = ab_ext_push_addressbook_fpropval(pext,
					pcolumns->pproptag[i]&0xFFFF, prow->ppvalue[i]);
			}
			if (EXT_ERR_SUCCESS != status) {
				return status;
			}
		}
	} else {
		return EXT_ERR_FORMAT;
	}
	return EXT_ERR_SUCCESS;
}

static int ab_ext_push_lproptag_array(EXT_PUSH *pext,
	const LPROPTAG_ARRAY *pproptags)
{
	int i;
	int status;
	
	status = ext_buffer_push_uint32(pext, pproptags->count);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	for (i=0; i<pproptags->count; i++) {
		status = ext_buffer_push_uint32(pext, pproptags->pproptag[i]);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	return EXT_ERR_SUCCESS;
}

static int ab_ext_push_mid_array(EXT_PUSH *pext,
	const MID_ARRAY *pmids)
{
	int i;
	int status;
	
	status = ext_buffer_push_uint32(pext, pmids->count);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	for (i=0; i<pmids->count; i++) {
		status = ext_buffer_push_uint32(pext, pmids->pmid[i]);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	return EXT_ERR_SUCCESS;
}

static int ab_ext_push_stat(EXT_PUSH *pext, const STAT *pstat)
{
	int status;
	
	status = ext_buffer_push_uint32(pext, pstat->sort_type);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, pstat->container_id);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, pstat->cur_rec);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_int32(pext, pstat->delta);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, pstat->num_pos);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, pstat->total_rec);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, pstat->codepage);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, pstat->template_locale);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	return ext_buffer_push_uint32(pext, pstat->sort_locale);
}

static int ab_ext_push_addressbook_propname(
	EXT_PUSH *pext, const ADDRESSBOOK_PROPNAME *ppropname)
{
	int status;
	
	status = ext_buffer_push_guid(pext, &ppropname->guid);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	return ext_buffer_push_uint32(pext, ppropname->id);
}

static int ab_ext_push_addressbook_colrow(
	EXT_PUSH *pext, const ADDRESSBOOK_COLROW *pcolrow)
{
	int i;
	int status;
	
	status = ab_ext_push_lproptag_array(pext, &pcolrow->columns);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, pcolrow->row_count);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	for (i=0; i<pcolrow->row_count; i++) {
		status = ab_ext_push_addressbook_proprow(pext,
				&pcolrow->columns, pcolrow->prows + i);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	return EXT_ERR_SUCCESS;
}

static int ab_ext_push_nsp_entryid(
	EXT_PUSH *pext, const NSP_ENTRYID *pentryid)
{
	int status;
	
	if (0x87 != pentryid->id_type && 0x0 != pentryid->id_type) {
		return EXT_ERR_FORMAT;
	}
	status = ext_buffer_push_uint8(pext, pentryid->id_type);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint8(pext, pentryid->r1);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint8(pext, pentryid->r2);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint8(pext, pentryid->r3);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_guid(pext, &pentryid->provider_uid);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, pentryid->display_type);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == pentryid->id_type) {
		return ext_buffer_push_string(pext, pentryid->payload.pdn);
	} else {
		return ext_buffer_push_uint32(pext, pentryid->payload.mid);
	}
}

int ab_ext_push_bind_response(EXT_PUSH *pext,
	const BIND_RESPONSE *presponse)
{
	int status;
	
	status = ext_buffer_push_uint32(pext, presponse->status);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->result);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_guid(pext, &presponse->server_guid);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	return ext_buffer_push_uint32(pext, 0);
}

int ab_ext_push_unbind_response(EXT_PUSH *pext,
	const UNBIND_RESPONSE *presponse)
{
	int status;
	
	status = ext_buffer_push_uint32(pext, presponse->status);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->result);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	return ext_buffer_push_uint32(pext, 0);
}

int ab_ext_push_comparemids_response(EXT_PUSH *pext,
	const COMPAREMIDS_RESPONSE *presponse)
{
	int status;
	
	status = ext_buffer_push_uint32(pext, presponse->status);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->result);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->result1);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	return ext_buffer_push_uint32(pext, 0);
}

int ab_ext_push_dntomid_response(EXT_PUSH *pext,
	const DNTOMID_RESPONSE *presponse)
{
	int status;
	
	status = ext_buffer_push_uint32(pext, presponse->status);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->result);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (NULL == presponse->poutmids) {
		status = ext_buffer_push_uint8(pext, 0);
	} else {
		status = ext_buffer_push_uint8(pext, 0xFF);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		status = ab_ext_push_mid_array(pext, presponse->poutmids);
	}
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	return ext_buffer_push_uint32(pext, 0);
}

int ab_ext_push_getmatches_response(EXT_PUSH *pext,
	const GETMATCHES_RESPONSE *presponse)
{
	int status;
	
	status = ext_buffer_push_uint32(pext, presponse->status);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->result);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (NULL == presponse->pstat) {
		status = ext_buffer_push_uint8(pext, 0);
	} else {
		status = ext_buffer_push_uint8(pext, 0xFF);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		status = ab_ext_push_stat(pext, presponse->pstat);
	}
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (NULL == presponse->pmids) {
		status = ext_buffer_push_uint8(pext, 0);
	} else {
		status = ext_buffer_push_uint8(pext, 0xFF);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		status = ab_ext_push_mid_array(pext, presponse->pmids);
	}
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (EC_SUCCESS != presponse->result) {
		status = ext_buffer_push_uint8(pext, 0);
	} else {
		status = ext_buffer_push_uint8(pext, 0xFF);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		status = ab_ext_push_addressbook_colrow(
				pext, &presponse->column_rows);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	return ext_buffer_push_uint32(pext, 0);
}

int ab_ext_push_getproplist_response(EXT_PUSH *pext,
	const GETPROPLIST_RESPONSE *presponse)
{
	int status;
	
	status = ext_buffer_push_uint32(pext, presponse->status);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->result);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (NULL == presponse->pproptags) {
		status = ext_buffer_push_uint8(pext, 0);
	} else {
		status = ext_buffer_push_uint8(pext, 0xFF);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		status = ab_ext_push_lproptag_array(pext, presponse->pproptags);
	}
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	return ext_buffer_push_uint32(pext, 0);
} 

int ab_ext_push_getprops_response(EXT_PUSH *pext,
	const GETPROPS_RESPONSE *presponse)
{
	int status;
	
	status = ext_buffer_push_uint32(pext, presponse->status);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->result);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->codepage);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (NULL == presponse->prow) {
		status = ext_buffer_push_uint8(pext, 0);
	} else {
		status = ext_buffer_push_uint8(pext, 0xFF);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		status = ab_ext_push_addressbook_proplist(pext, presponse->prow);
	}
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	return ext_buffer_push_uint32(pext, 0);
}

int ab_ext_push_getspecialtable_response(EXT_PUSH *pext,
	const GETSPECIALTABLE_RESPONSE *presponse)
{
	int i;
	int status;
	
	status = ext_buffer_push_uint32(pext, presponse->status);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->result);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->codepage);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (NULL == presponse->pversion) {
		status = ext_buffer_push_uint8(pext, 0);
	} else {
		status = ext_buffer_push_uint8(pext, 0xFF);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		status = ext_buffer_push_uint32(pext, *presponse->pversion);
	}
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (0 == presponse->count) {
		status = ext_buffer_push_uint8(pext, 0);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	} else {
		status = ext_buffer_push_uint8(pext, 0xFF);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		status = ext_buffer_push_uint32(pext, presponse->count);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		for (i=0; i<presponse->count; i++) {
			status = ab_ext_push_addressbook_proplist(
					pext, presponse->prow + i);
			if (EXT_ERR_SUCCESS != status) {
				return status;
			}
		}
	}
	return ext_buffer_push_uint32(pext, 0);
}

int ab_ext_push_gettemplateinfo_response(EXT_PUSH *pext,
	const GETTEMPLATEINFO_RESPONSE *presponse)
{
	int status;
	
	status = ext_buffer_push_uint32(pext, presponse->status);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->result);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->codepage);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (NULL == presponse->prow) {
		status = ext_buffer_push_uint8(pext, 0);
	} else {
		status = ext_buffer_push_uint8(pext, 0xFF);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		status = ab_ext_push_addressbook_proplist(
							pext, presponse->prow);
	}
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	return ext_buffer_push_uint32(pext, 0);
}

int ab_ext_push_modlinkatt_response(EXT_PUSH *pext,
	const MODLINKATT_RESPONSE *presponse)
{
	int status;
	
	status = ext_buffer_push_uint32(pext, presponse->status);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->result);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	return ext_buffer_push_uint32(pext, 0);
}

int ab_ext_push_modprops_response(EXT_PUSH *pext,
	const MODPROPS_RESPONSE *presponse)
{
	int status;
	
	status = ext_buffer_push_uint32(pext, presponse->status);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->result);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	return ext_buffer_push_uint32(pext, 0);
}

int ab_ext_push_queryrows_response(EXT_PUSH *pext,
	const QUERYROWS_RESPONSE *presponse)
{
	int status;
	
	status = ext_buffer_push_uint32(pext, presponse->status);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->result);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (NULL == presponse->pstat) {
		status = ext_buffer_push_uint8(pext, 0);
	} else {
		status = ext_buffer_push_uint8(pext, 0xFF);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		status = ab_ext_push_stat(pext, presponse->pstat);
	}
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (EC_SUCCESS != presponse->result) {
		status = ext_buffer_push_uint8(pext, 0);
	} else {
		status = ext_buffer_push_uint8(pext, 0xFF);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		status = ab_ext_push_addressbook_colrow(
				pext, &presponse->column_rows);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	return ext_buffer_push_uint32(pext, 0);
}

int ab_ext_push_querycolumns_response(EXT_PUSH *pext,
	const QUERYCOLUMNS_RESPONSE *presponse)
{
	int status;
	
	status = ext_buffer_push_uint32(pext, presponse->status);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->result);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (NULL == presponse->pcolumns) {
		status = ext_buffer_push_uint8(pext, 0);
	} else {
		status = ext_buffer_push_uint8(pext, 0xFF);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		status = ab_ext_push_lproptag_array(pext, presponse->pcolumns);
	}
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	return ext_buffer_push_uint32(pext, 0);
}

int ab_ext_push_resolvenames_response(EXT_PUSH *pext,
	const RESOLVENAMES_RESPONSE *presponse)
{
	int status;
	
	status = ext_buffer_push_uint32(pext, presponse->status);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->result);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->codepage);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (NULL != presponse->pmids) {
		status = ext_buffer_push_uint8(pext, 0);
	} else {
		status = ext_buffer_push_uint8(pext, 0xFF);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		status = ab_ext_push_mid_array(pext, presponse->pmids);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (EC_SUCCESS != presponse->result) {
		status = ext_buffer_push_uint8(pext, 0);
	} else {
		status = ext_buffer_push_uint8(pext, 0xFF);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		status = ab_ext_push_addressbook_colrow(
				pext, &presponse->column_rows);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	return ext_buffer_push_uint32(pext, 0);
}

int ab_ext_push_resortrestriction_response(EXT_PUSH *pext,
	const RESORTRESTRICTION_RESPONSE *presponse)
{
	int status;
	
	status = ext_buffer_push_uint32(pext, presponse->status);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->result);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (NULL == presponse->pstat) {
		status = ext_buffer_push_uint8(pext, 0);
	} else {
		status = ext_buffer_push_uint8(pext, 0xFF);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		status = ab_ext_push_stat(pext, presponse->pstat);
	}
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (NULL != presponse->poutmids) {
		status = ext_buffer_push_uint8(pext, 0);
	} else {
		status = ext_buffer_push_uint8(pext, 0xFF);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		status = ab_ext_push_mid_array(pext, presponse->poutmids);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	return ext_buffer_push_uint32(pext, 0);
}

int ab_ext_push_seekentries_response(EXT_PUSH *pext,
	const SEEKENTRIES_RESPONSE *presponse)
{
	int status;
	
	status = ext_buffer_push_uint32(pext, presponse->status);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->result);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (NULL == presponse->pstat) {
		status = ext_buffer_push_uint8(pext, 0);
	} else {
		status = ext_buffer_push_uint8(pext, 0xFF);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		status = ab_ext_push_stat(pext, presponse->pstat);
	}
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (EC_SUCCESS != presponse->result) {
		status = ext_buffer_push_uint8(pext, 0);
	} else {
		status = ext_buffer_push_uint8(pext, 0xFF);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		status = ab_ext_push_addressbook_colrow(
				pext, &presponse->column_rows);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
	}
	return ext_buffer_push_uint32(pext, 0);
}

int ab_ext_push_updatestat_response(EXT_PUSH *pext,
	const UPDATESTAT_RESPONSE *presponse)
{
	int status;
	
	status = ext_buffer_push_uint32(pext, presponse->status);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->result);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (NULL == presponse->pstat) {
		status = ext_buffer_push_uint8(pext, 0);
	} else {
		status = ext_buffer_push_uint8(pext, 0xFF);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		status = ab_ext_push_stat(pext, presponse->pstat);
	}
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	if (NULL == presponse->pdelta) {
		status = ext_buffer_push_uint8(pext, 0);
	} else {
		status = ext_buffer_push_uint8(pext, 0xFF);
		if (EXT_ERR_SUCCESS != status) {
			return status;
		}
		status = ext_buffer_push_int32(pext, *presponse->pdelta);
	}
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	return ext_buffer_push_uint32(pext, 0);
}

int ab_ext_push_getmailboxurl_response(EXT_PUSH *pext,
	const GETMAILBOXURL_RESPONSE *presponse)
{
	int status;
	
	status = ext_buffer_push_uint32(pext, presponse->status);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->result);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_wstring(pext, presponse->server_url);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	return ext_buffer_push_uint32(pext, 0);
}

int ab_ext_push_getaddressbookurl_response(EXT_PUSH *pext,
	const GETADDRESSBOOKURL_RESPONSE *presponse)
{
	int status;
	
	status = ext_buffer_push_uint32(pext, presponse->status);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_uint32(pext, presponse->result);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	status = ext_buffer_push_wstring(pext, presponse->server_url);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	return ext_buffer_push_uint32(pext, 0);
}

int ab_ext_push_failure_response(EXT_PUSH *pext, uint32_t status_code)
{
	int status;
	
	status = ext_buffer_push_uint32(pext, status_code);
	if (EXT_ERR_SUCCESS != status) {
		return status;
	}
	return ext_buffer_push_uint32(pext, 0);
}
