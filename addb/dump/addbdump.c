/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Dave Cohrs <Dave_Cohrs@xyratex.com>
 * Original creation date: 12/18/2012
 */

#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/getopts.h"
#include "lib/memory.h"
#include "lib/misc.h" /* M0_SET0 */
#include "lib/thread.h" /* LAMBDA */
#include "addb/addb.h"
#include "balloc/balloc.h"
#include "dtm/dtm.h"
#include "mero/init.h"
#include "mero/setup.h"
#include "stob/ad.h"
#include "stob/linux.h"

/*
 * Tracks related information pertinent to AD stob.
 */
struct dump_ad_stob {
	/* Allocation data storage domain.*/
	struct m0_stob_domain *as_dom;
	/* Back end storage object id, i.e. ad */
	struct m0_stob_id      as_id_back;
	/* Back end storage object. */
	struct m0_stob        *as_stob_back;
};

/*
 * Encapsulates stob, stob type and
 * stob domain references for linux and ad stobs.
 */
struct dump_stob {
	/* Type of storage domain (M0_AD_STOB or M0_LINUX_STOB) */
	int                    s_stype_nr;
	/* Linux storage domain. */
	struct m0_stob_domain *s_ldom;
	struct dump_ad_stob   *s_adom;
	/* The ADDB stob */
	struct m0_stob        *s_stob;
};

struct addb_dump_ctl;
/**
 * Abstract operations for dumping records.
 */
struct addb_dump_ops {
	/* Dump a segment sequence number */
	void (*ado_seq_nr_dump)(struct addb_dump_ctl *ctl, uint64_t seq_nr);
	/* Dump a record */
	void (*ado_rec_dump)(struct addb_dump_ctl *ctl, uint64_t nr,
			     const struct m0_addb_rec *rec);
	/* Dump the summary */
	void (*ado_summary_dump)(struct addb_dump_ctl *ctl, uint64_t nr,
				 uint64_t seg_nr, uint64_t max_seq_nr);
};

/*
 * Represents m0addbdump environment.
 */
struct addb_dump_ctl {
	/* ADDB Storage path */
	const char                  *adc_stpath;
	/* Database environment path. */
	const char                  *adc_dbpath;
	/* Binary data input file path */
	const char                  *adc_infile;
	/* Flags for record cursor */
	uint32_t                     adc_dump_flags;
	/* Display Timestamp in GMT */
	bool                         adc_gmt;
	/* Generate YAML output */
	bool                         adc_yaml;

	/* a string to display with error message */
	const char                  *adc_errstr;
	int                          adc_stype_nr;
	struct m0_dbenv              adc_db;
	struct dump_stob             adc_stob;
	struct m0_addb_segment_iter *adc_iter;
	FILE                        *adc_out;
	const struct addb_dump_ops  *adc_ops;
};

/* Returns valid stob type ID, or M0_STOB_TYPE_NR */
static int stype_parse(const char *stype)
{
	int i;

	if (stype == NULL)
		return M0_STOB_TYPE_NR;
	for (i = 0; i < ARRAY_SIZE(m0_cs_stypes); ++i) {
		if (strcasecmp(stype, m0_cs_stypes[i]) == 0)
			break;
	}
	return i;
}

static int dump_linux_stob_init(struct addb_dump_ctl *ctl)
{
	int rc;

	rc = m0_stob_domain_locate(&m0_linux_stob_type, ctl->adc_stpath,
				   &ctl->adc_stob.s_ldom) ?:
	    m0_linux_stob_setup(ctl->adc_stob.s_ldom, false);
	return rc;
}

static int dump_stob_locate(struct m0_stob_domain *dom,
			    struct m0_dtx *dtx,
			    const struct m0_stob_id *stob_id,
			    struct m0_stob **out)
{
	struct m0_stob *stob;
	int             rc;

	*out = NULL;
	rc = m0_stob_find(dom, stob_id, &stob);
	if (rc == 0) {
		/*
		 * Here, stob != NULL and m0_stob_find() has taken reference on
		 * stob. On error must call m0_stob_put() on stob, after this
		 * point.
		 */
		if (stob->so_state == CSS_UNKNOWN)
			rc = m0_stob_locate(stob, dtx);
		/* do not attempt to create, this is a dump utility! */
		if (rc != 0) {
			m0_stob_put(stob);
		} else {
			M0_ASSERT(stob->so_state == CSS_EXISTS);
			*out = stob;
		}
	}
	return rc;
}

