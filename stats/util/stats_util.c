/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Rajanikant Chirmade <rajanikant_chirmade@xyratex.com>
 * Original creation date: 09/16/2013
 */

#include <signal.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#include <sysexits.h>

#include "fop/fop.h"
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/getopts.h"
#include "lib/memory.h"
#include "rpc/rpclib.h"
#include "net/net.h"
#include "mero/init.h"
#include "addb/addb.h"
#include "stats/stats_api.h"
#include "stats/stats_fops.h"
#include "mero/setup.h"
#include "lib/chan.h"
#include "cfg/cfg.h"
#include "lib/user_space/getopts.h"

enum {
	STATS_MAX_COUNT     = 3,
	STATS_MAX_NAME_SIZE = 32,
	STATS_DEFAULT_COUNT = 1,
	STATS_DEFAULT_DELAY = 5,
	STATS_VALUE_WIDTH   = 16,
};

static struct m0_net_xprt        *xprt           = &m0_net_lnet_xprt;
static struct m0_net_domain       client_net_dom = { };
static bool signaled = false;

static struct m0_rpc_client_ctx cctx = {
	.rcx_net_dom               = &client_net_dom,
	.rcx_max_rpcs_in_flight    = 1,
	.rcx_max_rpc_msg_size      = M0_RPC_DEF_MAX_RPC_MSG_SIZE,
	.rcx_recv_queue_min_length = M0_NET_TM_RECV_QUEUE_DEF_LEN,
};

static void m0stats_help()
{
	fprintf(stderr,
"Usage: m0stats \n"
"\n"
"-e EndPoint    Specify the raw transport local end point address.\n"
"-R EndPoint    Specify the raw transport stats service end point address.\n"
"-s StatsNames  Specify one or more comma separated stats names.\n"
"-o FileName    Specify output file name.\n"
"-l             Lists defined statistics names.\n"
"-d             Specify the delay between updates in second. If no delay is\n"
"		specified default to %d second.\n"
"-c             Specify the number updates. If no count is specified, count\n"
"               default to %d.\n",
	STATS_DEFAULT_DELAY, STATS_DEFAULT_COUNT);
}

static int stats_parse_ids(const char                 *stats_list,
			   struct m0_addb_uint64_seq **stats_ids)
{
	char                      *p;
	const char                *stats = stats_list;
	uint64_t                   id;
	uint32_t                   nstats = 0;
	uint64_t                   ids[STATS_MAX_COUNT];
	struct m0_addb_uint64_seq *seq_ids;

	p = (char *)stats;
	while(p != NULL) {
		if (nstats > STATS_MAX_COUNT) {
			m0_console_printf("m0stats:only %d stats can be query"
					  "at once. Ignoring others.\n",
					  STATS_MAX_COUNT);
			break;
		}
		p = strchr(stats, ',');
		if (p != NULL)
			*p++ = '\0';

		id = m0_addb_rec_type_name2id(stats);
		if (id == M0_ADDB_RECID_UNDEF) {
			m0_console_printf("m0stats:stats \"%s\" not defined.\n",
					  stats);
			return -EINVAL;
		}
		ids[nstats++] = id;
		stats = p;
	}

	M0_ALLOC_PTR(seq_ids);
	if (seq_ids == NULL)
		return -ENOMEM;

	seq_ids->au64s_nr = nstats;
	M0_ALLOC_ARR(seq_ids->au64s_data, seq_ids->au64s_nr);
	if (seq_ids->au64s_data == NULL) {
		m0_free(seq_ids);
		return -ENOMEM;
	}
	memcpy(seq_ids->au64s_data, ids, nstats * sizeof(uint64_t));
	*stats_ids = seq_ids;

	return 0;
}

static int stats_type_width(const struct m0_addb_rec_type *rt)
{
	int i;
	int width = 0;

	for (i = 0; i < rt->art_rf_nr; ++i) {
		int fwidth = strlen(rt->art_rf[i].arfu_name) + 1;

		width += fwidth < STATS_VALUE_WIDTH ?
			 STATS_VALUE_WIDTH : fwidth;
	}

	return width;
}

static int stats_field_width(const char *fname)
{
	int fwidth = strlen(fname) + 1;

	return fwidth < STATS_VALUE_WIDTH ? STATS_VALUE_WIDTH : fwidth;
}

