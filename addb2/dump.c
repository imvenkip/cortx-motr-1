/* -*- C -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <nikita.danilov@seagate.com>
 * Original creation date: 06-Mar-2015
 */


/**
 * @addtogroup addb2
 *
 * @{
 */

#include <err.h>
#include <sys/stat.h>
#include <stdio.h>
#include <sysexits.h>

#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/varr.h"

#include "rpc/item.h"                 /* m0_rpc_item_type_lookup */
#include "fop/fop.h"
#include "stob/domain.h"
#include "stob/stob.h"
#include "mero/init.h"
#include "module/instance.h"
#include "rpc/rpc_opcodes.h"           /* M0_OPCODES_NR */

#include "addb2/identifier.h"
#include "addb2/consumer.h"
#include "addb2/storage.h"
#include "addb2/counter.h"

#include "stob/addb2.h"
#include "ioservice/io_addb2.h"
#include "m0t1fs/linux_kernel/m0t1fs_addb2.h"

enum { BUF_SIZE = 256 };

struct id_intrp {
	uint64_t             ii_id;
	const char          *ii_name;
	void               (*ii_print[15])(const uint64_t *v, char *buf);
};

static struct m0_varr value_id;

static void             id_init  (void);
static void             id_fini  (void);
static void             id_set   (struct id_intrp *intrp);
static void             id_set_nr(struct id_intrp *intrp, int nr);
static struct id_intrp *id_get   (uint64_t id);

static void rec_dump(const struct m0_addb2_record *rec);
static void val_dump(const char *prefix,
		     const struct m0_addb2_value *val, int indent);

#define DOM "./_addb2-dump"

int main(int argc, char **argv)
{
	struct m0_stob_domain  *dom;
	struct m0_stob         *stob;
	const char             *fname = argv[1];
	struct m0_addb2_sit    *sit;
	struct stat             buf;
	struct m0_addb2_record *rec;
	struct m0               instance = {0};
	int                     result;

	result = m0_init(&instance);
	if (result != 0)
		err(EX_CONFIG, "Cannot initialise mero: %d", result);
	result = m0_stob_domain_init("linuxstob:"DOM, "directio=true", &dom);
	if (result == 0)
		m0_stob_domain_destroy(dom);
	else if (result != -ENOENT)
		err(EX_CONFIG, "Cannot destroy domain: %d", result);
	result = m0_stob_domain_create_or_init("linuxstob:"DOM, "directio=true",
					       /* domain key, not important */
					       8, NULL, &dom);
	if (result != 0)
		err(EX_CANTCREAT, "Cannot create domain: %d", result);
	result = m0_stob_find_by_key(dom, 1 /* stob key, any */, &stob);
	if (result != 0)
		err(EX_CANTCREAT, "Cannot find stob: %d", result);
	result = m0_stob_locate(stob);
	if (result != 0)
		err(EX_CANTCREAT, "Cannot locate stob: %d", result);
	result = m0_stob_create(stob, NULL, fname);
	if (result != 0)
		err(EX_NOINPUT, "Cannot create stob: %d", result);
	M0_ASSERT(m0_stob_state_get(stob) == CSS_EXISTS);
	result = stat(fname, &buf);
	if (result != 0)
		err(EX_NOINPUT, "Cannot stat: %d", result);

	/** @todo XXX size parameter copied from m0_reqh_addb2_init(). */
	result = m0_addb2_sit_init(&sit, stob, 128ULL << 30, NULL);
	if (result != 0)
		err(EX_DATAERR, "Cannot initialise iterator: %d", result);
	id_init();
	while ((result = m0_addb2_sit_next(sit, &rec)) > 0)
		rec_dump(rec);
	id_fini();
	if (result != 0)
		err(EX_DATAERR, "Iterator error: %d", result);
	m0_addb2_sit_fini(sit);
	m0_stob_destroy(stob, NULL);
	m0_stob_domain_destroy(dom);
	m0_fini();
	return EX_OK;
}

static void dec(const uint64_t *v, char *buf)
{
	sprintf(buf, "%"PRId64, *v);
}

static void hex(const uint64_t *v, char *buf)
{
	sprintf(buf, "%"PRIx64, *v);
}

static void ptr(const uint64_t *v, char *buf)
{
	sprintf(buf, "@%p", *(void **)v);
}

static void bol(const uint64_t *v, char *buf)
{
	sprintf(buf, "%s", *v ? "true" : "false");
}

