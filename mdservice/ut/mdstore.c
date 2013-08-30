/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Yuriy Umanets <yuriy_umanets@xyratex.com>
 * Original creation date: 04/19/2011
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "lib/ub.h"
#include "lib/memory.h"
#include "lib/misc.h"              /* M0_SET0 */
#include "lib/bitstring.h"
#include "lib/processor.h"
#include "fop/fop.h"
#include "fop/ut/fop_put_norpc.h"
#include "cob/cob.h"
#include "reqh/reqh.h"
#include "mdstore/mdstore.h"
#include "net/net.h"
#include "ut/ut.h"
#include "ut/be.h"
#include "be/ut/helper.h"

#include "mdservice/ut/mdstore.h"
#include "mdservice/ut/lustre.h"

static const char db_name[] = "ut-mdservice";
static struct m0_cob_domain_id id = { 42 };

static struct m0_sm_group	*grp;
static struct m0_be_ut_backend	 ut_be;
static struct m0_be_ut_seg	 ut_seg;
static struct m0_be_seg		*be_seg;

static struct m0_mdstore        md;
static struct m0_reqh           reqh;
static struct m0_local_service  svc;
static struct m0_fol           *fol;

int m0_md_lustre_fop_alloc(struct m0_fop **fop, void *data);

static struct m0_chan test_signal;
static struct m0_mutex mutex;
static struct m0_clink clink;
static int locked = 0;
static int error = 0;

static void signal_locked()
{
        locked = 0;
        m0_clink_signal(&clink);
}

static void wait_locked()
{
        m0_clink_init(&clink, NULL);
        m0_clink_add_lock(&test_signal, &clink);

        while (locked)
                m0_chan_wait(&clink);

        m0_clink_del_lock(&clink);
        m0_clink_fini(&clink);
        locked = 1;
}

static void fom_fini(struct m0_local_service *service, struct m0_fom *fom)
{
        if (error == 0)
                error = m0_fom_rc(fom);

	fom_fop_put_norpc(fom);

        signal_locked();
}

const struct m0_local_service_ops svc_ops = {
        .lso_fini = fom_fini
};

static int db_reset(void)
{
        int rc = m0_ut_db_reset(db_name);
        M0_ASSERT(rc == 0);
        return rc;
}

static void test_mkfs(void)
{
        struct m0_md_lustre_fid	 testroot;
        struct m0_fid		 rootfid;
        struct m0_be_tx		 tx;
        struct m0_be_tx_credit	 cred = {};
        int			 fd;
	int			 rc;

	/* Init BE */
	m0_be_ut_backend_init(&ut_be);
	m0_be_ut_seg_init(&ut_seg, &ut_be, 1ULL << 24);
	m0_be_ut_seg_allocator_init(&ut_seg, &ut_be);
	be_seg = &ut_seg.bus_seg;
	grp = m0_be_ut_backend_sm_group_lookup(&ut_be);
	rc = m0_be_seg_dict_create(be_seg, grp);
	M0_ASSERT(rc == 0);

        fd = open(M0_MDSTORE_OPS_DUMP_PATH, O_RDONLY);
        M0_ASSERT(fd > 0);

        rc = read(fd, &testroot, sizeof(testroot));
        M0_ASSERT(rc == sizeof(testroot));
        close(fd);

        rc = m0_mdstore_init(&md, &id, be_seg, false);
        M0_UT_ASSERT(rc == -ENOENT);
        rc = m0_mdstore_create(&md, grp);
        M0_UT_ASSERT(rc == 0);

        /* Create root and other structures */
	m0_cob_tx_credit(&md.md_dom, M0_COB_OP_DOMAIN_MKFS, &cred);
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
        m0_fid_set(&rootfid, testroot.f_seq, testroot.f_oid);
        rc = m0_cob_domain_mkfs(&md.md_dom, &rootfid,
				&M0_COB_SESSIONS_FID, &tx);
        M0_UT_ASSERT(rc == 0);
	m0_ut_be_tx_end(&tx);

        /* Fini old mdstore */
        m0_mdstore_fini(&md);

        /* Init mdstore with root init flag set to 1 */
        rc = m0_mdstore_init(&md, &id, be_seg, true);
        M0_UT_ASSERT(rc == 0);

        /* Fini everything */
        m0_mdstore_fini(&md);
}

