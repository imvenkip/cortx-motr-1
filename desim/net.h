/*
 * Copyright 2009 ClusterStor.
 *
 * Nikita Danilov.
 */
#ifndef NET_H
#define NET_H

#include "desim/sim.h"

struct elevator;

struct net_conf {
	unsigned           nc_frag_size;
	unsigned           nc_rpc_size;
	sim_time_t         nc_rpc_delay_min;
	sim_time_t         nc_rpc_delay_max;
	sim_time_t         nc_frag_delay_min;
	sim_time_t         nc_frag_delay_max;
	unsigned long long nc_rate_min;
	unsigned long long nc_rate_max;
	unsigned long      nc_nob_max;
	unsigned long      nc_nob_inflight;
	unsigned long      nc_msg_max;
	unsigned long      nc_msg_inflight;
	struct sim_chan    nc_queue;
	struct cnt         nc_rpc_wait;
	struct cnt         nc_rpc_bulk_wait;
};

struct net_srv {
	unsigned            ns_nr_threads;
	sim_time_t          ns_pre_bulk_min;
	sim_time_t          ns_pre_bulk_max;
	struct sim_chan     ns_incoming;
	struct c2_list      ns_queue;
	struct sim_thread  *ns_thread;
	struct elevator    *ns_el;
	unsigned long long  ns_file_size;
	int                 ns_shutdown;
	unsigned            ns_active;
};

struct net_rpc {
	struct net_srv     *nr_srv;
	struct net_conf    *nr_conf;
	unsigned long       nr_fid;
	unsigned long long  nr_offset;    
	unsigned long       nr_todo;
	struct c2_list_link nr_inqueue;
	struct sim_chan     nr_wait;
	struct sim_chan     nr_bulk_wait;
	struct sim_thread  *nr_srv_thread;
};

void net_srv_init(struct sim *s, struct net_srv *srv);
void net_srv_fini(struct net_srv *srv);

void net_init(struct net_conf *net);
void net_fini(struct net_conf *net);

void net_rpc_init(struct net_rpc *rpc, struct net_conf *conf,
		  struct net_srv *srv, unsigned long fid,
		  unsigned long long offset, unsigned long nob);
void net_rpc_fini(struct net_rpc *rpc);
void net_rpc_send(struct sim_thread *t, struct net_rpc *rpc);
void net_rpc_bulk(struct sim_thread *t, struct net_rpc *rpc);

#endif /* NET_H */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