static void fid(const uint64_t *v, char *buf)
{
	sprintf(buf, FID_F, FID_P((struct m0_fid *)v));
}

static void skip(const uint64_t *v, char *buf)
{
	buf[0] = 0;
}

static void _clock(const uint64_t *v, char *buf)
{
	m0_time_t stamp = *v;
	time_t    ts    = m0_time_seconds(stamp);
	struct tm tm;

	localtime_r(&ts, &tm);
	sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d.%09lu",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
		tm.tm_min, tm.tm_sec, m0_time_nanoseconds(stamp));
}

static void fom_type(const uint64_t *v, char *buf)
{
	extern struct m0_fom_type *m0_fom__types[M0_OPCODES_NR];
	const struct m0_fom_type *ftype = m0_fom__types[*v];

	if (*v < ARRAY_SIZE(m0_fom__types))
		sprintf(buf, "%s", ftype->ft_conf->scf_name);
	else
		sprintf(buf, "?%i", (int)*v);
}

static void fom_state(const uint64_t *v, char *buf)
{
	extern struct m0_sm_conf fom_states_conf;
	struct m0_sm_trans_descr *d = &fom_states_conf.scf_trans[*v];

	sprintf(buf, "%s -[%s]-> %s",
		fom_states_conf.scf_state[d->td_src].sd_name,
		d->td_cause,
		fom_states_conf.scf_state[d->td_tgt].sd_name);
}

static void fom_phase(const uint64_t *v, char *buf)
{
	extern struct m0_sm_conf m0_generic_conf;
	struct m0_sm_trans_descr *d = &m0_generic_conf.scf_trans[*v];

	if (*v < m0_generic_conf.scf_trans_nr) {
		sprintf(buf, "%s -[%s]-> %s",
			m0_generic_conf.scf_state[d->td_src].sd_name,
			d->td_cause,
			m0_generic_conf.scf_state[d->td_tgt].sd_name);
	} else
		sprintf(buf, "phase transition %i", (int)*v);
}

static void rpcop(const uint64_t *v, char *buf)
{
	struct m0_rpc_item_type *it = m0_rpc_item_type_lookup(*v);

	if (it != NULL) {
		struct m0_fop_type *ft = M0_AMB(ft, it, ft_rpc_item_type);
		sprintf(buf, "%s", ft->ft_name);
	} else
		sprintf(buf, "?rpc: %"PRId64, *v);
}

static void counter(const uint64_t *v, char *buf)
{
	struct m0_addb2_counter_data *d = (void *)v;
	double avg;
	double dev;

	avg = d->cod_nr > 0 ? ((double)d->cod_sum) / d->cod_nr : 0;
	dev = d->cod_nr > 1 ? ((double)d->cod_ssq) / d->cod_nr - avg * avg : 0;

	sprintf(buf, "nr: %"PRId64" min: %"PRId64" max: %"PRId64
		" avg: %f dev: %f", d->cod_nr, d->cod_min, d->cod_max,
		avg, dev);
}

#define COUNTER &counter, &skip, &skip, &skip, &skip
#define FID &fid, &skip

