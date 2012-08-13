/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 2012-Jun-16
 */
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "lib/assert.h"
#include "stob/stob_id.h"
#include "desim/sim.h"
#include "desim/net.h"
#include "desim/c2t1fs.h"
#include "layout/linear_enum.h" /* struct c2_layout_linear_enum */

/**
   @addtogroup desim desim
   @{
 */

static void fid_to_stob_id(struct c2_fid *fid, struct c2_stob_id *stob_id)
{
	stob_id->si_bits.u_hi = fid->f_container;
	stob_id->si_bits.u_lo = fid->f_key;
}

static void thread_loop(struct sim *s, struct sim_thread *t, void *arg)
{
	struct c2t1fs_thread       *cth  = arg;
	struct c2t1fs_client       *cl   = cth->cth_client;
	struct c2t1fs_conf         *conf = cl->cc_conf;
	struct c2_pdclust_instance *pi   = cth->cth_pdclust_instance;
	uint32_t                    pl_N = pi->pi_layout->pl_attr.pa_N;
	uint32_t                    pl_K = pi->pi_layout->pl_attr.pa_K;
	struct c2_layout_enum      *le;
	int64_t                     nob;
	c2_bindex_t                 grp;
	c2_bcount_t                 unit;
	int                         result;

	C2_ASSERT(t == &cth->cth_thread);

	sim_log(s, SLL_TRACE, "thread [%i:%i]: seed: [%16lx:%16lx]\n",
		cl->cc_id, cth->cth_id, pi->pi_layout->pl_attr.pa_seed.u_hi,
		pi->pi_layout->pl_attr.pa_seed.u_lo);

	nob  = conf->ct_total;
	unit = conf->ct_unitsize;
	le = c2_striped_layout_to_enum(&pi->pi_layout->pl_base);
	for (nob = conf->ct_total, grp = 0; nob > 0;
	     nob -= unit * (pl_N + pl_K), grp++) {
		unsigned idx;

		sim_sleep(t, sim_rnd(conf->ct_delay_min, conf->ct_delay_max));
		/* loop over data and parity units, skip spare units. */
		for (idx = 0; idx < pl_N + pl_K; ++idx) {
			const struct c2_pdclust_src_addr src = {
				.sa_group = grp,
				.sa_unit  = idx
			};
			struct c2_pdclust_tgt_addr tgt;
			uint32_t                   obj;
			uint32_t                   srv;
			struct c2t1fs_conn        *conn;
			struct c2_fid              fid;
			struct c2_stob_id          stob_id;

			c2_pdclust_instance_map(pi, &src, &tgt);
			/* @todo for parity unit waste some time calculating
			   parity. Limit bus bandwidth. */
			obj = tgt.ta_obj;
			C2_ASSERT(obj < conf->ct_pool.po_width);
			srv  = obj / conf->ct_nr_devices;
			conn = &cl->cc_srv[srv];
			le->le_ops->leo_get(le, obj, &pi->pi_base.li_gfid,
					    &fid);
			fid_to_stob_id(&fid, &stob_id);
			result = c2_pool_alloc(&conf->ct_pool, &stob_id);
			C2_ASSERT(result == 0);	

			sim_log(s, SLL_TRACE,
				"%c [%3i:%3i] -> %4u@%3u [%4lu:%4lu] %6lu\n",
				"DPS"[c2_pdclust_unit_classify(pi->pi_layout,
							       idx)],
				cl->cc_id, cth->cth_id, obj, srv,
				stob_id.si_bits.u_hi, stob_id.si_bits.u_lo,
				tgt.ta_frame);

			/* wait until rpc can be send to the server. */
			while (conn->cs_inflight >= conf->ct_inflight_max)
				sim_chan_wait(&conn->cs_wakeup, t);
			conn->cs_inflight++;
			net_rpc_process(t, conf->ct_net, &conf->ct_srv[srv],
					&stob_id, tgt.ta_frame * unit, unit);
			conn->cs_inflight--;
			sim_chan_signal(&conn->cs_wakeup);
		}
	}
	sim_thread_exit(t);
}

static int threads_start(struct sim_callout *call)
{
	struct c2t1fs_conf *conf  = call->sc_datum;
	unsigned i;
	unsigned j;

	for (i = 0; i < conf->ct_nr_clients; ++i) {
		struct c2t1fs_client *c = &conf->ct_client[i];

		for (j = 0; j < conf->ct_nr_threads; ++j) {
			struct c2t1fs_thread *t = &c->cc_thread[j];

			sim_thread_init(call->sc_sim, &t->cth_thread, 0,
					thread_loop, t);
		}
	}
	return 1;
}

