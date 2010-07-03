#include <stdio.h>
#include <memory.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include "dba.h"

static void db_err(DB_ENV *dbenv, int rc, const char * msg)
{
	dbenv->err(dbenv, rc, msg);
	fprintf(stderr, "%s:%s\n", msg, db_strerror(rc));
}

static void c2_db_fini(struct c2_dba_ctxt *ctxt)
{
	DB_ENV  *dbenv = ctxt->dc_dbenv;
	int 	 rc;

	if (dbenv) {
		rc = dbenv->log_flush(dbenv, NULL);

		if (ctxt->dc_group_extent != NULL)
			ctxt->dc_group_extent->sync(ctxt->dc_group_extent, 0);
		if (rc != 0)
			db_err(dbenv, rc, "->log_flush()");

		if (ctxt->dc_group_extent != NULL) {
			ctxt->dc_group_extent->close(ctxt->dc_group_extent, 0);
			ctxt->dc_group_extent = NULL;
		}

		dbenv->close(dbenv, 0);
		ctxt->dc_dbenv = NULL;
	}
}


static int c2_db_insert(DB_ENV *dbenv, DB_TXN *tx, DB *db,
			void *key, size_t keysize,
			void *rec, size_t recsize)
{
	int result;
	DBT keyt;
	DBT rect;

	memset(&keyt, 0, sizeof keyt);
	memset(&rect, 0, sizeof rect);

	keyt.data = key;
	keyt.size = keysize;

	rect.data = rec;
	rect.size = recsize;

	result = db->put(db, tx, &keyt, &rect, 0);
	if (result != 0) {
		const char *msg = "DB->put() cannot insert into database";
		db_err(dbenv, result, msg);
	}

	return result;
}

static int c2_dba_compare(DB *db, const DBT *dbt1, const DBT *dbt2)
{
	c2_blockno_t	*bn1;
	c2_blockno_t	*bn2;

	bn1 = dbt1->data;
	bn2 = dbt2->data;
	if (*bn1 > *bn2 )
		return 1;
	else if (*bn1 == *bn2)
		return 0;
	else
		return -1;
}


int c2_db_open(DB_ENV *dbenv, const char *name, u_int32_t flags, DB **dbp)
{
	const char *msg;
	int rc;
	DB *db;

	*dbp = NULL;
	rc = db_create(dbp, dbenv, 0);
	msg = "create";
	if (rc == 0) {
		db = *dbp;
		db->set_bt_compare(db, c2_dba_compare);
		rc = db->open(db, NULL, name,
			      NULL, DB_BTREE, flags, 0664);
		msg = "open";
	}
	if (rc != 0) {
		char buf[256];
		snprintf(buf, 255, "database \"%s\": %s failure", name, msg);
		db_err(dbenv, rc, buf);
	}
	return rc;
}


int c2_db_init(struct c2_dba_ctxt *ctxt)
{
	int            	 rc;
	DB_ENV 		*dbenv;
	char		 path[MAXPATHLEN];

	rc = db_env_create(&dbenv, 0);
	if (rc != 0) {
		fprintf(stderr, "db_env_create: %s", db_strerror(rc));
		return rc;
	}

	ctxt->dc_dbenv = dbenv;
	dbenv->set_errfile(dbenv, stderr);
	dbenv->set_errpfx(dbenv, "db4s");

	rc = dbenv->set_flags(dbenv, DB_TXN_NOSYNC, 1);
	if (rc != 0) {
		db_err(dbenv, rc, "->set_flags(DB_TXN_NOSYNC)");
		c2_db_fini(ctxt);
		return rc;
	}

	ctxt->dc_cache_size = 1024*1024;
	if (ctxt->dc_cache_size != 0) {
		u_int32_t gbytes;
		u_int32_t bytes;

		gbytes = ctxt->dc_cache_size / (1024*1024*1024);
		bytes  = ctxt->dc_cache_size - (gbytes * (1024*1024*1024));
		rc = dbenv->set_cachesize(dbenv, gbytes, bytes, 1);
		if (rc != 0) {
			db_err(dbenv, rc, "->set_cachesize()");
			c2_db_fini(ctxt);
			return rc;
		}
	}

	rc = dbenv->set_thread_count(dbenv, ctxt->dc_nr_thread);
	if (rc != 0) {
		db_err(dbenv, rc, "->set_thread_count()");
		c2_db_fini(ctxt);
		return rc;
	}

#if 0
	dbenv->set_verbose(dbenv, DB_VERB_DEADLOCK, 1);
	dbenv->set_verbose(dbenv, DB_VERB_WAITSFOR, 1);
	dbenv->set_verbose(dbenv, DB_VERB_RECOVERY, 1);
	dbenv->set_verbose(dbenv, DB_VERB_FILEOPS, 1);
	dbenv->set_verbose(dbenv, DB_VERB_FILEOPS_ALL, 1);
#endif

	rc = mkdir(ctxt->dc_home, 0700);
	if (rc != 0 && errno != EEXIST) {
		db_err(dbenv, rc, "->mkdir() for home");
		c2_db_fini(ctxt);
		return rc;
	}

	snprintf(path, MAXPATHLEN - 1, "%s/d", ctxt->dc_home);
	mkdir(path, 0700);

	snprintf(path, MAXPATHLEN - 1, "%s/l", ctxt->dc_home);
	mkdir(path, 0700);

	snprintf(path, MAXPATHLEN - 1, "%s/t", ctxt->dc_home);
	mkdir(path, 0700);

	rc = dbenv->set_tmp_dir(dbenv, "t");
	if (rc != 0)
		db_err(dbenv, rc, "->set_tmp_dir()");

	rc = dbenv->set_lg_dir(dbenv, "l");
	if (rc != 0)
		db_err(dbenv, rc, "->set_lg_dir()");

	rc = dbenv->set_data_dir(dbenv, "d");
	if (rc != 0)
		db_err(dbenv, rc, "->set_data_dir()");

	/* Open the environment with full transactional support. */
	ctxt->dc_dbenv_flags = DB_CREATE|DB_THREAD|DB_INIT_LOG|
			       DB_INIT_MPOOL|DB_INIT_TXN|DB_INIT_LOCK|
	                       DB_RECOVER;
	rc = dbenv->open(dbenv, ctxt->dc_home, ctxt->dc_dbenv_flags, 0);
	if (rc != 0) {
		db_err(dbenv, rc, "environment open");
		return rc;
	}

	ctxt->dc_db_flags = DB_AUTO_COMMIT|DB_CREATE|
                            DB_THREAD|DB_TXN_NOSYNC|
	                    /*
			     * Both a data-base and a transaction
			     * must use "read uncommitted" to avoid
			     * dead-locks.
			     */
                             DB_READ_UNCOMMITTED;
	ctxt->dc_txn_flags = DB_READ_UNCOMMITTED|DB_TXN_NOSYNC;
	rc = c2_db_open(dbenv, "group_extent_1", ctxt->dc_db_flags, &ctxt->dc_group_extent);

	printf("path = %s\n", ctxt->dc_home);
	return 0;
}

