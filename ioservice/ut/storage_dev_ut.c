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
 * Original author: Mikhail Antropov <mikhail.v.antropov@seagate.com>
 * Original creation date: 08/07/2015
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_IOSERVICE
#include "lib/trace.h"

#include <unistd.h> /* get_current_dir_name */

#include "ioservice/storage_dev.h" /* m0_storage_devs */
#include "balloc/balloc.h"         /* BALLOC_DEF_BLOCK_SHIFT */
#include "rpc/rpclib.h"            /* m0_rpc_server_ctx */
#include "lib/finject.h"
#include "ut/misc.h"               /* M0_UT_PATH */
#include "ut/ut.h"

#define SERVER_ENDPOINT_ADDR "0@lo:12345:34:1"

static int rpc_start(struct m0_rpc_server_ctx *rpc_srv)
{
	enum {
		LOG_NAME_MAX_LEN     = 128,
		EP_MAX_LEN           = 24,
		RPC_SIZE_MAX_LEN     = 32,
	};
	 const char               *confd_ep = SERVER_ENDPOINT_ADDR;

	char                log_name[LOG_NAME_MAX_LEN];
	char                full_ep[EP_MAX_LEN];
	char                max_rpc_size[RPC_SIZE_MAX_LEN];
	struct m0_net_xprt *xprt = &m0_net_lnet_xprt;

	snprintf(full_ep, EP_MAX_LEN, "lnet:%s", confd_ep);
	snprintf(max_rpc_size, RPC_SIZE_MAX_LEN,
		 "%d", M0_RPC_DEF_MAX_RPC_MSG_SIZE);

#define NAME(ext) "io_sdev" ext
	char                    *argv[] = {
		NAME(""), "-T", "AD", "-D", NAME(".db"),
		"-S", NAME(".stob"), "-A", "linuxstob:"NAME(""),
		"-w", "10", "-e", full_ep, "-H", SERVER_ENDPOINT_ADDR,
		"-f", "<0x7200000000000001:1>",
		"-m", max_rpc_size,
		"-c", M0_UT_PATH("conf.xc"), "-P", M0_UT_CONF_PROFILE
	};
#undef NAME

	M0_SET0(rpc_srv);

	rpc_srv->rsx_xprts         = &xprt;
	rpc_srv->rsx_xprts_nr      = 1;
	rpc_srv->rsx_argv          = argv;
	rpc_srv->rsx_argc          = ARRAY_SIZE(argv);
	snprintf(log_name, LOG_NAME_MAX_LEN, "confd_%s.log", confd_ep);
	rpc_srv->rsx_log_file_name = log_name;

	return m0_rpc_server_start(rpc_srv);
}

static int create_test_file(const char *filename)
{
	int   rc;
	FILE *file;
	char str[0x1000]="rfb";

	file = fopen(filename, "w+");
	if (file == NULL)
		return errno;
	rc = fwrite(str, 0x1000, 1, file) == 1 ? 0 : -EINVAL;
	fclose(file);
	return rc;
}

