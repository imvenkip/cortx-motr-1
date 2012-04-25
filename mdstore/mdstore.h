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
 * Original creation date: 01/17/2011
 */

#ifndef __COLIBRI_MDSTORE_MDSTORE_H__
#define __COLIBRI_MDSTORE_MDSTORE_H__

#include "mdservice/md_fops_u.h"

struct c2_cob_domain_id;
struct c2_cob_domain;
struct c2_stob_id;
struct c2_db_tx;
struct c2_dbenv;
struct c2_fid;
struct c2_fop;
struct c2_cob;

struct c2_md_store {
        struct c2_cob_domain  md_dom;
        struct c2_cob        *md_root;

        /** 
           An ADDB context for events related to this store.
        */
        struct c2_addb_ctx    md_addb;
};

#define C2_MD_ROOT_NAME "ROOT"
extern struct c2_fid C2_MD_ROOT_FID;

/**
   Flags supplied to c2_md_store_locate() to point out where a cob 
   should be found: on store, in opened files table or orhans table.
*/
enum c2_md_store_locate_flags {
        /** Find cob on store. */
        C2_MD_LOCATE_STORED  = 1 << 0,
        /** Find cob in opened cobs table. */
        C2_MD_LOCATE_OPENED  = 1 << 1,
        /** Find cob in orphans table. */
        C2_MD_LOCATE_ORPHAN  = 1 << 2
};

/**
   Valid flags attribute updates.
*/
enum c2_md_valid_flags {
        C2_MD_ATIME   = 1 << 0,
        C2_MD_MTIME   = 1 << 1,
        C2_MD_CTIME   = 1 << 2,
        C2_MD_SIZE    = 1 << 3,
        C2_MD_MODE    = 1 << 4,
        C2_MD_UID     = 1 << 5,
        C2_MD_GID     = 1 << 6,
        C2_MD_BLOCKS  = 1 << 7,
        C2_MD_TYPE    = 1 << 8,
        C2_MD_FLAGS   = 1 << 9,
        C2_MD_NLINK   = 1 << 10,
        C2_MD_RDEV    = 1 << 11,
        C2_MD_BLKSIZE = 1 << 12
};

enum c2_md_fmode_flags {
        C2_MD_FMODE_READ   = 1 << 0,
        C2_MD_FMODE_WRITE  = 1 << 1,
        C2_MD_FMODE_LSEEK  = 1 << 2,
        C2_MD_FMODE_PREAD  = 1 << 3,
        C2_MD_FMODE_PWRITE = 1 << 4,
        C2_MD_FMODE_EXEC   = 1 << 5,

        /**
           Other flags from system fmode space are not of 
           interest right now.
         */
};

enum c2_md_acc_flags {
        C2_MD_MAY_EXEC     = 1 << 0,
        C2_MD_MAY_WRITE    = 1 << 1,
        C2_MD_MAY_READ     = 1 << 2,
        C2_MD_MAY_APPEND   = 1 << 3,
        C2_MD_MAY_ACCESS   = 1 << 4,
        C2_MD_MAY_OPEN     = 1 << 5,
        C2_MD_MAY_CHDIR    = 1 << 6
};

enum c2_md_open_flags {
        C2_MD_OPEN_CREAT   = 00000100,
        C2_MD_OPEN_EXCL    = 00000200,
        C2_MD_OPEN_TRUNC   = 00001000,
        C2_MD_OPEN_APPEND  = 00002000,
        C2_MD_OPEN_SYNC    = 00010000,
        C2_MD_OPEN_DIR     = 00200000
};

enum c2_md_bias_flags {
        C2_MD_BIAS_SCAN_IN_PROGRESS = 1 << 0
};

static inline int accmode(uint32_t flags)
{
        int result = 0;
        
        if (flags & C2_MD_FMODE_READ)
                result |= C2_MD_MAY_READ;
        if (flags & (C2_MD_FMODE_WRITE | C2_MD_OPEN_TRUNC | C2_MD_OPEN_APPEND))
                result |= C2_MD_MAY_WRITE;
        if (flags & C2_MD_FMODE_EXEC)
                result |= C2_MD_MAY_EXEC;
        return result;
}

/**
   These two structures used for testing mdstore functionality. To do
   so we use changelog dump created by dump.changelog program, parse
   it, convert to fops and feed to test program in fops form.
*/
struct c2_md_lustre_fid {
        uint64_t f_seq;
        uint32_t f_oid;
        uint32_t f_ver;
};

struct c2_md_lustre_logrec {
        uint16_t                 cr_namelen;
        uint16_t                 cr_flags;
        uint16_t                 cr_valid;
        uint32_t                 cr_mode;
        uint8_t                  cr_type;
        uint64_t                 cr_index;
        uint64_t                 cr_prev;
        uint64_t                 cr_time;
        uint64_t                 cr_atime;
        uint64_t                 cr_ctime;
        uint64_t                 cr_mtime;
        uint32_t                 cr_nlink;
        uint32_t                 cr_rdev;
        uint64_t                 cr_version;
        uint64_t                 cr_size;
        uint64_t                 cr_blocks;
        uint64_t                 cr_blksize;
        uint32_t                 cr_uid;
        uint32_t                 cr_gid;
        uint32_t                 cr_sid;
        uint64_t                 cr_clnid;
        struct c2_md_lustre_fid  cr_tfid;
        struct c2_md_lustre_fid  cr_pfid;
        char                     cr_name[0];
} __attribute__((packed));

