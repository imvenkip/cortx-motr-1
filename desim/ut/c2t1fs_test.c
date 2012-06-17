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

#include "colibri/init.h"
#include "lib/thread.h"    /* LAMBDA */
#include "lib/getopts.h"

#include "desim/sim.h"
#include "desim/storage.h"
#include "desim/chs.h"
#include "desim/net.h"
#include "desim/c2t1fs.h"
#include "desim/elevator.h"

/**
   @addtogroup desim desim
   @{
 */

static struct net_conf net = {
	.nc_frag_size      = 4*1024,
	.nc_rpc_size       =   1024,
	.nc_rpc_delay_min  =   1000, /* microsecond */
	.nc_rpc_delay_max  =   5000,
	.nc_frag_delay_min =    100,
	.nc_frag_delay_max =   1000,
	.nc_rate_min       =  750000000,
	.nc_rate_max       = 1000000000, /* 1GB/sec QDR Infiniband */
	.nc_nob_max        =     ~0UL,
	.nc_msg_max        =     ~0UL,
};

static struct net_srv srv0 = {
	.ns_nr_threads     =      64,
	.ns_pre_bulk_min   =       0,
	.ns_pre_bulk_max   =    1000,
	.ns_file_size      = 1024*1024*1024
};

static struct c2t1fs_conf c2t1fs = {
	.ct_nr_clients   = 0,
	.ct_nr_threads   = 0,
	.ct_nr_servers   = 0,
	.ct_nr_devices   = 0,
	.ct_N            = 8,
	.ct_K            = 2,
	.ct_unitsize     = 4*1024*1024,
	.ct_client_step  = 0,
	.ct_thread_step  = 0,
	.ct_inflight_max = 8,
	.ct_total        = 1024*1024*1024,
	.ct_delay_min    =             0,
	.ct_delay_max    =       1000000, /* millisecond */
	.ct_net          = &net,
	.ct_srv0         = &srv0
};

static struct chs_conf ST31000640SS = { /* Barracuda ES.2 */
	.cc_storage = {
		.sc_sector_size = 512,
	},
	.cc_heads          = 4*2,    /* sginfo */
	.cc_cylinders      = 153352, /* sginfo */
	.cc_track_skew     = 160,    /* sginfo */
	.cc_cylinder_skew  = 76,     /* sginfo */
	.cc_sectors_min    = 1220,   /* sginfo */
	.cc_sectors_max    = 1800,   /* guess */
	.cc_cyl_in_zone    = 48080,  /* sginfo */

	.cc_seek_avg            =  8500000, /* data sheet */
	.cc_seek_track_to_track =   800000, /* data sheet */
	.cc_seek_full_stroke    = 16000000, /* guess */
	.cc_write_settle        =   500000, /* guess */
	.cc_head_switch         =   500000, /* guess */
	.cc_command_latency     =        0, /* unknown */

	.cc_rps                 = 7200/60
};

static void workload_init(struct sim *s, int argc, char **argv)
{
	unsigned i;
	unsigned j;

	net_init(&net);
	c2t1fs_init(s, &c2t1fs);
	chs_conf_init(&ST31000640SS);

	for (i = 0; i < c2t1fs.ct_nr_servers; ++i) {
		for (j = 0; j < c2t1fs.ct_nr_devices; ++j) {
			struct chs_dev *disc;

			disc = sim_alloc(sizeof *disc);
			sim_name_set(&disc->cd_storage.sd_name, "%u:%u", i, j);
			chs_dev_init(disc, s, &ST31000640SS);
			elevator_init(&c2t1fs.ct_srv[i].ns_el[j],
				      &disc->cd_storage);
		}
	}
}

static void workload_fini(void)
{
	unsigned i;
	unsigned j;

	for (i = 0; i < c2t1fs.ct_nr_servers; ++i) {
		for (j = 0; j < c2t1fs.ct_nr_devices; ++j) {
			struct elevator *el;
			struct chs_dev  *disc;

			el = &c2t1fs.ct_srv[i].ns_el[j];
			disc = container_of(el->e_dev,
					    struct chs_dev, cd_storage);
			elevator_fini(el);
			chs_dev_fini(disc);
		}
	}
	chs_conf_fini(&ST31000640SS);
	c2t1fs_fini(&c2t1fs);
	net_fini(&net);
}

int main(int argc, char **argv)
{
	struct sim s;
	int        result;

	result = c2_init();
	C2_ASSERT(result == 0);

	result = C2_GETOPTS(argv[0], argc, argv,
	    C2_HELPARG('h'),
	    C2_FORMATARG('c', "clients", "%u", &c2t1fs.ct_nr_clients),
	    C2_FORMATARG('s', "servers", "%u", &c2t1fs.ct_nr_servers),
	    C2_FORMATARG('S', "server threads", "%u", &srv0.ns_nr_threads),
	    C2_FORMATARG('d', "devices", "%u", &c2t1fs.ct_nr_devices),
	    C2_FORMATARG('t', "threads", "%u", &c2t1fs.ct_nr_threads),
	    C2_FORMATARG('C', "client step", "%u", &c2t1fs.ct_client_step),
	    C2_FORMATARG('T', "thread step", "%u", &c2t1fs.ct_thread_step),
	    C2_FORMATARG('N', "N", "%u", &c2t1fs.ct_N),
	    C2_FORMATARG('K', "K", "%u", &c2t1fs.ct_K),
	    C2_FORMATARG('u', "unit size", "%lu", &c2t1fs.ct_unitsize),
	    C2_FORMATARG('n', "total", "%lu", &c2t1fs.ct_total),
	    C2_FORMATARG('f', "file size", "%llu", &srv0.ns_file_size),
	    C2_VOIDARG('v', "increase verbosity",
		       LAMBDA(void, (void){ sim_log_level++; } )));

	C2_ASSERT(result == 0);

	sim_init(&s);
	workload_init(&s, argc, argv);
	sim_run(&s);
	cnt_dump_all();
	workload_fini();
	sim_log(&s, SLL_WARN, "%10.2f\n",
		1000.0 * c2t1fs.ct_nr_clients *
		c2t1fs.ct_nr_threads * c2t1fs.ct_total / s.ss_bolt);
	sim_fini(&s);
	c2_fini();
	return 0;
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
