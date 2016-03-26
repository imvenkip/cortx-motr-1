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

#include <dlfcn.h>
#include <err.h>
#include <sys/stat.h>
#include <stdio.h>
#include <sysexits.h>
#include <execinfo.h>
#include <signal.h>
#include <bfd.h>
#include <stdlib.h>                    /* qsort */
#include <unistd.h>                    /* sleep */

#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/tlist.h"
#include "lib/varr.h"
#include "lib/getopts.h"
#include "lib/uuid.h"                  /* m0_node_uuid_string_set */

#include "rpc/item.h"                  /* m0_rpc_item_type_lookup */
#include "fop/fop.h"
#include "stob/domain.h"
#include "stob/stob.h"
#include "mero/init.h"
#include "module/instance.h"
#include "rpc/rpc_opcodes.h"           /* M0_OPCODES_NR */
#include "rpc/addb2.h"
#include "be/addb2.h"

#include "addb2/identifier.h"
#include "addb2/consumer.h"
#include "addb2/storage.h"
#include "addb2/counter.h"

#include "stob/addb2.h"
#include "ioservice/io_addb2.h"
#include "m0t1fs/linux_kernel/m0t1fs_addb2.h"
#include "sns/cm/cm.h"                 /* m0_sns_cm_repair_trigger_fop_init */

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
	uint64_t                  fo_tid;
	const struct m0_fom_type *fo_type;
	uint64_t                  fo_magix;
};

struct context {
	struct fom                    c_fom;
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
		     const struct m0_addb2_value *val, int indent, bool cr);
static void context_fill(struct context *ctx, const struct m0_addb2_value *val);
static void file_dump(struct m0_stob_domain *dom, const char *fname);

static void libbfd_init(const char *libpath);
static void libbfd_fini(void);
static void libbfd_resolve(uint64_t delta, char *buf);

static void flate(void);
static void deflate(void);

static void misc_init(void);
static void misc_fini(void);

#define DOM "./_addb2-dump"
extern int  optind;
static bool flatten = false;
static bool deflatten = false;
static m0_bindex_t offset = 0;
static int delay = 0;

int main(int argc, char **argv)
{
	struct m0_stob_domain  *dom;
	struct m0               instance = {0};
	int                     result;
	int                     i;

	m0_node_uuid_string_set(NULL);
	result = m0_init(&instance);
	if (result != 0)
		err(EX_CONFIG, "Cannot initialise mero: %d", result);

	misc_init();

	result = M0_GETOPTS("m0addb2dump", argc, argv,
			M0_FORMATARG('o', "Starting offset",
				     "%"SCNx64, &offset),
			M0_FORMATARG('c', "Continuous dump interval (sec.)",
				     "%i", &delay),
			M0_STRINGARG('l', "Mero library path",
				     LAMBDA(void, (const char *path) {
						     libbfd_init(path);
					     })),
			M0_FLAGARG('f', "Flatten output", &flatten),
			M0_FLAGARG('d', "De-flatten input", &deflatten));
	if (result != 0)
		err(EX_USAGE, "Wrong option: %d", result);
	if (deflatten) {
		if (flatten || optind < argc)
			err(EX_USAGE, "De-flattening is exclusive.");
		deflate();
		return EX_OK;
	}
	if (flatten && optind == argc) {
		flate();
		return EX_OK;
	}
	if ((delay != 0 || offset != 0) && optind + 1 < argc)
		err(EX_USAGE,
		    "Staring offset and continuous dump imply single file.");
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
	for (i = optind; i < argc; ++i)
		file_dump(dom, argv[i]);
	id_fini();
	m0_stob_domain_destroy(dom);
	libbfd_fini();
	misc_fini();
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
	struct m0_stob_id       stob_id;

	m0_stob_id_make(0, 1 /* stob key, any */, &dom->sd_id, &stob_id);
	result = m0_stob_find(&stob_id, &stob);
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
	do {
		result = m0_addb2_sit_init(&sit, stob, offset);
		if (delay > 0 && result == -EPROTO) {
			printf("Sleeping for %i seconds (%lx).\n",
			       delay, offset);
			sleep(delay);
			continue;
		}
		if (result != 0)
			err(EX_DATAERR, "Cannot initialise iterator: %d",
			    result);
		while ((result = m0_addb2_sit_next(sit, &rec)) > 0) {
			rec_dump(&(struct context){}, rec);
			if (rec->ar_val.va_id == M0_AVI_SIT)
				offset = rec->ar_val.va_data[3];
		}
		if (result != 0)
			err(EX_DATAERR, "Iterator error: %d", result);
		m0_addb2_sit_fini(sit);
	} while (delay > 0);
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
}

