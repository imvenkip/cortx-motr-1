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
#include "lib/misc.h"              /* C2_SET0 */
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
static struct c2_cob_domain_id id = { 42 };

static struct c2_dbenv          db;
static struct c2_md_store       md;
static struct c2_reqh           reqh;
static struct c2_local_service  svc;
static struct c2_fol            fol;
static int                      rc;

int c2_md_lustre_fop_alloc(struct c2_fop **fop, void *data);
void c2_md_lustre_fop_free(struct c2_fop *fop);

static int locked = 0;
static int error = 0;

static void fom_fini(struct c2_local_service *service, struct c2_fom *fom)
{
        struct c2_fop_ctx *ctx = fom->fo_fop_ctx;

        if (ctx->fc_cookie) {
	        struct c2_fop **ret = ctx->fc_cookie;
	        *ret = fom->fo_rep_fop;
	}
	if (error == 0)
	        error = fom->fo_rc;
	locked = 0;
}

const struct c2_local_service_ops svc_ops = {
	.lso_fini = fom_fini
};

static int db_reset(void)
{
        rc = c2_ut_db_reset(db_name);
        C2_ASSERT(rc == 0);
        return rc;
}

static void test_mkfs(void)
{
        struct c2_md_lustre_fid testroot;
        struct c2_fid           rootfid;
        struct c2_db_tx         tx;
        int                     rc;
        int                     fd;

        fd = open(C2_MDSTORE_OPS_DUMP_PATH, O_RDONLY);
        C2_ASSERT(fd > 0);
        
        rc = read(fd, &testroot, sizeof(testroot));
        C2_ASSERT(rc == sizeof(testroot));
        close(fd);

	rc = c2_dbenv_init(&db, db_name, 0);
	C2_UT_ASSERT(rc == 0);

        rc = c2_md_store_init(&md, &id, &db, 0);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_init(&tx, &db, 0);
	C2_UT_ASSERT(rc == 0);

        /* Create root and other structures */
        rootfid.f_container = testroot.f_seq;
        rootfid.f_key = testroot.f_oid;
        rc = c2_cob_domain_mkfs(&md.md_dom, &rootfid, &C2_COB_SESSIONS_FID, &tx);
        C2_UT_ASSERT(rc == 0);
	c2_db_tx_commit(&tx);

        /* Fini old mdstore */
        c2_md_store_fini(&md);

        /* Init mdstore with root init flag set to 1 */
        rc = c2_md_store_init(&md, &id, &db, 1);
	C2_UT_ASSERT(rc == 0);

        /* Fini everything */
        c2_md_store_fini(&md);
        c2_dbenv_fini(&db);
}

static void test_init(void)
{
	rc = c2_dbenv_init(&db, db_name, 0);
	C2_ASSERT(rc == 0);

	rc = c2_fol_init(&fol, &db);
	C2_ASSERT(rc == 0);

        rc = c2_md_store_init(&md, &id, &db, 1);
	C2_ASSERT(rc == 0);
        
        C2_SET0(&svc);
	svc.s_ops = &svc_ops;

        rc = c2_reqh_init(&reqh, NULL, &db, &md, &fol, &svc);
	C2_ASSERT(rc == 0);
}

enum {
	WAIT_FOR_REQH_SHUTDOWN = 1000000,
};

static void test_mdops(void)
{
        struct c2_md_lustre_logrec *rec;
        struct c2_md_lustre_fid root;
        int fd, result, size;
        struct c2_fop *fop;
	c2_time_t rdelay;
        
        fd = open(C2_MDSTORE_OPS_DUMP_PATH, O_RDONLY);
        C2_ASSERT(fd > 0);
        
        result = read(fd, &root, sizeof(root));
        C2_ASSERT(result == sizeof(root));
        error = 0;
        
        while (1) {
                fop = NULL;
	        
                /** 
                   All fops should be sent in order they stored in dump. This is why we wait here
                   for locked == 0, which is set in ->lso_fini()
                 */
	        while (locked) {
		        c2_nanosleep(c2_time_set(&rdelay, 0,
			             WAIT_FOR_REQH_SHUTDOWN * 1), NULL);
	        }
again:
                result = read(fd, &size, sizeof(size));
                if (result < sizeof(size))
                        break;

                rec = c2_alloc(size);
                C2_ASSERT(rec != NULL);

                result = read(fd, rec, size);
                C2_ASSERT(result == size);

                result = c2_md_lustre_fop_alloc(&fop, rec);
                c2_free(rec);

                if (result == -EOPNOTSUPP)
                        continue;
                
                if (result == -EAGAIN) {
                        /*
                         * Let's get second part of rename fop.
                         */
                        goto again;
                }
                        
	        locked = 1;
                C2_ASSERT(result == 0);
                c2_reqh_fop_handle(&reqh, fop, NULL);
        }
        close(fd);

        /* Make sure that all fops are handled. */
	while (locked) {
		c2_nanosleep(c2_time_set(&rdelay, 0,
			     WAIT_FOR_REQH_SHUTDOWN * 1), NULL);
	}
        C2_ASSERT(error == 0);
}

static void test_fini(void)
{
        c2_reqh_shutdown_wait(&reqh);
        c2_reqh_fini(&reqh);
        c2_fol_fini(&fol);
        c2_md_store_fini(&md);
        c2_dbenv_fini(&db);
}

const struct c2_test_suite mdservice_ut = {
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
