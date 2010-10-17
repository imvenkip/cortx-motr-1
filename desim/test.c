/*
 * Copyright 2009 ClusterStor.
 *
 * Nikita Danilov.
 */
#include <stdio.h>
#include <math.h> /* sqrt */

#include "sim.h"
#include "storage.h"
#include "chs.h"
#include "net.h"
#include "client.h"
#include "elevator.h"

#if 0
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
	.nc_msg_max        =     ~0UL
};

/* 
 * Seagate Cheetah 15K.7 SAS ST3450857SS
 *
 * http://www.seagate.com/staticfiles/support/disc/manuals/enterprise/cheetah/15K.7/100516226a.pdf
 *
 * Heads:             3*2
 * Cylinders:         107500
 * Sectors per track: 680--2040 (680 + (i << 7)/10000)
 * Rotational speed:  250 revolutions/sec 
 *
 * Avg rotational latency: 2ms
 *
 * Seek:                read write
 *       average:        3.4 3.9 
 *       track-to-track: 0.2 0.44
 *       full stroke:    6.6 7.4
 */
static struct chs_conf cheetah = {
	.cc_storage = {
		.sc_sector_size = 512,
	},
	.cc_heads          = 3*2,
	.cc_cylinders      = 107500,
	.cc_track_skew     = 0,
	.cc_cylinder_skew  = 0,
	.cc_sectors_min    =  680,
	.cc_sectors_max    = 2040,

	.cc_seek_avg            = 3400000,
	.cc_seek_track_to_track =  200000,
	.cc_seek_full_stroke    = 6600000,
	.cc_write_settle        =  220000,
	.cc_head_switch         =       0, /* unknown */
	.cc_command_latency     =       0, /* unknown */

	.cc_rps                 = 250
};
#endif

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

static struct chs_dev disc;
static struct elevator el;

#if 0
static struct net_srv  srv = {
	.ns_nr_threads     =      64,
	.ns_pre_bulk_min   =       0,
	.ns_pre_bulk_max   =    1000,
	.ns_el             = &el
};

static struct client_conf client = {
	.cc_nr_clients   =             0,
	.cc_nr_threads   =             0,
	.cc_total        = 100*1024*1024,
	.cc_count        =     1024*1024,
	.cc_opt_count    =     1024*1024,
	.cc_inflight_max =             8,
	.cc_delay_min    =             0,
	.cc_delay_max    =       1000000, /* millisecond */
	.cc_cache_max    =            ~0UL,
	.cc_dirty_max    =  32*1024*1024,
	.cc_net          = &net,
	.cc_srv          = &srv
};

static void workload_init(struct sim *s, int argc, char **argv)
{
	chs_conf_init(&cheetah);
	chs_dev_init(&disc, s, &cheetah);
	elevator_init(&el, &disc.cd_storage);
	net_init(&net);
	net_srv_init(s, &srv);
	client_init(s, &client);
}

static void workload_fini(void)
{
	client_fini(&client);
	net_srv_fini(&srv);
	net_fini(&net);
	elevator_fini(&el);
	chs_dev_fini(&disc);	
	chs_conf_fini(&cheetah);
}

int main(int argc, char **argv)
{
	struct sim s;
	unsigned clients = atoi(argv[1]);
	unsigned threads = atoi(argv[2]);
	unsigned long long filesize;
	
	client.cc_nr_clients = clients;
	client.cc_nr_threads = threads;
	srv.ns_file_size = filesize = threads * client.cc_total;
	sim_init(&s);
	workload_init(&s, argc, argv);
	sim_run(&s);
	/* workload_fini(); */
	cnt_dump_all();
	sim_log(&s, SLL_WARN, "%5i %5i %10.2f\n", clients, threads, 
		1000.0 * filesize * clients / s.ss_bolt);
	sim_fini(&s);
	return 0;
}

#else

static struct sim_thread seek_thr;

static double seekto(struct sim *s, int64_t sector, int sectors) 
{
	sim_time_t now;

	now = s->ss_bolt;
	elevator_io(&el, SRT_READ, sector, sectors);
	return (s->ss_bolt - now)/1000;
}

enum {
  LBA_D   =   10,
  ROUNDS  =   10,
  TRACK_D =    8,
  TRACK_S = 2500
};

static void seek_test_thread(struct sim *s, struct sim_thread *t, void *arg)
{
	int64_t in_num_sect = -1;
	int i;
	int j;
	int k;
	int round;
	int sectors;
	int64_t sector;
	double latency;
	double seeklat[LBA_D][LBA_D];
	double seeksqr[LBA_D][LBA_D];
	

	in_num_sect = 1953525168;

	/*
	 * repeated read.
	 */
	for (i = 0; i < LBA_D; ++i) {
		sector = in_num_sect * i / LBA_D;
		for (sectors = 1; sectors <= (1 << 16); sectors *= 2) {
			double avg;
			double sqr;

			seekto(s, sector, sectors);
			for (avg = sqr = 0.0, round = 0; round < ROUNDS; ++round) {
				latency = seekto(s, sector, sectors);
				avg += latency;
				sqr += latency*latency;
			}
			avg /= ROUNDS;
			printf("reading %4i sectors at %i/%i: %6.0f (%6.0f)\n", 
			       sectors, i, LBA_D, avg, sqrt(sqr/ROUNDS - avg*avg));
		}
	}

	/*
	 * seeks
	 */
	for (round = 0; round < ROUNDS; ++round) {
		for (i = 0; i < LBA_D; ++i) {
			for (j = 0; j < LBA_D; ++j) {
				int64_t sector_from;
				int64_t sector_to;

				sector_from = in_num_sect * i / LBA_D;
				sector_to = in_num_sect * j / LBA_D;
				/*
				 * another loop to average rotational latency out.
				 */
				for (k = 0; k < TRACK_D; ++k) {
					seekto(s, sector_from + TRACK_S*k/TRACK_D, 1);
					latency = seekto(s, sector_to + TRACK_S*round/ROUNDS, 1);
					seeklat[i][j] += latency;
					seeksqr[i][j] += latency*latency;
				}
			}
			printf(".");
		}
		printf("\n");
	}
	for (i = 0; i < LBA_D; ++i) {
		for (j = 0; j < LBA_D; ++j) {
			latency = seeklat[i][j] / ROUNDS / TRACK_D;
			printf("[%6.0f %4.0f]", latency, 
			       sqrt(seeksqr[i][j] / ROUNDS / TRACK_D - latency*latency));
		}
		printf("\n");
	}
	for (i = 0; i < LBA_D; ++i) {
		for (j = 0; j < LBA_D; ++j)
			printf("%6.0f, ", seeklat[i][j] / ROUNDS / TRACK_D);
		printf("\n");
	}

	sim_thread_exit(t);
}

static int seek_test_start(struct sim_callout *co)
{
	sim_thread_init(co->sc_sim, &seek_thr, 0, seek_test_thread, NULL);
	return 1;
}

int main(int argc, char **argv)
{
	struct sim s;

	chs_conf_init(&ST31000640SS);
	chs_dev_init(&disc, &s, &ST31000640SS);
	elevator_init(&el, &disc.cd_storage);
	sim_init(&s);

	sim_timer_add(&s, 0, seek_test_start, NULL);
	sim_run(&s);

	cnt_dump_all();
	sim_log(&s, SLL_WARN, "done\n");
	sim_fini(&s);
	return 0;
}

#endif

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
