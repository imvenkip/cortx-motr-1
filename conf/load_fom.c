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
 * Original author: Mikhail Antropov <mikhail.v.antropov@xyratex.com>
 * Original creation date: 16-Feb-2015
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SPIEL
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/tlist.h"
#include "lib/assert.h"
#include "lib/finject.h"
#include "conf/confd.h"         /* m0_confd, m0_confd_bob */
#include "conf/confd_stob.h"
#include "conf/flip_fom.h"      /* m0_conf_generate_conf_filename */
#include "conf/load_fop.h"
#include "conf/load_fom.h"
#include "conf/preload.h"       /* m0_confstr_parse, m0_confx_free */
#include "conf/obj.h"           /* m0_conf_objx_fid */
#include "conf/obj_ops.h"       /* m0_conf_obj_find */
#include "conf/onwire.h"        /* M0_CONFX_AT */
#include "fop/fop.h"
#include "fid/fid.h"
#include "mero/magic.h"
#ifndef __KERNEL__
  #include "mero/setup.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

/**
 * @addtogroup conf_foms
 * @{
 */

M0_TL_DESCR_DECLARE(rpcbulk, M0_EXTERN);

static int conf_load_fom_tick(struct m0_fom *fom);
static void conf_load_fom_fini(struct m0_fom *fom);

static int conf_net_buffer_allocate(struct m0_fom *);
static int conf_prepare(struct m0_fom *);
static int conf_buffer_free(struct m0_fom *);
static int conf_zero_copy_initiate(struct m0_fom *);
static int conf_zero_copy_finish(struct m0_fom *);
static size_t conf_load_fom_home_locality(const struct m0_fom *fom);

/**
 * Spiel Load FOM operation vector.
 */
const struct m0_fom_ops conf_load_fom_ops = {
	.fo_fini          = conf_load_fom_fini,
	.fo_tick          = conf_load_fom_tick,
	.fo_home_locality = conf_load_fom_home_locality,
};

/**
 * Spiel Load FOM type operation vector.
 */
const struct m0_fom_type_ops conf_load_fom_type_ops = {
	.fto_create = m0_conf_load_fom_create,
};

/**
 * Spiel FOM state transition table.
 */
struct m0_sm_state_descr conf_load_phases[] = {
	[M0_FOPH_CONF_FOM_PREPARE] = {
		.sd_flags     = M0_SDF_INITIAL,
		.sd_name      = "Conf_load_Prepare",
		.sd_allowed   = M0_BITS(M0_FOPH_CONF_FOM_BUFFER_ALLOCATE,
					M0_FOPH_FAILURE)
	},
	[M0_FOPH_CONF_FOM_BUFFER_ALLOCATE] = {
		.sd_name      = "Network_Buffer_Acquire",
		.sd_allowed   = M0_BITS(M0_FOPH_CONF_ZERO_COPY_INIT,
					M0_FOPH_FAILURE)
	},
	[M0_FOPH_CONF_ZERO_COPY_INIT] = {
		.sd_name      = "Zero-copy_Initiate",
		.sd_allowed   = M0_BITS(M0_FOPH_CONF_ZERO_COPY_WAIT,
					M0_FOPH_FAILURE)
	},
	[M0_FOPH_CONF_ZERO_COPY_WAIT] = {
		.sd_name      = "Zero-copy_Finish",
		.sd_allowed   = M0_BITS(M0_FOPH_CONF_BUFFER_FREE,
					M0_FOPH_FAILURE)
	},
	[M0_FOPH_CONF_BUFFER_FREE] = {
		.sd_name      = "Conf_load_Finish",
		.sd_allowed   = M0_BITS(M0_FOPH_SUCCESS,
					M0_FOPH_FAILURE)
	},
};


struct m0_sm_conf conf_load_conf = {
	.scf_name      = "Conf_load_phases",
	.scf_nr_states = ARRAY_SIZE(conf_load_phases),
	.scf_state     = conf_load_phases,
};

/**
 * Compare equals Spiel FOP - Spiel FOM
 */
static bool conf_load_fom_invariant(const struct m0_conf_load_fom *fom)
{
	return _0C(fom != NULL);
}

/**
 * Create and initiate Spiel FOM and return generic struct m0_fom
 * Find the corresponding fom_type and associate it with m0_fom.
 * Associate fop with fom type.
 *
 * @param fop file operation packet need to process
 * @param out file operation machine need to allocate and initiate
 *
 * @pre fop != NULL
 * @pre fop is m0_fop_conf_load
 * @pre out != NULL
 */