static void duration(struct context *ctx, const uint64_t *v, char *buf)
{
	m0_time_t elapsed = v[0];

	sprintf(buf, "%"PRId64".%09"PRId64,
		m0_time_seconds(elapsed), m0_time_nanoseconds(elapsed));
}

static void fom_type(struct context *ctx, const uint64_t *v, char *buf)
{
	const struct m0_fom_type *ftype = ctx->c_fom.fo_type;
	const struct m0_sm_conf  *conf  = &ftype->ft_conf;

	if (ftype != NULL) {
		M0_ASSERT(v[2] < conf->scf_nr_states);
		sprintf(buf, "'%s' transitions: %"PRId64" phase: %s",
			conf->scf_name, v[1], conf->scf_state[v[2]].sd_name);
	} else {
		sprintf(buf, "?'UNKNOWN-%"PRId64"' transitions: %"PRId64
			" phase: %"PRId64,
			ctx->c_fom.fo_tid, v[1], v[2]);
	}
}

static void sm_state(const struct m0_sm_conf *conf,
		     struct context *ctx, const uint64_t *v, char *buf)
{
	struct m0_sm_trans_descr *d = &conf->scf_trans[v[0]];
	/*
	 * v[0] - transition id
	 * v[1] - state id
	 * v[2] - time stamp
	 */
	if (conf->scf_trans == NULL) {
		M0_ASSERT(v[0] == 0);
		sprintf(buf, " --> %s", conf->scf_state[v[1]].sd_name);
	} else {
		M0_ASSERT(d->td_tgt == v[1]);
		sprintf(buf, "%s -[%s]-> %s",
			conf->scf_state[d->td_src].sd_name,
			d->td_cause, conf->scf_state[d->td_tgt].sd_name);
	}
}

extern struct m0_sm_conf fom_states_conf;
static void fom_state(struct context *ctx, const uint64_t *v, char *buf)
{
	sm_state(&fom_states_conf, ctx, v, buf);
}

static void fom_phase(struct context *ctx, const uint64_t *v, char *buf)
{
	const struct m0_sm_conf        *conf;
	const struct m0_sm_trans_descr *d;
	const struct m0_sm_state_descr *s;
	/*
	 * v[0] - transition id
	 * v[1] - state id
	 */
	if (ctx->c_fom.fo_type != NULL) {
		conf = &ctx->c_fom.fo_type->ft_conf;
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
	} else
		sprintf(buf, "phase ast transition %i", (int)v[0]);
}

static void rpcop(struct context *ctx, const uint64_t *v, char *buf)
{
	struct m0_rpc_item_type *it = m0_rpc_item_type_lookup(v[0]);

	if (it != NULL) {
		struct m0_fop_type *ft = M0_AMB(ft, it, ft_rpc_item_type);
		sprintf(buf, "%s", ft->ft_name);
	} else if (v[0] == 0)
		sprintf(buf, "none");
	else
		sprintf(buf, "?rpc: %"PRId64, v[0]);
}

static void sym(struct context *ctx, const uint64_t *v, char *buf)
{
	const void *addr = m0_ptr_unwrap(v[0]);

	if (v[0] != 0) {
		int     rc;
		Dl_info info;

		libbfd_resolve(v[0], buf);
		buf += strlen(buf);
		rc = dladdr(addr, &info); /* returns non-zero on *success* */
		if (rc != 0 && info.dli_sname != NULL) {
			sprintf(buf, " [%s+%lx]", info.dli_sname,
				addr - info.dli_saddr);
			buf += strlen(buf);
		}
		sprintf(buf, " @%p/%"PRIx64, addr, v[0]);
	}
}