static int dump_ad_stob_init(struct dump_stob *stob, uint64_t cid,
			     struct m0_dtx *tx, struct m0_dbenv *db)
{

	char                 ad_dname[MAXPATHLEN];
	struct dump_ad_stob *adstob;
	struct m0_stob_id   *bstob_id;
	struct m0_stob     **bstob;
	struct m0_balloc    *cb;
	int                  rc;

        M0_PRE(stob != NULL && db != NULL);
	M0_ALLOC_PTR(adstob);
	if (adstob == NULL)
		return -ENOMEM;

	rc = m0_dtx_open(tx, db);
	if (rc != 0)
		return rc;
	bstob = &adstob->as_stob_back;
	bstob_id = &adstob->as_id_back;
	bstob_id->si_bits.u_hi = cid;
	bstob_id->si_bits.u_lo = M0_AD_STOB_ID_LO;
	rc = dump_stob_locate(stob->s_ldom, tx, bstob_id, bstob);
	if (rc == 0) {
		sprintf(ad_dname, "%lx%lx",
			bstob_id->si_bits.u_hi, bstob_id->si_bits.u_lo);
		rc = m0_stob_domain_locate(&m0_ad_stob_type, ad_dname,
					   &adstob->as_dom);
	}
	if (rc != 0) {
		if (*bstob != NULL) {
			m0_stob_put(*bstob);
			*bstob = NULL;
		}
		m0_free(adstob);
	} else {
		M0_ASSERT(stob->s_adom == NULL);
		stob->s_adom = adstob;
		rc = m0_balloc_allocate(cid, &cb) ?:
		    m0_ad_stob_setup(adstob->as_dom, db,
				     *bstob, &cb->cb_ballroom,
				     BALLOC_DEF_CONTAINER_SIZE,
				     BALLOC_DEF_BLOCK_SHIFT,
				     BALLOC_DEF_BLOCKS_PER_GROUP,
				     BALLOC_DEF_RESERVED_GROUPS);
	}
	return rc;
}

static void dump_linux_stob_fini(struct dump_stob *stob)
{
	M0_PRE(stob != NULL);
	if (stob->s_ldom != NULL)
                stob->s_ldom->sd_ops->sdo_fini(stob->s_ldom);
}

static void dump_ad_stob_fini(struct dump_stob *stob)
{
	struct m0_stob        *bstob;
	struct dump_ad_stob   *adstob;
	struct m0_stob_domain *adom;

	M0_PRE(stob != NULL);

	if (stob->s_adom != NULL) {
		adstob = stob->s_adom;
		bstob = adstob->as_stob_back;
		adom = adstob->as_dom;
		if (bstob != NULL)
			m0_stob_put(bstob);
		adom->sd_ops->sdo_fini(adom);
		stob->s_adom = NULL;
		m0_free(adstob);
	}
}

static void dump_storage_fini(struct dump_stob *stob)
{
	M0_PRE(stob != NULL);

	if (stob->s_stob != NULL)
		m0_stob_put(stob->s_stob);
	if (stob->s_stype_nr == M0_AD_STOB)
		dump_ad_stob_fini(stob);
        dump_linux_stob_fini(stob);
}

static void cleanup(struct addb_dump_ctl *ctl)
{
	if (ctl->adc_iter != NULL)
		m0_addb_segment_iter_free(ctl->adc_iter);
	dump_storage_fini(&ctl->adc_stob);
	if (ctl->adc_dbpath != NULL)
		m0_dbenv_fini(&ctl->adc_db);
}