struct id_intrp ids[] = {
	{ M0_AVI_NULL,            "null" },
	{ M0_AVI_NODE,            "node",            { FID } },
	{ M0_AVI_LOCALITY,        "locality",        { &dec } },
	{ M0_AVI_THREAD,          "thread",          { &hex, &hex } },
	{ M0_AVI_SERVICE,         "service",         { FID } },
	{ M0_AVI_FOM,             "fom",             { &ptr, &dec, &dec } },
	{ M0_AVI_CLOCK,           "clock",           { &_clock } },
	{ M0_AVI_PHASE,           "fom-phase",       { &fom_phase, &_clock } },
	{ M0_AVI_STATE,           "fom-state",       { &fom_state, &_clock } },
	{ M0_AVI_ALLOC,           "alloc",           { &dec, &ptr } },
	{ M0_AVI_FOM_DESCR,       "fom-descr",       { &_clock, &fom_type,
						       FID, &hex, &rpcop,
						       &rpcop, &bol }  },
	{ M0_AVI_FOM_ACTIVE,      "fom-active",      { COUNTER } },
	{ M0_AVI_RUNQ,            "runq",            { COUNTER } },
	{ M0_AVI_WAIL,            "wail",            { COUNTER } },
	{ M0_AVI_AST,             "ast" },
	{ M0_AVI_FOM_CB,          "fom-cb" },
	{ M0_AVI_IOS_IO_DESCR,    "ios-io-descr",    { FID, FID,
						       &hex, &hex, &dec, &dec,
						       &dec, &dec, &dec } },
	{ M0_AVI_FS_OPEN,         "m0t1fs-open",     { FID, &hex } },
	{ M0_AVI_FS_LOOKUP,       "m0t1fs-lookup",   { FID } },
	{ M0_AVI_FS_CREATE,       "m0t1fs-create",   { FID, &hex, &dec } },
	{ M0_AVI_FS_READ,         "m0t1fs-read",     { FID } },
	{ M0_AVI_FS_WRITE,        "m0t1fs-write",    { FID } },
	{ M0_AVI_FS_IO_DESCR,     "m0t1fs-io-descr", { &dec, &dec } },
	{ M0_AVI_STOB_IO_LAUNCH,  "stob-io-launch",  { &_clock, &fid, &dec,
						       &dec, &dec, &dec,
						       &dec } },
	{ M0_AVI_STOB_IO_END,     "stob-io-end",     { &_clock, &fid, &dec,
						       &dec, &dec } },
	{ M0_AVI_STOB_IOQ,        "stob-ioq-thread", { &dec } },
	{ M0_AVI_STOB_IOQ_INFLIGHT, "stob-ioq-inflight", { COUNTER } },
	{ M0_AVI_STOB_IOQ_QUEUED, "stob-ioq-queued", { COUNTER } },
	{ M0_AVI_STOB_IOQ_GOT,    "stob-ioq-got",    { COUNTER } },

	{ M0_AVI_NODATA,          "nodata" },
};

static void id_init(void)
{
	int result;

	result = m0_varr_init(&value_id, M0_AVI_LAST, sizeof(char *), 4096);
	if (result != 0)
		err(EX_CONFIG, "Cannot initialise array: %d", result);
	id_set_nr(ids, ARRAY_SIZE(ids));
}

static void id_fini(void)
{
	m0_varr_fini(&value_id);
}

static void id_set(struct id_intrp *intrp)
{
	struct id_intrp **addr;

	if (intrp->ii_id < m0_varr_size(&value_id)) {
		addr = m0_varr_ele_get(&value_id, intrp->ii_id);
		if (addr != NULL)
			*addr = intrp;
	}
}

static void id_set_nr(struct id_intrp *intrp, int nr)
{
	while (nr-- > 0)
		id_set(&intrp[nr]);
}

static struct id_intrp *id_get(uint64_t id)
{
	struct id_intrp **addr;
	struct id_intrp  *intr = NULL;

	if (id < m0_varr_size(&value_id)) {
		addr = m0_varr_ele_get(&value_id, id);
		if (addr != NULL)
			intr = *addr;
	}
	return intr;
}

#define U64 "%16"PRIx64

static void rec_dump(const struct m0_addb2_record *rec)
{
	int i;

	val_dump("* ", &rec->ar_val, 0);
	for (i = 0; i < rec->ar_label_nr; ++i)
		val_dump("| ",&rec->ar_label[i], 8);
}

static int pad(int indent)
{
	return indent > 0 ? printf("%*.*s", indent, indent,
		   "                                                    ") : 0;
}

static void val_dump(const char *prefix,
		     const struct m0_addb2_value *val, int indent)
{
	struct id_intrp  *intrp = id_get(val->va_id);
	int               i;
	char              buf[BUF_SIZE];
	enum { WIDTH = 12 };

	printf(prefix);
	pad(indent);
	if (intrp != NULL)
		printf("%-16s ", intrp->ii_name);
	else
		printf(U64" ", val->va_id);
	for (i = 0, indent = 0; i < val->va_nr; ++i) {
		if (intrp == NULL)
			sprintf(buf, U64, val->va_data[i]);
		else if (intrp->ii_print[i] == NULL)
			sprintf(buf, "?"U64"?", val->va_data[i]);
		else {
			if (intrp->ii_print[i] == &skip)
				continue;
			intrp->ii_print[i](&val->va_data[i], buf);
		}
		if (i > 0)
			indent += printf(", ");
		indent += pad(WIDTH * i - indent);
		indent += printf("%s", buf);
	}
	printf("\n");
}

/** @} end of addb2 group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
