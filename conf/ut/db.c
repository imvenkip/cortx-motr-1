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

#include "lib/finject.h"
#include "conf/obj.h"
#include "conf/db.h"       /* m0_confdb_create, m0_confdb_read */
#include "conf/onwire.h"   /* m0_confx_obj, m0_confx */
#include "conf/preload.h"  /* m0_confstr_parse, m0_confx_free */
#include "conf/ut/file_helpers.h"
#include "reqh/reqh.h"
#include "ut/ut.h"
#include "be/ut/helper.h"
#include "ut/be.h"	   /* m0_be_ut__seg_dict_create */

#define _BUF(str) M0_BUF_INITS(str)

static struct m0_be_ut_backend ut_be;
static struct m0_be_ut_seg     ut_seg;
static struct m0_be_seg       *seg;

/* ----------------------------------------------------------------
 * Source of configuration data: conf/ut/conf_xc.txt
 *
 * fid table:
 *
 * profile (test-2) (1, 0)
 * filesystem (m0t1fs) (1, 1)
 * service (mds)       (1, 2)
 * service (ios)       (1, 3)
 * node     (N)        (1, 4)
 * nic     (nic0)      (1, 5)
 * sdev    (sdev0)     (1, 6)
 *
 * ---------------------------------------------------------------- */

enum {
	PROFILE,
	FILESYSTEM,
	MDS,
	IOS,
	N,
	NIC0,
	SDEV0,

	NR
};

static const struct m0_fid fids[NR] = {
	[PROFILE]    = M0_FID_TINIT('p', 1, 0),
	[FILESYSTEM] = M0_FID_TINIT('f', 1, 1),
	[MDS]        = M0_FID_TINIT('s', 1, 2),
	[IOS]        = M0_FID_TINIT('s', 1, 3),
	[N]          = M0_FID_TINIT('n', 1, 4),
	[NIC0]       = M0_FID_TINIT('i', 1, 5),
	[SDEV0]      = M0_FID_TINIT('d', 1, 6),
};

#define XCAST(xobj, type) ((struct type *)(&(xobj)->xo_u))

static void profile_check(const struct m0_confx_obj *xobj)
{
	M0_UT_ASSERT(m0_conf_fid_type(m0_conf_objx_fid(xobj)) ==
		     &M0_CONF_PROFILE_TYPE);
	M0_UT_ASSERT(m0_fid_eq(m0_conf_objx_fid(xobj), &fids[PROFILE]));

	M0_UT_ASSERT(m0_fid_eq(&XCAST(xobj, m0_confx_profile)->xp_filesystem,
			       &fids[FILESYSTEM]));
}

static void filesystem_check(const struct m0_confx_obj *xobj)
{
	const struct m0_confx_filesystem *x = XCAST(xobj, m0_confx_filesystem);

	M0_UT_ASSERT(m0_conf_fid_type(m0_conf_objx_fid(xobj)) ==
				      &M0_CONF_FILESYSTEM_TYPE);
	M0_UT_ASSERT(m0_fid_eq(m0_conf_objx_fid(xobj), &fids[FILESYSTEM]));

	M0_UT_ASSERT(x->xf_rootfid.f_container == 11);
	M0_UT_ASSERT(x->xf_rootfid.f_key == 22);

	M0_UT_ASSERT(x->xf_params.ab_count == 3);
	M0_UT_ASSERT(m0_buf_eq(&x->xf_params.ab_elems[0], &_BUF("50")));
	M0_UT_ASSERT(m0_buf_eq(&x->xf_params.ab_elems[1], &_BUF("60")));
	M0_UT_ASSERT(m0_buf_eq(&x->xf_params.ab_elems[2], &_BUF("70")));

	M0_UT_ASSERT(x->xf_services.af_count == 2);
	M0_UT_ASSERT(m0_fid_eq(&x->xf_services.af_elems[0], &fids[MDS]));
	M0_UT_ASSERT(m0_fid_eq(&x->xf_services.af_elems[1], &fids[IOS]));
}

static void md_service_check(const struct m0_confx_obj *xobj)
{
	const struct m0_confx_service *x = XCAST(xobj, m0_confx_service);

	M0_UT_ASSERT(m0_conf_fid_type(m0_conf_objx_fid(xobj)) ==
		     &M0_CONF_SERVICE_TYPE);
	M0_UT_ASSERT(m0_fid_eq(m0_conf_objx_fid(xobj), &fids[MDS]));

	M0_UT_ASSERT(x->xs_type == 1);
	M0_UT_ASSERT(x->xs_endpoints.ab_count == 1);
	M0_UT_ASSERT(m0_buf_eq(&x->xs_endpoints.ab_elems[0], &_BUF("addr0")));
	M0_UT_ASSERT(m0_fid_eq(&x->xs_node, &fids[N]));
}

