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

#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/tlist.h"
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

enum {
	BUF_SIZE  = 256,
	FIELD_MAX = 15
};
struct context;

struct id_intrp {
	uint64_t     ii_id;
	const char  *ii_name;
	void       (*ii_print[FIELD_MAX])(struct context *ctx,
					  const uint64_t *v, char *buf);
	const char  *ii_field[FIELD_MAX];
	void       (*ii_spec)(struct context *ctx, char *buf);
	int          ii_repeat;
};

struct fom {
	struct m0_tlink           fo_linkage;
	uint64_t                  fo_addr;
	const struct m0_fom_type *fo_type;
	m0_time_t                 fo_state_clock;
	m0_time_t                 fo_phase_clock;
	uint64_t                  fo_magix;
};

struct context {
	struct fom                    c_fom;
	m0_time_t                     c_clock;
	const struct m0_addb2_record *c_rec;
	const struct m0_addb2_value  *c_val;
};

static struct m0_varr value_id;

static void             id_init  (void);
static void             id_fini  (void);
static void             id_set   (struct id_intrp *intrp);
static void             id_set_nr(struct id_intrp *intrp, int nr);
static struct id_intrp *id_get   (uint64_t id);

static void rec_dump(struct context *ctx, const struct m0_addb2_record *rec);
static void val_dump(struct context *ctx, const char *prefix,
		     const struct m0_addb2_value *val, int indent);
static void context_fill(struct context *ctx, const struct m0_addb2_value *val);
static void file_dump(struct m0_stob_domain *dom, const char *fname);

#define DOM "./_addb2-dump"

int main(int argc, char **argv)
{
	struct m0_stob_domain  *dom;
	struct m0               instance = {0};
	int                     result;
	int                     i;

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
	id_init();
	for (i = 1; i < argc; ++i)
		file_dump(dom, argv[i]);
	id_fini();
	m0_stob_domain_destroy(dom);
	m0_fini();
	return EX_OK;
}

static void file_dump(struct m0_stob_domain *dom, const char *fname)
{
	struct m0_stob         *stob;
	struct m0_addb2_sit    *sit;
	struct stat             buf;
	struct m0_addb2_record *rec;
	int                     result;

	result = m0_stob_find_by_key(dom, 1 /* stob key, any */, &stob);
	if (result != 0)
		err(EX_CANTCREAT, "Cannot find stob: %d", result);
	if (m0_stob_state_get(stob) == CSS_UNKNOWN) {
		result = m0_stob_locate(stob);
		if (result != 0)
			err(EX_CANTCREAT, "Cannot locate stob: %d", result);
	}
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
	while ((result = m0_addb2_sit_next(sit, &rec)) > 0)
		rec_dump(&(struct context){}, rec);
	if (result != 0)
		err(EX_DATAERR, "Iterator error: %d", result);
	m0_addb2_sit_fini(sit);
	m0_stob_destroy(stob, NULL);
}

static void dec(struct context *ctx, const uint64_t *v, char *buf)
{
	sprintf(buf, "%"PRId64, v[0]);
}

static void hex(struct context *ctx, const uint64_t *v, char *buf)
{
	sprintf(buf, "%"PRIx64, v[0]);
}

static void oct(struct context *ctx, const uint64_t *v, char *buf)
{
	sprintf(buf, "%"PRIo64, v[0]);
}

static void ptr(struct context *ctx, const uint64_t *v, char *buf)
{
	sprintf(buf, "@%p", *(void **)v);
}

static void bol(struct context *ctx, const uint64_t *v, char *buf)
{
	sprintf(buf, "%s", v[0] ? "true" : "false");
}

static void fid(struct context *ctx, const uint64_t *v, char *buf)
{
	sprintf(buf, FID_F, FID_P((struct m0_fid *)v));
}

static void skip(struct context *ctx, const uint64_t *v, char *buf)
{
	buf[0] = 0;
}

