/* -*- C -*- */

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/time.h>
#include <string.h>

#include "lib/arith.h"    /* min_check */
#include "lib/memory.h"
#include "balloc.h"

/**
   C2 Data Block Allocator.
   BALLOC is a multi-block allocator, with pre-allocation. All metadata about
   block allocation is stored in database -- Oracle Berkeley DB.

 */

/* This macro is to control the debug verbose message */
//#define BALLOC_DEBUG

#ifdef BALLOC_DEBUG
  #define ENTER printf("===>>> %s:%d:%s\n", __FILE__, __LINE__, __func__)
  #define LEAVE printf("<<<=== %s:%d:%s\n", __FILE__, __LINE__, __func__)
  #define GOTHERE printf("!!! %s:%d:%s\n", __FILE__, __LINE__, __func__)
  #define debugp printf
#else
  #define ENTER
  #define LEAVE
  #define GOTHERE
  #define debugp(unused, ...)
#endif

void c2_balloc_debug_dump_free_extent(const char *tag,
				      struct c2_balloc_free_extent *fex)
{
	if (fex == NULL)
		return;

	debugp("dumping fe@%p:%s\n"
		"|---------logical=%10llu, gn=%10llu, start=%10llu, len=%10llu\n"
		"|---------logical=0x%08llx, gn=0x%08llx, start=0x%08llx, len=0x%08llx\n",
		fex, tag,
		(unsigned long long) fex->bfe_logical,
		(unsigned long long) fex->bfe_groupno,
		(unsigned long long) fex->bfe_start,
		(unsigned long long) fex->bfe_len,
		(unsigned long long) fex->bfe_logical,
		(unsigned long long) fex->bfe_groupno,
		(unsigned long long) fex->bfe_start,
		(unsigned long long) fex->bfe_len);
}

void c2_balloc_debug_dump_extent(const char *tag, struct c2_balloc_extent *ex)
{
	if (ex == NULL)
		return;

	debugp("dumping ex@%p:%s\n"
	       "|-----------start=%10llu, len=%10llu\n"
	       "|-----------start=0x%08llx, len=0x%08llx\n",
		ex, tag,
		(unsigned long long) ex->be_start,
		(unsigned long long) ex->be_len,
		(unsigned long long) ex->be_start,
		(unsigned long long) ex->be_len);
}

void c2_balloc_debug_dump_sb(const char *tag, struct c2_balloc_ctxt *ctxt)
{
	struct c2_balloc_super_block *sb;
	sb = &ctxt->bc_sb;
	if (ctxt == NULL)
		return;

	debugp("dumping sb@%p:%p:%s\n"
		"|---------magic=%llx, state=%llu, version=%llu\n"
		"|---------total=%llu, free=%llu, bs=%llu@%lx\n"
		"|---------gs=%llu:@%lx, gc=%llu, rsvd=%llu, prealloc=%llu\n"
		"|---------time format=%llu,\n"
		"|---------write=%llu,\n"
		"|---------mnt=%llu,\n"
		"|---------last_check=%llu\n"
		"|---------mount=%llu, max_mnt=%llu, stripe_size=%llu\n",
		ctxt, sb, tag,
		(unsigned long long) sb->bsb_magic,
		(unsigned long long) sb->bsb_state,
		(unsigned long long) sb->bsb_version,
		(unsigned long long) sb->bsb_totalsize,
		(unsigned long long) sb->bsb_freeblocks,
		(unsigned long long) sb->bsb_blocksize,
		(unsigned long     ) sb->bsb_bsbits,
		(unsigned long long) sb->bsb_groupsize,
		(unsigned long     ) sb->bsb_gsbits,
		(unsigned long long) sb->bsb_groupcount,
		(unsigned long long) sb->bsb_reserved_groups,
		(unsigned long long) sb->bsb_prealloc_count,
		(unsigned long long) sb->bsb_format_time,
		(unsigned long long) sb->bsb_write_time,
		(unsigned long long) sb->bsb_mnt_time,
		(unsigned long long) sb->bsb_last_check_time,
		(unsigned long long) sb->bsb_mnt_count,
		(unsigned long long) sb->bsb_max_mnt_count,
		(unsigned long long) sb->bsb_stripe_size
		);
}


/**
   db_err, reporting error messsage.

   @param dbenv pointer to db environment.
   @param rc error number returned from function call.
   @param msg message to output.

   @todo using ADDB instead
 */

#define MAX_ALLOCATION_CHUNK 2048ULL

static void db_err(DB_ENV *dbenv, int rc, const char * msg)
{
	dbenv->err(dbenv, rc, msg);
}

/**
   finaliazation of the balloc environment.

   @param ctxt context of this data-block-allocation.
 */
