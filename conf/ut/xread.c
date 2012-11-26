/* -*- c -*- */
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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 26-Sep-2012
 */

#include "conf/onwire.h"
#include "conf/onwire_xc.h"
#include "conf/preload.h"
#include "conf/conf_xcode.h"
#include "conf/obj.h"
#include "lib/memory.h"
#include "lib/arith.h"
#include "lib/misc.h"  /* C2_SET0 */
#include "lib/buf.h"
#include "lib/ut.h"
#include <stdlib.h>

#define DBPATH "./__confdb"

#define QUOTE(s) QUOTE_(s)
#define QUOTE_(s) #s

/* COLIBRI_CONFX_OBJ_CFG_XC comes from CFLAGS; see conf/ut/Makefile.am */
#define CONFX_CFG QUOTE(COLIBRI_CONFX_OBJ_CFG_XC)

struct xcode_test_rec {
	enum c2_conf_objtype type;
	void (*check)(const struct confx_object *conf);
};

static void profile_check(const struct confx_object *conf)
{
	/* parse_profile: ~id~ ~test-2~ */
	/* parse_profile: ~filesystem~ ~c2t1fs~ */

	const struct c2_buf id = C2_BUF_INITS("test-2");
	const struct c2_buf fs = C2_BUF_INITS("c2t1fs");

	C2_UT_ASSERT(c2_buf_eq(&conf->o_id, &id));

	C2_UT_ASSERT(conf->o_conf.u_type == C2_CO_PROFILE);
	C2_UT_ASSERT(c2_buf_eq(&conf->o_conf.u.u_profile.xp_filesystem, &fs));
}

static void filesystem_check(const struct confx_object *conf)
{
	/* parse_filesystem: ~id~ ~c2t1fs~ */
	/* parse_filesystem: ~rootfid~ ~[11, 22]~ */
	/* parse_filesystem: ~params~ ~["50","60","70"]~ */
	/* parse_filesystem: ~services~ ~["mds", "io"]~ */

	const struct c2_buf id = C2_BUF_INITS("c2t1fs");
	const struct confx_filesystem *xfs = &conf->o_conf.u.u_filesystem;

	C2_UT_ASSERT(c2_buf_eq(&conf->o_id, &id));

	C2_UT_ASSERT(conf->o_conf.u_type == C2_CO_FILESYSTEM);
	C2_UT_ASSERT(xfs->xf_rootfid.f_container == 11);
	C2_UT_ASSERT(xfs->xf_rootfid.f_key == 22);

	C2_UT_ASSERT(xfs->xf_params.ab_count == 3);
	C2_UT_ASSERT(c2_buf_eq(&xfs->xf_params.ab_elems[0],
			       &(const struct c2_buf) C2_BUF_INITS("50")));
	C2_UT_ASSERT(c2_buf_eq(&xfs->xf_params.ab_elems[1],
			       &(const struct c2_buf) C2_BUF_INITS("60")));
	C2_UT_ASSERT(c2_buf_eq(&xfs->xf_params.ab_elems[2],
			       &(const struct c2_buf) C2_BUF_INITS("70")));

	C2_UT_ASSERT(xfs->xf_services.ab_count == 2);
	C2_UT_ASSERT(c2_buf_eq(&xfs->xf_services.ab_elems[0],
			       &(const struct c2_buf) C2_BUF_INITS("mds")));
	C2_UT_ASSERT(c2_buf_eq(&xfs->xf_services.ab_elems[1],
			       &(const struct c2_buf) C2_BUF_INITS("io")));
}

static void service_check1(const struct confx_object *conf)
{
	/* parse_service: ~id~ ~mds~ */
	/* parse_service: ~svc_type~ ~1~ */
	/* parse_service: ~endpoints~ ~["addr0"]~ */
	/* parse_service: ~node~ ~N~ */

	const struct c2_buf id = C2_BUF_INITS("mds");
	const struct c2_buf node = C2_BUF_INITS("N");
	const struct confx_service *xsrv = &conf->o_conf.u.u_service;

	C2_UT_ASSERT(c2_buf_eq(&conf->o_id, &id));

	C2_UT_ASSERT(conf->o_conf.u_type == C2_CO_SERVICE);
	C2_UT_ASSERT(xsrv->xs_type == 1);

	C2_UT_ASSERT(xsrv->xs_endpoints.ab_count == 1);
	C2_UT_ASSERT(c2_buf_eq(&xsrv->xs_endpoints.ab_elems[0],
			       &(const struct c2_buf) C2_BUF_INITS("addr0")));

	C2_UT_ASSERT(c2_buf_eq(&xsrv->xs_node, &node));
}