static int setup(struct addb_dump_ctl *ctl)
{
	struct m0_dtx tx;
	int           rc;

	if (ctl->adc_dbpath != NULL) {
		rc = m0_dbenv_init(&ctl->adc_db, ctl->adc_dbpath, 0);
		if (rc != 0) {
			ctl->adc_errstr = ctl->adc_dbpath;
			return rc;
		}
	} else if (ctl->adc_infile != NULL) {
		rc = m0_addb_file_iter_alloc(&ctl->adc_iter, ctl->adc_infile);
		if (rc != 0)
			ctl->adc_errstr = ctl->adc_infile;
		return rc;
	}

	ctl->adc_stob.s_stype_nr = ctl->adc_stype_nr;
	m0_dtx_init(&tx);
	rc = dump_linux_stob_init(ctl);
	if (rc == 0 && ctl->adc_stype_nr == M0_AD_STOB)
		rc = dump_ad_stob_init(&ctl->adc_stob, M0_ADDB_STOB_ID_HI,
				       &tx, &ctl->adc_db);

	if (rc == 0) {
		rc = dump_stob_locate(ctl->adc_stype_nr == M0_LINUX_STOB ?
				      ctl->adc_stob.s_ldom :
				      ctl->adc_stob.s_adom->as_dom,
				      &tx, &m0_addb_stob_id,
				      &ctl->adc_stob.s_stob);
	}
	m0_dtx_done(&tx);
	if (rc != 0 && ctl->adc_dbpath != NULL)
		dump_storage_fini(&ctl->adc_stob);
	if (rc != 0)
		ctl->adc_errstr = ctl->adc_stpath;
	else
		rc = m0_addb_stob_iter_alloc(&ctl->adc_iter,
					     ctl->adc_stob.s_stob);
	return rc;
}

static void dump_cv_print(struct addb_dump_ctl *ctl,
			  const struct m0_addb_rec *r)
{
	FILE                      *out = ctl->adc_out;
	struct m0_addb_uint64_seq *s;
	int                        i;
	int                        j;

	for (i = 0; i < r->ar_ctxids.acis_nr; ++i) {
		fprintf(out, "       cv[%d]", i);
		s = &r->ar_ctxids.acis_data[i];
		for (j = 0; j < s->au64s_nr; ++j) {
			fprintf(out, "%c%lx",
				j == 0 ? ' ' : '/', s->au64s_data[j]);
		}
		fprintf(out, "\n");
	}
}

static void dump_ts_print(struct addb_dump_ctl *ctl,
			  const struct m0_addb_rec *r)
{
	FILE     *out = ctl->adc_out;
	struct tm tm;
	time_t    ts = m0_time_seconds(r->ar_ts);

	if (ctl->adc_gmt)
		gmtime_r(&ts, &tm);
	else
		localtime_r(&ts, &tm);
	fprintf(out, " @ %04d-%02d-%02d %02d:%02d:%02d.%09lu",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
		tm.tm_min, tm.tm_sec, m0_time_nanoseconds(r->ar_ts));
	if (r->ar_data.au64s_nr > 0)
		fprintf(out, "\n     ");
}

static void dump_ctx_rec_print(struct addb_dump_ctl *ctl,
			       const struct m0_addb_rec *r)
{
	FILE                          *out = ctl->adc_out;
	const struct m0_addb_ctx_type *ct;
	int                            i;

	ct = m0_addb_ctx_type_lookup(m0_addb_rec_rid_to_id(r->ar_rid));
	if (ct != NULL) {
		fprintf(out, "context %s", ct->act_name);
		if (ct->act_cf_nr != r->ar_data.au64s_nr) {
			fprintf(out, " !MISMATCH!");
			goto rawfields;
		}
		dump_ts_print(ctl, r);
		for (i = 0; i < r->ar_data.au64s_nr; ++i)
			fprintf(out, " %s=%lu",
				ct->act_cf[i], r->ar_data.au64s_data[i]);
	} else {
		fprintf(out, "unknown context %d",
			m0_addb_rec_rid_to_id(r->ar_rid));
	rawfields:
		dump_ts_print(ctl, r);
		for (i = 0; i < r->ar_data.au64s_nr; ++i)
			fprintf(out, " [%d]=%lu", i, r->ar_data.au64s_data[i]);
	}
	fprintf(out, "\n");
}