static void layout_build(struct c2t1fs_conf *conf)
{
	int                           result;
	struct c2_layout_linear_attr  lin_attr;
	struct c2_layout_linear_enum *lin_enum;
	struct c2_pdclust_attr        pl_attr;
	uint64_t                      lid;

	result = c2_dbenv_init(&conf->ct_dbenv, "c2t1fs_sim-db", 0);
	C2_ASSERT(result == 0);

	result = c2_layout_domain_init(&conf->ct_l_dom, &conf->ct_dbenv);
	C2_ASSERT(result == 0);

	result = c2_layout_standard_types_register(&conf->ct_l_dom);
	C2_ASSERT(result == 0);

	lin_attr.lla_nr = conf->ct_pool.po_width;
	lin_attr.lla_A = 0;
	lin_attr.lla_B = 1;
	result = c2_linear_enum_build(&conf->ct_l_dom, &lin_attr, &lin_enum);
 	C2_ASSERT(result == 0);

	pl_attr.pa_N = conf->ct_N;
	pl_attr.pa_K = conf->ct_K;
	pl_attr.pa_P = conf->ct_pool.po_width;
	pl_attr.pa_unit_size = conf->ct_unitsize;
	lid = 0x4332543146535349; /* C2T1FSSI */
	c2_uint128_init(&pl_attr.pa_seed, "c2t1fs_si_pdclus");

	result = c2_pdclust_build(&conf->ct_l_dom, lid, &pl_attr,
				  &lin_enum->lle_base, &conf->ct_pdclust);
 	C2_ASSERT(result == 0);

	/*
	 * Acquire a reference on the layout so that it does not get vanished,
	 * in between.
	 */
	c2_layout_get(&conf->ct_pdclust->pl_base.sl_base); 
}

static void layout_fini(struct c2t1fs_conf *conf)
{
	/*
	 * Delete the reference on the laout object that was acquired in
	 * layout_build() so that the layou gets deleted.
	 */
	c2_layout_put(&conf->ct_pdclust->pl_base.sl_base); 

	c2_layout_standard_types_unregister(&conf->ct_l_dom);
	c2_layout_domain_fini(&conf->ct_l_dom);
	c2_dbenv_fini(&conf->ct_dbenv);
}

void c2t1fs_init(struct sim *s, struct c2t1fs_conf *conf)
{
	unsigned                  i;
	unsigned                  j;
	struct c2_fid             gfid;
	int                       result;

	c2_pool_init(&conf->ct_pool, conf->ct_nr_servers * conf->ct_nr_devices);
	conf->ct_srv = sim_alloc(conf->ct_nr_servers * sizeof conf->ct_srv[0]);
	for (i = 0; i < conf->ct_nr_servers; ++i) {
		struct net_srv *srv = &conf->ct_srv[i];

		*srv = *conf->ct_srv0;
		srv->ns_nr_devices = conf->ct_nr_devices;
		net_srv_init(s, srv);
		sim_name_set(&srv->ns_name, "%u", i);
	}

	conf->ct_client = sim_alloc(conf->ct_nr_clients *
				    sizeof conf->ct_client[0]);

	layout_build(conf);
	c2_fid_set(&gfid, 0, 999);

	for (i = 0; i < conf->ct_nr_clients; ++i) {
		struct c2t1fs_client *c = &conf->ct_client[i];

		c->cc_conf   = conf;
		c->cc_id     = i;
		c->cc_thread = sim_alloc(conf->ct_nr_threads *
					 sizeof c->cc_thread[0]);
		c->cc_srv    = sim_alloc(conf->ct_nr_servers *
					 sizeof c->cc_srv[0]);
		for (j = 0; j < conf->ct_nr_servers; ++j)
			sim_chan_init(&c->cc_srv[j].cs_wakeup,
				      "inflight:%i:%i", i, j);
		for (j = 0; j < conf->ct_nr_threads; ++j) {
			struct c2t1fs_thread *th = &c->cc_thread[j];

			th->cth_id     = j;
			th->cth_client = c;
			result = c2_pdclust_instance_build(conf->ct_pdclust,
						&gfid,
						&th->cth_pdclust_instance);
			C2_ASSERT(result == 0);
			//todo tgt_reuse(conf, i, j);
		}
	}
	sim_timer_add(s, 0, threads_start, conf);
}

void c2t1fs_fini(struct c2t1fs_conf *conf)
{
	unsigned                    i;
	unsigned                    j;
	struct c2_pdclust_instance *pi;

	if (conf->ct_client != NULL) {
		for (i = 0; i < conf->ct_nr_clients; ++i) {
			struct c2t1fs_client *c = &conf->ct_client[i];

			if (c->cc_thread != NULL) {
				for (j = 0; j < conf->ct_nr_threads; ++j) {
					struct c2t1fs_thread *cth;

					cth = &c->cc_thread[j];
					pi  = cth->cth_pdclust_instance;
					sim_thread_fini(&cth->cth_thread);
					pi->pi_base.li_ops->lio_fini(
								&pi->pi_base);
				}
				sim_free(c->cc_thread);
			}
			if (c->cc_srv != NULL) {
				for (j = 0; j < conf->ct_nr_servers; ++j)
					sim_chan_fini(&c->cc_srv[j].cs_wakeup);
				sim_free(c->cc_srv);
			}
		}
		sim_free(conf->ct_client);
	}
	if (conf->ct_srv != NULL) {
		for (i = 0; i < conf->ct_nr_servers; ++i)
			net_srv_fini(&conf->ct_srv[i]);
	}
	layout_fini(conf);
	c2_pool_fini(&conf->ct_pool);
}

/** @} end of desim group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
