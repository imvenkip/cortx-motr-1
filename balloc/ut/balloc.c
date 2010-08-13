/* -*- C -*- */

#include <unistd.h>  /* getcwd */
#include <stdio.h>   /* fprintf */
#include <string.h>  /* memset */
#include <errno.h>   /* errno */
#include <stdlib.h>  /* free */
#include <sys/stat.h> /* mkdir */
#include <err.h>

#include "balloc/balloc.h"

static void db_err(DB_ENV *dbenv, int rc, const char * msg)
{
	dbenv->err(dbenv, rc, msg);
}

int c2_balloc_insert_record(DB_ENV *dbenv, DB_TXN *tx, DB *db,
			    void *key, size_t keysize,
			    void *rec, size_t recsize);
int c2_balloc_update_record(DB_ENV *dbenv, DB_TXN *tx, DB *db,
			    void *key, size_t keysize,
			    void *rec, size_t recsize);
int c2_balloc_del_record(DB_ENV *dbenv, DB_TXN *tx, DB *db,
			 void *key, size_t keysize);

/*
   Sample code to insert a record and iterate over a db
 */

static int db_insert(struct c2_balloc_ctxt *ctxt, 
		     struct c2_balloc_allocate_req *req)
{
	DB_TXN         *tx = NULL;
	DB_ENV	       *dbenv = ctxt->bc_dbenv;
	DB             *db = ctxt->bc_group_info[0].bgi_db_group_extent;
	int 		rc;
	c2_bindex_t	bn;
	c2_bcount_t	count;


	bn    = req->bar_logical;
	count = req->bar_len;

	rc = dbenv->txn_begin(dbenv, NULL, &tx, ctxt->bc_txn_flags);
	if (tx == NULL) {
		db_err(dbenv, rc, "cannot start transaction");
		return rc;
	}
	rc = c2_balloc_insert_record(dbenv, tx, db,
			             &bn, sizeof bn,
				     &count, sizeof count);
	if (rc != 0)
		db_err(dbenv, rc, "cannot insert");

	if (rc == 0)
		rc = tx->commit(tx, 0);
	else
		rc = tx->abort(tx);
	if (rc != 0)
		db_err(dbenv, rc, "cannot commit/abort transaction");
	return rc;
}

static int db_update(struct c2_balloc_ctxt *ctxt, 
		     struct c2_balloc_allocate_req *req)
{
	DB_TXN         *tx = NULL;
	DB_ENV	       *dbenv = ctxt->bc_dbenv;
	DB             *db = ctxt->bc_group_info[0].bgi_db_group_extent;
	int 		rc;
	c2_bindex_t	bn;
	c2_bcount_t	count;


	bn    = req->bar_logical;
	count = req->bar_len;

	rc = dbenv->txn_begin(dbenv, NULL, &tx, ctxt->bc_txn_flags);
	if (tx == NULL) {
		db_err(dbenv, rc, "cannot start transaction");
		return rc;
	}
	rc = c2_balloc_update_record(dbenv, tx, db,
			             &bn, sizeof bn,
				     &count, sizeof count);
	if (rc != 0)
		db_err(dbenv, rc, "cannot update");

	if (rc == 0)
		rc = tx->commit(tx, 0);
	else
		rc = tx->abort(tx);
	if (rc != 0)
		db_err(dbenv, rc, "cannot commit/abort transaction");
	return rc;
}

static int db_delete(struct c2_balloc_ctxt *ctxt, 
		     struct c2_balloc_allocate_req *req)
{
	DB_TXN         *tx = NULL;
	DB_ENV	       *dbenv = ctxt->bc_dbenv;
	DB             *db = ctxt->bc_group_info[0].bgi_db_group_extent;
	int 		rc;
	c2_bindex_t	bn;

	bn    = req->bar_logical;

	rc = dbenv->txn_begin(dbenv, NULL, &tx, ctxt->bc_txn_flags);
	if (tx == NULL) {
		db_err(dbenv, rc, "cannot start transaction");
		return rc;
	}
	rc = c2_balloc_del_record(dbenv, tx, db,
			          &bn, sizeof bn);

	if (rc != 0)
		db_err(dbenv, rc, "cannot del");
	if (rc == 0)
		rc = tx->commit(tx, 0);
	else
		rc = tx->abort(tx);
	return rc;
}

