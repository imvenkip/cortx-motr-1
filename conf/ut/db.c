/* -*- c -*- */
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
 * Original author: Anatoliy Bilenko <anatoliy_bilenko@xyratex.com>
 * Original creation date: 26-Sep-2012
 */

#include <stdlib.h>        /* system */
#include "conf/db.h"       /* m0_confdb_create, m0_confdb_read */
#include "conf/onwire.h"   /* m0_confx_obj, m0_confx */
#include "conf/obj.h"      /* m0_conf_objtype */
#include "conf/preload.h"  /* m0_confstr_parse, m0_confx_free */
#include "conf/ut/file_helpers.h"
#include "lib/ut.h"

#define _CONFDB_PATH "_conf.db"
#define _BUF(str) (const struct m0_buf)M0_BUF_INITS(str)

/* ----------------------------------------------------------------
 * Source of configuration data: conf/ut/conf_xc.txt
 * ---------------------------------------------------------------- */

static void profile_check(const struct m0_confx_obj *xobj)
{
	M0_UT_ASSERT(xobj->o_conf.u_type == M0_CO_PROFILE);
	M0_UT_ASSERT(m0_buf_eq(&xobj->o_id, &_BUF("test-2")));

	M0_UT_ASSERT(m0_buf_eq(&xobj->o_conf.u.u_profile.xp_filesystem,
			       &_BUF("m0t1fs")));
}

static void filesystem_check(const struct m0_confx_obj *xobj)
{
	const struct m0_confx_filesystem *x = &xobj->o_conf.u.u_filesystem;

	M0_UT_ASSERT(xobj->o_conf.u_type == M0_CO_FILESYSTEM);
	M0_UT_ASSERT(m0_buf_eq(&xobj->o_id, &_BUF("m0t1fs")));

	M0_UT_ASSERT(x->xf_rootfid.f_container == 11);
	M0_UT_ASSERT(x->xf_rootfid.f_key == 22);

	M0_UT_ASSERT(x->xf_params.ab_count == 3);
	M0_UT_ASSERT(m0_buf_eq(&x->xf_params.ab_elems[0], &_BUF("50")));
	M0_UT_ASSERT(m0_buf_eq(&x->xf_params.ab_elems[1], &_BUF("60")));
	M0_UT_ASSERT(m0_buf_eq(&x->xf_params.ab_elems[2], &_BUF("70")));

	M0_UT_ASSERT(x->xf_services.ab_count == 2);
	M0_UT_ASSERT(m0_buf_eq(&x->xf_services.ab_elems[0], &_BUF("mds")));
	M0_UT_ASSERT(m0_buf_eq(&x->xf_services.ab_elems[1], &_BUF("io")));
}

static void md_service_check(const struct m0_confx_obj *xobj)
{
	const struct m0_confx_service *x = &xobj->o_conf.u.u_service;

	M0_UT_ASSERT(xobj->o_conf.u_type == M0_CO_SERVICE);
	M0_UT_ASSERT(m0_buf_eq(&xobj->o_id, &_BUF("mds")));

	M0_UT_ASSERT(x->xs_type == 1);
	M0_UT_ASSERT(x->xs_endpoints.ab_count == 1);
	M0_UT_ASSERT(m0_buf_eq(&x->xs_endpoints.ab_elems[0], &_BUF("addr0")));
	M0_UT_ASSERT(m0_buf_eq(&x->xs_node, &_BUF("N")));
}

static void io_service_check(const struct m0_confx_obj *xobj)
{
	const struct m0_confx_service *x = &xobj->o_conf.u.u_service;

	M0_UT_ASSERT(xobj->o_conf.u_type == M0_CO_SERVICE);
	M0_UT_ASSERT(m0_buf_eq(&xobj->o_id, &_BUF("io")));

	M0_UT_ASSERT(x->xs_type == 2);
	M0_UT_ASSERT(x->xs_endpoints.ab_count == 3);
	M0_UT_ASSERT(m0_buf_eq(&x->xs_endpoints.ab_elems[0], &_BUF("addr1")));
	M0_UT_ASSERT(m0_buf_eq(&x->xs_endpoints.ab_elems[1], &_BUF("addr2")));
	M0_UT_ASSERT(m0_buf_eq(&x->xs_endpoints.ab_elems[2], &_BUF("addr3")));
	M0_UT_ASSERT(m0_buf_eq(&x->xs_node, &_BUF("N")));
}

static void node_check(const struct m0_confx_obj *xobj)
{
	const struct m0_confx_node *x = &xobj->o_conf.u.u_node;

	M0_UT_ASSERT(xobj->o_conf.u_type == M0_CO_NODE);
	M0_UT_ASSERT(m0_buf_eq(&xobj->o_id, &_BUF("N")));

	M0_UT_ASSERT(x->xn_memsize == 8000);
	M0_UT_ASSERT(x->xn_nr_cpu == 2);
	M0_UT_ASSERT(x->xn_last_state == 3);
	M0_UT_ASSERT(x->xn_flags == 2);
	M0_UT_ASSERT(x->xn_pool_id == 0);

	M0_UT_ASSERT(x->xn_nics.ab_count == 1);
	M0_UT_ASSERT(m0_buf_eq(&x->xn_nics.ab_elems[0], &_BUF("nic0")));

	M0_UT_ASSERT(x->xn_sdevs.ab_count == 1);
	M0_UT_ASSERT(m0_buf_eq(&x->xn_sdevs.ab_elems[0], &_BUF("sdev0")));
}