static void _clock(struct context *ctx, const uint64_t *v, char *buf)
{
	m0_time_t stamp = v[0];
	time_t    ts    = m0_time_seconds(stamp);
	struct tm tm;

	localtime_r(&ts, &tm);
	sprintf(buf, "%04d-%02d-%02d-%02d:%02d:%02d.%09lu",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
		tm.tm_min, tm.tm_sec, m0_time_nanoseconds(stamp));
	ctx->c_clock = stamp;
}

static void fom_type(struct context *ctx, const uint64_t *v, char *buf)
{
	const struct m0_fom_type *ftype = ctx->c_fom.fo_type;
	const struct m0_sm_conf  *conf  = ftype->ft_conf;

	M0_ASSERT(v[2] < conf->scf_nr_states);
	sprintf(buf, "'%s' transitions: %"PRId64" phase: %s",
		conf->scf_name, v[1], conf->scf_state[v[2]].sd_name);
}

extern struct m0_sm_conf fom_states_conf;

static void fom_state(struct context *ctx, const uint64_t *v, char *buf)
{
	struct m0_sm_trans_descr *d = &fom_states_conf.scf_trans[v[0]];
	/*
	 * v[0] - transition id
	 * v[1] - state id
	 * v[2] - time stamp
	 */
	M0_ASSERT(d->td_tgt == v[1]);
	sprintf(buf, "%s -[%s]-> %s",
		fom_states_conf.scf_state[d->td_src].sd_name,
		d->td_cause,
		fom_states_conf.scf_state[d->td_tgt].sd_name);
	ctx->c_fom.fo_state_clock = v[2];
}

static void fom_phase(struct context *ctx, const uint64_t *v, char *buf)
{
	const struct m0_sm_conf        *conf;
	const struct m0_sm_trans_descr *d;
	const struct m0_sm_state_descr *s;
	/*
	 * v[0] - transition id
	 * v[1] - state id
	 * v[2] - time stamp
	 */
	if (ctx->c_fom.fo_type != NULL) {
		conf = ctx->c_fom.fo_type->ft_conf;
		if (conf->scf_trans == NULL) {
			M0_ASSERT(v[0] == 0);
			s = &conf->scf_state[v[1]];
			sprintf(buf, " --> %s", s->sd_name);
		} else if (v[0] < conf->scf_trans_nr) {
			d = &conf->scf_trans[v[0]];
			s = &conf->scf_state[d->td_tgt];
			sprintf(buf, "%s -[%s]-> %s",
				conf->scf_state[d->td_src].sd_name,
				d->td_cause, s->sd_name);
		} else
			sprintf(buf, "phase transition %i", (int)v[0]);
		ctx->c_fom.fo_phase_clock = v[2];
	} else
		sprintf(buf, "phase ast transition %i", (int)v[0]);
}

static void rpcop(struct context *ctx, const uint64_t *v, char *buf)
{
	struct m0_rpc_item_type *it = m0_rpc_item_type_lookup(v[0]);

	if (it != NULL) {
		struct m0_fop_type *ft = M0_AMB(ft, it, ft_rpc_item_type);
		sprintf(buf, "%s", ft->ft_name);
	} else
		sprintf(buf, "?rpc: %"PRId64, v[0]);
}

static void counter(struct context *ctx, const uint64_t *v, char *buf)
{
	struct m0_addb2_counter_data *d = (void *)&v[1];
	double avg;
	double dev;

	avg = d->cod_nr > 0 ? ((double)d->cod_sum) / d->cod_nr : 0;
	dev = d->cod_nr > 1 ? ((double)d->cod_ssq) / d->cod_nr - avg * avg : 0;

	_clock(ctx, v, buf);
	sprintf(buf + strlen(buf), " nr: %"PRId64" min: %"PRId64" max: %"PRId64
		" avg: %f dev: %f", d->cod_nr, d->cod_min, d->cod_max,
		avg, dev);
}

static void sm_trans(const struct m0_sm_conf *conf, const char *name,
		     struct context *ctx, char *buf)
{
	int nob;
	int idx = ctx->c_val->va_id - conf->scf_addb2_counter;
	const struct m0_sm_trans_descr *trans = &conf->scf_trans[idx];