static void stats_print_header(FILE *out, struct m0_addb_uint64_seq *stats_ids)
{
	int                            i;
	int			       stats_width;
	int			       total_width = 0;
	const struct m0_addb_rec_type *rt[STATS_MAX_COUNT];

	for (i = 0; i < stats_ids->au64s_nr; ++i) {
		rt[i] = m0_addb_rec_type_lookup(stats_ids->au64s_data[i]);
		M0_ASSERT(rt[i] != NULL);
		stats_width = stats_type_width(rt[i]);
		total_width += stats_width;
		fprintf(out, "%*s", stats_width, rt[i]->art_name);
	}

	fprintf(out, "\n");
	for (i = 0; i < total_width; ++i)
		fprintf(out, "-");
	fprintf(out, "\n");

	for (i = 0; i < stats_ids->au64s_nr; ++i) {
		int j;
		for (j = 0; j < rt[i]->art_rf_nr; ++j)
			fprintf(out, "%*s",
				stats_field_width(rt[i]->art_rf[j].arfu_name),
				rt[i]->art_rf[j].arfu_name);
	}
	fprintf(out, "\n");
}

static void stats_print_values(FILE *out, struct m0_addb_uint64_seq *stats_ids,
			       const struct m0_stats_recs *stats_recs)
{
#undef STATS_FIELD_VALUE
#define STATS_FIELD_VALUE(stats_sum, j)			\
	(stats_sum.ss_data.au64s_nr != rt->art_rf_nr ?	\
	(uint64_t)0 : stats_sum.ss_data.au64s_data[j])

	int i;
	int j;

	for (i = 0; i < stats_ids->au64s_nr; ++i) {
		const struct m0_addb_rec_type *rt =
			m0_addb_rec_type_lookup(stats_ids->au64s_data[i]);
		if (rt == NULL) {
			fprintf(out, "Failed to retrived stats info.\n");
			break;
		}

		for (j = 0; j < rt->art_rf_nr; ++j)
			fprintf(out, "%*lu",
				stats_field_width(rt->art_rf[j].arfu_name),
				STATS_FIELD_VALUE(stats_recs->sf_stats[i], j));
	}
	fprintf(out, "\n");
#undef STATS_FIELD_VALUE
}

static int stats_print_list()
{
	int id;
	int max_rec_ids = m0_addb_rec_type_max_id();

	fprintf(stdout, "Available stats:\n");
	for (id = 1; id <= max_rec_ids; ++id) {
		const struct m0_addb_rec_type *rt = m0_addb_rec_type_lookup(id);
		if (rt != NULL && rt->art_base_type == M0_ADDB_BRT_STATS)
				fprintf(stdout, "%s\n", rt->art_name);
	}
	return 0;
}

static void sig_handler(int signum)
{
	signaled = true;
}

