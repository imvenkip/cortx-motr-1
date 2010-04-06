#ifndef __COLIBRI_DBTYPES_H
#define __COLIBRI_DBTYPES_H

/*
 * db4 "schema"
 */ 

typedef unsigned long long u64;
typedef unsigned long      u32;

struct fid {
	u64 id;
};

typedef u64 rowid_t;

struct namespace_key {
	struct fid nk_parent;
	char       nk_name[0];
};

/* Kind of file inode (stat data item) */
struct namespace_rec {
	struct fid nr_child;   /* child fid */
	size_t     nr_size;    /* file size */
	u_int64_t  nr_blocks;  /* file block */
	u_int64_t  nr_mtime;
	u_int64_t  nr_atime;
	u_int64_t  nr_ctime;
};

struct oi_key {
	struct fid ok_fid;
	u_int32_t  ok_nlink;
};

struct oi_rec {
	struct fid or_parent;
	char       or_name[0];
};

typedef struct fid fid_t;

#endif