static void dump_event_rec_print(struct addb_dump_ctl *ctl,
				 const struct m0_addb_rec *r)
{
	FILE                          *out = ctl->adc_out;
	const struct m0_addb_rec_type *rt;
	int                            fields_nr;
	int                            i;

	/*
	 * TS Type CTX Data
	 * one per line, mostly numeric.
	 */
	rt = m0_addb_rec_type_lookup(m0_addb_rec_rid_to_id(r->ar_rid));
	switch (rt != NULL ? rt->art_base_type : M0_ADDB_BRT_NR) {
	case M0_ADDB_BRT_EX:
		fprintf(out, "exception %s", rt->art_name);
		goto fields;
	case M0_ADDB_BRT_DP:
		fprintf(out, "datapoint %s", rt->art_name);
	fields:
		if (rt->art_rf_nr != r->ar_data.au64s_nr) {
			fprintf(out, " !MISMATCH!");
			goto rawfields;
		}
		dump_ts_print(ctl, r);
		for (i = 0; i < r->ar_data.au64s_nr; ++i)
			fprintf(out, " %s=%lu",
				rt->art_rf[i].arfu_name,
				r->ar_data.au64s_data[i]);
		break;
	case M0_ADDB_BRT_CNTR:
		fprintf(out, "counter %s", rt->art_name);
		fields_nr = sizeof(struct m0_addb_counter_data) /
			    sizeof(uint64_t);
		if (rt->art_rf_nr > 0)
			fields_nr += rt->art_rf_nr + 1;
		if (r->ar_data.au64s_nr != fields_nr) {
			fprintf(out, " !MISMATCH!");
			goto rawfields;
		}

		dump_ts_print(ctl, r);
		fprintf(out, " seq=%lu", r->ar_data.au64s_data[0]);
		fprintf(out, " num=%lu", r->ar_data.au64s_data[1]);
		fprintf(out, " tot=%lu", r->ar_data.au64s_data[2]);
		fprintf(out, " min=%lu", r->ar_data.au64s_data[3]);
		fprintf(out, " max=%lu", r->ar_data.au64s_data[4]);
		fprintf(out, " sumSQ=%lu", r->ar_data.au64s_data[5]);
		for (i = 6; i < r->ar_data.au64s_nr; ++i)
			fprintf(out, " [%ld]=%lu",
				i == 6 ? 0UL : rt->art_rf[i - 7].arfu_lower,
				r->ar_data.au64s_data[i]);
		break;
	case M0_ADDB_BRT_SEQ:
		fprintf(out, "sequence %s", rt->art_name);
		goto rawfields;
		break;
	default: /* rt == NULL */
		fprintf(out, "unknown event %d,",
			m0_addb_rec_rid_to_id(r->ar_rid));
	rawfields:
		dump_ts_print(ctl, r);
		for (i = 0; i < r->ar_data.au64s_nr; ++i)
			fprintf(out, " [%d]=%lu", i, r->ar_data.au64s_data[i]);
		break;
	}
	fprintf(out, "\n");
}

static void dump_seq_nr_print(struct addb_dump_ctl *ctl, uint64_t seq_nr)
{
	fprintf(ctl->adc_out, "Segment ID %lu\n", seq_nr);
}

static void dump_rec_print(struct addb_dump_ctl *ctl, uint64_t nr,
			   const struct m0_addb_rec *r)
{
	fprintf(ctl->adc_out, "%5lu:", nr);
	if (m0_addb_rec_is_event(r))
		dump_event_rec_print(ctl, r);
	else
		dump_ctx_rec_print(ctl, r);
	dump_cv_print(ctl, r);
}

static void dump_summary_print(struct addb_dump_ctl *ctl, uint64_t nr,
			       uint64_t seg_nr, uint64_t max_seq_nr)
{
	fprintf(ctl->adc_out,
		"Dumped %lu records in %lu segments (max segment ID %lu)\n",
		nr, seg_nr, max_seq_nr);
}

const struct addb_dump_ops dump_text_ops = {
	.ado_seq_nr_dump  = dump_seq_nr_print,
	.ado_rec_dump     = dump_rec_print,
	.ado_summary_dump = dump_summary_print,
};

static void dump_seq_nr_yaml(struct addb_dump_ctl *ctl, uint64_t seq_nr)
{
	fprintf(ctl->adc_out, "---\n");
	fprintf(ctl->adc_out, "segment: %lu\n", seq_nr);
}

