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

#include "lib/ut.h"
#include "lib/ub.h"
#include "lib/memory.h"
#include "lib/misc.h"              /* M0_SET0 */
#include "lib/bitstring.h"
#include "lib/processor.h"
#include "fop/fop.h"
#include "cob/cob.h"
#include "reqh/reqh.h"
#include "mdstore/mdstore.h"
#include "net/net.h"

#include "mdservice/ut/mdstore.h"
#include "mdservice/ut/lustre.h"

static const char db_name[] = "ut-mdservice";
static struct m0_cob_domain_id id = { 42 };

static struct m0_dbenv          db;
static struct m0_mdstore        md;
static struct m0_reqh           reqh;
static struct m0_local_service  svc;
static struct m0_fol            fol;
static int                      rc;

int m0_md_lustre_fop_alloc(struct m0_fop **fop, void *data);

static struct m0_chan test_signal;
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
        m0_clink_add(&test_signal, &clink);

        while (locked)
                m0_chan_wait(&clink);

        m0_clink_del(&clink);
        m0_clink_fini(&clink);
        locked = 1;
}

static void fom_fini(struct m0_local_service *service, struct m0_fom *fom)
{
        if (error == 0)
                error = m0_fom_rc(fom);
        m0_fop_free(fom->fo_fop);
        m0_fop_free(fom->fo_rep_fop);
        signal_locked();
}

const struct m0_local_service_ops svc_ops = {
        .lso_fini = fom_fini
};

static int db_reset(void)
{
        rc = m0_ut_db_reset(db_name);
        M0_ASSERT(rc == 0);
        return rc;
}

static void test_mkfs(void)
{
        struct m0_md_lustre_fid testroot;
        struct m0_fid           rootfid;
        struct m0_db_tx         tx;
        int                     rc;
        int                     fd;

        fd = open(M0_MDSTORE_OPS_DUMP_PATH, O_RDONLY);
        M0_ASSERT(fd > 0);

        rc = read(fd, &testroot, sizeof(testroot));
        M0_ASSERT(rc == sizeof(testroot));
        close(fd);

        rc = m0_dbenv_init(&db, db_name, 0);
        M0_UT_ASSERT(rc == 0);

        rc = m0_mdstore_init(&md, &id, &db, 0);
        M0_UT_ASSERT(rc == 0);

        rc = m0_db_tx_init(&tx, &db, 0);
        M0_UT_ASSERT(rc == 0);

        /* Create root and other structures */
        m0_fid_set(&rootfid, testroot.f_seq, testroot.f_oid);
        rc = m0_cob_domain_mkfs(&md.md_dom, (const struct m0_fid *)&rootfid, &M0_COB_SESSIONS_FID, &tx);
        M0_UT_ASSERT(rc == 0);
        m0_db_tx_commit(&tx);

        /* Fini old mdstore */
        m0_mdstore_fini(&md);

        /* Init mdstore with root init flag set to 1 */
        rc = m0_mdstore_init(&md, &id, &db, 1);
        M0_UT_ASSERT(rc == 0);

        /* Fini everything */
        m0_mdstore_fini(&md);
        m0_dbenv_fini(&db);
}

static void test_init(void)
{
        m0_chan_init(&test_signal);

        rc = m0_dbenv_init(&db, db_name, 0);
        M0_ASSERT(rc == 0);

        rc = m0_fol_init(&fol, &db);
        M0_ASSERT(rc == 0);

        rc = m0_mdstore_init(&md, &id, &db, 1);
        M0_ASSERT(rc == 0);

        M0_SET0(&svc);
        svc.s_ops = &svc_ops;

        rc = m0_reqh_init(&reqh, NULL, &db, &md, &fol, &svc);
        M0_ASSERT(rc == 0);
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
                 * All fops should be sent in order they stored in dump. This is why we wait here
                 * for locked == 0, which is set in ->lso_fini()
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

static void test_fini(void)
{
        m0_reqh_shutdown_wait(&reqh);
        m0_reqh_fini(&reqh);
        m0_fol_fini(&fol);
        m0_mdstore_fini(&md);
        m0_dbenv_fini(&db);
        m0_chan_fini(&test_signal);
}

const struct m0_test_suite mdservice_ut = {
        .ts_name = "mdservice-ut",
        .ts_init = db_reset,
        /* .ts_fini = db_reset, */
        .ts_tests = {
                { "mdservice-mkfs", test_mkfs },
                { "mdservice-init", test_init },
                { "mdservice-ops", test_mdops },
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
