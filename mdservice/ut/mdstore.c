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

static const char db_name[] = "ut-mdstore";
static struct c2_cob_domain_id id = { 42 };

static struct c2_dbenv       db;
static struct c2_md_store    md;
static struct c2_reqh        reqh;
static struct c2_fol         fol;
static int                   rc;

int c2_md_lustre_fop_alloc(struct c2_fop **fop, void *data);
void c2_md_lustre_fop_free(struct c2_fop *fop);

static int db_reset(void)
{
        rc = c2_ut_db_reset(db_name);
        C2_ASSERT(rc == 0);
        return rc;
}

#include "mdservice/ut/mdstore.h"

static void test_mkfs(void)
{
        struct c2_md_lustre_fid root;
        struct c2_cob_nskey  *nskey;
        struct c2_cob_nsrec   nsrec;
        struct c2_cob_omgkey  omgkey;
        struct c2_cob_omgrec  omgrec;
        struct c2_cob_fabrec *fabrec;
        struct c2_db_pair     pair;
        struct c2_db_tx       tx;
        struct c2_cob        *cob;
        time_t                now;
        int                   rc;
        int                   fd;

        fd = open(C2_MDSTORE_OPS_DUMP_PATH, O_RDONLY);
        C2_ASSERT(fd > 0);
        
        rc = read(fd, &root, sizeof(root));
        C2_ASSERT(rc == sizeof(root));
        close(fd);

	rc = c2_dbenv_init(&db, db_name, 0);
	C2_UT_ASSERT(rc == 0);

        rc = c2_md_store_init(&md, &id, &db, 0);
	C2_UT_ASSERT(rc == 0);

        C2_SET0(&nsrec);

        /*
         * Insert replicator root into colibri root, so that
         * we can always find root after mkfs using known
         * parent fid and name "ROOT".
         */
        c2_cob_make_nskey(&nskey, &C2_MD_ROOT_FID, C2_MD_ROOT_NAME, 
                          strlen(C2_MD_ROOT_NAME));

        /*
         * Zero omgid is for root directory.
         */        
        nsrec.cnr_omgid = 0;
        nsrec.cnr_fid.f_container = root.f_seq;
        nsrec.cnr_fid.f_key = root.f_oid;

        nsrec.cnr_nlink = 1;
        nsrec.cnr_size = 4096;
        nsrec.cnr_blksize = 4096;
        nsrec.cnr_blocks = 16;
        time(&now);
        nsrec.cnr_atime = nsrec.cnr_mtime = nsrec.cnr_ctime = now;

        /*
         * Root is owner of root dir and has corresponding permissions.
         */
        omgrec.cor_uid = 0;
        omgrec.cor_gid = 0;
        omgrec.cor_mode = S_IFDIR | 
                          S_IRUSR | S_IWUSR | S_IXUSR | /* rwx for owner */
                          S_IRGRP | S_IXGRP |           /* r-x for group */
                          S_IROTH | S_IXOTH;            /* r-x for others */

        rc = c2_db_tx_init(&tx, &db, 0);
	C2_UT_ASSERT(rc == 0);
        
        /*
         * Create root cob.
         */
        c2_cob_make_fabrec(&fabrec, NULL, 0);
        rc = c2_cob_create(&md.md_dom, nskey, &nsrec, fabrec, 
                           &omgrec, &cob, &tx);
	C2_UT_ASSERT(rc == 0);
        c2_cob_put(cob);

        /*
         * Create terminator omgid record with id == ~0.
         */
        omgkey.cok_omgid = ~0ULL;
        
        /*
         * Zero out terminator omrec, we don't need its 
         * fields.
         */
        C2_SET0(&omgrec);
        
        /* 
         * Add ~0ULL omgid to fileattr-omg table. 
         */
        c2_db_pair_setup(&pair, &md.md_dom.cd_fileattr_omg,
			 &omgkey, sizeof omgkey,
			 &omgrec, sizeof omgrec);

        rc = c2_table_insert(&tx, &pair);
	C2_UT_ASSERT(rc == 0);
        c2_db_pair_release(&pair);
	c2_db_pair_fini(&pair);

        c2_db_tx_commit(&tx);
        c2_md_store_fini(&md);
        c2_dbenv_fini(&db);
}

static void test_init(void)
{
        rc = c2_processors_init();
        C2_ASSERT(rc == 0);

	rc = c2_dbenv_init(&db, db_name, 0);
	C2_ASSERT(rc == 0);

	rc = c2_fol_init(&fol, &db);
	C2_ASSERT(rc == 0);

        rc = c2_md_store_init(&md, &id, &db, 1);
	C2_ASSERT(rc == 0);
        
        rc = c2_reqh_init(&reqh, NULL, NULL, &db, &md, &fol);
	C2_ASSERT(rc == 0);
}

static void test_ops(void)
{
        struct c2_md_lustre_logrec *rec;
        struct c2_md_lustre_fid root;
        struct c2_fop *fop;
        int fd, result, size;
        
        fd = open(C2_MDSTORE_OPS_DUMP_PATH, O_RDONLY);
        C2_ASSERT(fd > 0);
        
        result = read(fd, &root, sizeof(root));
        C2_ASSERT(result == sizeof(root));
        
        while (1) {
                fop = NULL;
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
                        
                C2_ASSERT(result == 0);

                c2_reqh_fop_handle(&reqh, fop, NULL);
                
                c2_md_lustre_fop_free(fop);
                c2_fop_free(fop);
        }
        
        close(fd);
}

static void test_fini(void)
{
        c2_reqh_fini(&reqh);
        c2_md_store_fini(&md);
        c2_fol_fini(&fol);
        c2_dbenv_fini(&db);
}

const struct c2_test_suite mdservice_ut = {
	.ts_name = "mdservice-ut",
	.ts_init = db_reset,
	/* .ts_fini = db_reset, */
	.ts_tests = {
		{ "mdstore-mkfs", test_mkfs },
		{ "mdstore-init", test_init },
		{ "mdstore-ops",  test_ops },
		{ "mdstore-fini", test_fini },
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