M0_INTERNAL int m0_conf_load_fom_create(struct m0_fop   *fop,
					struct m0_fom  **out,
					struct m0_reqh  *reqh)
{
	struct m0_fom           *fom;
	struct m0_conf_load_fom *conf_load_fom;
	struct m0_fop           *rep_fop;

	M0_PRE(fop != NULL);
	M0_PRE(m0_is_conf_load_fop(fop));
	M0_PRE(out != NULL);

	M0_ENTRY("fop=%p", fop);

	M0_ALLOC_PTR(conf_load_fom);
	rep_fop = m0_fop_reply_alloc(fop, &m0_fop_conf_load_rep_fopt);
	if (conf_load_fom == NULL || rep_fop == NULL) {
		m0_free(conf_load_fom);
		m0_free(rep_fop);
		return M0_RC(-ENOMEM);
	}

	fom = &conf_load_fom->clm_gen;
	m0_fom_init(fom, &fop->f_type->ft_fom_type, &conf_load_fom_ops,
		    fop, rep_fop, reqh);
	*out = fom;

	M0_LOG(M0_DEBUG, "fom=%p", fom);
	return M0_RC(0);
}

/**
 * Prepare FOM data.
 * Set current Confd version as FOP report parameter.
 *
 * @param fom file operation machine instance.
 * @pre fom != NULL
 * @pre fom->fo_fop if Conf Load type
 * @pre fom->fo_service != NULL
 * @pre m0_fom_phase(fom) == M0_FOPH_CONF_FOM_PREPARE
 */
#ifndef __KERNEL__
static int conf_prepare(struct m0_fom *fom)
{
	struct m0_confd             *confd;
	struct m0_fop_conf_load_rep *rep;

	M0_PRE(fom != NULL);
	M0_PRE(m0_is_conf_load_fop(fom->fo_fop));
	M0_PRE(fom->fo_service != NULL);
	M0_PRE(m0_fom_phase(fom) == M0_FOPH_CONF_FOM_PREPARE);

	M0_ENTRY("fom=%p", fom);

	confd = bob_of(fom->fo_service, struct m0_confd, d_reqh, &m0_confd_bob);
	rep = m0_conf_fop_to_load_fop_rep(fom->fo_rep_fop);
	rep->clfr_version = m0_conf_version(confd->d_cache);
	m0_fom_phase_set(fom, M0_FOPH_CONF_FOM_BUFFER_ALLOCATE);

	return M0_RC(M0_FSO_AGAIN);
}
#else  /* __KERNEL__ */
static int conf_prepare(struct m0_fom *fom)
{
	M0_PRE(fom != NULL);
	M0_ENTRY("fom=%p", fom);

	m0_fom_phase_move(fom, -ENOENT, M0_FOPH_FAILURE);

	return M0_RC(M0_FSO_AGAIN);
}
#endif  /* __KERNEL__ */

/**
 * Allocate network buffer for process bulk request.
 *
 * @param fom file operation machine instance.
 * @pre fom != NULL
 * @pre fom->fo_fop if Conf Load type
 * @pre fom->fo_service != NULL
 * @pre m0_fom_phase(fom) == M0_FOPH_CONF_FOM_BUFFER_ALLOCATE
 */
static int conf_net_buffer_allocate(struct m0_fom *fom)
{
	struct m0_fop           *fop;
	struct m0_conf_load_fom *conf_fom;
	struct m0_fop_conf_load *conf_fop;
	int                      size;
	struct m0_bufvec        *buf_vec;
	int                      rc;
	int                      seg_size;

	M0_PRE(fom != NULL);
	M0_PRE(m0_is_conf_load_fop(fom->fo_fop));
	M0_PRE(fom->fo_service != NULL);
	M0_PRE(m0_fom_phase(fom) == M0_FOPH_CONF_FOM_BUFFER_ALLOCATE);

	M0_ENTRY("fom=%p", fom);

	conf_fom = container_of(fom, struct m0_conf_load_fom, clm_gen);
	M0_ASSERT(conf_load_fom_invariant(conf_fom));

	fop = fom->fo_fop;
	conf_fop = m0_conf_fop_to_load_fop(fop);

	size = conf_fop->clf_desc.bdd_used;
	seg_size = m0_conf_segment_size(fop);

	buf_vec = &conf_fom->clm_net_buffer.nb_buffer;

	rc = m0_bufvec_alloc_aligned(buf_vec, (size + seg_size - 1) / seg_size,
				     seg_size, PAGE_SHIFT);
	if (rc == 0)
		conf_fom->clm_net_buffer.nb_qtype = M0_NET_QT_ACTIVE_BULK_RECV;

	m0_fom_phase_moveif(fom, rc, M0_FOPH_CONF_ZERO_COPY_INIT,
			    M0_FOPH_FAILURE);

	return M0_RC(M0_FSO_AGAIN);
}