static void storage_dev_test(void)
{
	int                      rc;
	struct m0_storage_devs   devs;
	const char              *location = "linuxstob:io_sdev";
	struct m0_stob_domain   *domain;
	struct m0_storage_dev   *dev1, *dev2, *dev3;
	struct m0_storage_space  space;
	struct m0_rpc_server_ctx rpc_srv;
	int                      total_size;
	int                      grp_size;
	char                    *cwd;
	char                    *fname1, *fname2;
	int                      block_size = 1 << BALLOC_DEF_BLOCK_SHIFT;
	struct m0_conf_sdev      sdev = {
		.sd_obj = { .co_id = M0_FID_TINIT(
				M0_CONF_SDEV_TYPE.cot_ftype.ft_id, 0, 12) },
		.sd_bsize = block_size };


	/* pre-init */
	rc = rpc_start(&rpc_srv);
	M0_UT_ASSERT(rc == 0);

	cwd = get_current_dir_name();
	M0_UT_ASSERT(cwd != NULL);
	rc = asprintf(&fname1, "%s/test1", cwd);
	M0_UT_ASSERT(rc > 0);
	rc = asprintf(&fname2, "%s/test2", cwd);
	M0_UT_ASSERT(rc > 0);
	rc = create_test_file(fname1);
	M0_UT_ASSERT(rc == 0);
	rc = create_test_file(fname2);
	M0_UT_ASSERT(rc == 0);

	/* init */
	domain = m0_stob_domain_find_by_location(location);
	rc = m0_storage_devs_init(&devs,
				  rpc_srv.rsx_mero_ctx.cc_reqh_ctx.rc_beseg,
				  domain,
				  &rpc_srv.rsx_mero_ctx.cc_reqh_ctx.rc_reqh);
	M0_UT_ASSERT(rc == 0);
	m0_storage_devs_lock(&devs);

	/* attach */
	/*
	 * Total size accounts space for reserved groups and one non-reserved
	 * group.
	 */
	grp_size = BALLOC_DEF_BLOCKS_PER_GROUP * block_size;
	total_size = grp_size;
	rc = m0_storage_dev_new(&devs, 10, fname2, total_size, NULL, &dev1);
	M0_UT_ASSERT(rc == 0);
	m0_storage_dev_attach(dev1, &devs);

	sdev.sd_size = total_size;
	sdev.sd_filename = fname1;
	sdev.sd_dev_idx = 12;
	m0_fi_enable("m0_storage_dev_new_by_conf", "no-conf-dev");
	rc = m0_storage_dev_new_by_conf(&devs, &sdev, &dev2);
	M0_UT_ASSERT(rc == 0);
	m0_storage_dev_attach(dev2, &devs);
	m0_fi_disable("m0_storage_dev_new_by_conf", "no-conf-dev");

	m0_fi_enable_once("m0_alloc", "fail_allocation");
	rc = m0_storage_dev_new(&devs, 13, "../../some-file", total_size,
				NULL, &dev3);
	M0_UT_ASSERT(rc == -ENOMEM);

	m0_fi_enable_off_n_on_m("m0_alloc", "fail_allocation", 1, 1);
	rc = m0_storage_dev_new(&devs, 13, "../../some-file", total_size,
				NULL, &dev3);
	m0_fi_disable("m0_alloc", "fail_allocation");
	M0_UT_ASSERT(rc == -ENOMEM);

	/* find*/
	dev1 = m0_storage_devs_find_by_cid(&devs, 10);
	M0_UT_ASSERT(dev1 != NULL);

	dev2 = m0_storage_devs_find_by_cid(&devs, 12);
	M0_UT_ASSERT(dev2 != NULL);

	dev3 = m0_storage_devs_find_by_cid(&devs, 13);
	M0_UT_ASSERT(dev3 == NULL);

	/* space */
	m0_storage_dev_space(dev1, &space);
	M0_UT_ASSERT(space.sds_block_size  == block_size);
	/* Free blocks don't include blocks for reserved groups and we have
	 * only one non-reserved group */
	M0_UT_ASSERT(space.sds_free_blocks == BALLOC_DEF_BLOCKS_PER_GROUP);
	M0_UT_ASSERT(space.sds_total_size  == total_size);


	/* detach */
	m0_storage_dev_detach(dev1);
	m0_storage_dev_detach(dev2);

	/* fini */
	m0_storage_devs_unlock(&devs);
	m0_storage_devs_fini(&devs);

	m0_rpc_server_stop(&rpc_srv);
	unlink("test1");
	unlink("test2");
	free(fname1);
	free(fname2);
	free(cwd);
}

const struct m0_ut_suite storage_dev_ut = {
	.ts_name = "storage-dev-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "storage-dev-test", storage_dev_test },
		{ NULL, NULL },
	},
};
M0_EXPORTED(storage_dev_ut);

#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
