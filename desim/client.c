/*
 * Copyright 2009, 2010 ClusterStor.
 *
 * Nikita Danilov.
 */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>

#include "lib/assert.h"
#include "desim/sim.h"
#include "desim/net.h"
#include "desim/client.h"

/**
   @addtogroup desim desim
   @{
 */

struct client_thread_param {
	struct client_conf *ctp_conf;
	unsigned            ctp_client;
	unsigned            ctp_thread;
};

struct client_write_ext {
	unsigned long long  cwe_offset;
	unsigned long       cwe_count;
	struct c2_list_link cwe_linkage;
};

static void client_rpc_send(struct sim_thread *t, 
			    struct client *c, struct client_conf *conf, 
			    unsigned long long offset, unsigned long count)
{
	struct net_rpc rpc;

	net_rpc_init(&rpc, conf->cc_net, conf->cc_srv, 
		     c->cl_fid, offset, count);
	net_rpc_send(t, &rpc);
	net_rpc_bulk(t, &rpc);
	net_rpc_fini(&rpc);
}
 
static void client_pageout(struct sim *s, struct sim_thread *t, void *arg)
{
	struct client           *c    = arg;
	struct client_conf      *conf = c->cl_conf;
	unsigned                 size = conf->cc_opt_count;
	struct client_write_ext *ext;

	while (1) {
		while (c->cl_dirty - c->cl_io < size) {
			sim_chan_wait(&c->cl_cache_busy, t);
			if (conf->cc_shutdown)
				sim_thread_exit(t);
		}
		C2_ASSERT(!c2_list_is_empty(&c->cl_write_ext));
		ext = container_of(c->cl_write_ext.l_head, 
				   struct client_write_ext, cwe_linkage);
		/* no real cache management for now */
		C2_ASSERT(ext->cwe_count == size);
		c2_list_del(&ext->cwe_linkage);
		sim_log(s, SLL_TRACE, "P%2i/%2i: %6lu %10llu %8u\n", c->cl_id, 
			c->cl_inflight, c->cl_fid, ext->cwe_offset, size);
		c->cl_io += size;
		c->cl_inflight++;
		client_rpc_send(t, c, conf, ext->cwe_offset, size);
		c->cl_inflight--;
		c->cl_io -= size;
		C2_ASSERT(c->cl_cached >= size);
		C2_ASSERT(c->cl_dirty  >= size);
		c->cl_cached -= size;
		c->cl_dirty  -= size;
		sim_chan_broadcast(&c->cl_cache_free);
		sim_free(ext);
	}
}

static void client_write_loop(struct sim *s, struct sim_thread *t, void *arg)
{
	struct client_thread_param *param = arg;
	unsigned                    clid  = param->ctp_client;
	unsigned                    trid  = param->ctp_thread;
	struct client_conf         *conf  = param->ctp_conf;
	struct client              *cl    = &conf->cc_client[clid];
	unsigned long               nob   = 0;
	unsigned                    count = conf->cc_count;
	unsigned long long          off   = conf->cc_total * trid;
	struct client_write_ext    *ext;

	C2_ASSERT(t == &cl->cl_thread[trid]);
	C2_ASSERT(cl->cl_id == clid);

	while (nob < conf->cc_total) {
		sim_sleep(t, sim_rnd(conf->cc_delay_min, conf->cc_delay_max));
		while (cl->cl_dirty + count > conf->cc_dirty_max)
			sim_chan_wait(&cl->cl_cache_free, t);
		ext = sim_alloc(sizeof *ext);
		ext->cwe_offset = off;
		ext->cwe_count  = count;
		c2_list_add_tail(&cl->cl_write_ext, &ext->cwe_linkage);
		cl->cl_cached += count;
		cl->cl_dirty  += count;
		sim_chan_broadcast(&cl->cl_cache_busy);
		sim_log(s, SLL_TRACE, "W%2i/%2i: %6lu %10llu %8u\n", 
			clid, trid, cl->cl_fid, off, count);
		nob += count;
		off += count;
	}
	sim_thread_exit(t);
}

static int client_threads_start(struct sim_callout *call)
{
	struct client_conf        *conf  = call->sc_datum;
	struct client_thread_param param = {
		.ctp_conf = conf
	};
	unsigned i;
	unsigned j;

	for (i = 0; i < conf->cc_nr_clients; ++i) {
		struct client *c = &conf->cc_client[i];
		for (j = 0; j < conf->cc_nr_threads; ++j) {
			param.ctp_client = i;
			param.ctp_thread = j;
			sim_thread_init(call->sc_sim, &c->cl_thread[j], 0,
					client_write_loop, &param);
		}
		for (j = 0; j < conf->cc_inflight_max; ++j)
			sim_thread_init(call->sc_sim, &c->cl_pageout[j], 0,
					client_pageout, c);
	}
	return 1;
}

void client_init(struct sim *s, struct client_conf *conf)
{
	unsigned i;

	cnt_init(&conf->cc_cache_free, NULL, "client::cache-free");
	cnt_init(&conf->cc_cache_busy, NULL, "client::cache-busy");
	conf->cc_client = sim_alloc(conf->cc_nr_clients * 
				    sizeof conf->cc_client[0]);
	for (i = 0; i < conf->cc_nr_clients; ++i) {
		struct client *c;

		c = &conf->cc_client[i];
		c2_list_init(&c->cl_write_ext);
		c->cl_conf = conf;
		c->cl_fid  = i;
		c->cl_id   = i;
		sim_chan_init(&c->cl_cache_free, "client-%04i::cache-free", i);
		sim_chan_init(&c->cl_cache_busy, "client-%04i::cache-busy", i);
		c->cl_cache_free.ch_cnt_sleep.c_parent = &conf->cc_cache_free;
		c->cl_cache_busy.ch_cnt_sleep.c_parent = &conf->cc_cache_busy;

		c->cl_thread = sim_alloc(conf->cc_nr_threads * 
					 sizeof c->cl_thread[0]);
		c->cl_pageout = sim_alloc(conf->cc_inflight_max * 
					  sizeof c->cl_pageout[0]);
	}
	sim_timer_add(s, 0, client_threads_start, conf);
}

void client_fini(struct client_conf *conf)
{
	unsigned i;
	unsigned j;

	conf->cc_shutdown = 1;
	if (conf->cc_client != NULL) {
		for (i = 0; i < conf->cc_nr_clients; ++i) {
			struct client *c = &conf->cc_client[i];

			sim_chan_broadcast(&c->cl_cache_busy);
			sim_chan_fini(&c->cl_cache_free);
			sim_chan_fini(&c->cl_cache_busy);
			if (c->cl_thread != NULL) {
				for (j = 0; j < conf->cc_nr_threads; ++j)
					sim_thread_fini(&c->cl_thread[j]);
				sim_free(c->cl_thread);
			}
			if (c->cl_pageout != NULL) {
				for (j = 0; j < conf->cc_inflight_max; ++j)
					sim_thread_fini(&c->cl_pageout[j]);
				sim_free(c->cl_pageout);
			}
			c2_list_fini(&c->cl_write_ext);
		}
		sim_free(conf->cc_client);
	}
	cnt_fini(&conf->cc_cache_free);
	cnt_fini(&conf->cc_cache_busy);
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