	M0_PRE(conf->scf_addb2_key > 0);
	M0_PRE(0 <= idx && idx < 100);

	nob = sprintf(buf, "%s/%s: %s -[%s]-> %s ", name,
		      conf->scf_name, conf->scf_state[trans->td_src].sd_name,
		      trans->td_cause, conf->scf_state[trans->td_tgt].sd_name);
	counter(ctx, &ctx->c_val->va_data[0], buf + nob);
}

static void fom_state_counter(struct context *ctx, char *buf)
{
	sm_trans(&fom_states_conf, "", ctx, buf);
}

extern struct m0_fop_type m0_fop_cob_readv_fopt;
static void io_read_phase_counter(struct context *ctx, char *buf)
{
	sm_trans(m0_fop_cob_readv_fopt.ft_fom_type.ft_conf, "read", ctx, buf);
}

extern struct m0_fop_type m0_fop_cob_writev_fopt;
static void io_write_phase_counter(struct context *ctx, char *buf)
{
	sm_trans(m0_fop_cob_writev_fopt.ft_fom_type.ft_conf, "write", ctx, buf);
}

#define COUNTER  &counter, &skip, &skip, &skip, &skip, &skip
#define FID &fid, &skip

struct id_intrp ids[] = {
	{ M0_AVI_NULL,            "null" },
	{ M0_AVI_NODE,            "node",            { FID } },
	{ M0_AVI_LOCALITY,        "locality",        { &dec } },
	{ M0_AVI_THREAD,          "thread",          { &hex, &hex } },
	{ M0_AVI_SERVICE,         "service",         { FID } },
	{ M0_AVI_FOM,             "fom",             { &ptr, &fom_type,
						       &skip, &skip } },
	{ M0_AVI_CLOCK,           "clock",           { &_clock } },
	{ M0_AVI_PHASE,           "fom-phase",       { &fom_phase, &skip,
						       &_clock } },
	{ M0_AVI_STATE,           "fom-state",       { &fom_state, &skip,
						       &_clock } },
	{ M0_AVI_STATE_COUNTER,   "",
	  .ii_repeat = M0_AVI_STATE_COUNTER_END - M0_AVI_STATE_COUNTER,
	  .ii_spec   = &fom_state_counter },
	{ M0_AVI_ALLOC,           "alloc",           { &dec, &ptr },
	  { "size", "addr" } },
	{ M0_AVI_FOM_DESCR,       "fom-descr",       { &_clock, FID,
						       &hex, &rpcop,
						       &rpcop, &bol },
	  { NULL, "service", NULL, "sender",
	    "req-opcode", "rep-opcode", "local" } },
	{ M0_AVI_FOM_ACTIVE,      "fom-active",      { COUNTER } },
	{ M0_AVI_RUNQ,            "runq",            { COUNTER } },
	{ M0_AVI_WAIL,            "wail",            { COUNTER } },
	{ M0_AVI_AST,             "ast" },
	{ M0_AVI_FOM_CB,          "fom-cb" },
	{ M0_AVI_IOS_IO_DESCR,    "ios-io-descr",    { FID, FID,
						       &hex, &hex, &dec, &dec,
						       &dec, &dec, &dec },
	  { "file", NULL, "cob", NULL, "read-v", "write-v",
	    "seg-nr", "count", "offset", "descr-nr", "colour" }},
	{ M0_AVI_IOS_READ_COUNTER,   "",
	  .ii_repeat = M0_AVI_IOS_READ_COUNTER_END - M0_AVI_IOS_READ_COUNTER,
	  .ii_spec   = &io_read_phase_counter },
	{ M0_AVI_IOS_WRITE_COUNTER,   "",
	  .ii_repeat = M0_AVI_IOS_WRITE_COUNTER_END - M0_AVI_IOS_WRITE_COUNTER,
	  .ii_spec   = &io_write_phase_counter },
	{ M0_AVI_FS_OPEN,         "m0t1fs-open",     { FID, &oct },
	  { NULL, NULL, "flags" }},
	{ M0_AVI_FS_LOOKUP,       "m0t1fs-lookup",   { FID } },
	{ M0_AVI_FS_CREATE,       "m0t1fs-create",   { FID, &oct, &dec },
	  { NULL, NULL, "mode", "rc" } },
	{ M0_AVI_FS_READ,         "m0t1fs-read",     { FID } },
	{ M0_AVI_FS_WRITE,        "m0t1fs-write",    { FID } },
	{ M0_AVI_FS_IO_DESCR,     "m0t1fs-io-descr", { &dec, &dec },
	  { "offset", "rc" }},
	{ M0_AVI_STOB_IO_LAUNCH,  "stob-io-launch",  { &_clock, FID, &dec,
						       &dec, &dec, &dec,
						       &dec },
	  { NULL, NULL, NULL, "count", "bvec-nr", "ivec-nr", "offset" } },
	{ M0_AVI_STOB_IO_END,     "stob-io-end",     { &_clock, FID, &dec,
						       &dec, &dec },
	  { NULL, NULL, NULL, "rc", "count", "frag" } },
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
		M0_ASSERT(addr != NULL);
		M0_ASSERT(*addr == NULL);
		*addr = intrp;
	}
}

