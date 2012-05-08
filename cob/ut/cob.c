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
 * Original author: Nathan Rutman <nathan_rutman@xyratex.com>
 * Original creation date: 10/24/2010
 */

#include "lib/ut.h"
#include "lib/ub.h"
#include "lib/memory.h"
#include "lib/misc.h"              /* C2_SET0 */
#include "lib/bitstring.h"
#include "cob/cob.h"

static const char db_name[] = "ut-cob";
static const char test_name[] = "hello_world";
static const char add_name[] = "add_name";
static const char wrong_name[] = "wrong_name";

static struct c2_cob_domain_id id = { 42 };
static struct c2_dbenv       db;
static struct c2_cob_domain  dom;
static struct c2_cob         *cob;
static int rc;

static int db_reset(void)
{
        rc = c2_ut_db_reset(db_name);
        /* C2_UT_ASSERT not usable during ts_init */
        C2_ASSERT(rc == 0);
        return rc;
}

static void test_mkfs(void)
{
        struct c2_db_tx         tx;
        int                     rc;

	rc = c2_dbenv_init(&db, db_name, 0);
	C2_UT_ASSERT(rc == 0);

        rc = c2_cob_domain_init(&dom, &db, &id);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_init(&tx, &db, 0);
	C2_UT_ASSERT(rc == 0);

        /* Create root and other structures */
        rc = c2_cob_domain_mkfs(&dom, &C2_COB_SLASH_FID, &C2_COB_SESSIONS_FID, &tx);
        C2_UT_ASSERT(rc == 0);
	c2_db_tx_commit(&tx);

        /* Fini everything */
        c2_cob_domain_fini(&dom);
        c2_dbenv_fini(&db);
}

static void test_init(void)
{

	rc = c2_dbenv_init(&db, db_name, 0);
        /* test_init is called by ub_init which hates C2_UT_ASSERT */
	C2_ASSERT(rc == 0);

        rc = c2_cob_domain_init(&dom, &db, &id);
	C2_ASSERT(rc == 0);
}

static void test_fini(void)
{
        c2_cob_domain_fini(&dom);
	c2_dbenv_fini(&db);
}

static void test_create(void)
{
        struct c2_cob_nskey    *key;
        struct c2_cob_nsrec    nsrec;
        struct c2_cob_fabrec  *fabrec;
        struct c2_cob_omgrec   omgrec;
        struct c2_fid          pfid;
        struct c2_db_tx        tx;

	C2_SET0(&nsrec);
	C2_SET0(&omgrec);

        /* pfid, filename */
        pfid.f_container = 0x123;
        pfid.f_key = 0x456;
        c2_cob_make_nskey(&key, &pfid, test_name, strlen(test_name));

        nsrec.cnr_fid.f_container = 0xabc;
        nsrec.cnr_fid.f_key = 0xdef;

        nsrec.cnr_nlink = 0;

        c2_db_tx_init(&tx, dom.cd_dbenv, 0);
        rc = c2_cob_alloc(&dom, &cob);
        C2_UT_ASSERT(rc == 0);
        c2_cob_make_fabrec(&fabrec, NULL, 0);
	rc = c2_cob_create(&dom, key, &nsrec, fabrec,
	                   &omgrec, &cob, &tx);
	C2_UT_ASSERT(rc == 0);

        nsrec.cnr_nlink++;
        rc = c2_cob_update(cob, &nsrec, NULL, NULL, &tx);
	C2_UT_ASSERT(rc == 0);
        c2_cob_put(cob);
        c2_db_tx_commit(&tx);
}

/**
   Test that add_name works.
*/
static void test_add_name(void)
{
        struct c2_cob_nskey *nskey;
        struct c2_fid        pfid;
        struct c2_db_tx      tx;

        /* pfid, filename */
        pfid.f_container = 0x123;
        pfid.f_key = 0x456;

        c2_db_tx_init(&tx, dom.cd_dbenv, 0);

        /* lookup for cob created before using @test_name. */
        c2_cob_make_nskey(&nskey, &pfid, test_name, strlen(test_name));
        rc = c2_cob_lookup(&dom, nskey, CA_NSKEY_FREE, &cob, &tx);
        C2_UT_ASSERT(rc == 0);

        /* add new name to existing cob */
        c2_cob_make_nskey(&nskey, &pfid, add_name, strlen(add_name));
        cob->co_nsrec.cnr_linkno = cob->co_nsrec.cnr_cntr;
        rc = c2_cob_add_name(cob, nskey, &cob->co_nsrec, &tx);
        C2_UT_ASSERT(rc == 0);
        c2_cob_put(cob);

        /* lookup for new name */
        rc = c2_cob_lookup(&dom, nskey, 0, &cob, &tx);
        C2_UT_ASSERT(rc == 0);
        c2_cob_put(cob);
        c2_free(nskey);

        /* lookup for wrong name, should fail. */
        c2_cob_make_nskey(&nskey, &pfid, wrong_name, strlen(wrong_name));
        rc = c2_cob_lookup(&dom, nskey, 0, &cob, &tx);
        C2_UT_ASSERT(rc != 0);
        c2_free(nskey);

        c2_db_tx_commit(&tx);
}