enum c2_md_lustre_logrec_type {
        RT_MARK     = 0,
        RT_CREATE   = 1,
        RT_MKDIR    = 2,
        RT_HARDLINK = 3,
        RT_SOFTLINK = 4,
        RT_MKNOD    = 5,
        RT_UNLINK   = 6,
        RT_RMDIR    = 7,
        RT_RENAME   = 8,
        RT_EXT      = 9,
        RT_OPEN     = 10,
        RT_CLOSE    = 11,
        RT_IOCTL    = 12,
        RT_TRUNC    = 13,
        RT_SETATTR  = 14,
        RT_XATTR    = 15,
        RT_HSM      = 16,
        RT_MTIME    = 17,
        RT_CTIME    = 18,
        RT_ATIME    = 19,
        RT_LAST
};

struct c2_dirent {
        uint32_t             d_namelen;
        uint32_t             d_reclen;
        char                 d_name[0];
};

struct c2_rdpg {
        struct c2_bitstring *r_pos;
        struct c2_buf        r_buf;
        struct c2_bitstring *r_end;
};

/**
   Init mdstore and get it ready to work. If init_root == !!1
   then root cob is initialized.
*/
int c2_md_store_init(struct c2_md_store         *md, 
                     struct c2_cob_domain_id    *id,
                     struct c2_dbenv            *db, 
                     int                         init_root);

/**
   Finalize mdstore instance.
*/
void c2_md_store_fini(struct c2_md_store        *md);

/**
   Handle link operation described by @pfid and @name. Input
   cob is so called statdata cob and returned by c2_cob_locate(). 
   Error code is returned in error case or zero otherwise.
*/
int c2_md_store_link(struct c2_md_store         *md, 
                     struct c2_fid              *pfid, 
                     struct c2_cob              *cob, 
                     const char                 *name,
                     int                         namelen,
                     struct c2_db_tx            *tx);
                     
/**
   Handle unlink operation described by @pfid and @name. Input
   cob is so called statdata cob and returned by c2_cob_locate(). 
   Error code is returned in error case or zero otherwise.
*/
int c2_md_store_unlink(struct c2_md_store       *md, 
                       struct c2_fid            *pfid, 
                       struct c2_cob            *cob,
                       const char               *name,
                       int                       namelen,
                       struct c2_db_tx          *tx);
                       
/**
   Handle rename operation described by params. Input cobs are
   statdata cobs and returned by c2_cob_locate(). Rest of the
   arguments are self explanatory.

   Error code is returned in error case or zero otherwise.
*/
int c2_md_store_rename(struct c2_md_store       *md, 
                       struct c2_fid            *pfid_tgt,
                       struct c2_fid            *pfid_src,
                       struct c2_cob            *cob_tgt,
                       struct c2_cob            *cob_src,
                       const char               *tname,
                       int                       tnamelen,
                       const char               *sname,
                       int                       snamelen,
                       struct c2_db_tx          *tx);

/**
   Handle create operation described by @attr on @cob. Input @cob
   is returned by c2_cob_alloc().
   
   Error code is returned in error case or zero otherwise.
*/
int c2_md_store_create(struct c2_md_store       *md, 
                       struct c2_fid            *pfid,
                       struct c2_cob_attr       *attr,
                       struct c2_cob           **out,
                       struct c2_db_tx          *tx);

/**
   Handle open operation described by @flags on @cob. Input @cob
   is so called statdata cob and returned by c2_cob_locate(). 
   Error code is returned in error case or zero otherwise.
*/
int c2_md_store_open(struct c2_md_store         *md, 
                     struct c2_cob              *cob,
                     int                         flags,
                     struct c2_db_tx            *tx);

/**
   Handle close operation on @cob. Input @cob is so called statdata
   cob and returned by c2_cob_locate(). 

   Error code is returned in error case or zero otherwise.
*/
int c2_md_store_close(struct c2_md_store        *md, 
                      struct c2_cob             *cob,
                      struct c2_db_tx           *tx);

/**
   Handle setattr operation described by @attr on @cob. Input @cob
   is so called statdata cob and returned by c2_cob_locate(). 
   
   Error code is returned in error case or zero otherwise.
*/
int c2_md_store_setattr(struct c2_md_store      *md, 
                        struct c2_cob           *cob,
                        struct c2_cob_attr      *attr,
                        struct c2_db_tx         *tx);

/**
   Get attributes of @cob into passed @attr. Input @cob
   is so called statdata cob and returned by c2_cob_locate(). 
   
   Error code is returned in error case or zero otherwise.
*/
int c2_md_store_getattr(struct c2_md_store      *md,
                        struct c2_cob           *cob,
                        struct c2_cob_attr      *attr,
                        struct c2_db_tx         *tx);

/**
   Handle readdir operation described by @rdpg on @cob. Input @cob
   is so called statdata cob and returned by c2_cob_locate(). 
   
   Error code is returned in error case or something >= 0 otherwise.
*/
int c2_md_store_readdir(struct c2_md_store      *md, 
                        struct c2_cob           *cob,
                        struct c2_rdpg          *rdpg,
                        struct c2_db_tx         *tx);

/**
   Find cob by fid.
*/
int c2_md_store_locate(struct c2_md_store       *md, 
                       const struct c2_fid      *fid,
                       struct c2_cob           **cob, 
                       int                       flags, 
                       struct c2_db_tx          *tx);

/**
   Find cob by parent and name.
*/
int c2_md_store_lookup(struct c2_md_store       *md, 
                       struct c2_fid            *pfid,
                       const char               *name, 
                       int                       namelen, 
                       struct c2_cob           **cob, 
                       struct c2_db_tx          *tx);

/**
   Get path by @fid. Path @path is allocated by
   c2_alloc() on success and path is saved there.
   When it is not longer needed it may be freed
   with c2_free().
 */
int c2_md_store_path(struct c2_md_store         *md,
                     struct c2_fid              *fid,
                     char                      **path);

/* __COLIBRI_MDSTORE_MDSTORE_H__ */
#endif

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