static void test_init(void)
{
        struct m0_be_tx		 tx;
        struct m0_be_tx_credit	 cred = {};
	int			 rc;

	m0_mutex_init(&mutex);
        m0_chan_init(&test_signal, &mutex);

	fol = m0_ut_be_alloc(sizeof *fol, be_seg, &ut_be);
	M0_UT_ASSERT(fol != NULL);

        m0_fol_init(fol, be_seg);
	m0_fol_credit(fol, M0_FO_CREATE, 1, &cred);
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	M0_BE_OP_SYNC(op, rc = m0_fol_create(fol, &tx, &op));
	M0_UT_ASSERT(rc == 0);
	m0_ut_be_tx_end(&tx);

        rc = m0_mdstore_init(&md, &id, be_seg, true);
        M0_ASSERT(rc == 0);

        M0_SET0(&svc);
        svc.s_ops = &svc_ops;

	rc = M0_REQH_INIT(&reqh,
		          .rhia_dtm       = NULL,
		          .rhia_db        = be_seg,
		          .rhia_mdstore   = &md,
		          .rhia_fol       = fol,
		          .rhia_svc       = &svc);
        M0_ASSERT(rc == 0);
	m0_reqh_start(&reqh);
}

static void test_fini(void)
{
	struct m0_be_tx		 tx;
	struct m0_be_tx_credit	 cred = {};
	int			 rc;

	m0_reqh_shutdown_wait(&reqh);
	m0_reqh_services_terminate(&reqh);
	m0_reqh_fini(&reqh);

	m0_fol_credit(fol, M0_FO_DESTROY, 1, &cred);
	m0_ut_be_tx_begin(&tx, &ut_be, &cred);
	M0_BE_OP_SYNC(op, m0_fol_destroy(fol, &tx, &op));
	m0_ut_be_tx_end(&tx);
	m0_fol_fini(fol);
	m0_ut_be_free(fol, sizeof *fol, be_seg, &ut_be);

	rc = m0_mdstore_destroy(&md, grp);
	M0_UT_ASSERT(rc == 0);
	m0_mdstore_fini(&md);

	m0_chan_fini_lock(&test_signal);
	m0_mutex_fini(&mutex);

	rc = m0_be_seg_dict_destroy(be_seg, grp);
	M0_ASSERT(rc == 0);
	m0_be_ut_seg_allocator_fini(&ut_seg, &ut_be);
	m0_be_ut_seg_fini(&ut_seg);
	m0_be_ut_backend_fini(&ut_be);
}

static void test_mdops(void)
{
        struct m0_md_lustre_logrec *rec;
        struct m0_md_lustre_fid root;
        int fd, result, size;
        struct m0_fop *fop;

        fd = open(M0_MDSTORE_OPS_DUMP_PATH, O_RDONLY);
        M0_ASSERT(fd > 0);

        result = read(fd, &root, sizeof(root));
        M0_ASSERT(result == sizeof(root));
        error = 0;

        while (1) {
                fop = NULL;

                /**
                 * All fops should be sent in order they stored in dump.
                 * This is why we wait here for locked == 0, which is set
                 * in ->lso_fini().
                 */
                wait_locked();
again:
                result = read(fd, &size, sizeof(size));
                if (result < sizeof(size)) {
                        signal_locked();
                        break;
                }

                rec = m0_alloc(size);
                M0_ASSERT(rec != NULL);

                result = read(fd, rec, size);
                M0_ASSERT(result == size);

                result = m0_md_lustre_fop_alloc(&fop, rec);
                m0_free(rec);

                if (result == -EOPNOTSUPP) {
                        signal_locked();
                        continue;
                }

                if (result == -EAGAIN) {
                        /*
                         * Let's get second part of rename fop.
                         */
                        goto again;
                }

                M0_ASSERT(result == 0);
                m0_reqh_fop_handle(&reqh, fop);
		m0_fop_put(fop);
        }
        close(fd);

        /* Make sure that all fops are handled. */
        wait_locked();
        M0_ASSERT(error == 0);
}

const struct m0_test_suite mdservice_ut = {
        .ts_name = "mdservice-ut",
        .ts_init = db_reset,
        /* .ts_fini = db_reset, */
        .ts_tests = {
                { "mdservice-mkfs", test_mkfs },
                { "mdservice-init", test_init },
                { "mdservice-ops",  test_mdops },
                { "mdservice-fini", test_fini },
                { NULL, NULL }
        }
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
