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

#include "ut/ut.h"
#include "lib/ub.h"
#include "lib/memory.h"
#include "lib/misc.h"              /* M0_SET0 */
#include "lib/bitstring.h"
#include "cob/cob.h"

static const char db_name[]    = "ut-cob";
static const char test_name[]  = "hello_world";
static const char add_name[]   = "add_name";
static const char wrong_name[] = "wrong_name";
static struct m0_cob_domain_id id = { 42 };
static struct m0_dbenv         db;
static struct m0_cob_domain    dom;
static struct m0_cob          *cob;

static int db_reset(void)
{
	int rc;

	rc = m0_ut_db_reset(db_name);
	M0_ASSERT(rc == 0);
	return rc;
}

static int _locate(int c, int k)
{
	struct m0_db_tx     tx;
	struct m0_fid       fid;
	struct m0_cob_oikey oikey;
	int                 rc;

	m0_fid_set(&fid, c, k);

	oikey.cok_fid = fid;
	oikey.cok_linkno = 0;

	m0_db_tx_init(&tx, dom.cd_dbenv, 0);
	rc = m0_cob_locate(&dom, &oikey, 0, &cob, &tx);
	m0_db_tx_commit(&tx);

	return rc;
}

static void test_mkfs(void)
{
	struct m0_db_tx tx;
	int             rc;

	rc = m0_dbenv_init(&db, db_name, 0);
	M0_UT_ASSERT(rc == 0);

	rc = m0_cob_domain_init(&dom, &db, &id);
	M0_UT_ASSERT(rc == 0);

	rc = m0_db_tx_init(&tx, &db, 0);
	M0_UT_ASSERT(rc == 0);

	/* Create root and other structures */
	rc = m0_cob_domain_mkfs(&dom, &M0_COB_SLASH_FID,
				&M0_COB_SESSIONS_FID, &tx);
	M0_UT_ASSERT(rc == 0);

	rc = _locate(1, 2); /* slash */
	M0_UT_ASSERT(rc == 0);
	rc = _locate(1, 3); /* session */
	M0_UT_ASSERT(rc == 0);
	rc = _locate(1, 1); /* root */
	M0_UT_ASSERT(rc != 0);
	m0_db_tx_commit(&tx);

	/* Fini everything */
	m0_cob_domain_fini(&dom);
	m0_dbenv_fini(&db);
}

static void test_init(void)
{
	int rc;

	rc = m0_dbenv_init(&db, db_name, 0);
	/* test_init is called by ub_init which hates M0_UT_ASSERT */
	M0_ASSERT(rc == 0);

	rc = m0_cob_domain_init(&dom, &db, &id);
	M0_ASSERT(rc == 0);
}

static void test_fini(void)
{
	m0_cob_domain_fini(&dom);
	m0_dbenv_fini(&db);
}

static void test_create(void)
{
	struct m0_cob_nskey  *key;
	struct m0_cob_nsrec   nsrec;
	struct m0_cob_fabrec *fabrec;
	struct m0_cob_omgrec  omgrec;
	struct m0_fid         pfid;
	struct m0_db_tx       tx;
	int                   rc;

	M0_SET0(&nsrec);
	M0_SET0(&omgrec);

	/* pfid, filename */
	m0_fid_set(&pfid, 0x123, 0x456);
	m0_cob_nskey_make(&key, &pfid, test_name, strlen(test_name));

	m0_fid_set(&nsrec.cnr_fid, 0xabc, 0xdef);
	nsrec.cnr_nlink = 0;

	m0_db_tx_init(&tx, dom.cd_dbenv, 0);
	rc = m0_cob_alloc(&dom, &cob);
	M0_UT_ASSERT(rc == 0);
	m0_cob_fabrec_make(&fabrec, NULL, 0);
	rc = m0_cob_create(cob, key, &nsrec, fabrec, &omgrec, &tx);
	M0_UT_ASSERT(rc == 0);

	++nsrec.cnr_nlink;
	rc = m0_cob_update(cob, &nsrec, NULL, NULL, &tx);
	M0_UT_ASSERT(rc == 0);
	m0_cob_put(cob);
	m0_db_tx_commit(&tx);
}