static void io_service_check(const struct m0_confx_obj *xobj)
{
	const struct m0_confx_service *x = XCAST(xobj, m0_confx_service);

	M0_UT_ASSERT(m0_conf_fid_type(m0_conf_objx_fid(xobj)) == &M0_CONF_SERVICE_TYPE);
	M0_UT_ASSERT(m0_fid_eq(m0_conf_objx_fid(xobj), &fids[IOS]));

	M0_UT_ASSERT(x->xs_type == 2);
	M0_UT_ASSERT(x->xs_endpoints.ab_count == 3);
	M0_UT_ASSERT(m0_buf_eq(&x->xs_endpoints.ab_elems[0], &_BUF("addr1")));
	M0_UT_ASSERT(m0_buf_eq(&x->xs_endpoints.ab_elems[1], &_BUF("addr2")));
	M0_UT_ASSERT(m0_buf_eq(&x->xs_endpoints.ab_elems[2], &_BUF("addr3")));
	M0_UT_ASSERT(m0_fid_eq(&x->xs_node, &fids[N]));
}

static void node_check(const struct m0_confx_obj *xobj)
{
	const struct m0_confx_node *x = XCAST(xobj, m0_confx_node);

	M0_UT_ASSERT(m0_conf_fid_type(m0_conf_objx_fid(xobj)) ==
		     &M0_CONF_NODE_TYPE);
	M0_UT_ASSERT(m0_fid_eq(m0_conf_objx_fid(xobj), &fids[N]));

	M0_UT_ASSERT(x->xn_memsize == 8000);
	M0_UT_ASSERT(x->xn_nr_cpu == 2);
	M0_UT_ASSERT(x->xn_last_state == 3);
	M0_UT_ASSERT(x->xn_flags == 2);
	M0_UT_ASSERT(x->xn_pool_id == 0);

	M0_UT_ASSERT(x->xn_nics.af_count == 1);
	M0_UT_ASSERT(m0_fid_eq(&x->xn_nics.af_elems[0], &fids[NIC0]));

	M0_UT_ASSERT(x->xn_sdevs.af_count == 1);
	M0_UT_ASSERT(m0_fid_eq(&x->xn_sdevs.af_elems[0], &fids[SDEV0]));
}

static void nic_check(const struct m0_confx_obj *xobj)
{
	const struct m0_confx_nic *x = XCAST(xobj, m0_confx_nic);

	M0_UT_ASSERT(m0_conf_fid_type(m0_conf_objx_fid(xobj)) ==
		     &M0_CONF_NIC_TYPE);
	M0_UT_ASSERT(m0_fid_eq(m0_conf_objx_fid(xobj), &fids[NIC0]));

	M0_UT_ASSERT(x->xi_iface == 5);
	M0_UT_ASSERT(x->xi_mtu == 8192);
	M0_UT_ASSERT(x->xi_speed == 10000);
	M0_UT_ASSERT(m0_buf_eq(&x->xi_filename, &_BUF("ib0")));
	M0_UT_ASSERT(x->xi_last_state == 3);
}

static void sdev_check(const struct m0_confx_obj *xobj)
{
	const struct m0_confx_sdev *x = XCAST(xobj, m0_confx_sdev);

	M0_UT_ASSERT(m0_conf_fid_type(m0_conf_objx_fid(xobj)) ==
		     &M0_CONF_SDEV_TYPE);
	M0_UT_ASSERT(m0_fid_eq(m0_conf_objx_fid(xobj), &fids[SDEV0]));

	M0_UT_ASSERT(x->xd_iface == 4);
	M0_UT_ASSERT(x->xd_media == 1);
	M0_UT_ASSERT(x->xd_size == 596000000000);
	M0_UT_ASSERT(x->xd_last_state == 3);
	M0_UT_ASSERT(x->xd_flags == 4);
	M0_UT_ASSERT(m0_buf_eq(&x->xd_filename, &_BUF("/dev/sdev0")));
}