void c2_balloc_fini(struct c2_balloc_ctxt *ctxt)
{
	DB_ENV  *dbenv = ctxt->bc_dbenv;
	struct c2_balloc_group_info *gi;
	DB      *db;
	int	 i;

	if (dbenv) {
		dbenv->log_flush(dbenv, NULL);
		if (ctxt->bc_db_sb != NULL) {
			ctxt->bc_db_sb->sync(ctxt->bc_db_sb, 0);
			ctxt->bc_db_sb->close(ctxt->bc_db_sb, 0);
			ctxt->bc_db_sb = NULL;
		}
		if (ctxt->bc_group_info) {
			for (i = 0 ; i < ctxt->bc_sb.bsb_groupcount; i++) {
				gi = &ctxt->bc_group_info[i];
				db = gi->bgi_db_group_extent;
				if (db != NULL) {
					db->sync(db, 0);
					db->close(db, 0);
					db = NULL;
				}
				db = gi->bgi_db_group_desc;
				if (db != NULL) {
					db->sync(db, 0);
					db->close(db, 0);
					db = NULL;
				}
				if (gi->bgi_extents) {
					c2_free(gi->bgi_extents);
					gi->bgi_extents = NULL;
				}
			}

			c2_free(ctxt->bc_group_info);
			ctxt->bc_group_info = NULL;
		}
		dbenv->close(dbenv, 0);
		ctxt->bc_dbenv = NULL;
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
int c2_balloc_insert_record(DB_ENV *dbenv, DB_TXN *tx, DB *db,
			    void *key, size_t keysize,
			    void *rec, size_t recsize)
{
	int result;
	DBT keyt;
	DBT rect;
//	ENTER;

	memset(&keyt, 0, sizeof keyt);
	memset(&rect, 0, sizeof rect);

	keyt.data = key;
	keyt.size = keysize;

	rect.data = rec;
	rect.size = recsize;

	result = db->put(db, tx, &keyt, &rect, DB_NOOVERWRITE);
	if (result != 0) {
		const char *msg = "DB->put() cannot insert into database";
		db_err(dbenv, result, msg);
	}

//	LEAVE;
	return result;
}

/**
   Update a key/record pair into the db.

   @param dbenv pointer to this db env.
   @param tx transaction. See more on DB->put().
   @param db the database into which key/record will be inserted to.
   @param key pointer to key. Any data type is accepted.
   @param keysize size of data.
   @param rec pointer to record. Any data type is accpeted.
   @param recsize size of record.

   @return 0 on success; Otherwise, error number is returned.

 */
int c2_balloc_update_record(DB_ENV *dbenv, DB_TXN *tx, DB *db,
			    void *key, size_t keysize,
			    void *rec, size_t recsize)
{
	int result = 0;
	DBT keyt;
	DBT rect;
//	ENTER;

	memset(&keyt, 0, sizeof keyt);
	memset(&rect, 0, sizeof rect);

	keyt.data = key;
	keyt.size = keysize;
	keyt.flags = DB_DBT_MALLOC;

	rect.data = rec;
	rect.size = recsize;

	if ((result = db->exists(db, tx, &keyt, 0)) == 0) {
		memset(&keyt, 0, sizeof keyt);
		keyt.data = key;
		keyt.size = keysize;
		result = db->put(db, tx, &keyt, &rect, 0);
	}
	if (result != 0) {
		const char *msg = "DB->put() cannot update into database";
		db_err(dbenv, result, msg);
	}

//	LEAVE;
	return result;
}


/**
   Delete a key/record pair from the db.

   @param dbenv pointer to this db env.
   @param tx transaction. See more on DB->del().
   @param db the database from which key/record will be deleted.
   @param key pointer to key. Any data type is accepted.
   @param keysize size of data.

   @return 0 on success; Otherwise, error number is returned.

 */
int c2_balloc_del_record(DB_ENV *dbenv, DB_TXN *tx, DB *db,
			 void *key, size_t keysize)
{
	int result;
	DBT keyt;
//	ENTER;

	memset(&keyt, 0, sizeof keyt);

	keyt.data = key;
	keyt.size = keysize;

	result = db->del(db, tx, &keyt, 0);
	if (result != 0) {
		const char *msg = "DB->del() cannot delete from database";
		db_err(dbenv, result, msg);
	}

//	LEAVE;
	return result;
}


/**
   Get a key/record pair from the db.

   The caller should free the result record memory if succeeded to call
   this function.

   @param dbenv pointer to this db env.
   @param tx transaction. See more on DB->put().
   @param db the database from which key/record will be retrieved from.
   @param key [in] pointer to key. Any data type is accepted.
   @param keysize [in] size of data.
   @param rec [out] pointer to record. Any data type is accpeted.
   @param recsize [out] size of record.

   @return 0 on success; Otherwise, error number is returned.

 */
int c2_balloc_get_record(DB_ENV *dbenv, DB_TXN *tx, DB *db,
			 void *key, size_t keysize,
			 void **rec, size_t *recsize)
{
	int result;
	DBT keyt;
	DBT rect;

	memset(&keyt, 0, sizeof keyt);
	memset(&rect, 0, sizeof rect);
	keyt.data = key;
	keyt.size = keysize;
	rect.flags = DB_DBT_MALLOC;

	result = db->get(db, tx, &keyt, &rect, 0);
	if (result != 0) {
		const char *msg = "DB->get() cannot get into database";
		db_err(dbenv, result, msg);
	} else {
		*rec = rect.data;
		*recsize = rect.size;
	}
	return result;
}



/**
   Comparison function for block number, supplied to database.
 */
static int c2_balloc_blockno_compare(DB *db, const DBT *dbt1, const DBT *dbt2)
{
	c2_bindex_t	*bn1;
	c2_bindex_t	*bn2;

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

static int 
c2_balloc_open_db(DB_ENV *dbenv, const char *name, uint32_t flags,
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
int c2_balloc_format(struct c2_balloc_format_req *req)
{
	int            	 rc;
	DB_ENV 		*dbenv;
	char		 path[MAXPATHLEN];
        uint32_t         dbenv_flags;
        uint32_t  	 db_flags;
	DB              *group_extents;
	DB              *group_desc;
	DB              *super_block;
	uint32_t	 number_of_groups;
	uint32_t	 i;

	struct c2_balloc_extent      ext;
	struct c2_balloc_group_desc  gd;
	struct c2_balloc_super_block sb;

	ENTER;

	rc = db_env_create(&dbenv, 0);
	if (rc != 0) {
		fprintf(stderr, "db_env_create: %s", db_strerror(rc));
		return rc;
	}

	dbenv->set_errfile(dbenv, stderr);
	dbenv->set_errpfx(dbenv, "c2db format");

	rc = mkdir(req->bfr_db_home, 0700);
	if (rc != 0 && errno != EEXIST) {
		rc = -errno;
		fprintf(stderr, "->mkdir() for home failed: %d\n", errno);
		dbenv->close(dbenv, 0);
		return rc;
	}

	snprintf(path, ARRAY_SIZE(path), "%s/d", req->bfr_db_home);
	rc = mkdir(path, 0700);
	if (rc != 0) {
		rc = -errno;
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

	snprintf(path, ARRAY_SIZE(path), "%s/l", req->bfr_db_home);
	mkdir(path, 0700);

	snprintf(path, ARRAY_SIZE(path), "%s/t", req->bfr_db_home);
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
	rc = dbenv->open(dbenv, req->bfr_db_home, dbenv_flags, 0);
	if (rc != 0) {
		db_err(dbenv, rc, "environment open");
		dbenv->close(dbenv, 0);
		return rc;
	}

	db_flags = DB_CREATE;
	number_of_groups = req->bfr_totalsize / req->bfr_blocksize /
			   req->bfr_groupsize;

	if (number_of_groups == 0)
		number_of_groups = 1;

	debugp("total=%llu, bs=%llu, groupsize=%llu, groups=%d resvd=%llu\n",
		(unsigned long long)req->bfr_totalsize,
		(unsigned long long)req->bfr_blocksize,
                (unsigned long long)req->bfr_groupsize,
		number_of_groups,
		(unsigned long long)req->bfr_reserved_groups);

	if (number_of_groups <= req->bfr_reserved_groups) {
		fprintf(stderr, "container is too small\n");
		dbenv->close(dbenv, 0);
		return -EINVAL;
	}

	rc = c2_balloc_open_db(dbenv, "super_block", db_flags,
			    NULL, &super_block);
	if (rc == 0) {
		struct timeval now;

		gettimeofday(&now, NULL);
		memset(&sb, 0, sizeof sb);

		/* TODO verification of these parameters */
		sb.bsb_magic = C2_BALLOC_SB_MAGIC;
		sb.bsb_state = 0;
		sb.bsb_version = C2_BALLOC_SB_VERSION;
		sb.bsb_totalsize = req->bfr_totalsize;
		sb.bsb_blocksize = req->bfr_blocksize; /* should be power of 2*/
		sb.bsb_groupsize = req->bfr_groupsize; /* should be power of 2*/
		sb.bsb_bsbits    = ffs(req->bfr_blocksize) - 1;
		sb.bsb_gsbits    = ffs(req->bfr_groupsize) - 1;
		sb.bsb_groupcount = number_of_groups;
		sb.bsb_reserved_groups = req->bfr_reserved_groups;
		sb.bsb_freeblocks = (number_of_groups - sb.bsb_reserved_groups)
					<< sb.bsb_gsbits;
		sb.bsb_prealloc_count  = 16;
		sb.bsb_format_time = ((uint64_t)now.tv_sec) << 32 | now.tv_usec;
		sb.bsb_write_time  = sb.bsb_format_time;
		sb.bsb_mnt_time    = sb.bsb_format_time;
		sb.bsb_last_check_time  = sb.bsb_format_time;
		sb.bsb_mnt_count   = 0;
		sb.bsb_max_mnt_count   = 1023;
		sb.bsb_stripe_size   = 0;

		rc = c2_balloc_insert_record(dbenv, NULL, super_block,
					  &sb.bsb_magic, sizeof sb.bsb_magic,
					  &sb, sizeof sb);
		if (rc != 0) {
			fprintf(stderr, "insert super_block failed:"
				"rc=%d\n", rc);
		}
		super_block->sync(super_block, 0);
		super_block->close(super_block, 0);
	} else {
		fprintf(stderr, "create super_block db failed\n");
		dbenv->close(dbenv, 0);
		return rc;
	}
	for (i = 0; i < number_of_groups; i++) {
		char db_name[64];

		debugp("creating extent for group %d\n", i);
		snprintf(db_name, ARRAY_SIZE(db_name), 
			 "group_extents_%09d", i);
		rc = c2_balloc_open_db(dbenv, db_name, db_flags,
				       c2_balloc_blockno_compare,
				       &group_extents);
		if (rc == 0) {
			ext.be_start = i << sb.bsb_gsbits;
			if (i < req->bfr_reserved_groups) {
				ext.be_len = 0;
			} else {
				ext.be_len = req->bfr_groupsize;
			}
			rc = c2_balloc_insert_record(dbenv, NULL,
						     group_extents,
						     &ext.be_start,
						     sizeof ext.be_start,
						     &ext.be_len,
						     sizeof ext.be_len);
			if (rc != 0) {
				fprintf(stderr, "insert extent failed:"
					"group=%d, rc=%d\n", i, rc);
			}
			group_extents->sync(group_extents, 0);
			group_extents->close(group_extents, 0);
		} else {
			fprintf(stderr, "create free extent failed:i=%d\n", i);
			break;
		}

		debugp("creating  group_desc for group %d\n", i);
		snprintf(db_name, ARRAY_SIZE(db_name), "group_desc_%09d", i);
		rc = c2_balloc_open_db(dbenv, db_name, db_flags,
				    NULL, &group_desc);
		if (rc == 0) {
			gd.bgd_groupno = i;
			if (i < req->bfr_reserved_groups) {
				gd.bgd_freeblocks = 0;
				gd.bgd_fragments  = 0;
				gd.bgd_maxchunk   = 0;
			} else {
				gd.bgd_freeblocks = req->bfr_groupsize;
				gd.bgd_fragments  = 1;
				gd.bgd_maxchunk   = req->bfr_groupsize;
			}
			rc = c2_balloc_insert_record(dbenv, NULL, group_desc,
						     &gd.bgd_groupno,
						     sizeof gd.bgd_groupno,
						     &gd,
						     sizeof gd);
			if (rc != 0) {
				fprintf(stderr, "insert gd failed:"
					"group=%d, rc=%d\n", i, rc);
			}
			group_desc->sync(group_desc, 0);
			group_desc->close(group_desc, 0);
		} else {
			fprintf(stderr, "create group desc failed:i=%d\n", i);
			break;
		}
	}
	dbenv->close(dbenv, 0);

	LEAVE;
	return rc;
}

static int c2_balloc_read_sb(struct c2_balloc_ctxt *ctxt)
{
	struct c2_balloc_super_block *sb = &ctxt->bc_sb;
	struct c2_balloc_super_block *on_disk;
	size_t size;
	int    rc;

	sb->bsb_magic = C2_BALLOC_SB_MAGIC;

	rc = c2_balloc_get_record(ctxt->bc_dbenv, NULL, ctxt->bc_db_sb,
				  &sb->bsb_magic,
			          sizeof sb->bsb_magic,
				  (void**)&on_disk, &size);

	if (rc == 0) {
		if (size != sizeof *sb) {
			fprintf(stderr, "size mismatch\n");
		}
		memcpy(sb, on_disk, size);
		c2_free(on_disk);
	}

	return rc;
}

/* sync current superblock into db */
static int c2_balloc_sync_sb(struct c2_balloc_ctxt *ctxt)
{
	struct c2_balloc_super_block *sb = &ctxt->bc_sb;
	int    rc;
	ENTER;

	if (!(ctxt->bc_sb.bsb_state & C2_BALLOC_SB_DIRTY)) {
		LEAVE;
		return 0;
	}

	rc = c2_balloc_update_record(ctxt->bc_dbenv, ctxt->bc_tx,
				     ctxt->bc_db_sb,
				     &sb->bsb_magic, sizeof sb->bsb_magic,
				     sb, sizeof *sb);

	ctxt->bc_sb.bsb_state &= ~C2_BALLOC_SB_DIRTY;
	LEAVE;
	return rc;
}

/* sync current superblock into db */
static int c2_balloc_sync_group_info(struct c2_balloc_ctxt *ctxt,
				     struct c2_balloc_group_info *gi)
{
	struct c2_balloc_group_desc gd;
	int rc;
	ENTER;

	if (! (gi->bgi_state & C2_BALLOC_GROUP_INFO_DIRTY)) {
		LEAVE;
		return 0;
	}

	gd.bgd_groupno    = gi->bgi_groupno;
	gd.bgd_freeblocks = gi->bgi_freeblocks;
	gd.bgd_fragments  = gi->bgi_fragments;
	gd.bgd_maxchunk   = gi->bgi_maxchunk;

	rc = c2_balloc_update_record(ctxt->bc_dbenv, ctxt->bc_tx,
				     gi->bgi_db_group_desc,
				     &gd.bgd_groupno, sizeof gd.bgd_groupno,
				     &gd, sizeof gd);

	gi->bgi_state &= ~C2_BALLOC_GROUP_INFO_DIRTY;
	LEAVE;
	return rc;
}

static int c2_balloc_load_group_info(struct c2_balloc_ctxt *ctxt,
				     struct c2_balloc_group_info *gi)
{
	struct c2_balloc_group_desc *on_disk;
	size_t size;
	int    rc;

	rc = c2_balloc_get_record(ctxt->bc_dbenv, NULL, gi->bgi_db_group_desc,
				  &gi->bgi_groupno, sizeof gi->bgi_groupno,
				  (void**)&on_disk, &size);

	if (rc == 0) {
		if (size != sizeof *on_disk) {
			fprintf(stderr, "size mismatch\n");
		}
		gi->bgi_groupno    = on_disk->bgd_groupno;
		gi->bgi_freeblocks = on_disk->bgd_freeblocks;
		gi->bgi_fragments  = on_disk->bgd_fragments;
		gi->bgi_maxchunk   = on_disk->bgd_maxchunk;
		gi->bgi_state      = C2_BALLOC_GROUP_INFO_INIT;
		gi->bgi_extents    = NULL;
		c2_list_init(&gi->bgi_prealloc_list);
		c2_mutex_init(&gi->bgi_mutex);

		free(on_disk);
	}

	return rc;
}


/**
   Init the database operation environment, opening databases, ...

   @param ctxt pointer to the operation context environment, e.g. db_home is
          passed by this into this function.
   @return 0 means success. Otherwise errer number will be returned.
   @see c2_balloc_fini
 */
int c2_balloc_init(struct c2_balloc_ctxt *ctxt)
{
	struct c2_balloc_group_info *gi;
	DB_ENV 		*dbenv;
	int            	 rc;
	int 		 i;
	ENTER;

	c2_mutex_init(&ctxt->bc_sb_mutex);

	rc = db_env_create(&dbenv, 0);
	if (rc != 0) {
		fprintf(stderr, "db_env_create: %s", db_strerror(rc));
		return rc;
	}

	ctxt->bc_dbenv = dbenv;
	dbenv->set_errfile(dbenv, stderr);
	dbenv->set_errpfx(dbenv, "balloc");

	rc = dbenv->set_flags(dbenv, DB_TXN_NOSYNC, 1);
	if (rc != 0) {
		db_err(dbenv, rc, "->set_flags(DB_TXN_NOSYNC)");
		c2_balloc_fini(ctxt);
		return rc;
	}

	ctxt->bc_cache_size = 1024 * 1024 * 64;
	if (ctxt->bc_cache_size != 0) {
		uint32_t gbytes;
		uint32_t bytes;

		gbytes = ctxt->bc_cache_size / (1024*1024*1024);
		bytes  = ctxt->bc_cache_size - (gbytes * (1024*1024*1024));
		rc = dbenv->set_cachesize(dbenv, gbytes, bytes, 1);
		if (rc != 0) {
			db_err(dbenv, rc, "->set_cachesize()");
			c2_balloc_fini(ctxt);
			return rc;
		}
	}

	rc = dbenv->set_thread_count(dbenv, ctxt->bc_nr_thread);
	if (rc != 0) {
		db_err(dbenv, rc, "->set_thread_count()");
		c2_balloc_fini(ctxt);
		return rc;
	}

#if 0
	dbenv->set_verbose(dbenv, DB_VERB_DEADLOCK, 1);
	dbenv->set_verbose(dbenv, DB_VERB_WAITSFOR, 1);
	dbenv->set_verbose(dbenv, DB_VERB_RECOVERY, 1);
	dbenv->set_verbose(dbenv, DB_VERB_FILEOPS, 1);
	dbenv->set_verbose(dbenv, DB_VERB_FILEOPS_ALL, 1);
#endif

	rc = dbenv->set_tmp_dir(dbenv, "t");
	if (rc != 0)
		db_err(dbenv, rc, "->set_tmp_dir()");

	rc = dbenv->set_lg_dir(dbenv, "l");
	if (rc != 0)
		db_err(dbenv, rc, "->set_lg_dir()");

	rc = dbenv->set_data_dir(dbenv, "d");
	if (rc != 0) {
		db_err(dbenv, rc, "->set_data_dir()");
		c2_balloc_fini(ctxt);
		return rc;
	}

	/* Open the environment with full transactional support. */
       ctxt->bc_dbenv_flags = DB_CREATE|DB_THREAD|DB_INIT_LOG|
                              DB_INIT_MPOOL|DB_INIT_TXN|DB_INIT_LOCK|
                              DB_RECOVER;
	rc = dbenv->open(dbenv, ctxt->bc_home, ctxt->bc_dbenv_flags, 0);
	if (rc != 0) {
		db_err(dbenv, rc, "environment open");
		c2_balloc_fini(ctxt);
		return rc;
	}

	ctxt->bc_db_flags = DB_AUTO_COMMIT|DB_CREATE|
                            DB_THREAD|DB_TXN_NOSYNC|
	                    /*
			     * Both a data-base and a transaction
			     * must use "read uncommitted" to avoid
			     * dead-locks.
			     */
                             DB_READ_UNCOMMITTED;
	ctxt->bc_txn_flags = DB_READ_UNCOMMITTED|DB_TXN_NOSYNC;

	rc = c2_balloc_open_db(dbenv, "super_block", ctxt->bc_db_flags,
			       NULL, &ctxt->bc_db_sb);

	if (rc != 0) {
		db_err(dbenv, rc, "open super block");
		c2_balloc_fini(ctxt);
		return rc;
	}

	rc = c2_balloc_read_sb(ctxt);
	if (rc != 0) {
		db_err(dbenv, rc, "open super block");
		c2_balloc_fini(ctxt);
		return rc;
	}

	debugp("Group Count = %lu\n", ctxt->bc_sb.bsb_groupcount);

	i = ctxt->bc_sb.bsb_groupcount * sizeof (struct c2_balloc_group_info);
	ctxt->bc_group_info = c2_alloc(i);
	if (ctxt->bc_group_info == NULL) {
		rc = -ENOMEM;
		db_err(dbenv, rc, "malloc");
		c2_balloc_fini(ctxt);
		return rc;
	}

	for (i = 0; i < ctxt->bc_sb.bsb_groupcount; i++ ) {
		char db_name[64];

		gi = &ctxt->bc_group_info[i];
		gi->bgi_groupno = i;

		snprintf(db_name, ARRAY_SIZE(db_name)-1 , "group_desc_%09d", i);
		rc = c2_balloc_open_db(dbenv, db_name, ctxt->bc_db_flags,
				       NULL, &gi->bgi_db_group_desc);

		snprintf(db_name, ARRAY_SIZE(db_name)-1, "group_extents_%09d", i);
		rc |= c2_balloc_open_db(dbenv, db_name, ctxt->bc_db_flags,
				        c2_balloc_blockno_compare,
				        &gi->bgi_db_group_extent);
		if (rc == 0)
			rc = c2_balloc_load_group_info(ctxt, gi);
		if (rc != 0) {
			db_err(dbenv, rc, "opening db");
			c2_balloc_fini(ctxt);
			break;
		}

		/* TODO verify the super_block info based on the group info */
	}

	c2_balloc_debug_dump_sb(__func__, ctxt);
	LEAVE;
	return rc;
}

int c2_balloc_txn_started(struct c2_balloc_ctxt *ctxt)
{
	return (ctxt->bc_tx != NULL);
}

int c2_balloc_start_txn(struct c2_balloc_ctxt *ctxt)
{
	DB_ENV	       *dbenv = ctxt->bc_dbenv;
	int 		rc;

	rc = dbenv->txn_begin(dbenv, NULL, &ctxt->bc_tx, ctxt->bc_txn_flags);
	if (ctxt->bc_tx == NULL)
		db_err(dbenv, rc, "cannot start transaction");
	return rc;
}

int c2_balloc_commit_txn(struct c2_balloc_ctxt *ctxt)
{
	int 		rc;

	C2_ASSERT(ctxt->bc_tx != NULL);
	rc = ctxt->bc_tx->commit(ctxt->bc_tx, 0);
	ctxt->bc_tx = NULL;
	if (rc != 0)
		db_err(ctxt->bc_dbenv, rc, "cannot commit transaction");
	return rc;
}


int c2_balloc_abort_txn(struct c2_balloc_ctxt *ctxt)
{
	int 		rc;

	C2_ASSERT(ctxt->bc_tx != NULL);
	rc = ctxt->bc_tx->abort(ctxt->bc_tx);
	ctxt->bc_tx = NULL;
	if (rc != 0)
		db_err(ctxt->bc_dbenv, rc, "cannot abort transaction");
	return rc;
}

static int 
c2_balloc_discard_prealloc_internal(struct c2_balloc_prealloc *prealloc)
{
	c2_list_del(&prealloc->bpr_link);
	return 0;
}

enum c2_balloc_allocation_status {
	C2_BALLOC_AC_STATUS_FOUND    = 1,
	C2_BALLOC_AC_STATUS_CONTINUE = 2,
	C2_BALLOC_AC_STATUS_BREAK    = 3,
};

struct c2_balloc_allocation_context {
	struct c2_balloc_ctxt         *bac_ctxt;
	struct c2_balloc_allocate_req *bac_req;
	struct c2_balloc_free_extent   bac_orig; /*< original */
	struct c2_balloc_free_extent   bac_goal; /*< after normalization */
	struct c2_balloc_free_extent   bac_best; /*< best available */
	struct c2_balloc_free_extent   bac_final;/*< final results */

	uint64_t		       bac_flags;
	uint64_t		       bac_criteria;
	uint32_t	               bac_order2;  /* order of 2 */
	uint32_t	               bac_scanned; /* groups scanned */
	uint32_t	               bac_found;   /* count of found */
	uint32_t	               bac_status;  /* allocation status */
};

static int c2_balloc_init_ac(struct c2_balloc_ctxt *ctxt,
			     struct c2_balloc_allocate_req *req,
			     struct c2_balloc_allocation_context *bac)
{
	ENTER;
	struct c2_balloc_super_block *sb = &ctxt->bc_sb;

	bac->bac_ctxt = ctxt;
	bac->bac_req  = req;
	req->bar_physical = 0;
	bac->bac_order2  = 0;
	bac->bac_scanned = 0;
	bac->bac_found   = 0;
	bac->bac_flags   = req->bar_flags;
	bac->bac_status  = C2_BALLOC_AC_STATUS_CONTINUE;
	bac->bac_criteria = 0;

	bac->bac_orig.bfe_logical = req->bar_logical;
	bac->bac_orig.bfe_groupno = req->bar_goal >> sb->bsb_gsbits;
	bac->bac_orig.bfe_start   = req->bar_goal;
	bac->bac_orig.bfe_len     = req->bar_len;

	bac->bac_goal = bac->bac_orig;

	memset(&bac->bac_best, 0, sizeof bac->bac_best);
	memset(&bac->bac_final, 0, sizeof bac->bac_final);

	LEAVE;
	return 0;
}


static int c2_balloc_use_prealloc(struct c2_balloc_allocation_context *bac)
{
	struct c2_balloc_prealloc *prealloc = bac->bac_req->bar_prealloc;
	ENTER;

	if (prealloc != NULL) {
		if (bac->bac_req->bar_len <= prealloc->bpr_remaining) {
			bac->bac_req->bar_physical = prealloc->bpr_physical +
					             prealloc->bpr_lcount -
					             prealloc->bpr_remaining;
			prealloc->bpr_remaining -= bac->bac_req->bar_len;
			if (prealloc->bpr_remaining == 0) {
				c2_balloc_discard_prealloc_internal(prealloc);
				bac->bac_req->bar_prealloc = NULL;
			}
			LEAVE;
			return 1;
		} else {
			/* let's  discard prealloc and search again. */
			c2_balloc_discard_prealloc_internal(prealloc);
			bac->bac_req->bar_prealloc = NULL;
		}
	}

	LEAVE;
	return 0;
}

static int c2_balloc_claim_free_blocks(struct c2_balloc_ctxt *ctxt,
				       c2_bcount_t blocks)
{
	int rc;
	ENTER;

	debugp("bsb_freeblocks = %llu, blocks=%llu\n",
		(unsigned long long)ctxt->bc_sb.bsb_freeblocks,
		(unsigned long long)blocks);
	c2_mutex_lock(&ctxt->bc_sb_mutex);
		rc = (ctxt->bc_sb.bsb_freeblocks >= blocks);
	c2_mutex_unlock(&ctxt->bc_sb_mutex);

	LEAVE;
	return rc;
}

/*
 * here we normalize request for locality group
 */
static void 
c2_balloc_normalize_group_request(struct c2_balloc_allocation_context *bac)
{
	if (bac->bac_ctxt->bc_sb.bsb_stripe_size)
		bac->bac_goal.bfe_len = bac->bac_ctxt->bc_sb.bsb_stripe_size;
}

/*
 * Normalization means making request better in terms of
 * size and alignment
 */
static void
c2_balloc_normalize_request(struct c2_balloc_allocation_context *bac)
{
	c2_bcount_t size = bac->bac_orig.bfe_len;
	ENTER;

	/* do normalize only for data requests. metadata requests
	   do not need preallocation */
	if (!(bac->bac_flags & C2_BALLOC_HINT_DATA))
		return;

	/* sometime caller may want exact blocks */
	if (bac->bac_flags & C2_BALLOC_HINT_GOAL_ONLY)
		return;

	/* caller may indicate that preallocation isn't
	 * required (it's a tail, for example) */
	if (bac->bac_flags & C2_BALLOC_HINT_NOPREALLOC)
		return;

	if (bac->bac_flags & C2_BALLOC_HINT_GROUP_ALLOC) {
		c2_balloc_normalize_group_request(bac);
		return;
	}

	if (size <= 4 ) {
		size = 4;
	} else if (size <= 8) {
		size = 8;
	} else if (size <= 16) {
		size = 16;
	} else if (size <= 32) {
		size = 32;
	} else if (size <= 64) {
		size = 64;
	} else if (size <= 128) {
		size = 128;
	} else if (size <= 256) {
		size = 256;
	} else if (size <= 512) {
		size = 512;
	} else if (size <= 1024) {
		size = 1024;
	} else if (size <= 2048) {
		size = 2048;
	} else {
		debugp("lenth %llu is too large, truncate to %llu\n",
			(unsigned long long) size, MAX_ALLOCATION_CHUNK);
		size = MAX_ALLOCATION_CHUNK;
	}

	if (size > bac->bac_ctxt->bc_sb.bsb_groupsize)
		size = bac->bac_ctxt->bc_sb.bsb_groupsize;

	/* now prepare goal request */
	bac->bac_goal.bfe_len = size;

	debugp("goal: start=%llu@%llu, size=%llu(was %llu) logical=%llu\n",
		(unsigned long long) bac->bac_goal.bfe_start,
		(unsigned long long) bac->bac_goal.bfe_groupno,
		(unsigned long long) bac->bac_goal.bfe_len,
		(unsigned long long) bac->bac_orig.bfe_len,
		(unsigned long long) bac->bac_goal.bfe_logical);
	LEAVE;
}

static void c2_balloc_lock_group(struct c2_balloc_group_info *grp)
{
	c2_mutex_lock(&grp->bgi_mutex);
}

static int c2_balloc_trylock_group(struct c2_balloc_group_info *grp)
{
	return c2_mutex_trylock(&grp->bgi_mutex);
}

static void c2_balloc_unlock_group(struct c2_balloc_group_info *grp)
{
	c2_mutex_unlock(&grp->bgi_mutex);
}

/* called under group lock */
static int c2_balloc_load_extents(struct c2_balloc_group_info *grp)
{
	DB  *db = grp->bgi_db_group_extent;
	DBC *cursor;
	DBT  nkeyt;
	DBT  nrect;
	int  result = 0;
	int size;
        c2_bcount_t maxchunk = 0;
	c2_bcount_t count = 0;
	
	if (grp->bgi_extents != NULL) {
		/* already loaded */
		return 0;
	}

	size = (grp->bgi_fragments + 1) * sizeof (struct c2_balloc_extent);
	grp->bgi_extents = c2_alloc(size);
	if (grp->bgi_extents == NULL)
		return -ENOMEM;

	if (grp->bgi_fragments == 0)
		goto out;

	result = db->cursor(db, NULL, &cursor, 0);
	if (result != 0) {
		c2_free(grp->bgi_extents);
		grp->bgi_extents = NULL;
		return result;
	}

	while (1) {
		memset(&nkeyt, 0, sizeof nkeyt);
		memset(&nrect, 0, sizeof nrect);
		nrect.flags = DB_DBT_MALLOC;
		nkeyt.flags = DB_DBT_MALLOC;

		result = cursor->get(cursor, &nkeyt,
				     &nrect, DB_NEXT);
		if ( result != 0)
			break;

		grp->bgi_extents[count].be_start = *((c2_bindex_t *)nkeyt.data);
		grp->bgi_extents[count].be_len = *((c2_bindex_t *)nrect.data);
		if (grp->bgi_extents[count].be_len > maxchunk)
			maxchunk = grp->bgi_extents[count].be_len;
//		debugp("...[%08llx, %08llx]\n",
//			(unsigned long long) grp->bgi_extents[count].be_start,
//			(unsigned long long) grp->bgi_extents[count].be_len);

		free(nkeyt.data);
		free(nrect.data);

		count++;
		if (count >= grp->bgi_fragments)
			break;
	}
	cursor->close(cursor);

	if (result == DB_NOTFOUND && count != grp->bgi_fragments)
		debugp("fragments mismatch: count=%llu, fragments=%lld\n",
			(unsigned long long)count,
			(unsigned long long)grp->bgi_fragments);
	if (result != 0) {
		c2_free(grp->bgi_extents);
		grp->bgi_extents = NULL;
	}

out:
	if (grp->bgi_maxchunk != maxchunk) {
		grp->bgi_state |= C2_BALLOC_GROUP_INFO_DIRTY;
	}

	return result;
}

static int c2_balloc_release_extents(struct c2_balloc_group_info *grp)
{
//	c2_free(grp->bgi_extents);
//	grp->bgi_extents = NULL;
	return 0;
}

/* called under group lock */
static int c2_balloc_find_extent_exact(struct c2_balloc_allocation_context *bac,
				       struct c2_balloc_group_info *grp,
				       struct c2_balloc_free_extent *goal,
				       struct c2_balloc_extent *ex)
{
	c2_bcount_t i;
	c2_bcount_t found = 0;
	c2_bindex_t start;
	c2_bcount_t len;
	struct c2_balloc_extent *fragment;

	for (i = 0; i < grp->bgi_fragments; i++) {
		fragment = &grp->bgi_extents[i];
		start = fragment->be_start;
		len   = fragment->be_len;

		if ((start <= goal->bfe_start) &&
		    (goal->bfe_start < start + len)) {
			found = 1;
			*ex = *fragment;
			c2_balloc_debug_dump_extent(__func__, ex);
			break;
		}
		if (start > goal->bfe_start)
			break;
	}

	return found;
}

/* called under group lock */
static int c2_balloc_find_extent_buddy(struct c2_balloc_allocation_context *bac,
				       struct c2_balloc_group_info *grp,
				       c2_bcount_t len,
				       struct c2_balloc_extent *ex)
{
	struct c2_balloc_super_block *sb = &bac->bac_ctxt->bc_sb;
	c2_bcount_t i;
	c2_bcount_t found = 0;
	c2_bindex_t start;
	struct c2_balloc_extent *fragment;
	struct c2_balloc_extent min = {.be_len = 0xffffffff };

	start = grp->bgi_groupno << sb->bsb_gsbits;

	for (i = 0; i < grp->bgi_fragments; i++) {
		fragment = &grp->bgi_extents[i];

repeat:
		{
			char msg[128];
			sprintf(msg, "buddy len=%d:%x start=%llu:%llx",
				(int)len, (int)len,
				(unsigned long long)start,
				(unsigned long long)start);
			(void)msg;
			c2_balloc_debug_dump_extent(msg, fragment);
		}
		if ((fragment->be_start == start) && (fragment->be_len >= len)){

			found = 1;
			if (fragment->be_len < min.be_len)
				min = *fragment;
		}
		if (fragment->be_start > start) {
			do {
				start += len;
			} while (fragment->be_start > start);
			if (start > ((grp->bgi_groupno + 1) << sb->bsb_gsbits))
				break;
			/* we changed the 'start'. let's restart seaching. */
			goto repeat;
		}
	}

	if (found)
		*ex = min;

	return found;
}


static int c2_balloc_use_best_found(struct c2_balloc_allocation_context *bac)
{
	bac->bac_best.bfe_logical = bac->bac_goal.bfe_logical;
	bac->bac_best.bfe_len = min_check(bac->bac_best.bfe_len, 
					  bac->bac_goal.bfe_len);

	/* preallocation can change bac_best, thus we store actually
	 * allocated blocks for history */
	bac->bac_final = bac->bac_best;
	bac->bac_status = C2_BALLOC_AC_STATUS_FOUND;

	return 0;
}

static int c2_balloc_new_preallocation(struct c2_balloc_allocation_context *bac)
{
	debugp("New pre-allocatoin: original len=%llu, result len=%llu\n",
		(unsigned long long)bac->bac_orig.bfe_len,
		(unsigned long long)bac->bac_best.bfe_len);
	return 0;
}

enum c2_balloc_update_operation {
	C2_BALLOC_ALLOC = 1,
	C2_BALLOC_FREE  = 2,
};


/* the group is under lock now */
static int c2_balloc_update_db(struct c2_balloc_ctxt * ctxt,
			       struct c2_balloc_group_info *grp,
			       struct c2_balloc_free_extent *target,
			       enum c2_balloc_update_operation op)
{
	c2_bcount_t i;
	int rc = 0;
	ENTER;

	c2_balloc_debug_dump_free_extent(__func__, target);
	if (op == C2_BALLOC_ALLOC) {
		struct c2_balloc_extent *current = NULL;

		for (i = 0; i < grp->bgi_fragments; i++) {
			current = &grp->bgi_extents[i];

			if ((current->be_start <= target->bfe_start) &&
			        (target->bfe_start < current->be_start + current->be_len))
				break;
		}

		C2_ASSERT(i < grp->bgi_fragments);

		c2_balloc_debug_dump_extent("current=", current);

		if (current->be_start == target->bfe_start) {
			c2_balloc_del_record(ctxt->bc_dbenv,
					     ctxt->bc_tx,
					     grp->bgi_db_group_extent,
					     &current->be_start,
					     sizeof (current->be_start));

			if (target->bfe_len < current->be_len) {
				current->be_start += target->bfe_len;
				current->be_len   -= target->bfe_len;
				c2_balloc_insert_record(ctxt->bc_dbenv,
							ctxt->bc_tx,
							grp->bgi_db_group_extent,
							&current->be_start,
							sizeof current->be_start,
							&current->be_len,
							sizeof current->be_len);
			} else {
				memmove(current, current + 1, (grp->bgi_fragments - i) * sizeof(struct c2_balloc_extent));
				grp->bgi_fragments--;
				grp->bgi_extents = realloc(grp->bgi_extents, (grp->bgi_fragments + 1) * sizeof (struct c2_balloc_extent));
			}
		} else {
			struct c2_balloc_extent next = *current;

			current->be_len = target->bfe_start - current->be_start;
			c2_balloc_debug_dump_extent("alloc 222222222222", current);

			c2_balloc_update_record(ctxt->bc_dbenv,
						ctxt->bc_tx,
						grp->bgi_db_group_extent,
						&current->be_start,
						sizeof current->be_start,
						&current->be_len,
						sizeof current->be_len);
			if (next.be_start + next.be_len > target->bfe_start + target->bfe_len) {
				next.be_len   = (next.be_start + next.be_len) - (target->bfe_start + target->bfe_len);
				next.be_start = target->bfe_start + target->bfe_len;
				c2_balloc_insert_record(ctxt->bc_dbenv,
							ctxt->bc_tx,
							grp->bgi_db_group_extent,
							&next.be_start,
							sizeof next.be_start,
							&next.be_len,
							sizeof next.be_len);
				c2_balloc_debug_dump_extent("alloc 333333333333", &next);
				current++;
				if (i + 1 < grp->bgi_fragments) {
					memmove(current + 1, current, (grp->bgi_fragments - i) * sizeof(struct c2_balloc_extent));
				}
				*current = next;
				grp->bgi_fragments++;
				grp->bgi_extents = realloc(grp->bgi_extents, (grp->bgi_fragments + 1) * sizeof (struct c2_balloc_extent));
			}
		}

		ctxt->bc_sb.bsb_freeblocks -= target->bfe_len;
		/* TODO update the maxchunk please */
		grp->bgi_freeblocks -= target->bfe_len;

		grp->bgi_state |= C2_BALLOC_GROUP_INFO_DIRTY;
		ctxt->bc_sb.bsb_state |= C2_BALLOC_SB_DIRTY;

		rc = c2_balloc_sync_sb(ctxt);
		rc |= c2_balloc_sync_group_info(ctxt, grp);

		LEAVE;
	} else if (op == C2_BALLOC_FREE) {
		struct c2_balloc_extent *current = NULL;
		struct c2_balloc_extent *prev = NULL;
		int found = 0;

		for (i = 0; i < grp->bgi_fragments; i++) {
			current = &grp->bgi_extents[i];

			if (target->bfe_start <= current->be_start) {
				found = 1;
				break;
			}
			prev = current;
		}
		c2_balloc_debug_dump_extent("prev=", prev);
		c2_balloc_debug_dump_extent("current=", current);

		if (current && target->bfe_start + target->bfe_len > current->be_start) {
			fprintf(stderr, "double free\n");
			return -EINVAL;
		}
		if (prev && prev->be_start + prev->be_len > target->bfe_start) {
			fprintf(stderr, "double free\n");
			return -EINVAL;
		}

		if (!found) {
			if (i == 0) {
				/* no fragments at all */
				c2_balloc_insert_record(ctxt->bc_dbenv,
							ctxt->bc_tx,
							grp->bgi_db_group_extent,
							&target->bfe_start,
							sizeof target->bfe_start,
							&target->bfe_len,
							sizeof target->bfe_len);

				current = &grp->bgi_extents[0];
				current->be_start = target->bfe_start;
				current->be_len   = target->bfe_len;
				grp->bgi_fragments++;
				grp->bgi_extents = realloc(grp->bgi_extents, (grp->bgi_fragments + 1) * sizeof (struct c2_balloc_extent));
			} else {
				/* at the tail */
				if (prev->be_start + prev->be_len < target->bfe_start) {
				 /* to be the last one, standalone*/
					c2_balloc_insert_record(ctxt->bc_dbenv,
								ctxt->bc_tx,
								grp->bgi_db_group_extent,
								&target->bfe_start,
								sizeof target->bfe_start,
								&target->bfe_len,
								sizeof target->bfe_len);

					current++;
					current->be_start = target->bfe_start;
					current->be_len   = target->bfe_len;
					grp->bgi_fragments++;
					grp->bgi_extents = realloc(grp->bgi_extents, (grp->bgi_fragments + 1) * sizeof (struct c2_balloc_extent));
				} else {
					prev->be_len   += target->bfe_len;
					c2_balloc_update_record(ctxt->bc_dbenv,
								ctxt->bc_tx,
								grp->bgi_db_group_extent,
								&prev->be_start,
								sizeof prev->be_start,
								&prev->be_len,
								sizeof prev->be_len);
				}
			}
		} else if (found && prev == NULL) {
			/* on the head */
			if (target->bfe_start + target->bfe_len < current->be_start) {
				/* to be the first one */
				c2_balloc_insert_record(ctxt->bc_dbenv,
							ctxt->bc_tx,
							grp->bgi_db_group_extent,
							&target->bfe_start,
							sizeof target->bfe_start,
							&target->bfe_len,
							sizeof target->bfe_len);

				memmove(current + 1, current, grp->bgi_fragments * sizeof(struct c2_balloc_extent));
				current = &grp->bgi_extents[0];
				current->be_start = target->bfe_start;
				current->be_len   = target->bfe_len;
				grp->bgi_fragments++;
				grp->bgi_extents = realloc(grp->bgi_extents, (grp->bgi_fragments + 1) * sizeof (struct c2_balloc_extent));
			} else {
				/* join the first one */
				c2_balloc_del_record(ctxt->bc_dbenv,
						     ctxt->bc_tx,
						     grp->bgi_db_group_extent,
						     &current->be_start,
						     sizeof (current->be_start));
				current->be_start = target->bfe_start;
				current->be_len   = target->bfe_len + current->be_len;
				c2_balloc_insert_record(ctxt->bc_dbenv,
							ctxt->bc_tx,
							grp->bgi_db_group_extent,
							&current->be_start,
							sizeof current->be_start,
							&current->be_len,
							sizeof current->be_len);
			}
		} else {
			/* in the middle */
			if (prev->be_start + prev->be_len == target->bfe_start && target->bfe_start + target->bfe_len == current->be_start) {
				/* joint to both */
				c2_balloc_del_record(ctxt->bc_dbenv,
						     ctxt->bc_tx,
						     grp->bgi_db_group_extent,
						     &current->be_start,
						     sizeof (current->be_start));
				prev->be_len   += target->bfe_len + current->be_len;
				c2_balloc_update_record(ctxt->bc_dbenv,
							ctxt->bc_tx,
							grp->bgi_db_group_extent,
							&prev->be_start,
							sizeof prev->be_start,
							&prev->be_len,
							sizeof prev->be_len);
				memmove(current, current + 1, (grp->bgi_fragments - i) * sizeof(struct c2_balloc_extent));
				grp->bgi_fragments--;
				grp->bgi_extents = realloc(grp->bgi_extents, (grp->bgi_fragments + 1) * sizeof (struct c2_balloc_extent));
			} else if (prev->be_start + prev->be_len == target->bfe_start) {
				/* joint with prev */
				prev->be_len += target->bfe_len;
				c2_balloc_update_record(ctxt->bc_dbenv,
							ctxt->bc_tx,
							grp->bgi_db_group_extent,
							&prev->be_start,
							sizeof prev->be_start,
							&prev->be_len,
							sizeof prev->be_len);
			} else if (target->bfe_start + target->bfe_len == current->be_start) {
				/* joint with current */
				c2_balloc_del_record(ctxt->bc_dbenv,
						     ctxt->bc_tx,
						     grp->bgi_db_group_extent,
						     &current->be_start,
						     sizeof (current->be_start));
				current->be_start = target->bfe_start;
				current->be_len   = target->bfe_len + current->be_len;
				c2_balloc_insert_record(ctxt->bc_dbenv,
							ctxt->bc_tx,
							grp->bgi_db_group_extent,
							&current->be_start,
							sizeof current->be_start,
							&current->be_len,
							sizeof current->be_len);
			} else {
				/* add a new one */
				c2_balloc_insert_record(ctxt->bc_dbenv,
							ctxt->bc_tx,
							grp->bgi_db_group_extent,
							&target->bfe_start,
							sizeof target->bfe_start,
							&target->bfe_len,
							sizeof target->bfe_len);

				memmove(current + 1, current, grp->bgi_fragments * sizeof(struct c2_balloc_extent));
				current->be_start = target->bfe_start;
				current->be_len   = target->bfe_len;
				grp->bgi_fragments++;
				grp->bgi_extents = realloc(grp->bgi_extents, (grp->bgi_fragments + 1) * sizeof (struct c2_balloc_extent));
			}
		}

		ctxt->bc_sb.bsb_freeblocks += target->bfe_len;
		/* TODO update the maxchunk please */
		grp->bgi_freeblocks += target->bfe_len;

		grp->bgi_state |= C2_BALLOC_GROUP_INFO_DIRTY;
		ctxt->bc_sb.bsb_state |= C2_BALLOC_SB_DIRTY;

		rc = c2_balloc_sync_sb(ctxt);
		rc |= c2_balloc_sync_group_info(ctxt, grp);

		LEAVE;
	} else {
		rc = -EINVAL;
		LEAVE;
	}
	return rc;
}

static int c2_balloc_find_by_goal(struct c2_balloc_allocation_context *bac)
{
	c2_bindex_t group = bac->bac_goal.bfe_groupno;
	struct c2_balloc_group_info *grp = &bac->bac_ctxt->bc_group_info[group];
	struct c2_balloc_extent ex = { 0 };
	int found;
	int ret = 0;
	ENTER;
	return 0;

	debugp("groupno=%llu, start=%llu len=%llu, groupsize (%llu)\n",
		(unsigned long long)bac->bac_goal.bfe_groupno,
		(unsigned long long)bac->bac_goal.bfe_start,
		(unsigned long long)bac->bac_goal.bfe_len,
		(unsigned long long)bac->bac_ctxt->bc_sb.bsb_groupsize
        );

	if (!(bac->bac_flags & C2_BALLOC_HINT_TRY_GOAL))
		goto out;

	c2_balloc_lock_group(grp);
	if (grp->bgi_maxchunk < bac->bac_goal.bfe_len)
		goto out_unlock;	

	ret = c2_balloc_load_extents(grp);
	if (ret)
		goto out_unlock;

	found = c2_balloc_find_extent_exact(bac, grp, &bac->bac_goal, &ex);
	debugp("found ? max len = %llu\n", (unsigned long long)ex.be_len);

	if (found) {
		bac->bac_found++;
		bac->bac_best.bfe_groupno = grp->bgi_groupno;
		bac->bac_best.bfe_start   = bac->bac_goal.bfe_start;
		bac->bac_best.bfe_len     = ex.be_start + ex.be_len - bac->bac_goal.bfe_start;
		ret = c2_balloc_use_best_found(bac);
	}

	/* update db according to the allocation result */
	if (ret == 0 && bac->bac_status == C2_BALLOC_AC_STATUS_FOUND) {
		if (bac->bac_goal.bfe_len < bac->bac_best.bfe_len)
			c2_balloc_new_preallocation(bac);

		ret = c2_balloc_update_db(bac->bac_ctxt, grp,
					  &bac->bac_final, C2_BALLOC_ALLOC);
	}

	c2_balloc_release_extents(grp);
out_unlock:
	c2_balloc_unlock_group(grp);
out:
	LEAVE;
	return ret;
}

static int c2_balloc_good_group(struct c2_balloc_allocation_context *bac,
			        struct c2_balloc_group_info *gi)
{
	c2_bcount_t free = gi->bgi_freeblocks;
	c2_bcount_t fragments = gi->bgi_fragments;

	if (free == 0)
		return 0;

	if (fragments == 0)
		return 0;

	switch (bac->bac_criteria) {
	case 0:
		if (gi->bgi_maxchunk > bac->bac_goal.bfe_len)
			return 1;
		break;
	case 1:
		if ((free / fragments) >= bac->bac_goal.bfe_len)
			return 1;
		break;
	case 2:
		if (free >= bac->bac_goal.bfe_len)
			return 1;
		break;
	case 3:
		return 1;
	default:
		break;
	}

	return 0;
}

static int c2_balloc_simple_scan_group(struct c2_balloc_allocation_context *bac,
				       struct c2_balloc_group_info *grp)
{
	struct c2_balloc_super_block *sb = &bac->bac_ctxt->bc_sb;
	struct c2_balloc_extent ex;
	c2_bcount_t len;
	int found = 0;
	ENTER;

	C2_ASSERT(bac->bac_order2 > 0);

	len = 1 << bac->bac_order2;
	for (; len <= sb->bsb_groupsize; len = len << 1) {
		debugp("searching at %d (gs = %d) for order = %d, len=%d:%x\n",
			(int)grp->bgi_groupno,
			(int)sb->bsb_groupsize,
			(int)bac->bac_order2,
			(int)len,
			(int)len);

		found = c2_balloc_find_extent_buddy(bac, grp, len, &ex);
		if (found)
			break;
	}
	if (found) {
		c2_balloc_debug_dump_extent(__func__, &ex);

		bac->bac_found++;
		bac->bac_best.bfe_groupno = grp->bgi_groupno;
		bac->bac_best.bfe_start   = ex.be_start;
		bac->bac_best.bfe_len     = bac->bac_goal.bfe_len;
		c2_balloc_use_best_found(bac);
	}

	LEAVE;
	return 0;
}

int c2_balloc_scan_aligned(struct c2_balloc_allocation_context *bac)
{
	return 0;
}

static int
c2_balloc_complex_scan_group(struct c2_balloc_allocation_context *bac)
{
	return 0;
}

static int c2_balloc_try_best_found(struct c2_balloc_allocation_context *bac)
{
	return 0;
}

static int
c2_balloc_regular_allocator(struct c2_balloc_allocation_context *bac)
{
	c2_bcount_t ngroups, group, i;
	int cr;
	int rc = 0;
	ENTER;

	ngroups = bac->bac_ctxt->bc_sb.bsb_groupcount;

	/* first, try the goal */
	rc = c2_balloc_find_by_goal(bac);
	if (rc != 0 || bac->bac_status == C2_BALLOC_AC_STATUS_FOUND ||
	    (bac->bac_flags & C2_BALLOC_HINT_GOAL_ONLY)) {
		LEAVE;
		return rc;
	}

	/* XXX ffs works on little-endian platform? */
	i = ffs(bac->bac_goal.bfe_len);
	bac->bac_order2 = 0;
	/*
	 * We search using buddy data only if the order of the request
	 * is greater than equal to the threshould.
	 */
	if (i >= 2) {
		/*
		 * This should tell if fe_len is exactly power of 2
		 */
		if ((bac->bac_goal.bfe_len & (~(1 << (i - 1)))) == 0)
			bac->bac_order2 = i - 1;
        }

	cr = bac->bac_order2 ? 0 : 1;
	/*
	 * cr == 0 try to get exact allocation,
	 * cr == 2  try to get anything
	 */
repeat:
	for (;cr < 3 && bac->bac_status == C2_BALLOC_AC_STATUS_CONTINUE; cr++) {
		bac->bac_criteria = cr;
		/*
		 * searching for the right group start
		 * from the goal value specified
		 */
		group = bac->bac_goal.bfe_groupno;

		for (i = 0; i < ngroups; group++, i++) {
			struct c2_balloc_group_info *grp;

			if (group == ngroups)
				group = 0;

			grp = &bac->bac_ctxt->bc_group_info[group];

			rc = c2_balloc_trylock_group(grp);
			if (rc != 0) {
				/* This group is under processing by others. */
				continue;
			}

			/* quick check to skip empty groups */
			if (grp->bgi_freeblocks == 0) {
				c2_balloc_unlock_group(grp);
				continue;
			}

			rc = c2_balloc_load_extents(grp);
			if (rc != 0) {
				c2_balloc_unlock_group(grp);
				goto out;
			}

			if (!c2_balloc_good_group(bac, grp)) {
				c2_balloc_unlock_group(grp);
				continue;
			}
			bac->bac_scanned++;

			if (cr == 0)
				rc = c2_balloc_simple_scan_group(bac, grp);
			else if (cr == 1 &&
					bac->bac_goal.bfe_len == bac->bac_ctxt->bc_sb.bsb_stripe_size)
				rc = c2_balloc_simple_scan_group(bac, grp);
			else
				rc = c2_balloc_complex_scan_group(bac);

			/* update db according to the allocation result */
			if (rc == 0 && bac->bac_status == C2_BALLOC_AC_STATUS_FOUND) {
				if (bac->bac_goal.bfe_len < bac->bac_best.bfe_len)
					c2_balloc_new_preallocation(bac);

				rc = c2_balloc_update_db(bac->bac_ctxt, grp,
							 &bac->bac_final,
							 C2_BALLOC_ALLOC);
			}


			c2_balloc_release_extents(grp);
			c2_balloc_unlock_group(grp);

			if (bac->bac_status != C2_BALLOC_AC_STATUS_CONTINUE)
				break;
		}
	}

	if (bac->bac_best.bfe_len > 0 && bac->bac_status != C2_BALLOC_AC_STATUS_FOUND &&
	    !(bac->bac_flags & C2_BALLOC_HINT_FIRST)) {
		/*
		 * We've been searching too long. Let's try to allocate
		 * the best chunk we've found so far
		 */

		c2_balloc_try_best_found(bac);
		if (bac->bac_status != C2_BALLOC_AC_STATUS_FOUND) {
			/*
			 * Someone more lucky has already allocated it.
			 * The only thing we can do is just take first
			 * found block(s)
			printk(KERN_DEBUG "EXT4-fs: someone won our chunk\n");
			 */
			memset(&bac->bac_best, 0 , sizeof bac->bac_best);
			bac->bac_status = C2_BALLOC_AC_STATUS_CONTINUE;
			bac->bac_flags |= C2_BALLOC_HINT_FIRST;
			cr = 3;
			goto repeat;
		}
	}
out:
	LEAVE;
	return rc;
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

   @param ctxt balloc operation context environment.
   @param req allocate request which includes all parameters.
   @return 0 means success.
	   Result allocated blocks are again stored in "req":
	   result physical block number = bar_physical,
	   result count of blocks = bar_len.
           Upon failure, non-zero error number is returned.
 */
int c2_balloc_allocate(struct c2_balloc_ctxt *ctxt,
		       struct c2_balloc_allocate_req *req)
{
	struct c2_balloc_allocation_context bac;
	int rc;
	int start_txn = 0;
	ENTER;


	if (!c2_balloc_txn_started(ctxt)) {
		rc = c2_balloc_start_txn(ctxt);
		if (rc != 0)
			return rc;
		start_txn = 1;
	}

	c2_balloc_init_ac(ctxt, req, &bac);

	while (req->bar_len &&
	       !c2_balloc_claim_free_blocks(ctxt, req->bar_len)) {
		req->bar_len = req->bar_len >> 1;
	}
	if (!req->bar_len) {
		rc = -ENOSPC;
		goto out;
	}

	/* Step 1. query the pre-allocation */
	if (!c2_balloc_use_prealloc(&bac)) {
		/* we did not find suitable free space in prealloc. */

		/* Step 2. Iterate over groups */
		c2_balloc_normalize_request(&bac);

		rc = c2_balloc_regular_allocator(&bac);
		bac.bac_req->bar_err = rc;
		if (rc == 0 && bac.bac_status == C2_BALLOC_AC_STATUS_FOUND) {
			bac.bac_req->bar_physical = bac.bac_final.bfe_start;
			bac.bac_req->bar_len = bac.bac_final.bfe_len;
		}
	}
out:
	if (start_txn) {
		if (rc == 0)
			c2_balloc_commit_txn(ctxt);
		else
			c2_balloc_abort_txn(ctxt);
	}
	
	LEAVE;
	return rc;
}

/**
   Free multiple blocks owned by some object to free space.

   @param ctxt balloc operation context environment.
   @param req block free request which includes all parameters.
   @return 0 means success. Upon failure, non-zero error number is returned.
 */
int c2_balloc_free(struct c2_balloc_ctxt *ctxt, struct c2_balloc_free_req *req)
{
	struct c2_balloc_free_extent fex;
	struct c2_balloc_group_info *grp;
	struct c2_balloc_super_block *sb = &ctxt->bc_sb;
	c2_bcount_t group;
	c2_bindex_t start, off;
	c2_bcount_t len, step;
	c2_bcount_t mask;
	int start_txn = 0;
	int rc = 0;
	ENTER;

	mask = ~(sb->bsb_groupsize - 1);

	start = req->bfr_physical;
	len = req->bfr_len;

	group = (start + len) >> sb->bsb_gsbits;
	debugp("start=0x%llx, len=0x%llx, start_group=%llu, end_group=%llu, group count=%llu\n",
		(unsigned long long)start,
		(unsigned long long)len,
		(unsigned long long)start >> sb->bsb_gsbits,
		(unsigned long long)(start + len) >> sb->bsb_gsbits,
		(unsigned long long)sb->bsb_groupcount
		);
	if (group > sb->bsb_groupcount)
		return -EINVAL;

	if (!c2_balloc_txn_started(ctxt)) {
		rc = c2_balloc_start_txn(ctxt);
		if (rc != 0)
			return rc;
		start_txn = 1;
	}

	while (rc == 0 && len > 0) {
		group = start >> sb->bsb_gsbits;

		grp = &ctxt->bc_group_info[group];
		c2_balloc_lock_group(grp);

		rc = c2_balloc_load_extents(grp);
		if (rc != 0) {
			c2_balloc_unlock_group(grp);
			goto out;
		}

		off = start & (sb->bsb_groupsize - 1);
		step = (off + len > sb->bsb_groupsize) ? sb->bsb_groupsize  - off : len;

		fex.bfe_logical = 0;
		fex.bfe_groupno = group;
		fex.bfe_start   = start;
		fex.bfe_len     = step;
		c2_balloc_debug_dump_free_extent("freeing...", &fex);
		rc = c2_balloc_update_db(ctxt, grp, &fex, C2_BALLOC_FREE);
		c2_balloc_release_extents(grp);
		c2_balloc_unlock_group(grp);
		start += step;
		len -= step;
	}

out:
	if (start_txn) {
		if (rc == 0)
			c2_balloc_commit_txn(ctxt);
		else
			c2_balloc_abort_txn(ctxt);
	}

	LEAVE;
	return rc;
}

/**
   Discard the pre-allocation for object.

   @param ctxt balloc operation context environment.
   @param req discard request which includes all parameters.
   @return 0 means success. Upon failure, non-zero error number is returned.
 */
int c2_balloc_discard_prealloc(struct c2_balloc_ctxt *ctxt,
			       struct c2_balloc_discard_req *req)
{

	return 0;
}

/**
   modify the allocation status forcibly.

   This function may be used by fsck or some other tools to modify the
   allocation status directly.

   @param ctxt balloc operation context environment.
   @param alloc true to make the specifed extent as allocated, otherwise make
          the extent as free.
   @param ext user supplied extent to check.
   @return 0 means success. Upon failure, non-zero error number is returned.
 */
int c2_balloc_enforce(struct c2_balloc_ctxt *ctxt, bool alloc,
		      struct c2_balloc_extent *ext)
{
	return 0;
}


/**
   Query the allocation status.

   @param ctxt balloc operation context environment.
   @param ext user supplied extent to check.
   @return true if the extent is fully allocated. Otherwise, false is returned.
 */
bool c2_balloc_query(struct c2_balloc_ctxt *ctxt, struct c2_balloc_extent *ext)
{

	return false;
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