static void dump_ts_yaml(struct addb_dump_ctl *ctl, const struct m0_addb_rec *r)
{
	FILE *out = ctl->adc_out;

	fprintf(out, " timestamp: ");
	if (ctl->adc_gmt) {
		struct tm tm;
		time_t    ts = m0_time_seconds(r->ar_ts);

		gmtime_r(&ts, &tm);
		fprintf(out, "%04d-%02d-%02dT%02d:%02d:%02d.%09luZ\n",
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec,
			m0_time_nanoseconds(r->ar_ts));
	} else {
		/* just dump as a number, easier to parse in C */
		fprintf(out, "%lu\n", r->ar_ts);
	}
}

static void dump_cv_yaml(struct addb_dump_ctl *ctl, const struct m0_addb_rec *r)
{
	FILE                      *out = ctl->adc_out;
	struct m0_addb_uint64_seq *s;
	int                        i;
	int                        j;

	fprintf(out, " cv:\n");
	for (i = 0; i < r->ar_ctxids.acis_nr; ++i) {
		s = &r->ar_ctxids.acis_data[i];
		fprintf(out, "  - [");
		for (j = 0; j < s->au64s_nr; ++j) {
			if (j > 0)
				fprintf(out, ",");
			fprintf(out, " 0x%lx", s->au64s_data[j]);
		}
		fprintf(out, " ]\n");
	}
}
static void dump_rawfields_yaml(struct addb_dump_ctl *ctl, bool dump_id,
				const struct m0_addb_rec *r)
{
	FILE *out = ctl->adc_out;
	int   i;

	if (dump_id)
		fprintf(out, " id: %d\n", m0_addb_rec_rid_to_id(r->ar_rid));
	dump_ts_yaml(ctl, r);
	if (r->ar_data.au64s_nr > 0) {
		fprintf(out, " rawfields: [ ");
		for (i = 0; i < r->ar_data.au64s_nr; ++i) {
			if (i > 0)
				fprintf(out, ", ");
			fprintf(out, "%lu", r->ar_data.au64s_data[i]);
		}
		fprintf(out, " ]\n");
	}
}

static void dump_ctx_rec_yaml(struct addb_dump_ctl *ctl, uint64_t nr,
			      const struct m0_addb_rec *r)
{
	FILE                          *out = ctl->adc_out;
	const struct m0_addb_ctx_type *ct;
	int                            i;

	ct = m0_addb_ctx_type_lookup(m0_addb_rec_rid_to_id(r->ar_rid));
	if (ct != NULL) {
		fprintf(out, "context: # %lu\n", nr);
		fprintf(out, " name: %s\n", ct->act_name);
		if (ct->act_cf_nr != r->ar_data.au64s_nr)
			goto rawfields;
		dump_ts_yaml(ctl, r);
		if (r->ar_data.au64s_nr > 0) {
			fprintf(out, " fields:\n");
			for (i = 0; i < r->ar_data.au64s_nr; ++i)
				fprintf(out, "  %s: %lu\n",
					ct->act_cf[i],
					r->ar_data.au64s_data[i]);
		}
	} else {
		fprintf(out, "unknown context: # %lu\n", nr);
	rawfields:
		dump_rawfields_yaml(ctl, ct == NULL, r);
	}
}