static void nic_check(const struct m0_confx_obj *xobj)
{
	const struct m0_confx_nic *x = &xobj->o_conf.u.u_nic;

	M0_UT_ASSERT(xobj->o_conf.u_type == M0_CO_NIC);
	M0_UT_ASSERT(m0_buf_eq(&xobj->o_id, &_BUF("nic0")));

	M0_UT_ASSERT(x->xi_iface == 5);
	M0_UT_ASSERT(x->xi_mtu == 8192);
	M0_UT_ASSERT(x->xi_speed == 10000);
	M0_UT_ASSERT(m0_buf_eq(&x->xi_filename, &_BUF("ib0")));
	M0_UT_ASSERT(x->xi_last_state == 3);
}

static void sdev_check(const struct m0_confx_obj *xobj)
{
	const struct m0_confx_sdev *x = &xobj->o_conf.u.u_sdev;

	M0_UT_ASSERT(xobj->o_conf.u_type == M0_CO_SDEV);
	M0_UT_ASSERT(m0_buf_eq(&xobj->o_id, &_BUF("sdev0")));

	M0_UT_ASSERT(x->xd_iface == 4);
	M0_UT_ASSERT(x->xd_media == 1);
	M0_UT_ASSERT(x->xd_size == 596000000000);
	M0_UT_ASSERT(x->xd_last_state == 3);
	M0_UT_ASSERT(x->xd_flags == 4);
	M0_UT_ASSERT(m0_buf_eq(&x->xd_filename, &_BUF("/dev/sdev0")));
	M0_UT_ASSERT(x->xd_partitions.ab_count == 1);
	M0_UT_ASSERT(m0_buf_eq(&x->xd_partitions.ab_elems[0], &_BUF("part0")));
}

static void partition_check(const struct m0_confx_obj *xobj)
{
	const struct m0_confx_partition *x = &xobj->o_conf.u.u_partition;

	M0_UT_ASSERT(xobj->o_conf.u_type == M0_CO_PARTITION);
	M0_UT_ASSERT(m0_buf_eq(&xobj->o_id, &_BUF("part0")));

	M0_UT_ASSERT(x->xa_start == 0);
	M0_UT_ASSERT(x->xa_size == 596000000000);
	M0_UT_ASSERT(x->xa_index == 0);
	M0_UT_ASSERT(x->xa_type == 7);
	M0_UT_ASSERT(m0_buf_eq(&x->xa_file, &_BUF("/dev/sda1")));
}

static void cleanup(void)
{
	int rc = system("rm -rf " _CONFDB_PATH) ?:
		system("rm -f " _CONFDB_PATH ".errlog") ?:
		system("rm -f " _CONFDB_PATH ".msglog");
	M0_UT_ASSERT(rc == 0);
}

void test_confdb(void)
{
	struct m0_confx *enc;
	int              i;
	int              rc;
	char             buf[1024] = {0};
	struct {
		enum m0_conf_objtype type;
		void               (*check)(const struct m0_confx_obj *xobj);
	} tests[] = {
		{ M0_CO_PROFILE,    profile_check     },
		{ M0_CO_FILESYSTEM, filesystem_check  },
		{ M0_CO_SERVICE,    io_service_check  },
		{ M0_CO_SERVICE,    md_service_check  },
		{ M0_CO_NODE,       node_check        },
		{ M0_CO_NIC,        nic_check         },
		{ M0_CO_SDEV,       sdev_check        },
		{ M0_CO_PARTITION,  partition_check   }
	};

	cleanup();

	rc = m0_ut_file_read(M0_CONF_UT_PATH("conf_xc.txt"), buf, sizeof buf);
	M0_UT_ASSERT(rc == 0);

	rc = m0_confstr_parse("[0]", &enc);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(enc->cx_nr == 0);
	m0_confx_free(enc);

	rc = m0_confstr_parse(buf, &enc);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(enc->cx_nr == 8);

	rc = m0_confdb_create(_CONFDB_PATH, enc);
	M0_UT_ASSERT(rc == 0);
	m0_confx_free(enc);

	rc = m0_confdb_read(_CONFDB_PATH, &enc);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(enc->cx_nr == ARRAY_SIZE(tests));
	for (i = 0; i < ARRAY_SIZE(tests); ++i)
		tests[i].check(&enc->cx_objs[i]);
	m0_confx_free(enc);

	cleanup();
}

#undef _BUF
#undef _CONFDB_PATH