/**
   Test that del_name works.
*/
static void test_del_name(void)
{
        struct c2_cob_nskey *nskey;
        struct c2_fid        pfid;
        struct c2_db_tx      tx;

        /* pfid, filename */
        pfid.f_container = 0x123;
        pfid.f_key = 0x456;

        c2_db_tx_init(&tx, dom.cd_dbenv, 0);

        /* lookup for cob created before using @test_name. */
        c2_cob_make_nskey(&nskey, &pfid, test_name, strlen(test_name));
        rc = c2_cob_lookup(&dom, nskey, CA_NSKEY_FREE, &cob, &tx);
        C2_UT_ASSERT(rc == 0);

        /* del name that we created in prev test */
        c2_cob_make_nskey(&nskey, &pfid, add_name, strlen(add_name));
        rc = c2_cob_del_name(cob, nskey, &tx);
        C2_UT_ASSERT(rc == 0);
        c2_cob_put(cob);

        /* lookup for new name */
        rc = c2_cob_lookup(&dom, nskey, 0, &cob, &tx);
        C2_UT_ASSERT(rc != 0);
        c2_free(nskey);

        c2_db_tx_commit(&tx);
}

/** 
   Lookup by name, make sure cfid is right.
*/
static void test_lookup(void)
{
        struct c2_db_tx      tx;
        struct c2_cob_nskey *nskey;
        struct c2_fid        pfid;

        pfid.f_container = 0x123;
        pfid.f_key = 0x456;
        c2_cob_make_nskey(&nskey, &pfid, test_name, strlen(test_name));
        c2_db_tx_init(&tx, dom.cd_dbenv, 0);
        rc = c2_cob_lookup(&dom, nskey, CA_NSKEY_FREE, &cob, &tx);
        c2_db_tx_commit(&tx);
        C2_UT_ASSERT(rc == 0);
        C2_UT_ASSERT(cob != NULL);
        C2_UT_ASSERT(cob->co_dom == &dom);
        C2_UT_ASSERT(cob->co_valid & CA_NSREC);
        C2_UT_ASSERT(cob->co_nsrec.cnr_fid.f_container == 0xabc);
        C2_UT_ASSERT(cob->co_nsrec.cnr_fid.f_key == 0xdef);

        /* We should have cached the key also, unless oom */
        C2_UT_ASSERT(cob->co_valid & CA_NSKEY);

        c2_cob_put(cob);
}

static int test_locate_internal(void)
{
        struct c2_db_tx      tx;
        struct c2_fid        fid;
        struct c2_cob_oikey  oikey;

        fid.f_container = 0xabc;
        fid.f_key = 0xdef;
        
        oikey.cok_fid = fid;
        oikey.cok_linkno = 0;

        c2_db_tx_init(&tx, dom.cd_dbenv, 0);
        rc = c2_cob_locate(&dom, &oikey, &cob, &tx);
        c2_db_tx_commit(&tx);

        return rc;
}

/** 
   Lookup by fid, make sure pfid is right. 
*/
static void test_locate(void)
{
        rc = test_locate_internal();
        C2_UT_ASSERT(rc == 0);
        C2_UT_ASSERT(cob != NULL);
        C2_UT_ASSERT(cob->co_dom == &dom);

        /* We should have saved the NSKEY */
        C2_UT_ASSERT(cob->co_valid & CA_NSKEY);
        C2_UT_ASSERT(cob->co_nskey->cnk_pfid.f_container == 0x123);
        C2_UT_ASSERT(cob->co_nskey->cnk_pfid.f_key == 0x456);

        /* Assuming we looked up the NSREC at the same time */
        C2_UT_ASSERT(cob->co_valid & CA_NSREC);

        c2_cob_put(cob);
}