static void dump_event_rec_yaml(struct addb_dump_ctl *ctl, uint64_t nr,
				const struct m0_addb_rec *r)
{
	FILE                          *out = ctl->adc_out;
	const struct m0_addb_rec_type *rt;
	int                            fields_nr;
	int                            i;

	rt = m0_addb_rec_type_lookup(m0_addb_rec_rid_to_id(r->ar_rid));
	switch (rt != NULL ? rt->art_base_type : M0_ADDB_BRT_NR) {
	case M0_ADDB_BRT_EX:
		fprintf(out, "exception: # %lu\n", nr);
		goto fields;
	case M0_ADDB_BRT_DP:
		fprintf(out, "datapoint: # %lu\n", nr);
	fields:
		fprintf(out, " name: %s\n", rt->art_name);
		if (rt->art_rf_nr != r->ar_data.au64s_nr)
			goto rawfields;
		dump_ts_yaml(ctl, r);
		if (r->ar_data.au64s_nr > 0) {
			fprintf(out, " fields:\n");
			for (i = 0; i < r->ar_data.au64s_nr; ++i)
				fprintf(out, "  %s: %lu\n",
					rt->art_rf[i].arfu_name,
					r->ar_data.au64s_data[i]);
		}
		break;
	case M0_ADDB_BRT_CNTR:
		fprintf(out, "counter: # %lu\n", nr);
		fprintf(out, " name: %s\n", rt->art_name);
		fields_nr = sizeof(struct m0_addb_counter_data) /
			    sizeof(uint64_t);
		if (rt->art_rf_nr > 0)
			fields_nr += rt->art_rf_nr + 1;
		if (r->ar_data.au64s_nr != fields_nr)
			goto rawfields;

		dump_ts_yaml(ctl, r);
		fprintf(out, " seq: %lu\n", r->ar_data.au64s_data[0]);
		fprintf(out, " num: %lu\n", r->ar_data.au64s_data[1]);
		fprintf(out, " tot: %lu\n", r->ar_data.au64s_data[2]);
		fprintf(out, " min: %lu\n", r->ar_data.au64s_data[3]);
		fprintf(out, " max: %lu\n", r->ar_data.au64s_data[4]);
		fprintf(out, " sumSQ: %lu\n", r->ar_data.au64s_data[5]);
		if (r->ar_data.au64s_nr > 6) {
			fprintf(out, " histogram: [ 0");
			for (i = 7; i < r->ar_data.au64s_nr; ++i)
				fprintf(out, ", %lu",
					rt->art_rf[i - 7].arfu_lower);
			fprintf(out, " ]\n");
			fprintf(out, " counts: [ ");
			for (i = 6; i < r->ar_data.au64s_nr; ++i) {
				if (i > 6)
					fprintf(out, ", ");
				fprintf(out, "%lu", r->ar_data.au64s_data[i]);
			}
			fprintf(out, " ]\n");
		}
		break;
	case M0_ADDB_BRT_SEQ:
		fprintf(out, "sequence: # %lu\n", nr);
		fprintf(out, " name: %s", rt->art_name);
		goto rawfields;
		break;
	default: /* rt == NULL */
		fprintf(out, "unknown event: # %lu\n", nr);
	rawfields:
		dump_rawfields_yaml(ctl, rt == NULL, r);
		break;
	}
}

static void dump_rec_yaml(struct addb_dump_ctl *ctl, uint64_t nr,
			  const struct m0_addb_rec *r)
{
	fprintf(ctl->adc_out, "---\n");
	if (m0_addb_rec_is_event(r))
		dump_event_rec_yaml(ctl, nr, r);
	else
		dump_ctx_rec_yaml(ctl, nr, r);
	dump_cv_yaml(ctl, r);
}

static void dump_summary_yaml(struct addb_dump_ctl *ctl, uint64_t nr,
			      uint64_t seg_nr, uint64_t max_seq_nr)
{
	fprintf(ctl->adc_out, "---\n");
	fprintf(ctl->adc_out,
		"summary:\n"
		" records: %lu\n"
		" segments: %lu\n"
		" max_segment_id: %lu\n", nr, seg_nr, max_seq_nr);
}

const struct addb_dump_ops dump_yaml_ops = {
	.ado_seq_nr_dump  = dump_seq_nr_yaml,
	.ado_rec_dump     = dump_rec_yaml,
	.ado_summary_dump = dump_summary_yaml,
};

static int bindump(struct addb_dump_ctl *ctl)
{
	const struct m0_bufvec *bv;
	uint64_t                cur_seq_nr;
	uint64_t                max_seq_nr = 0;
	uint64_t                segcount   = 0;
	int                     i;
	int                     rc;

	while (1) {
		rc = ctl->adc_iter->asi_nextbuf(ctl->adc_iter, &bv);
		if (rc < 0)
			break;
		M0_ASSERT(bv != NULL);
		cur_seq_nr = ctl->adc_iter->asi_seq_get(ctl->adc_iter);
		segcount++;
		if (cur_seq_nr > max_seq_nr)
			max_seq_nr = cur_seq_nr;
		for (i = 0; i < bv->ov_vec.v_nr; ++i) {
			rc = fwrite(bv->ov_buf[i],
				    bv->ov_vec.v_count[i], 1, ctl->adc_out);
			if (rc != 1) {
				rc = errno != 0 ? -errno : -EIO;
				goto fail;
			}
		}
	}
	/*
	 * "ctl->adc_out" may be stdout, summary must be sent elsewhere.
	 * Note the output format is chosen to be YAML-compatible.
	 */
	fprintf(stderr, "Dumped segments: %lu\nMax segment ID: %lu\n",
		segcount, max_seq_nr);
fail:
	if (rc == -ENODATA)
		rc = 0;
	return rc;
}