/**
 * Initiate zero-copy
 * Initiates zero-copy for batch of descriptors.
 * And wait for zero-copy to complete for all descriptors.
 * Network layer signaled on m0_rpc_bulk::rb_chan on completion.
 *
 * @param fom file operation machine.
 *
 * @pre fom != NULL
 * @pre fom->fo_fop if Conf Load type
 * @pre m0_fom_phase(fom) == M0_FOPH_CONF_ZERO_COPY_INIT
 */
static int conf_zero_copy_initiate(struct m0_fom *fom)
{
	int                          rc;
	struct m0_conf_load_fom     *conf_fom;
	struct m0_fop               *fop;
	struct m0_fop_conf_load     *conf_fop;
	struct m0_net_buf_desc_data *nbd_data;
	struct m0_rpc_bulk          *rbulk;
	struct m0_rpc_bulk_buf      *rb_buf;
	struct m0_net_buffer        *nb;
	struct m0_net_domain        *dom;
	uint32_t                     segs_nr = 0;
	m0_bcount_t                  seg_size;

	M0_PRE(fom != NULL);
	M0_PRE(m0_is_conf_load_fop(fom->fo_fop));
	M0_PRE(m0_fom_phase(fom) == M0_FOPH_CONF_ZERO_COPY_INIT);

	M0_ENTRY("fom=%p", fom);

	conf_fom = container_of(fom, struct m0_conf_load_fom, clm_gen);
	M0_ASSERT(conf_load_fom_invariant(conf_fom));

	fop = fom->fo_fop;
	conf_fop = m0_conf_fop_to_load_fop(fop);
	rbulk = &conf_fom->clm_bulk;
	m0_rpc_bulk_init(rbulk);

	dom      = m0_fop_domain_get(fop);
	nbd_data = &conf_fop->clf_desc;

	/* Create rpc bulk bufs list using prepare net buffers */
	nb = &conf_fom->clm_net_buffer;

	seg_size = m0_conf_segment_size(fop);

	/*
	 * Calculate number of segments by length of data
	 * Segment number Round up
	 */
	segs_nr = (conf_fop->clf_desc.bdd_used + seg_size - 1) / seg_size;

	M0_LOG(M0_DEBUG, "segs_nr %d", segs_nr);

	rc = m0_rpc_bulk_buf_add(rbulk, segs_nr, dom, nb, &rb_buf);
	if (rc != 0) {
		m0_fom_phase_move(fom, rc, M0_FOPH_FAILURE);
		M0_LEAVE();
		return M0_FSO_AGAIN;
	}

	/*
	 * On completion of zero-copy on all buffers rpc_bulk
	 * sends signal on channel rbulk->rb_chan.
	 */
	m0_mutex_lock(&rbulk->rb_mutex);
	m0_fom_wait_on(fom, &rbulk->rb_chan, &fom->fo_cb);
	m0_mutex_unlock(&rbulk->rb_mutex);

	/*
	 * This function deletes m0_rpc_bulk_buf object one
	 * by one as zero copy completes on respective buffer.
	 */
	rc = m0_rpc_bulk_load(rbulk,
			      fop->f_item.ri_session->s_conn,
			      nbd_data,
			      &m0_rpc__buf_bulk_cb);
	if (rc != 0) {
		m0_mutex_lock(&rbulk->rb_mutex);
		m0_fom_callback_cancel(&fom->fo_cb);
		m0_mutex_unlock(&rbulk->rb_mutex);
		m0_rpc_bulk_buflist_empty(rbulk);
		m0_rpc_bulk_fini(rbulk);
		m0_fom_phase_move(fom, rc, M0_FOPH_FAILURE);
		M0_LEAVE();
		return M0_FSO_AGAIN;
	}

	m0_fom_phase_set(fom, M0_FOPH_CONF_ZERO_COPY_WAIT);

	return M0_RC(M0_FSO_WAIT);
}

