/* -*- C -*- */
/*
 * COPYRIGHT 2014 SEAGATE LLC
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF SEAGATE LLC,
 * ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF SEAGATE TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF SEAGATE LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF SEAGATE'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A SEAGATE REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author:  Juan   Gonzalez <juan.gonzalez@seagate.com>
 *                   James  Morse    <james.s.morse@seagate.com>
 *                   Sining Wu       <sining.wu@seagate.com>
 * Original creation date: 30-Oct-2014
 *
 * Subsequent modification: Abhishek Saha <abhishek.saha@seagate.com>
 * Modification date: 31-Dec-2018
 */
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <getopt.h>
#include <libgen.h>

#include "lib/string.h"
#include "clovis/clovis.h"
#include "clovis/clovis_idx.h"
#include "clovis/st/utils/clovis_helper.h"

/* Clovis parameters */

static struct m0_clovis          *clovis_instance = NULL;
static struct m0_clovis_container clovis_container;
static struct m0_clovis_config    clovis_conf;
static struct m0_idx_dix_config   dix_conf;

static void usage(FILE *file, char *prog_name)
{
	fprintf(file, "Usage: %s [OPTION]...\n"
"Delete from MERO.\n"
"\n"
"Mandatory arguments to long options are mandatory for short options too.\n"
"  -l, --local         ADDR        local endpoint address\n"
"  -H, --ha            ADDR        HA endpoint address\n"
"  -p, --profile       FID         profile FID\n"
"  -P, --process       FID         process FID\n"
"  -o, --object        FID         ID of the mero object\n"
"  -h, --help                      shows this help text and exit\n"
, prog_name);
}


int main(int argc, char **argv)
{
	int        rc;
	struct m0_uint128 id = M0_CLOVIS_ID_APP;
	int                c;
	int                option_index = 0;

	static struct option l_opts[] = {
				{"local",       required_argument, NULL, 'l'},
				{"ha",          required_argument, NULL, 'H'},
				{"profile    ", required_argument, NULL, 'p'},
				{"process",     required_argument, NULL, 'P'},
				{"object",      required_argument, NULL, 'o'},
				{"help",        no_argument,       NULL, 'h'},
				{0,             0,                 0,     0 }};

	while ((c = getopt_long(argc, argv, ":l:H:p:P:o:h", l_opts,
			       &option_index)) != -1)
	{
		switch (c) {
			case 'l': clovis_conf.cc_local_addr = optarg;
				  continue;
			case 'H': clovis_conf.cc_ha_addr = optarg;
				  continue;
			case 'p': clovis_conf.cc_profile = optarg;
				  continue;
			case 'P': clovis_conf.cc_process_fid = optarg;
				  continue;
			case 'o': id.u_lo = atoi(optarg);
				  continue;
			case 'h': usage(stderr, basename(argv[0]));
				  exit(EXIT_FAILURE);
			case '?': fprintf(stderr, "Unsupported option '%c'\n",
					  optopt);
				  usage(stderr, basename(argv[0]));
				  exit(EXIT_FAILURE);
			case ':': fprintf(stderr, "No argument given for '%c'\n",
					  optopt);
				  usage(stderr, basename(argv[0]));
				  exit(EXIT_FAILURE);
			default:  fprintf(stderr, "Unsupported option '%c'\n", c);
		}
	}

	clovis_conf.cc_is_oostore            = true;
	clovis_conf.cc_is_read_verify        = false;
	clovis_conf.cc_tm_recv_queue_min_len = M0_NET_TM_RECV_QUEUE_DEF_LEN;
	clovis_conf.cc_max_rpc_msg_size      = M0_RPC_DEF_MAX_RPC_MSG_SIZE;
	clovis_conf.cc_idx_service_conf      = &dix_conf;
	dix_conf.kc_create_meta              = false;
	clovis_conf.cc_idx_service_id        = M0_CLOVIS_IDX_DIX;


	rc = clovis_init(&clovis_conf, &clovis_container, &clovis_instance);
	if (rc < 0) {
		fprintf(stderr, "clovis_init failed! rc = %d\n", rc);
		exit(EXIT_FAILURE);
	}

	clovis_unlink(&clovis_container, id);

	clovis_fini(clovis_instance);

	return 0;
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