static int dump(struct addb_dump_ctl *ctl)
{
	struct m0_addb_rec   *rec;
	struct m0_addb_cursor cur;
	uint64_t              cur_seq_nr;
	uint64_t              seq_nr     = 0;
	uint64_t              max_seq_nr = 0;
	uint64_t              count      = 0;
	uint64_t              segcount   = 0;
	int                   rc;

	if (ctl->adc_yaml)
		ctl->adc_ops = &dump_yaml_ops;
	else
		ctl->adc_ops = &dump_text_ops;
	rc = m0_addb_cursor_init(&cur, ctl->adc_iter, ctl->adc_dump_flags);
	if (rc != 0)
		return rc;
	while (1) {
		rc = m0_addb_cursor_next(&cur, &rec);
		if (rc == -ENODATA) {
			if (count != 0)
				rc = 0;
			break;
		}
		M0_ASSERT(rec != NULL);
		cur_seq_nr = ctl->adc_iter->asi_seq_get(ctl->adc_iter);
		if (seq_nr != cur_seq_nr) {
			segcount++;
			seq_nr = cur_seq_nr;
			ctl->adc_ops->ado_seq_nr_dump(ctl, seq_nr);
			if (seq_nr > max_seq_nr)
				max_seq_nr = seq_nr;
		}
		ctl->adc_ops->ado_rec_dump(ctl, count, rec);
		count++;
	}
	m0_addb_cursor_fini(&cur);
	ctl->adc_ops->ado_summary_dump(ctl, count, segcount, max_seq_nr);
	return rc;
}

static void addbdump_help(FILE *out)
{
	int i;

	M0_PRE(out != NULL);
	fprintf(out,
		"Usage: m0addbdump [-h]\n"
		"   or  m0addbdump -T StobType [-D DBPath] [-c][-e][-u][-y]"
		" -A ADDBStobPath\n"
		"                  [-o path]\n"
		"   or  m0addbdump -f path [-c][-e][-u][-y] [-o path]\n"
		"   or  m0addbdump -b -T StobType [-D DBPath]"
		" -A ADDBStobPath [-o path]\n\n");
	fprintf(out,
		"  -b       Dump binary data.  All valid segments are dumped.\n"
		"           The output can be inspected later using -f.\n"
		"  -c       Dump only context records.\n"
		"  -e       Dump only event records.\n"
		"  -u       Output timestamps in UTC.\n"
		"  -y       Dump YAML output.\n");
	fprintf(out,
		"  -f path  Read data from a binary file created using -b\n"
		"           rather than reading from the ADDB repository.\n");
	fprintf(out,
		"  -o path  Write output to a file rather than stdout.\n");
	fprintf(out, "\nSupported stob types:");
	for (i = 0; i < ARRAY_SIZE(m0_cs_stypes); ++i)
		fprintf(out, " %s", m0_cs_stypes[i]);
	fprintf(out, "\n");
	fprintf(out,
		"\nThe DBPath and ADDBStobPath should be the same as those\n"
		"specified for the matching options of the m0d\n"
		"request handler that generated the ADDB repository.\n"
		"The DBPath is not required for Linux stob.\n");
	fprintf(out, "\n"
		"Each dumped record includes a sequence identifier, the event\n"
		"type, its timestamp, its fields and its context(s).\n");
}