/**
 * Copy data from FOM Net buffer field to IO STOB
 * STOB domain is placed to confd configure file folder
 * FID consists to old version, new version and TX id @see M0_CONFD_FID
 *
 * @param conf_fom file operation machine.
 */
static int conf_fom_conf_file_save(struct m0_conf_load_fom *conf_fom)
{
	int                          rc = 0;
	char                        *location = NULL;
	struct m0_stob              *stob;
	struct m0_net_buffer        *nb = &conf_fom->clm_net_buffer;
	struct m0_fop_conf_load     *conf_fop;
	struct m0_fop_conf_load_rep *conf_fop_rep;

	M0_ENTRY();

	conf_fop = m0_conf_fop_to_load_fop(conf_fom->clm_gen.fo_fop);
	conf_fop_rep = m0_conf_fop_to_load_fop_rep(conf_fom->clm_gen.fo_rep_fop);

	rc = m0_conf_stob_location_generate(&conf_fom->clm_gen, &location) ?:
	     m0_confd_stob_init(&stob, location,
				&M0_CONFD_FID(conf_fop_rep->clfr_version,
					      conf_fop->clf_version,
					      conf_fop->clf_tx_id)) ?:
	     m0_confd_stob_bufvec_write(stob, &nb->nb_buffer);

	m0_confd_stob_fini(stob);
	m0_free(location);

	return M0_RC(rc);
}

/**
 * Zero-copy Finish
 * Check for zero-copy result.
 *
 * @param fom file operation machine.
 *
 * @pre fom != NULL
 * @pre fom->fo_fop if Conf Load type
 * @pre m0_fom_phase(fom) == M0_FOPH_CONF_ZERO_COPY_WAIT
 */
static int conf_zero_copy_finish(struct m0_fom *fom)
{
	struct m0_conf_load_fom *conf_fom;
	struct m0_rpc_bulk      *rbulk;

	M0_ENTRY("fom=%p", fom);

	M0_PRE(fom != NULL);
	M0_PRE(m0_is_conf_load_fop(fom->fo_fop));
	M0_PRE(m0_fom_phase(fom) == M0_FOPH_CONF_ZERO_COPY_WAIT);

	conf_fom = container_of(fom, struct m0_conf_load_fom, clm_gen);
	M0_ASSERT(conf_load_fom_invariant(conf_fom));

	rbulk = &conf_fom->clm_bulk;
	m0_mutex_lock(&rbulk->rb_mutex);

	if (rbulk->rb_rc == 0)
		rbulk->rb_rc = conf_fom_conf_file_save(conf_fom);

	m0_fom_phase_moveif(fom,
			    rbulk->rb_rc,
			    M0_FOPH_CONF_BUFFER_FREE,
			    M0_FOPH_FAILURE);

	m0_mutex_unlock(&rbulk->rb_mutex);
	m0_rpc_bulk_fini(rbulk);
	return M0_RC(M0_FSO_AGAIN);
}

/**
 * Restore Conf cache from string.
 *
 * @param cache result cache.
 * @param buf string contain cache
 *
 * @pre cache != NULL
 * @pre buf != NULL
 * @pre cache is locked
 */
M0_INTERNAL int m0_conf_cache_from_string(struct m0_conf_cache *cache,
					  char                 *buf)
{
	struct m0_confx *enc;
	int              i;
	int              rc;

	M0_ENTRY();

	M0_PRE(cache != NULL);
	M0_PRE(buf != NULL);
	M0_PRE(m0_mutex_is_locked(cache->ca_lock));

	rc = m0_confstr_parse(buf, &enc);
	if (rc != 0)
		return M0_RC(rc);

	for (i = 0; i < enc->cx_nr && rc == 0; ++i) {
		struct m0_conf_obj        *obj;
		const struct m0_confx_obj *xobj = M0_CONFX_AT(enc, i);

		rc = m0_conf_obj_find(cache, m0_conf_objx_fid(xobj), &obj) ?:
			m0_conf_obj_fill(obj, xobj, cache);
	}

	m0_confx_free(enc);
	return M0_RC(rc);
}

