#include <stdio.h>
#include <memory.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/time.h>

#include "dba.h"

/**
   C2 Data Block Allocator.
   DBA is a multi-block allocator, with pre-allocation. All metadata about
   block allocation is stored in database -- Oracle Berkeley DB.

 */



/**
   db_err, reporting error messsage.

   @param dbenv pointer to db environment.
   @param rc error number returned from function call.
   @param msg message to output.
 */
static void db_err(DB_ENV *dbenv, int rc, const char * msg)
{
	dbenv->err(dbenv, rc, msg);
	fprintf(stderr, "%s:%s\n", msg, db_strerror(rc));
}



/**
   finaliazation of the dba environment.

   @param ctxt context of this data-block-allocation.
 */
static void c2_dba_fini(struct c2_dba_ctxt *ctxt)
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


/**
   Insert a key/record pair into the db.

   @param dbenv pointer to this db env.
   @param tx transaction. See more on DB->put().
   @param db the database into which key/record will be inserted to.
   @param key pointer to key. Any data type is accepted.
   @param keysize size of data.
   @param rec pointer to record. Any data type is accpeted.
   @param recsize size of record.

   @return 0 on success; Otherwise, error number is returned.

 */
static int c2_dba_insert_record(DB_ENV *dbenv, DB_TXN *tx, DB *db,
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


/**
   Comparison function for block number, supplied to database.
 */
static int c2_dba_blockno_compare(DB *db, const DBT *dbt1, const DBT *dbt2)
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


/**
   Open/create a database.

   @param dbenv database environment.
   @param name database name
   @param flags flags to open the database. Depending on the flags, this function
          may open an existing database, or create a new one.
   @param key_compare comparison function for key in this database.
   @param dbp [out] db pointer returned.
*/

static int c2_dba_open_db(DB_ENV *dbenv, const char *name, uint32_t flags,
			  int (*key_compare)(DB *db, const DBT *dbt1, const DBT *dbt2),
			  DB **dbp)
{
	const char *msg;
	int rc;
	DB *db;

	*dbp = NULL;
	rc = db_create(dbp, dbenv, 0);
	msg = "create";
	if (rc == 0) {
		db = *dbp;

		if (key_compare)
			db->set_bt_compare(db, key_compare);

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

/**
   Format the container: create database, fill them with initial information.

   This routine will create a "super_block" database to store global parameters
   for this container. It will also create "group free extent" and "group_desc"
   for every group. If some groups are reserved for special purpse, then they
   will be marked as "allocated" at the format time, and those groups will not
   be used by normal allocation routines.

   @param req pointer to this format request. All configuration will be passed
          by this parameter.
   @return 0 means success. Otherwize, error number will be returned.
 */
int c2_dba_format(struct c2_dba_format_req *req)
{
	int            	 rc;
	DB_ENV 		*dbenv;
	char		 path[MAXPATHLEN];
        uint32_t         dbenv_flags;
        uint32_t  	 db_flags;
	DB              *group_free_extent;
	DB              *group_desc;
	DB              *super_block;
	uint32_t	 number_of_groups;
	uint32_t	 i;

	struct c2_dba_extent      ext;
	struct c2_dba_group_desc  gd;
	struct c2_dba_super_block sb;

	ENTER;

	rc = db_env_create(&dbenv, 0);
	if (rc != 0) {
		fprintf(stderr, "db_env_create: %s", db_strerror(rc));
		return rc;
	}

	dbenv->set_errfile(dbenv, stderr);
	dbenv->set_errpfx(dbenv, "c2db format");

	rc = mkdir(req->dfr_db_home, 0700);
	if (rc != 0 && errno != EEXIST) {
		fprintf(stderr, "->mkdir() for home failed: %d\n", errno);
		dbenv->close(dbenv, 0);
		return rc;
	}

	snprintf(path, MAXPATHLEN - 1, "%s/d", req->dfr_db_home);
	rc = mkdir(path, 0700);
	if (rc != 0) {
		if (errno == EEXIST) {
			fprintf(stderr, "database dir %s already exists. "
				"Please remove it and then format again\n",
				path);
		} else {
			fprintf(stderr, "create database dir %s failed: %d\n",
				path, errno);
		}
		dbenv->close(dbenv, 0);
		return rc;
	}

	snprintf(path, MAXPATHLEN - 1, "%s/l", req->dfr_db_home);
	mkdir(path, 0700);

	snprintf(path, MAXPATHLEN - 1, "%s/t", req->dfr_db_home);
	mkdir(path, 0700);

	rc = dbenv->set_data_dir(dbenv, "d");
	if (rc != 0) {
		db_err(dbenv, rc, "->set_data_dir()");
		dbenv->close(dbenv, 0);
		return rc;
	}

	/* Open the environment with full transactional support. */
	dbenv_flags = DB_CREATE|DB_THREAD|DB_INIT_LOG|
		      DB_INIT_MPOOL|DB_INIT_TXN|DB_INIT_LOCK|
	              DB_RECOVER;
	rc = dbenv->open(dbenv, req->dfr_db_home, dbenv_flags, 0);
	if (rc != 0) {
		db_err(dbenv, rc, "environment open");
		dbenv->close(dbenv, 0);
		return rc;
	}

	db_flags = DB_CREATE;
	number_of_groups = req->dfr_totalsize / req->dfr_blocksize /
			   req->dfr_groupsize;

	if (number_of_groups == 0)
		number_of_groups = 1;
	printf("totalsize=%llu, blocksize=%d, groupsize=%d, groups=%d resvd=%d\n",
		(unsigned long long)req->dfr_totalsize, req->dfr_blocksize,
                req->dfr_groupsize, number_of_groups, req->dfr_reserved_groups);
	if (number_of_groups <= req->dfr_reserved_groups) {
		fprintf(stderr, "container is too small\n");
		dbenv->close(dbenv, 0);
		return -EINVAL;
	}

	rc = c2_dba_open_db(dbenv, "super_block", db_flags,
			    NULL, &super_block);
	if (rc == 0) {
		struct timeval now;

		gettimeofday(&now, NULL);
		memset(&sb, 0, sizeof sb);

		sb.dsb_magic = C2_DBA_SB_MAGIC;
		sb.dsb_state = C2_DBA_SB_CLEAN;
		sb.dsb_version = C2_DBA_SB_VERSION;
		sb.dsb_totalsize = req->dfr_totalsize;
		sb.dsb_blocksize = req->dfr_blocksize;
		sb.dsb_groupsize = req->dfr_groupsize;
		sb.dsb_reserved_groups = req->dfr_reserved_groups;
		sb.dsb_freeblocks      = sb.dsb_blocksize * number_of_groups;
		sb.dsb_prealloc_count  = 16;
		sb.dsb_format_time = ((uint64_t)now.tv_sec) << 32 | now.tv_usec;

		rc = c2_dba_insert_record(dbenv, NULL, super_block,
					  &sb.dsb_magic, sizeof sb.dsb_magic,
					  &sb, sizeof sb);

		super_block->sync(super_block, 0);
		super_block->close(super_block, 0);
	} else {
		fprintf(stderr, "create super_block db failed\n");
		dbenv->close(dbenv, 0);
		return rc;
	}
	for (i = 0; i < number_of_groups; i++) {
		char db_name[64];

		printf("creating extent for group %d\n", i);
		snprintf(db_name, 63, "group_free_extent_%d", i);
		rc = c2_dba_open_db(dbenv, db_name, db_flags,
				    c2_dba_blockno_compare, &group_free_extent);
		if (rc == 0) {
			ext.ext_start = i * req->dfr_groupsize;
			if (i < req->dfr_reserved_groups) {
				ext.ext_len = 0;
			} else {
				ext.ext_len = req->dfr_groupsize;
			}
			rc = c2_dba_insert_record(dbenv, NULL, group_free_extent,
						  &ext.ext_start, sizeof ext.ext_start,
						  &ext.ext_len, sizeof ext.ext_len);
			if (rc != 0) {
				fprintf(stderr, "insert extent failed:"
					"group=%d, rc=%d\n", i, rc);
			}
			group_free_extent->sync(group_free_extent, 0);
			group_free_extent->close(group_free_extent, 0);
		} else {
			fprintf(stderr, "create free extent db failed:i=%d\n", i);
			break;
		}

		printf("creating  group_desc for group %d\n", i);
		snprintf(db_name, 63, "group_desc_%d", i);
		rc = c2_dba_open_db(dbenv, db_name, db_flags,
				    NULL, &group_desc);
		if (rc == 0) {
			if (i < req->dfr_reserved_groups) {
				gd.dgd_freeblocks = 0;
				gd.dgd_fragments  = 0;
				gd.dgd_maxchunk   = 0;
			} else {
				gd.dgd_freeblocks = req->dfr_groupsize;
				gd.dgd_fragments  = 1;
				gd.dgd_maxchunk   = req->dfr_groupsize;
			}
			rc = c2_dba_insert_record(dbenv, NULL, group_desc,
						  &i, sizeof i,
						  &gd, sizeof gd);
			if (rc != 0) {
				fprintf(stderr, "insert gd failed:"
					"group=%d, rc=%d\n", i, rc);
			}
			group_desc->sync(group_desc, 0);
			group_desc->close(group_desc, 0);
		} else {
			fprintf(stderr, "create free extent db failed:i=%d\n", i);
			break;
		}
	}
	dbenv->close(dbenv, 0);

	LEAVE;
	return 0;
}


/**
   Init the database operation environment, opening databases, ...

   @param ctxt pointer to the operation context environment.
   @return 0 means success. Otherwise errer number will be returned.
   @see c2_dba_fini
 */
int c2_dba_init(struct c2_dba_ctxt *ctxt)
{
	int            	 rc;
	DB_ENV 		*dbenv;
	char		 path[MAXPATHLEN];
	ENTER;

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
		c2_dba_fini(ctxt);
		return rc;
	}

	ctxt->dc_cache_size = 1024*1024;
	if (ctxt->dc_cache_size != 0) {
		uint32_t gbytes;
		uint32_t bytes;

		gbytes = ctxt->dc_cache_size / (1024*1024*1024);
		bytes  = ctxt->dc_cache_size - (gbytes * (1024*1024*1024));
		rc = dbenv->set_cachesize(dbenv, gbytes, bytes, 1);
		if (rc != 0) {
			db_err(dbenv, rc, "->set_cachesize()");
			c2_dba_fini(ctxt);
			return rc;
		}
	}

	rc = dbenv->set_thread_count(dbenv, ctxt->dc_nr_thread);
	if (rc != 0) {
		db_err(dbenv, rc, "->set_thread_count()");
		c2_dba_fini(ctxt);
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
		c2_dba_fini(ctxt);
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
	rc = c2_dba_open_db(dbenv, "group_extent_1", ctxt->dc_db_flags,
			    c2_dba_blockno_compare, &ctxt->dc_group_extent);

	LEAVE;
	return 0;
}

/**
   Allocate multiple blocks for some object.

   This routine will search suitable free space, and determine where to allocate
   from.  Caller can provide some hint (goal). Pre-allocation is used depending
   the character of the I/O sequences, and the current state of the active I/O.
   When trying allocate blocks from free space, we will allocate from the
   best suitable chunks, which are represented as buddy.

   Allocation will first try to use pre-allocation if it exists. Pre-allocation
   can be per-object, or group based.

   This routine will first check the group description to see if enough free
   space is availabe, and if largest contiguous chunk satisfy the request. This
   checking will be done group by group, until allocation succeeded or failed.
   If failed, the largest available contiguous chunk size is returned, and the
   caller can decide whether to use a smaller request.

   While searching free space from group to group, the free space extent will be
   loaded into cache.  We cache as much free space extent up to some
   specified memory limitation.  This is a configurable parameter, or default
   value will be choosed based on system memory.

   @param ctxt dba operation context environment.
   @param req allocate request which includes all parameters.
   @return 0 means success. Result allocated blocks are again stored in "req".
           Upon failure, non-zero error number is returned.
 */
int c2_dba_allocate(struct c2_dba_ctxt *ctxt, struct c2_dba_allocate_req *req)
{
	DB_TXN         *tx = NULL;
	DB_ENV	       *dbenv = ctxt->dc_dbenv;
	int 		rc;

	rc = dbenv->txn_begin(dbenv, NULL, &tx, ctxt->dc_txn_flags);
	if (tx == NULL) {
		db_err(dbenv, rc, "cannot start transaction");
		return rc;
	}

	/* Step 1. query the pre-allocation */

	/* Step 2. Iterate over groups */

	/* Step 3. Update the group free space extent and group desc. */

	if (rc == 0)
		rc = tx->commit(tx, 0);
	else
		rc = tx->abort(tx);
	if (rc != 0)
		db_err(dbenv, rc, "cannot commit/abort transaction");
	return rc;
}

/**
   Free multiple blocks owned by some object to free space.

   @param ctxt dba operation context environment.
   @param req block free request which includes all parameters.
   @return 0 means success. Upon failure, non-zero error number is returned.
 */
int c2_dba_free(struct c2_dba_ctxt *ctxt, struct c2_dba_free_req *req)
{

	return 0;
}

/**
   Discard the pre-allocation for object.

   @param ctxt dba operation context environment.
   @param req discard request which includes all parameters.
   @return 0 means success. Upon failure, non-zero error number is returned.
 */
int c2_dba_discard_prealloc(struct c2_dba_ctxt *ctxt, struct c2_dba_discard_req *req)
{

	return 0;
}



/**
   Sample code to insert a record and iterate over a db
 */
int db_insert(struct c2_dba_ctxt *ctxt, struct c2_dba_allocate_req *req)
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
	rc = c2_dba_insert_record(dbenv, tx, ctxt->dc_group_extent,
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
		nkeyt.data = &req->dar_goal;
		nkeyt.size = sizeof req->dar_goal;

		result = cursor->get(cursor, &nkeyt,
					     &nrect, DB_SET_RANGE);
		if ( result == 0) {
			bn = nkeyt.data;
			count = nrect.data;
			printf("[%08llx, %lx]\n",
			       (unsigned long long) *bn, (unsigned long)*count);

		while ((result = cursor->get(cursor, &nkeyt,
					     &nrect, DB_NEXT)) == 0) {
			/* XXX db4 does not guarantee proper alignment */
			bn = nkeyt.data;
			count = nrect.data;

			printf("...[%08llx, %lx]\n",
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

	struct c2_dba_allocate_req alloc_req;
	struct c2_dba_format_req   format_req;

	int rc = 0;
	char path[256];

	getcwd(path, 255);

	format_req.dfr_db_home = path;
	format_req.dfr_totalsize = 4096ULL * 1024 * 1024 * 10; //=40GB
	format_req.dfr_blocksize = 4096;
	format_req.dfr_groupsize = 4096 * 8; //=128MB = ext4 group size
	format_req.dfr_reserved_groups = 2;

//	rc = c2_dba_format(&format_req);

//	return rc;

	ctxt.dc_home = path;
	rc = c2_dba_init(&ctxt);
	if (rc != 0) {
		fprintf(stderr, "c2_dba_init error: %d\n", rc);
		return rc;
	}
	alloc_req.dar_logical = 0x1234;
	alloc_req.dar_lcount  = 0x5678;
	rc = db_insert(&ctxt, &alloc_req);
	if (rc != 0) {
		fprintf(stderr, "c2_dba_allocate error: %d\n", rc);
		return rc;
	}

	alloc_req.dar_logical = 0x1122;
	alloc_req.dar_lcount  = 0x3344;
	rc = db_insert(&ctxt, &alloc_req);
	if (rc != 0) {
		fprintf(stderr, "c2_dba_allocate error: %d\n", rc);
		return rc;
	}


	alloc_req.dar_logical = 0x7788;
	alloc_req.dar_lcount  = 0x9900;
	rc = db_insert(&ctxt, &alloc_req);
	if (rc != 0) {
		fprintf(stderr, "c2_dba_allocate error: %d\n", rc);
		return rc;
	}

	alloc_req.dar_logical = 0x1111;
	alloc_req.dar_lcount  = 0x2222;
	rc = db_insert(&ctxt, &alloc_req);
	if (rc != 0) {
		fprintf(stderr, "c2_dba_allocate error: %d\n", rc);
		return rc;
	}


	alloc_req.dar_goal = 0x1000;
	db_list(&ctxt, &alloc_req);

	alloc_req.dar_goal = 0x5000;
	db_list(&ctxt, &alloc_req);


	c2_dba_fini(&ctxt);
	return 0;
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
