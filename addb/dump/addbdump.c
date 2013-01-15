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

/*
 * Represents m0addbdump environment.
 */
struct addb_dump_ctl {
	/* Type of storage. */
	const char                  *adc_stype;
	/* ADDB Storage path */
	const char                  *adc_stpath;
	/* Database environment path. */
	const char                  *adc_dbpath;
	/* Binary data input file path */
	const char                  *adc_infile;
	/* Output file path */
	const char                  *adc_outfile;
	/* Dump binary data */
	bool                         adc_binarydump;

	/* a string to display with error message */
	const char                  *adc_errstr;
	int                          adc_stype_nr;
	struct m0_dbenv              adc_db;
	struct dump_stob             adc_stob;
	struct m0_addb_segment_iter *adc_iter;
};
static struct addb_dump_ctl ctl;

/* Returns valid stob type ID, or M0_STOB_TYPE_NR */
static int stype_parse(const char *stype)
{
	int i;

	if (stype == NULL)
		return M0_STOB_TYPE_NR;
	for (i = 0; i < ARRAY_SIZE(m0_cs_stypes); ++i) {
		if (strcasecmp(ctl.adc_stype, m0_cs_stypes[i]) == 0)
			break;
	}
	return i;
}

static int dump_linux_stob_init(const char *path, struct dump_stob *stob)
{
	int rc;

	rc = m0_stob_domain_locate(&m0_linux_stob_type, path, &stob->s_ldom) ?:
	    m0_linux_stob_setup(stob->s_ldom, false);
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

static void cleanup(void)
{
	if (ctl.adc_iter != NULL)
		m0_addb_segment_iter_free(ctl.adc_iter);
	dump_storage_fini(&ctl.adc_stob);
	if (ctl.adc_dbpath != NULL)
		m0_dbenv_fini(&ctl.adc_db);
}

static int setup(void)
{
	struct m0_dtx tx;
	int           rc;

	if (ctl.adc_dbpath != NULL) {
		rc = m0_dbenv_init(&ctl.adc_db, ctl.adc_dbpath, 0);
		if (rc != 0) {
			ctl.adc_errstr = ctl.adc_dbpath;
			return rc;
		}
	} else if (ctl.adc_infile != NULL) {
		rc = m0_addb_file_iter_alloc(&ctl.adc_iter, ctl.adc_infile);
		if (rc != 0)
			ctl.adc_errstr = ctl.adc_infile;
		return rc;
	}

	ctl.adc_stob.s_stype_nr = ctl.adc_stype_nr;
	m0_dtx_init(&tx);
	rc = dump_linux_stob_init(ctl.adc_stpath, &ctl.adc_stob);
	if (rc == 0 && ctl.adc_stype_nr == M0_AD_STOB)
		rc = dump_ad_stob_init(&ctl.adc_stob, M0_ADDB_STOB_ID_HI,
				       &tx, &ctl.adc_db);

	if (rc == 0) {
		rc = dump_stob_locate(ctl.adc_stype_nr == M0_LINUX_STOB ?
				      ctl.adc_stob.s_ldom :
				      ctl.adc_stob.s_adom->as_dom,
				      &tx, &m0_addb_stob_id,
				      &ctl.adc_stob.s_stob);
	}
	m0_dtx_done(&tx);
	if (rc != 0 && ctl.adc_dbpath != NULL)
		dump_storage_fini(&ctl.adc_stob);
	if (rc != 0)
		ctl.adc_errstr = ctl.adc_stpath;
	else
		rc = m0_addb_stob_iter_alloc(&ctl.adc_iter,
					     ctl.adc_stob.s_stob);
	return rc;
}

static void dump_cv(FILE *out, struct m0_addb_rec *r)
{
	struct m0_addb_uint64_seq *s;
	int i;
	int j;

	for (i = 0; i < r->ar_ctxids.acis_nr; ++i) {
		fprintf(out, " cv[%d]", i);
		s = &r->ar_ctxids.acis_data[i];
		for (j = 0; j < s->au64s_nr; ++j) {
			fprintf(out, "%c0x%lx",
				j == 0 ? ' ' : '/', s->au64s_data[j]);
		}
		fprintf(out, "\n");
	}
}

static void dump_ts(FILE *out, struct m0_addb_rec *r)
{
	fprintf(out, " TS:0x%lx", r->ar_ts);
}

static void dump_ctx_rec(FILE *out, struct m0_addb_rec *r)
{
	const struct m0_addb_ctx_type *ct;
	int                            i;

	ct = m0_addb_ctx_type_lookup(m0_addb_rec_rid_to_id(r->ar_rid));
	if (ct != NULL) {
		fprintf(out, "context %s", ct->act_name);
		if (ct->act_cf_nr != r->ar_data.au64s_nr) {
			fprintf(out, " !MISMATCH!");
			goto rawfields;
		}
		dump_ts(out, r);
		for (i = 0; i < r->ar_data.au64s_nr; ++i)
			fprintf(out, " %s=%lu",
				ct->act_cf[i], r->ar_data.au64s_data[i]);
	} else {
		fprintf(out, "unknown context %d",
			m0_addb_rec_rid_to_id(r->ar_rid));
	rawfields:
		dump_ts(out, r);
		for (i = 0; i < r->ar_data.au64s_nr; ++i)
			fprintf(out, " [%d]=%lu", i, r->ar_data.au64s_data[i]);
	}
	fprintf(out, "\n");
	dump_cv(out, r);
}

static void dump_event_rec(FILE *out, struct m0_addb_rec *r)
{
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
		dump_ts(out, r);
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

		dump_ts(out, r);
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
		dump_ts(out, r);
		for (i = 0; i < r->ar_data.au64s_nr; ++i)
			fprintf(out, " [%d]=%lu", i, r->ar_data.au64s_data[i]);
		break;
	}
	fprintf(out, "\n");
	dump_cv(out, r);
}