/**
 * Finalise bufvec and free allocated memory.
 *
 * @param fom instance file operation machine under execution
 *
 * @pre fom != NULL
 * @pre fom->fo_fop if Conf Load type
 * @pre m0_fom_phase(fom) == M0_FOPH_CONF_BUFFER_FREE
 */
static int conf_buffer_free(struct m0_fom *fom)
{
	struct m0_conf_load_fom *conf_fom;
	struct m0_fop_conf_load *conf_fop;

	M0_PRE(fom != NULL);
	M0_PRE(m0_is_conf_load_fop(fom->fo_fop));
	M0_PRE(m0_fom_phase(fom) == M0_FOPH_CONF_BUFFER_FREE);

	M0_ENTRY("fom=%p", fom);

	conf_fom = container_of(fom, struct m0_conf_load_fom, clm_gen);
	M0_ASSERT(conf_load_fom_invariant(conf_fom));

	conf_fop = m0_conf_fop_to_load_fop(fom->fo_fop);

	if( conf_fop->clf_desc.bdd_desc.nbd_data != NULL) {
		m0_free(conf_fop->clf_desc.bdd_desc.nbd_data);
		conf_fop->clf_desc.bdd_desc.nbd_len = 0;
	}
	m0_bufvec_free_aligned(&conf_fom->clm_net_buffer.nb_buffer, PAGE_SHIFT);

	m0_fom_phase_set(fom, M0_FOPH_SUCCESS);

	return M0_RC(M0_FSO_AGAIN);
}

/**
 * Phase transition for the Spiel Load operation.
 *
 * @param fom instance file operation machine under execution
 *
 * @pre fom != NULL
 * @pre fom->fo_fop if Conf Load type
 */
static int conf_load_fom_tick(struct m0_fom *fom)
{
	int                          rc = 0;
	struct m0_conf_load_fom     *conf_fom;
	struct m0_fop_conf_load_rep *rep;

	M0_ENTRY("fom %p", fom);

	M0_PRE(fom != NULL);
	M0_PRE(m0_is_conf_load_fop(fom->fo_fop));

	if (m0_fom_phase(fom) < M0_FOPH_NR)
		return m0_fom_tick_generic(fom);


	conf_fom = container_of(fom, struct m0_conf_load_fom, clm_gen);
	M0_ASSERT(conf_load_fom_invariant(conf_fom));

	switch (m0_fom_phase(fom)) {
	case M0_FOPH_CONF_FOM_PREPARE:
		rc = conf_prepare(fom);
		break;
	case M0_FOPH_CONF_FOM_BUFFER_ALLOCATE:
		rc = conf_net_buffer_allocate(fom);
		break;
	case M0_FOPH_CONF_ZERO_COPY_INIT:
		rc = conf_zero_copy_initiate(fom);
		break;
	case M0_FOPH_CONF_ZERO_COPY_WAIT:
		rc = conf_zero_copy_finish(fom);
		break;
	case M0_FOPH_CONF_BUFFER_FREE:
		rc = conf_buffer_free(fom);
		break;
	default:
		M0_ASSERT(0);
		break;
	}

	if (M0_IN(m0_fom_phase(fom), (M0_FOPH_SUCCESS, M0_FOPH_FAILURE))) {
		rep = m0_conf_fop_to_load_fop_rep(fom->fo_rep_fop);
		rep->clfr_rc    = m0_fom_rc(fom);
		rep->clfr_count = conf_fom->clm_count;
	}

	return M0_RC(rc);
}

/**
 * Finalise of Spiel file operation machine.
 * This is the right place to free all resources acquired by FOM
 *
 * @param fom instance file operation machine under execution
 *
 * @pre fom != NULL
 */
static void conf_load_fom_fini(struct m0_fom *fom)
{
	struct m0_conf_load_fom *conf_fom;

	M0_PRE(fom != NULL);
	conf_fom = container_of(fom, struct m0_conf_load_fom, clm_gen);
	M0_ASSERT(conf_load_fom_invariant(conf_fom));

	m0_fom_fini(fom);
	m0_free(conf_fom);
}


static size_t conf_load_fom_home_locality(const struct m0_fom *fom)
{
	M0_ENTRY();
	M0_PRE(fom != NULL);
	M0_LEAVE();
	return 1;
}

#undef M0_TRACE_SUBSYSTEM

/** @} end of io_foms */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