static void id_set_nr(struct id_intrp *batch, int nr)
{
	while (nr-- > 0) {
		struct id_intrp *intrp = &batch[nr];
		int              i;

		id_set(intrp);
		for (i = 0; i < intrp->ii_repeat; ++i) {
			++(intrp->ii_id);
			id_set(intrp);
		}
	}
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

static void rec_dump(struct context *ctx, const struct m0_addb2_record *rec)
{
	int i;

	ctx->c_rec = rec;
	context_fill(ctx, &rec->ar_val);
	for (i = 0; i < rec->ar_label_nr; ++i)
		context_fill(ctx, &rec->ar_label[i]);
	val_dump(ctx, "* ", &rec->ar_val, 0);
	for (i = 0; i < rec->ar_label_nr; ++i)
		val_dump(ctx, "| ", &rec->ar_label[i], 8);
}

static int pad(int indent)
{
	return indent > 0 ? printf("%*.*s", indent, indent,
		   "                                                    ") : 0;
}

static void val_dump(struct context *ctx, const char *prefix,
		     const struct m0_addb2_value *val, int indent)
{
	struct id_intrp  *intrp = id_get(val->va_id);
	int               i;
	char              buf[BUF_SIZE];
	enum { WIDTH = 12 };

#define BEND (buf + strlen(buf))

	ctx->c_val = val;
	printf(prefix);
	pad(indent);
	if (intrp != NULL && intrp->ii_spec != NULL) {
		intrp->ii_spec(ctx, buf);
		printf("%s\n", buf);
		return;
	}
	if (intrp != NULL)
		printf("%-16s ", intrp->ii_name);
	else
		printf(U64" ", val->va_id);
	for (i = 0, indent = 0; i < val->va_nr; ++i) {
		buf[0] = 0;
		if (intrp == NULL)
			sprintf(buf, "?"U64"?", val->va_data[i]);
		else {
			if (intrp->ii_field[i] != NULL)
				sprintf(buf, "%s: ", intrp->ii_field[i]);
			if (intrp->ii_print[i] == NULL)
				sprintf(BEND, "?"U64"?", val->va_data[i]);
			else {
				if (intrp->ii_print[i] == &skip)
					continue;
				intrp->ii_print[i](ctx, &val->va_data[i], BEND);
			}
		}
		if (i > 0)
			indent += printf(", ");
		indent += pad(WIDTH * i - indent);
		indent += printf("%s", buf);
	}
	printf("\n");
#undef BEND
}

extern struct m0_fom_type *m0_fom__types[M0_OPCODES_NR];
static void context_fill(struct context *ctx, const struct m0_addb2_value *val)
{
	switch (val->va_id) {
	case M0_AVI_FOM:
		ctx->c_fom.fo_addr = val->va_data[0];
		M0_ASSERT(val->va_data[1] < ARRAY_SIZE(m0_fom__types));
		ctx->c_fom.fo_type = m0_fom__types[val->va_data[1]];
		break;
	}
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