static void test_add_name(void)
{
	struct m0_cob_nskey *nskey;
	struct m0_fid        pfid;
	struct m0_db_tx      tx;
	int                  rc;

	/* pfid, filename */
	m0_fid_set(&pfid, 0x123, 0x456);

	m0_db_tx_init(&tx, dom.cd_dbenv, 0);

	/* lookup for cob created before using @test_name. */
	m0_cob_nskey_make(&nskey, &pfid, test_name, strlen(test_name));
	rc = m0_cob_lookup(&dom, nskey, M0_CA_NSKEY_FREE, &cob, &tx);
	M0_UT_ASSERT(rc == 0);

	/* add new name to existing cob */
	m0_cob_nskey_make(&nskey, &pfid, add_name, strlen(add_name));
	cob->co_nsrec.cnr_linkno = cob->co_nsrec.cnr_cntr;
	rc = m0_cob_name_add(cob, nskey, &cob->co_nsrec, &tx);
	M0_UT_ASSERT(rc == 0);
	m0_cob_put(cob);

	/* lookup for new name */
	rc = m0_cob_lookup(&dom, nskey, 0, &cob, &tx);
	M0_UT_ASSERT(rc == 0);
	m0_cob_put(cob);
	m0_free(nskey);

	/* lookup for wrong name, should fail. */
	m0_cob_nskey_make(&nskey, &pfid, wrong_name, strlen(wrong_name));
	rc = m0_cob_lookup(&dom, nskey, 0, &cob, &tx);
	M0_UT_ASSERT(rc != 0);
	m0_free(nskey);

	m0_db_tx_commit(&tx);
}

static void test_del_name(void)
{
	struct m0_cob_nskey *nskey;
	struct m0_fid        pfid;
	struct m0_db_tx      tx;
	int                  rc;

	/* pfid, filename */
	m0_fid_set(&pfid, 0x123, 0x456);

	m0_db_tx_init(&tx, dom.cd_dbenv, 0);

	/* lookup for cob created before using @test_name. */
	m0_cob_nskey_make(&nskey, &pfid, test_name, strlen(test_name));
	rc = m0_cob_lookup(&dom, nskey, M0_CA_NSKEY_FREE, &cob, &tx);
	M0_UT_ASSERT(rc == 0);

	/* del name that we created in prev test */
	m0_cob_nskey_make(&nskey, &pfid, add_name, strlen(add_name));
	rc = m0_cob_name_del(cob, nskey, &tx);
	M0_UT_ASSERT(rc == 0);
	m0_cob_put(cob);

	/* lookup for new name */
	rc = m0_cob_lookup(&dom, nskey, 0, &cob, &tx);
	M0_UT_ASSERT(rc != 0);
	m0_free(nskey);

	m0_db_tx_commit(&tx);
}

/** Lookup by name, make sure cfid is right. */
static void test_lookup(void)
{
	struct m0_db_tx      tx;
	struct m0_cob_nskey *nskey;
	struct m0_fid        pfid;
	int                  rc;

	m0_fid_set(&pfid, 0x123, 0x456);
	m0_cob_nskey_make(&nskey, &pfid, test_name, strlen(test_name));
	m0_db_tx_init(&tx, dom.cd_dbenv, 0);
	rc = m0_cob_lookup(&dom, nskey, M0_CA_NSKEY_FREE, &cob, &tx);
	m0_db_tx_commit(&tx);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(cob != NULL);
	M0_UT_ASSERT(cob->co_dom == &dom);
	M0_UT_ASSERT(cob->co_flags & M0_CA_NSREC);
	M0_UT_ASSERT(cob->co_nsrec.cnr_fid.f_container == 0xabc);
	M0_UT_ASSERT(cob->co_nsrec.cnr_fid.f_key == 0xdef);

	/* We should have cached the key also, unless oom */
	M0_UT_ASSERT(cob->co_flags & M0_CA_NSKEY);

	m0_cob_put(cob);
}

/** Lookup by fid, make sure pfid is right. */
static void test_locate(void)
{
	int rc;

	rc = _locate(0xabc, 0xdef);
	M0_UT_ASSERT(rc == 0);

	M0_UT_ASSERT(cob != NULL);
	M0_UT_ASSERT(cob->co_dom == &dom);

	/* We should have saved the NSKEY */
	M0_UT_ASSERT(cob->co_flags & M0_CA_NSKEY);
	M0_UT_ASSERT(cob->co_nskey->cnk_pfid.f_container == 0x123);
	M0_UT_ASSERT(cob->co_nskey->cnk_pfid.f_key == 0x456);

	/* Assuming we looked up the NSREC at the same time */
	M0_UT_ASSERT(cob->co_flags & M0_CA_NSREC);

	m0_cob_put(cob);

	/* We should fail here, since there is no such cob */
	rc = _locate(0x123, 0x456);
	M0_UT_ASSERT(rc != 0);
}

static void test_delete(void)
{
	struct m0_db_tx tx;
	int             rc;

	/* gets ref */
	rc = _locate(0xabc, 0xdef);
	M0_UT_ASSERT(rc == 0);

	m0_db_tx_init(&tx, dom.cd_dbenv, 0);
	rc = m0_cob_delete_put(cob, &tx);
	m0_db_tx_commit(&tx);
	M0_UT_ASSERT(rc == 0);

	/* should fail now */
	rc = _locate(0xabc, 0xdef);
	M0_UT_ASSERT(rc != 0);
}