static void service_check2(const struct confx_object *conf)
{
	/* parse_service: ~id~ ~io~ */
	/* parse_service: ~svc_type~ ~2~ */
	/* parse_service: ~endpoints~ ~["addr1","addr2","addr3"]~ */
	/* parse_service: ~node~ ~N~ */

	const struct c2_buf id = C2_BUF_INITS("io");
	const struct c2_buf node = C2_BUF_INITS("N");
	const struct confx_service *xsrv = &conf->o_conf.u.u_service;

	C2_UT_ASSERT(c2_buf_eq(&conf->o_id, &id));

	C2_UT_ASSERT(conf->o_conf.u_type == C2_CO_SERVICE);
	C2_UT_ASSERT(xsrv->xs_type == 2);

	C2_UT_ASSERT(xsrv->xs_endpoints.ab_count == 3);
	C2_UT_ASSERT(c2_buf_eq(&xsrv->xs_endpoints.ab_elems[0],
			       &(const struct c2_buf) C2_BUF_INITS("addr1")));
	C2_UT_ASSERT(c2_buf_eq(&xsrv->xs_endpoints.ab_elems[1],
			       &(const struct c2_buf) C2_BUF_INITS("addr2")));
	C2_UT_ASSERT(c2_buf_eq(&xsrv->xs_endpoints.ab_elems[2],
			       &(const struct c2_buf) C2_BUF_INITS("addr3")));

	C2_UT_ASSERT(c2_buf_eq(&xsrv->xs_node, &node));
}

static void node_check(const struct confx_object *conf)
{
	/* parse_node: ~id~ ~N~ */
	/* parse_node: ~memsize~ ~8000~ */
	/* parse_node: ~nr_cpu~ ~2~ */
	/* parse_node: ~last_state~ ~3~ */
	/* parse_node: ~flags~ ~2~ */
	/* parse_node: ~pool_id~ ~0~ */
	/* parse_node: ~nics~ ~["nic0"]~ */
	/* parse_node: ~sdevs~ ~["sdev0"]~ */

	const struct c2_buf id = C2_BUF_INITS("N");
	const struct confx_node *xnode = &conf->o_conf.u.u_node;

	C2_UT_ASSERT(c2_buf_eq(&conf->o_id, &id));

	C2_UT_ASSERT(conf->o_conf.u_type == C2_CO_NODE);
	C2_UT_ASSERT(xnode->xn_memsize == 8000);
	C2_UT_ASSERT(xnode->xn_nr_cpu == 2);
	C2_UT_ASSERT(xnode->xn_last_state == 3);
	C2_UT_ASSERT(xnode->xn_flags == 2);
	C2_UT_ASSERT(xnode->xn_pool_id == 0);

	C2_UT_ASSERT(xnode->xn_nics.ab_count == 1);
	C2_UT_ASSERT(c2_buf_eq(&xnode->xn_nics.ab_elems[0],
			       &(const struct c2_buf) C2_BUF_INITS("nic0")));

	C2_UT_ASSERT(xnode->xn_sdevs.ab_count == 1);
	C2_UT_ASSERT(c2_buf_eq(&xnode->xn_sdevs.ab_elems[0],
			       &(const struct c2_buf) C2_BUF_INITS("sdev0")));
}

static void nic_check(const struct confx_object *conf)
{
	/* parse_nic: ~id~ ~nic0~ */
	/* parse_nic: ~iface_type~ ~5~ */
	/* parse_nic: ~mtu~ ~8192~ */
	/* parse_nic: ~speed~ ~10000~ */
	/* parse_nic: ~filename~ ~ib0~ */
	/* parse_nic: ~last_state~ ~3~ */

	const struct c2_buf id = C2_BUF_INITS("nic0");
	const struct c2_buf fn = C2_BUF_INITS("ib0");

	C2_UT_ASSERT(c2_buf_eq(&conf->o_id, &id));

	C2_UT_ASSERT(conf->o_conf.u_type == C2_CO_NIC);

	C2_UT_ASSERT(conf->o_conf.u.u_nic.xi_iface == 5);
	C2_UT_ASSERT(conf->o_conf.u.u_nic.xi_mtu == 8192);
	C2_UT_ASSERT(conf->o_conf.u.u_nic.xi_speed == 10000);
	C2_UT_ASSERT(c2_buf_eq(&conf->o_conf.u.u_nic.xi_filename, &fn));
	C2_UT_ASSERT(conf->o_conf.u.u_nic.xi_last_state == 3);
}

static void sdev_check(const struct confx_object *conf)
{
	/* parse_sdev: ~id~ ~sdev0~ */
	/* parse_sdev: ~iface~ ~4~ */
	/* parse_sdev: ~media~ ~1~ */
	/* parse_sdev: ~size~ ~596000000000~ */
	/* parse_sdev: ~last_state~ ~3~ */
	/* parse_sdev: ~flags~ ~4~ */
	/* parse_sdev: ~partitions~ ~["part0"]~ */
	/* parse_sdev: ~filename~ ~/dev/sdev0~ */

	const struct c2_buf id = C2_BUF_INITS("sdev0");
	const struct c2_buf fn = C2_BUF_INITS("/dev/sdev0");
	const struct confx_sdev *xsd = &conf->o_conf.u.u_sdev;

	C2_UT_ASSERT(c2_buf_eq(&conf->o_id, &id));

	C2_UT_ASSERT(conf->o_conf.u_type == C2_CO_SDEV);
	C2_UT_ASSERT(xsd->xd_iface == 4);
	C2_UT_ASSERT(xsd->xd_media == 1);
	C2_UT_ASSERT(xsd->xd_size == 596000000000);
	C2_UT_ASSERT(xsd->xd_last_state == 3);
	C2_UT_ASSERT(xsd->xd_flags == 4);
	C2_UT_ASSERT(c2_buf_eq(&xsd->xd_filename, &fn));

	C2_UT_ASSERT(xsd->xd_partitions.ab_count == 1);
	C2_UT_ASSERT(c2_buf_eq(&xsd->xd_partitions.ab_elems[0],
			       &(const struct c2_buf) C2_BUF_INITS("part0")));
}