int main(int argc, char *argv[])
{
	static const char   *m0addbdump = "m0addbdump";
	struct addb_dump_ctl ctl;
	const char          *outfilepath = NULL;
	const char          *stype = NULL;
	bool                 dump_binary = false;
	bool                 dump_ctx = false;
	bool                 dump_event = false;
	uint64_t             min_seg_id = 0;
	int                  rc = 0;
	int                  r2;

	rc = m0_init();
	if (rc != 0) {
		M0_ASSERT(rc < 0);
		return -rc;
	}
	M0_SET0(&ctl);

	r2 = M0_GETOPTS(m0addbdump, argc, argv,
			M0_STRINGARG('T', "Storage domain type",
				LAMBDA(void, (const char *str)
				{
					if (stype != NULL)
						rc = -EINVAL;
					else
						stype = str;
				})),
			M0_STRINGARG('A', "ADDB Storage domain name",
				LAMBDA(void, (const char *str)
				{
					if (ctl.adc_stpath != NULL)
						rc = -EINVAL;
					else
						ctl.adc_stpath = str;
				})),
			M0_STRINGARG('D', "Database environment path",
				LAMBDA(void, (const char *str)
				{
					if (ctl.adc_dbpath != NULL)
						rc = -EINVAL;
					else
						ctl.adc_dbpath = str;
				})),
			M0_FLAGARG('b', "Dump binary data", &dump_binary),
			M0_FLAGARG('c', "Dump context records", &dump_ctx),
			M0_FLAGARG('e', "Dump event records", &dump_event),
			M0_STRINGARG('f', "Binary data input file",
				LAMBDA(void, (const char *str)
				{
					if (ctl.adc_infile != NULL)
						rc = -EINVAL;
					else
						ctl.adc_infile = str;
				})),
			M0_STRINGARG('o', "Output file",
				LAMBDA(void, (const char *str)
				{
					if (outfilepath != NULL)
						rc = -EINVAL;
					else
						outfilepath = str;
				})),
			M0_FORMATARG('s', "Minimum segment ID", "%lu",
				     &min_seg_id),
			M0_FLAGARG('u', "Display timestamp in UCT",
				   &ctl.adc_gmt),
			M0_FLAGARG('y', "Generate YAML output", &ctl.adc_yaml),
			M0_VOIDARG('h', "detailed usage help",
				LAMBDA(void, (void)
				{
					addbdump_help(stderr);
					rc = 1;
				})));
	if (rc == 0)
		rc = r2;
	if (rc == 0) {
		if (dump_binary &&
		    (ctl.adc_gmt || ctl.adc_yaml || dump_ctx || dump_event)) {
			rc = EINVAL;
			addbdump_help(stderr);
			goto done;
		} else if (ctl.adc_infile != NULL) {
			if (stype != NULL || dump_binary ||
			    ctl.adc_stpath != NULL || ctl.adc_dbpath != NULL) {
				rc = EINVAL;
				addbdump_help(stderr);
				goto done;
			}
		} else {
			ctl.adc_stype_nr = stype_parse(stype);
			if (ctl.adc_stype_nr == M0_STOB_TYPE_NR ||
			    ctl.adc_stpath == NULL ||
			    (ctl.adc_dbpath == NULL &&
			     ctl.adc_stype_nr == M0_AD_STOB)) {
				rc = EINVAL;
				addbdump_help(stderr);
				goto done;
			}
		}
	}
	if (rc != 0)
		goto done;

	if (dump_ctx)
		ctl.adc_dump_flags |= M0_ADDB_CURSOR_REC_CTX;
	if (dump_event)
		ctl.adc_dump_flags |= M0_ADDB_CURSOR_REC_EVENT;
	if (outfilepath != NULL) {
		ctl.adc_out = fopen(outfilepath, "w");
		if (ctl.adc_out == NULL) {
			rc = -errno;
			goto done;
		}
	} else {
		ctl.adc_out = stdout;
	}
	rc = setup(&ctl);
	if (rc != 0)
		goto done;
	ctl.adc_iter->asi_seq_set(ctl.adc_iter, min_seg_id);
	if (dump_binary)
		rc = bindump(&ctl);
	else
		rc = dump(&ctl);
	cleanup(&ctl);
done:
	if (ctl.adc_out != NULL && ctl.adc_out != stdout)
		fclose(ctl.adc_out);
	m0_fini();
	if (rc < 0) {
		rc = -rc;
		if (ctl.adc_errstr == NULL)
			ctl.adc_errstr = m0addbdump;
		fprintf(stderr, "%s: %s\n", ctl.adc_errstr, strerror(rc));
	}
	return rc;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