int c2_db_allocate(struct c2_dba_ctxt *ctxt, struct c2_dba_allocate_req *req)
{
	DB_TXN         *tx = NULL;
	DB_ENV	       *dbenv = ctxt->dc_dbenv;
	int 		rc;
	c2_blockno_t	bn;
	c2_blockcount_t	count;


	bn    = req->dar_logical;
	count = req->dar_lcount;

	rc = dbenv->txn_begin(dbenv, NULL, &tx, ctxt->dc_txn_flags);
	if (tx == NULL) {
		db_err(dbenv, rc, "cannot start transaction");
		return rc;
	}
	rc = c2_db_insert(dbenv, tx, ctxt->dc_group_extent,
			  &bn, sizeof bn, &count, sizeof count);

	if (rc == 0)
		rc = tx->commit(tx, 0);
	else
		rc = tx->abort(tx);
	if (rc != 0)
		db_err(dbenv, rc, "cannot commit/abort transaction");
	return rc;
}


static int db_list(struct c2_dba_ctxt *ctxt, struct c2_dba_allocate_req *req)
{
	DB_ENV	       *dbenv = ctxt->dc_dbenv;
	int  result;
	DBT  nkeyt;
	DBT  nrect;
	DBC *cursor;

	c2_blockno_t	*bn;
	c2_blockcount_t	*count;

	memset(&nkeyt, 0, sizeof nkeyt);
	memset(&nrect, 0, sizeof nrect);

	nrect.flags = DB_DBT_MALLOC;

	result = ctxt->dc_group_extent->cursor(ctxt->dc_group_extent, NULL, &cursor, 0);
	if (result == 0) {
		c2_blockno_t	goal = 5000;
		nkeyt.data = &goal;
		nkeyt.size = sizeof goal;

		result = cursor->get(cursor, &nkeyt,
					     &nrect, DB_SET_RANGE);
		if ( result == 0) {
			bn = nkeyt.data;
			count = nrect.data;
			printf("[%08llu, %lu]\n",
			       (unsigned long long) *bn, (unsigned long)*count);

		while ((result = cursor->get(cursor, &nkeyt,
					     &nrect, DB_NEXT)) == 0) {
			/* XXX db4 does not guarantee proper alignment */
			bn = nkeyt.data;
			count = nrect.data;

			printf("...[%08llu, %lu]\n",
			       (unsigned long long) *bn, (unsigned long)*count);
		}
		if (result != DB_NOTFOUND)
			ctxt->dc_group_extent->err(ctxt->dc_group_extent,
						   result, "Full iteration");
		else
			result = 0;
		}

		cursor->close(cursor);
	}
	if (result != 0)
		db_err(dbenv, result, "cannot list database contents");

	return result;
}


int main()
{
	struct c2_dba_ctxt ctxt = {
		.dc_nr_thread = 1
		};

	struct c2_dba_allocate_req request;

	int rc = 0;
	char path[256];

	getcwd(path, 255);
	ctxt.dc_home = path;

	rc = c2_db_init(&ctxt);
	if (rc != 0) {
		fprintf(stderr, "c2_db_init error: %d\n", rc);
		return rc;
	}
	request.dar_logical = 1234;
	request.dar_lcount  = 5678;
	rc = c2_db_allocate(&ctxt, &request);
	if (rc != 0) {
		fprintf(stderr, "c2_db_allocate error: %d\n", rc);
		return rc;
	}

	request.dar_logical = 4321;
	request.dar_lcount  = 8765;
	rc = c2_db_allocate(&ctxt, &request);
	if (rc != 0) {
		fprintf(stderr, "c2_db_allocate error: %d\n", rc);
		return rc;
	}

	request.dar_logical = 1111;
	request.dar_lcount  = 2222;
	rc = c2_db_allocate(&ctxt, &request);
	if (rc != 0) {
		fprintf(stderr, "c2_db_allocate error: %d\n", rc);
		return rc;
	}


	request.dar_logical = 8888;
	request.dar_lcount  = 9999;
	rc = c2_db_allocate(&ctxt, &request);
	if (rc != 0) {
		fprintf(stderr, "c2_db_allocate error: %d\n", rc);
		return rc;
	}


	db_list(&ctxt, &request);

	c2_db_fini(&ctxt);
	return 0;
}