/** 
   Test if delete works. 
*/
static void test_delete(void)
{
        struct c2_db_tx      tx;

        /* gets ref */
        rc = test_locate_internal();
        C2_ASSERT(rc == 0);

        c2_db_tx_init(&tx, dom.cd_dbenv, 0);
        /* drops ref */
        rc = c2_cob_delete(cob, &tx);
        c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

        /* should fail now */
        rc = test_locate_internal();
        C2_UT_ASSERT(rc != 0);
}

const struct c2_test_suite cob_ut = {
	.ts_name = "cob-ut",
	.ts_init = db_reset,
	/* .ts_fini = db_reset, */
	.ts_tests = {
		{ "cob-mkfs", test_mkfs },
		{ "cob-init", test_init },
                { "cob-create", test_create },
                { "cob-lookup", test_lookup },
                { "cob-locate", test_locate },
                { "cob-add-name", test_add_name },
                { "cob-del-name", test_del_name },
                { "cob-delete", test_delete },
		{ "cob-fini", test_fini },
		{ NULL, NULL }
	}
};

/*
 * UB
 */

/* CU_ASSERT doesn't work for ub tests */
#define C2_UB_ASSERT C2_ASSERT

static struct c2_db_tx cob_ub_tx;

enum {
	UB_ITER = 100000
};

static void ub_init(void)
{
	db_reset();
	test_init();
        rc = c2_db_tx_init(&cob_ub_tx, dom.cd_dbenv, 0);
        C2_ASSERT(rc == 0);
}

static void ub_fini(void)
{
        rc = c2_db_tx_commit(&cob_ub_tx);
	C2_ASSERT(rc == 0);
	test_fini();
	db_reset();
}

static void newtx(int i) {
        int rc;

        if (i && i % 10 == 0) {
                rc = c2_db_tx_commit(&cob_ub_tx);
                C2_UB_ASSERT(rc == 0);
                rc = c2_db_tx_init(&cob_ub_tx, dom.cd_dbenv, 0);
                C2_UB_ASSERT(rc == 0);
        }
}

static void ub_create(int i)
{
        struct c2_cob_nskey   *key;
        struct c2_cob_nsrec    nsrec;
        struct c2_cob_fabrec  *fabrec;
        struct c2_cob_omgrec   omgrec;
        struct c2_fid          fid;

	C2_SET0(&nsrec);
	C2_SET0(&omgrec);

        newtx(i);

        /* pfid == cfid for data objects, so here we are identifying
           uniquely in the namespace by {pfid, ""} */
        fid.f_container = 0xAA;
        fid.f_key = i;
        c2_cob_make_nskey(&key, &fid, "", 0);

        nsrec.cnr_fid.f_container = 0xAA;
        nsrec.cnr_fid.f_key = i;
        nsrec.cnr_nlink = 1;

        rc = c2_cob_alloc(&dom, &cob);
        C2_UT_ASSERT(rc == 0);

        c2_cob_make_fabrec(&fabrec, NULL, 0);
	rc = c2_cob_create(&dom, key, &nsrec, fabrec, &omgrec, 
	                   &cob, &cob_ub_tx);
	C2_UB_ASSERT(rc == 0);

        c2_cob_put(cob);
}

static void ub_lookup(int i)
{
        struct c2_cob_nskey *key;
        struct c2_fid        fid;

        newtx(i);

        /* pfid == cfid for data objects */
        fid.f_container = 0xAA;
        fid.f_key = i;
        c2_cob_make_nskey(&key, &fid, "", 0);
        rc = c2_cob_lookup(&dom, key, CA_NSKEY_FREE, &cob, &cob_ub_tx);
        C2_UB_ASSERT(rc == 0);
        C2_UB_ASSERT(cob != NULL);
        C2_UB_ASSERT(cob->co_dom == &dom);

        C2_UB_ASSERT(cob->co_valid & CA_NSREC);
        C2_UB_ASSERT(cob->co_nsrec.cnr_fid.f_container == 0xAA);
        C2_UB_ASSERT(cob->co_nsrec.cnr_fid.f_key == i);

        /* We should be holding the nskey until the final put */
        C2_UB_ASSERT(cob->co_valid & CA_NSKEY);

        c2_cob_put(cob);
}


struct c2_ub_set c2_cob_ub = {
	.us_name = "cob-ub",
	.us_init = ub_init,
	.us_fini = ub_fini,
	.us_run  = {
		{ .ut_name = "create",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_create },

		{ .ut_name = "lookup",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_lookup },

		{ .ut_name = NULL }
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