static int bindump(FILE *out)
{
	const struct m0_bufvec *bv;
	int                     i;
	int                     rc;

	while (1) {
		rc = ctl.adc_iter->asi_nextbuf(ctl.adc_iter, &bv);
		if (rc < 0)
			break;
		M0_ASSERT(bv != NULL);
		for (i = 0; i < bv->ov_vec.v_nr; ++i) {
			rc = fwrite(bv->ov_buf[i],
				    bv->ov_vec.v_count[i], 1, out);
			if (rc != 1) {
				rc = errno != 0 ? -errno : -EIO;
				goto fail;
			}
		}
	}
fail:
	if (rc == -ENODATA)
		rc = 0;
	return rc;
}

static int dump(uint32_t flags, FILE *out)
{
	struct m0_addb_rec   *rec;
	struct m0_addb_cursor cur;
	int                   count = 0;
	int                   rc;

	rc = m0_addb_cursor_init(&cur, ctl.adc_iter, flags);
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
		fprintf(out, "%5d:", count);
		if (m0_addb_rec_is_event(rec)) {
			dump_event_rec(out, rec);
		} else {
			M0_ASSERT(m0_addb_rec_is_ctx(rec));
			dump_ctx_rec(out, rec);
		}
		count++;
	}
	m0_addb_cursor_fini(&cur);
	fprintf(out, "dumped %d records\n", count);
	return rc;
}

static void addbdump_help(FILE *out)
{
	int i;

	M0_PRE(out != NULL);
	fprintf(out,
		"Usage: m0addbdump [-h]\n"
		"   or  m0addbdump [-b] -T StobType [-D DBPath] -A ADDBStobPath"
		" [-o path]\n"
		"   or  m0addbdump -f path [-o path]\n\n");
	fprintf(out,
		"  -b       Dump binary data.  All valid segments are dumped.\n"
		"           The output can be inspected later using -f.\n");
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
	static const char *m0addbdump = "m0addbdump";
	FILE              *out = NULL;
	int                rc = 0;
	int                r2;

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
					if (ctl.adc_stype != NULL)
						rc = -EINVAL;
					else
						ctl.adc_stype = str;
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
			M0_VOIDARG('b', "Dump binary data",
				LAMBDA(void, (void)
				{
					if (ctl.adc_binarydump)
						rc = -EINVAL;
					else
						ctl.adc_binarydump = true;
				})),
			M0_STRINGARG('f', "Binary data input file",
				LAMBDA(void, (const char *str)
				{
					if (ctl.adc_infile != NULL)
						rc = -EINVAL;
					else
						ctl.adc_infile = str;
				})),
			M0_STRINGARG('o', "Database environment path",
				LAMBDA(void, (const char *str)
				{
					if (ctl.adc_outfile != NULL)
						rc = -EINVAL;
					else
						ctl.adc_outfile = str;
				})),
			M0_VOIDARG('h', "usage help",
				LAMBDA(void, (void)
				{
					addbdump_help(stderr);
					rc = 1;
				})));
	if (rc == 0)
		rc = r2;
	if (rc == 0) {
		if (ctl.adc_infile != NULL) {
			if (ctl.adc_stype != NULL ||
			    ctl.adc_stpath != NULL ||
			    ctl.adc_dbpath != NULL ||
			    ctl.adc_binarydump) {
				rc = EINVAL;
				addbdump_help(stderr);
				goto done;
			}
		} else {
			ctl.adc_stype_nr = stype_parse(ctl.adc_stype);
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

	if (ctl.adc_outfile != NULL) {
		out = fopen(ctl.adc_outfile, "w");
		if (out == NULL) {
			rc = -errno;
			goto done;
		}
	}
	rc = setup();
	if (rc != 0)
		goto done;
	if (ctl.adc_binarydump)
		rc = bindump(out != NULL ? out : stdout);
	else
		rc = dump(0, out != NULL ? out : stdout);
	cleanup();
done:
	if (out != NULL)
		fclose(out);
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
