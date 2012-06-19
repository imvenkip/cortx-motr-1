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

/**
   @addtogroup desim desim
   @{
 */

static void thread_loop(struct sim *s, struct sim_thread *t, void *arg)
{
	struct c2t1fs_thread     *cth  = arg;
	struct c2t1fs_client     *cl   = cth->cth_client;
	struct c2t1fs_conf       *conf = cl->cc_conf;
	struct c2_pdclust_layout *l    = cth->cth_pdclust;
	int64_t                   nob;
	c2_bindex_t               grp;
	c2_bcount_t               unit;

	C2_ASSERT(t == &cth->cth_thread);

	sim_log(s, SLL_TRACE, "thread [%i:%i]: seed: [%16lx:%16lx]\n",
		cl->cc_id, cth->cth_id, l->pl_seed.u_hi, l->pl_seed.u_lo);

	nob  = conf->ct_total;
	unit = conf->ct_unitsize;
	for (nob = conf->ct_total, grp = 0; nob > 0;
	     nob -= unit * (l->pl_N + l->pl_K), grp++) {
		unsigned idx;

		sim_sleep(t, sim_rnd(conf->ct_delay_min, conf->ct_delay_max));
		/* loop over data and parity units, skip spare units. */
		for (idx = 0; idx < l->pl_N + l->pl_K; ++idx) {
			const struct c2_pdclust_src_addr src = {
				.sa_group = grp,
				.sa_unit  = idx
			};
			struct c2_pdclust_tgt_addr tgt;
			uint32_t                   obj;
			uint32_t                   srv;
			struct c2t1fs_conn        *conn;
			struct c2_stob_id         *id;

			c2_pdclust_layout_map(l, &src, &tgt);
			/* @todo for parity unit waste some time calculating
			   parity. Limit bus bandwidth. */
			obj = tgt.ta_obj;
			C2_ASSERT(obj < conf->ct_pool.po_width);
			srv  = obj / conf->ct_nr_devices;
			conn = &cl->cc_srv[srv];
			id   = &l->pl_tgt[obj];

			sim_log(s, SLL_TRACE,
				"%c [%3i:%3i] -> %4u@%3u [%4lu:%4lu] %6lu\n",
				"DPS"[c2_pdclust_unit_classify(l, idx)],
				cl->cc_id, cth->cth_id, obj, srv,
				id->si_bits.u_hi, id->si_bits.u_lo,
				tgt.ta_frame);

			/* wait until rpc can be send to the server. */
			while (conn->cs_inflight >= conf->ct_inflight_max)
				sim_chan_wait(&conn->cs_wakeup, t);
			conn->cs_inflight++;
			net_rpc_process(t, conf->ct_net, &conf->ct_srv[srv],
					id, tgt.ta_frame * unit, unit);
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

/*
 * With the current dummy implementation of c2_pool_alloc(), c2_pdclust_build()
 * allocates a fresh set of target objects every time. If the same seed was
 * already used, re-used target objects.
 */
void tgt_reuse(struct c2t1fs_conf *conf, unsigned i0, unsigned j0)
{
	struct c2_pdclust_layout *p0;
	unsigned                  i1;
	unsigned                  j1;
	unsigned                  k;

	p0 = conf->ct_client[i0].cc_thread[j0].cth_pdclust;

	for (i1 = 0; i1 <= i0 ; ++i1) {
		for (j1 = 0; j1 < conf->ct_nr_threads; ++j1) {
			struct c2_pdclust_layout *p1;

			p1 = conf->ct_client[i1].cc_thread[j1].cth_pdclust;
			if (p1 != NULL && p1 != p0 &&
			    c2_uint128_eq(&p0->pl_seed, &p1->pl_seed)) {
				for (k = 0; k < conf->ct_pool.po_width; ++k)
					p0->pl_tgt[k] = p1->pl_tgt[k];
				return;
			}
		}
	}
	for (k = 0; k < conf->ct_pool.po_width; ++k)
		p0->pl_tgt[k].si_bits.u_hi = k;
}

void c2t1fs_init(struct sim *s, struct c2t1fs_conf *conf)
{
	unsigned i;
	unsigned j;
	int      result;

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
			struct c2_uint128     dummy_id;
			struct c2_uint128     seed;
			uint64_t              delta;

			delta = i * conf->ct_client_step +
				j * conf->ct_thread_step;
			seed.u_hi = 42 + delta;
			seed.u_lo = 11 - delta;

			th->cth_id     = j;
			th->cth_client = c;
			result = c2_pdclust_build(&conf->ct_pool, &dummy_id,
						  conf->ct_N, conf->ct_K,
						  conf->ct_unitsize, &seed,
						  &th->cth_pdclust);
			C2_ASSERT(result == 0);
			tgt_reuse(conf, i, j);
		}
	}
	sim_timer_add(s, 0, threads_start, conf);
}

void c2t1fs_fini(struct c2t1fs_conf *conf)
{
	unsigned i;
	unsigned j;

	if (conf->ct_client != NULL) {
		for (i = 0; i < conf->ct_nr_clients; ++i) {
			struct c2t1fs_client *c = &conf->ct_client[i];

			if (c->cc_thread != NULL) {
				for (j = 0; j < conf->ct_nr_threads; ++j) {
					struct c2t1fs_thread *cth;

					cth = &c->cc_thread[j];
					sim_thread_fini(&cth->cth_thread);
					c2_pdclust_fini(cth->cth_pdclust);
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