static void counter(struct context *ctx, const uint64_t *v, char *buf)
{
	struct m0_addb2_counter_data *d = (void *)&v[0];
	double avg;
	double dev;

	avg = d->cod_nr > 0 ? ((double)d->cod_sum) / d->cod_nr : 0;
	dev = d->cod_nr > 1 ? ((double)d->cod_ssq) / d->cod_nr - avg * avg : 0;

	sprintf(buf + strlen(buf), " nr: %"PRId64" min: %"PRId64" max: %"PRId64
		" avg: %f dev: %f datum: %"PRIx64" ",
		d->cod_nr, d->cod_min, d->cod_max, avg, dev, d->cod_datum);
	sym(ctx, &d->cod_datum, buf + strlen(buf));
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

static void fop_counter(struct context *ctx, char *buf)
{
	uint64_t mask = ctx->c_val->va_id - M0_AVI_FOP_TYPES_RANGE_START;
	struct m0_fop_type *fopt = m0_fop_type_find(mask >> 12);
	const struct m0_sm_conf *conf;

	M0_ASSERT_INFO(fopt != NULL, "mask: %"PRIx64, mask);
	switch ((mask >> 8) & 0xf) {
	case M0_AFC_PHASE:
		conf = &fopt->ft_fom_type.ft_conf;
		break;
	case M0_AFC_STATE:
		conf = &fopt->ft_fom_type.ft_state_conf;
		break;
	case M0_AFC_RPC_OUT:
		conf = &fopt->ft_rpc_item_type.rit_outgoing_conf;
		break;
	case M0_AFC_RPC_IN:
		conf = &fopt->ft_rpc_item_type.rit_incoming_conf;
		break;
	default:
		M0_IMPOSSIBLE("Wrong mask.");
	}
	sm_trans(conf, fopt->ft_name, ctx, buf);
}

static void rpc_in(struct context *ctx, const uint64_t *v, char *buf)
{
	extern const struct m0_sm_conf incoming_item_sm_conf;
	sm_state(&incoming_item_sm_conf, ctx, v, buf);
}

static void rpc_out(struct context *ctx, const uint64_t *v, char *buf)
{
	extern const struct m0_sm_conf outgoing_item_sm_conf;
	sm_state(&outgoing_item_sm_conf, ctx, v, buf);
}

extern struct m0_sm_conf be_tx_sm_conf;
static void tx_state(struct context *ctx, const uint64_t *v, char *buf)
{
	sm_state(&be_tx_sm_conf, ctx, v, buf);
}

static void tx_state_counter(struct context *ctx, char *buf)
{
	sm_trans(&be_tx_sm_conf, "tx", ctx, buf);
}

extern struct m0_sm_conf op_states_conf;
static void beop_state_counter(struct context *ctx, char *buf)
{
	sm_trans(&op_states_conf, "be-op", ctx, buf);
}


#define COUNTER  &counter, &skip, &skip, &skip, &skip, &skip, &skip
#define FID &fid, &skip
#define TIMED &duration, &sym

struct id_intrp ids[] = {
	{ M0_AVI_NULL,            "null" },
	{ M0_AVI_NODE,            "node",            { FID } },
	{ M0_AVI_LOCALITY,        "locality",        { &dec } },
	{ M0_AVI_PID,             "pid",             { &dec } },
	{ M0_AVI_THREAD,          "thread",          { &hex, &hex } },
	{ M0_AVI_SERVICE,         "service",         { FID } },
	{ M0_AVI_FOM,             "fom",             { &ptr, &fom_type,
						       &skip, &skip } },
	{ M0_AVI_CLOCK,           "clock",           { } },
	{ M0_AVI_PHASE,           "fom-phase",       { &fom_phase, &skip } },
	{ M0_AVI_STATE,           "fom-state",       { &fom_state, &skip } },
	{ M0_AVI_STATE_COUNTER,   "",
	  .ii_repeat = M0_AVI_STATE_COUNTER_END - M0_AVI_STATE_COUNTER,
	  .ii_spec   = &fom_state_counter },
	{ M0_AVI_ALLOC,           "alloc",           { &dec, &ptr },
	  { "size", "addr" } },
	{ M0_AVI_FOM_DESCR,       "fom-descr",       { FID, &hex, &rpcop,
						       &rpcop, &bol },
	  { "service", NULL, "sender",
	    "req-opcode", "rep-opcode", "local" } },
	{ M0_AVI_FOM_ACTIVE,      "fom-active",      { COUNTER } },
	{ M0_AVI_RUNQ,            "runq",            { COUNTER } },
	{ M0_AVI_WAIL,            "wail",            { COUNTER } },
	{ M0_AVI_AST,             "ast" },
	{ M0_AVI_LOCALITY_FORQ_DURATION, "loc-forq-duration", { TIMED } },
	{ M0_AVI_LOCALITY_FORQ,      "loc-forq-counter",  { COUNTER } },
	{ M0_AVI_LOCALITY_CHAN_WAIT, "loc-wait-counter",  { COUNTER } },
	{ M0_AVI_LOCALITY_CHAN_CB,   "loc-cb-counter",    { COUNTER } },
	{ M0_AVI_LOCALITY_CHAN_QUEUE,"loc-queue-counter", { COUNTER } },
	{ M0_AVI_IOS_IO_DESCR,    "ios-io-descr",    { FID, FID,
						       &hex, &hex, &dec, &dec,
						       &dec, &dec, &dec },
	  { "file", NULL, "cob", NULL, "read-v", "write-v",
	    "seg-nr", "count", "offset", "descr-nr", "colour" }},
	{ M0_AVI_FS_OPEN,         "m0t1fs-open",     { FID, &oct },
	  { NULL, NULL, "flags" }},
	{ M0_AVI_FS_LOOKUP,       "m0t1fs-lookup",   { FID } },
	{ M0_AVI_FS_CREATE,       "m0t1fs-create",   { FID, &oct, &dec },
	  { NULL, NULL, "mode", "rc" } },
	{ M0_AVI_FS_READ,         "m0t1fs-read",     { FID } },
	{ M0_AVI_FS_WRITE,        "m0t1fs-write",    { FID } },
	{ M0_AVI_FS_IO_DESCR,     "m0t1fs-io-descr", { &dec, &dec },
	  { "offset", "rc" }},
	{ M0_AVI_FS_IO_MAP,       "m0t1fs-io-map",     { &dec, FID, &dec,
							 &dec, &dec,&dec,
							 &dec,&dec },
	  { "req_state", NULL, NULL, "unit_type", "device_state",
	    "frame", "target", "group", "unit" }},
	{ M0_AVI_STOB_IO_LAUNCH,  "stob-io-launch",  { FID, &dec,
						       &dec, &dec, &dec, &dec },
	  { NULL, NULL, "count", "bvec-nr", "ivec-nr", "offset" } },
	{ M0_AVI_STOB_IO_END,     "stob-io-end",     { FID, &duration,
						       &dec, &dec, &dec },
	  { NULL, NULL, "duration", "rc", "count", "frag" } },
	{ M0_AVI_STOB_IOQ,        "stob-ioq-thread", { &dec } },
	{ M0_AVI_STOB_IOQ_INFLIGHT, "stob-ioq-inflight", { COUNTER } },
	{ M0_AVI_STOB_IOQ_QUEUED, "stob-ioq-queued", { COUNTER } },
	{ M0_AVI_STOB_IOQ_GOT,    "stob-ioq-got",    { COUNTER } },

	{ M0_AVI_RPC_LOCK,        "rpc-machine-lock", { &ptr } },
	{ M0_AVI_RPC_REPLIED,     "rpc-replied",      { &ptr, &rpcop } },
	{ M0_AVI_RPC_OUT_PHASE,   "rpc-out-phase",    { &rpc_out, &skip } },
	{ M0_AVI_RPC_IN_PHASE,    "rpc-in-phase",    { &rpc_in, &skip } },
	{ M0_AVI_BE_TX_STATE,     "tx-state",       { &tx_state, &skip  } },
	{ M0_AVI_BE_TX_COUNTER,   "",
	  .ii_repeat = M0_AVI_BE_TX_COUNTER_END - M0_AVI_BE_TX_COUNTER,
	  .ii_spec   = &tx_state_counter },
	{ M0_AVI_BE_OP_COUNTER,   "",
	  .ii_repeat = M0_AVI_BE_OP_COUNTER_END - M0_AVI_BE_OP_COUNTER,
	  .ii_spec   = &beop_state_counter },
	{ M0_AVI_FOP_TYPES_RANGE_START,   "",
	  .ii_repeat = M0_AVI_FOP_TYPES_RANGE_END-M0_AVI_FOP_TYPES_RANGE_START,
	  .ii_spec   = &fop_counter },
	{ M0_AVI_SIT,             "sit",  { &hex, &hex, &hex, &hex, &hex,
					    &dec, &dec, FID },
	  { "seq", "offset", "prev", "next", "size", "idx", "nr", "fid" } },
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
	val_dump(ctx, "* ", &rec->ar_val, 0, !flatten);
	for (i = 0; i < rec->ar_label_nr; ++i)
		val_dump(ctx, "| ", &rec->ar_label[i], 8, !flatten);
	if (flatten)
		puts("");
}

static int pad(int indent)
{
	return indent > 0 ? printf("%*.*s", indent, indent,
		   "                                                    ") : 0;
}

static void val_dump(struct context *ctx, const char *prefix,
		     const struct m0_addb2_value *val, int indent, bool cr)
{
	struct id_intrp  *intrp = id_get(val->va_id);
	int               i;
	char              buf[BUF_SIZE];
	enum { WIDTH = 12 };

#define BEND (buf + strlen(buf))

	ctx->c_val = val;
	printf(prefix);
	pad(indent);
	if (indent == 0 && val->va_time != 0) {
		_clock(ctx, &val->va_time, buf);
		printf("%s ", buf);
	}
	if (intrp != NULL && intrp->ii_spec != NULL) {
		intrp->ii_spec(ctx, buf);
		printf("%s%s", buf, cr ? "\n" : " ");
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
	printf("%s", cr ? "\n" : " ");
#undef BEND
}

extern struct m0_fom_type *m0_fom__types[M0_OPCODES_NR];
static void context_fill(struct context *ctx, const struct m0_addb2_value *val)
{
	switch (val->va_id) {
	case M0_AVI_FOM:
		ctx->c_fom.fo_addr = val->va_data[0];
		ctx->c_fom.fo_tid  = val->va_data[1];
		M0_ASSERT(val->va_data[1] < ARRAY_SIZE(m0_fom__types));
		ctx->c_fom.fo_type = m0_fom__types[val->va_data[1]];
		break;
	}
}

static bfd      *abfd;
static asymbol **syms;
static uint64_t  base;
static size_t    nr;

static int asymbol_cmp(const void *a0, const void *a1)
{
	const asymbol *const* s0 = a0;
	const asymbol *const* s1 = a1;

	return ((int)bfd_asymbol_value(*s0)) - ((int)bfd_asymbol_value(*s1));
}

/**
 * Initialises bfd.
 */
static void libbfd_init(const char *libpath)
{
	unsigned symtab_size;
	size_t   i;

	bfd_init();
	abfd = bfd_openr(libpath, 0);
	if (abfd == NULL)
		err(EX_OSERR, "bfd_openr(): %d.", errno);
	bfd_check_format(abfd, bfd_object); /* cargo-cult call. */
	symtab_size = bfd_get_symtab_upper_bound(abfd);
	syms = (asymbol **) m0_alloc(symtab_size);
	if (syms == NULL)
		err(EX_UNAVAILABLE, "Cannot allocate symtab.");
	nr = bfd_canonicalize_symtab(abfd, syms);
	for (i = 0; i < nr; ++i) {
		if (strcmp(syms[i]->name, "m0_ptr_wrap") == 0) {
			base = bfd_asymbol_value(syms[i]);
			break;
		}
	}
	if (base == 0)
		err(EX_CONFIG, "No base symbol found.");
	qsort(syms, nr, sizeof syms[0], &asymbol_cmp);
}

static void libbfd_fini(void)
{
	m0_free(syms);
}

static void libbfd_resolve(uint64_t delta, char *buf)
{
	static uint64_t    cached = 0;
	static const char *name   = NULL;

	if (abfd == NULL)
		;
	else if (delta == cached)
		sprintf(buf, " %s", name);
	else {
		size_t mid;
		size_t left = 0;
		size_t right = nr;

		cached = delta;
		delta += base;
		while (left + 1 < right) {
			asymbol *sym;

			mid = (left + right) / 2;
			sym = syms[mid];

			if (bfd_asymbol_value(sym) > delta)
				right = mid;
			else if (bfd_asymbol_value(sym) < delta)
				left = mid;
			else {
				left = mid;
				break;
			}
		}
		name = syms[left]->name;
		sprintf(buf, " %s", name);
	}
}

static void deflate(void)
{
	int ch;

	while ((ch = getchar()) != EOF) {
		if (ch == '|')
			putchar('\n');
		putchar(ch);
	}
}

static void flate(void)
{
	int ch;
	int prev;

	for (prev = 0; (ch = getchar()) != EOF; prev = ch) {
		if (prev == '\n' && ch == '|')
			prev = ' ';
		if (prev != 0)
			putchar(prev);
	}
	if (prev != 0)
		putchar(prev);
}

static void misc_init(void)
{
	m0_sns_cm_repair_trigger_fop_init();
	m0_sns_cm_rebalance_trigger_fop_init();
	m0_sns_cm_repair_sw_onwire_fop_init();
	m0_sns_cm_rebalance_sw_onwire_fop_init();
}

static void misc_fini(void)
{
	m0_sns_cm_repair_trigger_fop_fini();
	m0_sns_cm_rebalance_trigger_fop_fini();
	m0_sns_cm_repair_sw_onwire_fop_fini();
	m0_sns_cm_rebalance_sw_onwire_fop_fini();
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