static int db_list2(struct c2_balloc_ctxt *ctxt, 
		    struct c2_balloc_allocate_req *req)
{
	int  result;
	DBT  nkeyt;
	DBT  nrect;
	DBC *cursor;
	DB  *db = ctxt->bc_group_info[0].bgi_db_group_extent;

	c2_bindex_t	*bn;
	c2_bcount_t	*count;

	memset(&nkeyt, 0, sizeof nkeyt);
	memset(&nrect, 0, sizeof nrect);

	result = db->cursor(db, NULL, &cursor, 0);
	if (result == 0) {
		while (1) {
			memset(&nkeyt, 0, sizeof nkeyt);
			memset(&nrect, 0, sizeof nrect);
			nrect.flags = DB_DBT_MALLOC;
			nkeyt.flags = DB_DBT_MALLOC;

			result = cursor->get(cursor, &nkeyt,
				     &nrect, DB_NEXT);
			if ( result != 0)
				break;

			bn = nkeyt.data;
			count = nrect.data;

			printf("...[%08llx, %lx]\n", (unsigned long long) *bn, 
			       (unsigned long)*count);
			free(bn);
			free(count);
		}

		cursor->close(cursor);
	}

	return result;
}

int main(int argc, char **argv)
{
	struct c2_balloc_ctxt         ctxt = {
		.bc_nr_thread = 1,
	};
	struct c2_balloc_allocate_req alloc_req = { 0 };

	int rc;
	char *path;

	if (argc != 2)
		errx(1, "Usage: balloc path-to-db-dir");

	path = argv[1];

	ctxt.bc_home = path;
	rc = c2_balloc_init(&ctxt);
	if (rc != 0) {
		fprintf(stderr, "c2_balloc_init error: %d\n", rc);
		return rc;
	}
	alloc_req.bar_logical = 0x1234;
	alloc_req.bar_len  = 0x5678;
	rc = db_insert(&ctxt, &alloc_req);
	if (rc != 0) {
		fprintf(stderr, "db_insert error: %d\n", rc);
		return rc;
	}

	alloc_req.bar_logical = 0x1122;
	alloc_req.bar_len  = 0x3344;
	rc = db_insert(&ctxt, &alloc_req);
	if (rc != 0) {
		fprintf(stderr, "db_insert error: %d\n", rc);
		return rc;
	}


	alloc_req.bar_logical = 0x7788;
	alloc_req.bar_len  = 0x9900;
	rc = db_insert(&ctxt, &alloc_req);
	if (rc != 0) {
		fprintf(stderr, "db_insert error: %d\n", rc);
		return rc;
	}

	alloc_req.bar_logical = 0x1111;
	alloc_req.bar_len  = 0x2222;
	rc = db_insert(&ctxt, &alloc_req);
	if (rc != 0) {
		fprintf(stderr, "db_insert error: %d\n", rc);
		return rc;
	}
	alloc_req.bar_logical = 0x1111;
	alloc_req.bar_len  = 0x2222;
	rc = db_insert(&ctxt, &alloc_req);
	if (rc != 0) {
		fprintf(stderr, "db_insert error: %d\n", rc);
		return rc;
	}
	alloc_req.bar_logical = 0x7788;
	alloc_req.bar_len  = 0x99AA;
	rc = db_update(&ctxt, &alloc_req);
	if (rc != 0) {
		fprintf(stderr, "db_update error: %d\n", rc);
		return rc;
	}

	alloc_req.bar_logical = 0x1234;
	alloc_req.bar_len  = 0x5678;
	rc = db_delete(&ctxt, &alloc_req);
	if (rc != 0) {
		fprintf(stderr, "db_update error: %d\n", rc);
		return rc;
	}

	db_list2(&ctxt, &alloc_req);

	alloc_req.bar_logical = 0x88;
	alloc_req.bar_goal    = 0x8000 * 3 + 1;
	alloc_req.bar_len     = 0xabc;
	alloc_req.bar_flags   = C2_BALLOC_HINT_DATA | C2_BALLOC_HINT_TRY_GOAL;
	rc = c2_balloc_allocate(&ctxt, &alloc_req);

	c2_balloc_fini(&ctxt);
	return rc;
}


/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