const struct m0_test_suite cob_ut = {
	.ts_name = "cob-ut",
	.ts_init = db_reset,
	.ts_tests = {
		{ "cob-mkfs",     test_mkfs },
		{ "cob-init",     test_init },
		{ "cob-create",   test_create },
		{ "cob-lookup",   test_lookup },
		{ "cob-locate",   test_locate },
		{ "cob-add-name", test_add_name },
		{ "cob-del-name", test_del_name },
		{ "cob-delete",   test_delete },
		{ "cob-fini",     test_fini },
		{ NULL, NULL }
	}
};

/*
 * UB
 */

static struct m0_db_tx cob_ub_tx;

enum { UB_ITER = 100000 };

static int ub_init(const char *opts M0_UNUSED)
{
	struct m0_db_tx tx;
	int             rc;

	db_reset();
	test_init();

	/* Create root and other structures */
	rc = m0_db_tx_init(&tx, &db, 0) ?:
		m0_cob_domain_mkfs(&dom, &M0_COB_SLASH_FID,
				   &M0_COB_SESSIONS_FID, &tx);
	M0_ASSERT(rc == 0);
	m0_db_tx_commit(&tx);

	return m0_db_tx_init(&cob_ub_tx, dom.cd_dbenv, 0);
}

static void ub_fini(void)
{
	int rc;

	rc = m0_db_tx_commit(&cob_ub_tx);
	M0_ASSERT(rc == 0);
	test_fini();
	db_reset();
}

static void newtx(int i)
{
	int rc;

	if (i != 0 && i % 10 == 0) {
		rc = m0_db_tx_commit(&cob_ub_tx);
		M0_UB_ASSERT(rc == 0);
		rc = m0_db_tx_init(&cob_ub_tx, dom.cd_dbenv, 0);
		M0_UB_ASSERT(rc == 0);
	}
}

static void ub_create(int i)
{
	struct m0_cob_nskey  *key;
	struct m0_cob_nsrec   nsrec;
	struct m0_cob_fabrec *fabrec;
	struct m0_cob_omgrec  omgrec;
	struct m0_fid         fid;
	int                   rc;

	M0_SET0(&nsrec);
	M0_SET0(&omgrec);

	newtx(i);

	/* pfid == cfid for data objects, so here we are identifying
	 * uniquely in the namespace by {pfid, ""} */
	m0_fid_set(&fid, 0xAA, i);
	m0_cob_nskey_make(&key, &fid, "", 0);

	m0_fid_set(&nsrec.cnr_fid, 0xAA, i);
	nsrec.cnr_nlink = 1;

	rc = m0_cob_alloc(&dom, &cob);
	M0_UB_ASSERT(rc == 0);

	m0_cob_fabrec_make(&fabrec, NULL, 0);
	rc = m0_cob_create(cob, key, &nsrec, fabrec, &omgrec, &cob_ub_tx);
	M0_UB_ASSERT(rc == 0);

	m0_cob_put(cob);
}

static void ub_lookup(int i)
{
	struct m0_cob_nskey *key;
	struct m0_fid        fid;
	int                  rc;

	newtx(i);

	/* pfid == cfid for data objects */
	m0_fid_set(&fid, 0xAA, i);
	m0_cob_nskey_make(&key, &fid, "", 0);
	rc = m0_cob_lookup(&dom, key, M0_CA_NSKEY_FREE, &cob, &cob_ub_tx);
	M0_UB_ASSERT(rc == 0);
	M0_UB_ASSERT(cob != NULL);
	M0_UB_ASSERT(cob->co_dom == &dom);

	M0_UB_ASSERT(cob->co_flags & M0_CA_NSREC);
	M0_UB_ASSERT(cob->co_nsrec.cnr_fid.f_container == 0xAA);
	M0_UB_ASSERT(cob->co_nsrec.cnr_fid.f_key == i);

	/* We should be holding the nskey until the final put */
	M0_UB_ASSERT(cob->co_flags & M0_CA_NSKEY);

	m0_cob_put(cob);
}

struct m0_ub_set m0_cob_ub = {
	.us_name = "cob-ub",
	.us_init = ub_init,
	.us_fini = ub_fini,
	.us_run  = {
		{ .ub_name  = "create",
		  .ub_iter  = UB_ITER,
		  .ub_round = ub_create },

		{ .ub_name  = "lookup",
		  .ub_iter  = UB_ITER,
		  .ub_round = ub_lookup },

		{ .ub_name = NULL }
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