int main(int argc, char *argv[])
{
	int                         i;
	int                         rc = 0;
	int                         r2;
	struct sigaction            sa;
	const char                 *local_addr = NULL;
	const char                 *remote_addr = NULL;
	const char                 *outfile     = NULL;
	const char                 *stats_list  = NULL;
	bool                        list_names = false;
	m0_time_t		    delay = STATS_DEFAULT_DELAY;
	uint64_t                    count = STATS_DEFAULT_COUNT;
	struct m0_stats_recs       *stats_recs = NULL;
	struct m0_addb_uint64_seq  *stats_ids  = NULL;
	struct cs_endpoint_and_xprt epx;
	FILE			   *fout;

	r2 = M0_GETOPTS("m0stats", argc, argv,
			M0_STRINGARG('R', "Stats service endpoint",
				LAMBDA(void, (const char *str)
				{
					if (remote_addr != NULL)
						rc = -EINVAL;
					else
						remote_addr = str;
				})),
			M0_STRINGARG('e', "Local endpoint",
				LAMBDA(void, (const char *str)
				{
					if (local_addr != NULL)
						rc = -EINVAL;
					else
						local_addr = str;
				})),
			M0_STRINGARG('s', "Stats list",
				LAMBDA(void, (const char *str)
				{
					if (stats_list != NULL)
						rc = -EINVAL;
					else
						stats_list = str;
				})),
			M0_STRINGARG('o', "Output file",
				LAMBDA(void, (const char *str)
				{
					if (outfile != NULL)
						rc = -EINVAL;
					else
						outfile = str;
				})),
			M0_FORMATARG('c', "Number of results", "%lu", &count),
			M0_FORMATARG('d', "Delay between results", "%lu",
				     &delay),
			M0_FLAGARG('l', "List defined stats", &list_names),
			M0_VOIDARG('h', "Detailed usage help",
				LAMBDA(void, (void)
				{
					rc = 1;
				})));
	rc = rc != 0 ? : r2;
	if (rc != 0) {
		m0stats_help();
		return 1;
	}

	if (!ergo(list_names,
		 local_addr == NULL && remote_addr == NULL &&
		 stats_list == NULL && outfile == NULL)){
		m0stats_help();
		return 1;
	}

        /* set up a signal handler to remove work arena */
        M0_SET0(&sa);
        sigaddset(&sa.sa_mask, SIGTERM);
        sigaddset(&sa.sa_mask, SIGINT);
        sigaddset(&sa.sa_mask, SIGQUIT);
        sigaddset(&sa.sa_mask, SIGPIPE);
        sa.sa_handler = sig_handler;
        sigaction(SIGTERM, &sa, NULL);
        sigaction(SIGINT,  &sa, NULL);
        sigaction(SIGQUIT, &sa, NULL);
        sigaction(SIGPIPE, &sa, NULL);

	rc = m0_init(NULL);
	if (rc != 0) {
		fprintf(stderr, "Failed to initialize library. rc = %d\n", rc);
		return rc;
	}

	if (list_names) {
		stats_print_list();
		goto mero_fini;
	}

	if (local_addr == NULL || remote_addr == NULL || stats_list == NULL) {
		m0stats_help();
		goto mero_fini;
	}

	if (outfile != NULL) {
		fout = fopen(outfile, "w");
		if (fout == NULL) {
			fprintf(stderr, "m0stast:Failed to open output file.\n"
				"rc = %d.\n", errno);
			return 1;
		}
	} else
		fout = stdout;

	rc = m0_ep_and_xprt_extract(&epx, local_addr);
	if (rc != 0) {
                fprintf(stderr,
			"m0stats:Failed to extract endpoint. rc = %d\n", rc);
		goto mero_fini_fclose;
	}
	cctx.rcx_local_addr = epx.ex_endpoint;

	rc = m0_ep_and_xprt_extract(&epx, remote_addr);
	if (rc != 0) {
                fprintf(stderr,
			"m0stats:Failed to extract endpoint. rc = %d\n", rc);
		goto mero_fini_fclose;
	}
	cctx.rcx_remote_addr  = epx.ex_endpoint;


        rc = m0_net_xprt_init(xprt);
        if (rc != 0) {
                fprintf(stderr,
			"m0stats:Failed to initialize transport. rc = %d\n",
			rc);
		goto mero_fini_fclose;
        }

	rc = m0_net_domain_init(&client_net_dom, xprt, &m0_addb_proc_ctx);
	M0_ASSERT(rc == 0);

	rc = m0_rpc_client_start(&cctx);
	if (rc != 0) {
		fprintf(stderr,
			"m0stats:m0_rpc_client_start failed. rc = %d.\n", rc);
		rc = EX_UNAVAILABLE;
		goto domain_fini;
        }

	rc = stats_parse_ids(stats_list, &stats_ids);
	if (rc != 0)
		goto disconnect;

	stats_print_header(fout, stats_ids);
	for (i = 0; i < count && !signaled; ++i) {
		rc = m0_stats_query(&cctx.rcx_session, stats_ids, &stats_recs);
		if (rc != 0) {
			fprintf(stderr,"m0stats:m0_stats_query failed."
				"rc = %d.\n", rc);
			rc = EX_UNAVAILABLE;
		}

		if (stats_recs != NULL) {
			stats_print_values(fout, stats_ids, stats_recs);
			m0_stats_free(stats_recs);
		}
		m0_nanosleep(delay * 1000 * 1000 * 1000, NULL);
	}

disconnect:
	rc = m0_rpc_client_stop(&cctx);
	M0_ASSERT(rc == 0);

domain_fini:
	m0_net_domain_fini(&client_net_dom);
mero_fini_fclose:
	fclose(fout);
mero_fini:
	m0_fini();
	return rc;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