static void partition_check(const struct confx_object *conf)
{
	/* parse_partition: ~id~ ~part0~ */
	/* parse_partition: ~start~ ~0~ */
	/* parse_partition: ~size~ ~596000000000~ */
	/* parse_partition: ~index~ ~0~ */
	/* parse_partition: ~pa_type~ ~7~ */
	/* parse_partition: ~filename~ ~sda1~ */

	const struct c2_buf id = C2_BUF_INITS("part0");
	const struct c2_buf fn = C2_BUF_INITS("sda1");

	C2_UT_ASSERT(c2_buf_eq(&conf->o_id, &id));

	C2_UT_ASSERT(conf->o_conf.u_type == C2_CO_PARTITION);
	C2_UT_ASSERT(conf->o_conf.u.u_partition.xa_start == 0);
	C2_UT_ASSERT(conf->o_conf.u.u_partition.xa_size == 596000000000);
	C2_UT_ASSERT(conf->o_conf.u.u_partition.xa_index == 0);
	C2_UT_ASSERT(conf->o_conf.u.u_partition.xa_type == 7);
	C2_UT_ASSERT(c2_buf_eq(&conf->o_conf.u.u_partition.xa_file, &fn));
}

static void conf_xc_read(char *buf, size_t buf_size)
{
	FILE *f;
	int   n;

	f = fopen(CONFX_CFG, "r");
	C2_UT_ASSERT(f != NULL);

	n = fread(buf, 1, buf_size, f);
	C2_UT_ASSERT(n > 0);
	buf[n] = '\0';

	fclose(f);
}

static void cleanup(void)
{
	char command[256];
	int  rc;

	snprintf(command, ARRAY_SIZE(command),
		 "rm -rf %s; rm -f %s.errlog; rm -f %s.msglog",
		 DBPATH, DBPATH, DBPATH);
	rc = system(command);
	C2_UT_ASSERT(rc == 0);
}

void test_confx_xcode(void)
{
	enum { KB = 1 << 10 };
	char                      buf[32*KB] = {0};
	struct confx_object       objx[64];
	struct confx_object      *db_objx;
	struct confx_object       decoded;
	struct c2_conf_xcode_pair kv;
	int                       i;
	int                       n;
	int                       rc;
	struct xcode_test_rec     xcode_test[] = {
		{ C2_CO_PROFILE,    profile_check    },
		{ C2_CO_FILESYSTEM, filesystem_check },
		{ C2_CO_SERVICE,    service_check1   },
		{ C2_CO_SERVICE,    service_check2   },
		{ C2_CO_NODE,       node_check       },
		{ C2_CO_NIC,        nic_check        },
		{ C2_CO_SDEV,       sdev_check       },
		{ C2_CO_PARTITION,  partition_check  }
	};

	cleanup();
	conf_xc_read(buf, ARRAY_SIZE(buf));

	n = c2_confx_obj_nr(buf);
	C2_UT_ASSERT(n == 8);

	rc = c2_conf_parse(buf, objx, n - 1);
	C2_UT_ASSERT(rc == -ENOMEM);

	rc = c2_conf_parse(buf, objx, ARRAY_SIZE(objx));
	C2_UT_ASSERT(rc == n);

	/* encode/decode test */
	for (i = 0; i < n; ++i) {
		xcode_test[i].check(&objx[i]);

		rc = c2_confx_encode(&objx[i], &kv);
		C2_UT_ASSERT(rc == 0);

		decoded.o_conf.u_type = xcode_test[i].type;
		rc = c2_confx_decode(&kv, &decoded);
		C2_UT_ASSERT(rc == 0);
		xcode_test[i].check(&decoded);

		c2_free(kv.xp_val.b_addr);
		c2_confx_fini(&decoded, 1);
	}

	/* confdb prepartation/validation test */
	rc = c2_confx_db_create(DBPATH, objx, n);
	C2_UT_ASSERT(rc == 0);
	c2_confx_fini(objx, n);

	n = c2_confx_db_read(DBPATH, &db_objx);
	C2_UT_ASSERT(n == 8);

	/* The order of objects read from db can be different. The following
	   expression adjsuts xcode_test to match current entries order in db */
	C2_SWAP(xcode_test[2].check, xcode_test[3].check);

	for (i = 0; i < n; ++i)
		xcode_test[i].check(&db_objx[i]);

	c2_confx_fini(db_objx, n);
	c2_free(db_objx);
	cleanup();
}