static void conf_ut_db_init()
{
	m0_be_ut_backend_init(&ut_be);
	m0_be_ut_seg_init(&ut_seg, &ut_be, 1ULL << 24);
	seg = ut_seg.bus_seg;
}

static void conf_ut_db_fini()
{
	/*
	 * XXX: Call m0_ut_backend_fini_with_reqh() after
	 *      fixing m0_confdb_destroy().
	 */
	m0_be_ut_seg_fini(&ut_seg);
        m0_be_ut_backend_fini(&ut_be);
}

static int conf_ut_be_tx_create(struct m0_be_tx *tx,
				struct m0_be_ut_backend *ut_be,
				struct m0_be_tx_credit *accum)
{
        m0_be_ut_tx_init(tx, ut_be);
        m0_be_tx_prep(tx, accum);
        return m0_be_tx_open_sync(tx);
}

static void conf_ut_be_tx_fini(struct m0_be_tx *tx)
{
        m0_be_tx_close_sync(tx);
        m0_be_tx_fini(tx);
}

void test_confdb(void)
{
	struct m0_confx        *enc;
	struct m0_confx        *dec;
	struct m0_be_tx_credit  accum = {};
	struct m0_be_tx         tx = {};
	int                     i;
	int                     j;
	int                     hit;
	int                     rc;
	char                    buf[4096] = {0};
	struct {
		const struct m0_fid *fid;
		void (*check)(const struct m0_confx_obj *xobj);
	} tests[] = {
		{ &fids[PROFILE],    &profile_check     },
		{ &fids[FILESYSTEM], &filesystem_check  },
		{ &fids[MDS],        &md_service_check  },
		{ &fids[IOS],        &io_service_check  },
		{ &fids[N],          &node_check        },
		{ &fids[NIC0],       &nic_check         },
		{ &fids[SDEV0],      &sdev_check        }
	};

	rc = m0_ut_file_read(M0_CONF_UT_PATH("conf_xc.txt"), buf, sizeof buf);
	M0_UT_ASSERT(rc == 0);

	rc = m0_confstr_parse("[0]", &enc);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(enc->cx_nr == 0);
	m0_confx_free(enc);

	rc = m0_confstr_parse(buf, &enc);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(enc->cx_nr == 7);

	conf_ut_db_init();

	rc = m0_confdb_create_credit(seg, enc, &accum);
	M0_UT_ASSERT(rc == 0);
	rc = conf_ut_be_tx_create(&tx, &ut_be, &accum);
	M0_UT_ASSERT(rc == 0);

	m0_fi_enable("confdb_table_init", "ut_confdb_create_failure");
	rc = m0_confdb_create(seg, &tx, enc);
	M0_UT_ASSERT(rc < 0);
	m0_fi_disable("confdb_table_init", "ut_confdb_create_failure");

	m0_fi_enable("confx_obj_dup", "ut_confx_obj_dup_failure");
	rc = m0_confdb_create(seg, &tx, enc);
	M0_UT_ASSERT(rc < 0);
	m0_fi_disable("confx_obj_dup", "ut_confx_obj_dup_failure");

	rc = m0_confdb_create(seg, &tx, enc);
	M0_UT_ASSERT(rc == 0);
	conf_ut_be_tx_fini(&tx);

	rc = m0_confdb_read(seg, &dec);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(enc->cx_nr == ARRAY_SIZE(tests));
	/*
	 * @dec can be re-ordered w.r.t. to @enc.
	 */
	for (hit = 0, i = 0; i < dec->cx_nr; ++i) {
		struct m0_confx_obj *o = M0_CONFX_AT(dec, i);

		for (j = 0; j < ARRAY_SIZE(tests); ++j) {
			if (m0_fid_eq(m0_conf_objx_fid(o), tests[j].fid)) {
				tests[j].check(o);
				hit++;
			}
		}
	}
	M0_UT_ASSERT(hit == ARRAY_SIZE(tests));
	m0_confx_free(enc);
	m0_confdb_fini(seg);
	M0_SET0(&accum);
	rc = m0_confdb_destroy_credit(seg, &accum);
	M0_UT_ASSERT(rc == 0);
	rc = conf_ut_be_tx_create(&tx, &ut_be, &accum);
	M0_UT_ASSERT(rc == 0);
	rc = m0_confdb_destroy(seg, &tx);
        M0_UT_ASSERT(rc == 0);
	conf_ut_be_tx_fini(&tx);
	conf_ut_db_fini();
}

#undef _BUF
