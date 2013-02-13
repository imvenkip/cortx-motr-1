/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 03/29/2011
 */

#pragma once

#ifndef __MERO_MDSERVICE_MD_FOPS_H__
#define __MERO_MDSERVICE_MD_FOPS_H__

#include "lib/types.h"
#include "xcode/xcode_attr.h"
#include "fid/fid_xc.h"
#include "fid/fid.h"

extern struct m0_fop_type m0_fop_create_fopt;
extern struct m0_fop_type m0_fop_lookup_fopt;
extern struct m0_fop_type m0_fop_link_fopt;
extern struct m0_fop_type m0_fop_unlink_fopt;
extern struct m0_fop_type m0_fop_open_fopt;
extern struct m0_fop_type m0_fop_close_fopt;
extern struct m0_fop_type m0_fop_setattr_fopt;
extern struct m0_fop_type m0_fop_getattr_fopt;
extern struct m0_fop_type m0_fop_setxattr_fopt;
extern struct m0_fop_type m0_fop_getxattr_fopt;
extern struct m0_fop_type m0_fop_delxattr_fopt;
extern struct m0_fop_type m0_fop_listxattr_fopt;
extern struct m0_fop_type m0_fop_statfs_fopt;
extern struct m0_fop_type m0_fop_rename_fopt;
extern struct m0_fop_type m0_fop_readdir_fopt;
extern struct m0_fop_type m0_fop_layout_fopt;

extern struct m0_fop_type m0_fop_create_rep_fopt;
extern struct m0_fop_type m0_fop_lookup_rep_fopt;
extern struct m0_fop_type m0_fop_link_rep_fopt;
extern struct m0_fop_type m0_fop_unlink_rep_fopt;
extern struct m0_fop_type m0_fop_open_rep_fopt;
extern struct m0_fop_type m0_fop_close_rep_fopt;
extern struct m0_fop_type m0_fop_setattr_rep_fopt;
extern struct m0_fop_type m0_fop_getattr_rep_fopt;
extern struct m0_fop_type m0_fop_setxattr_rep_fopt;
extern struct m0_fop_type m0_fop_getxattr_rep_fopt;
extern struct m0_fop_type m0_fop_delxattr_rep_fopt;
extern struct m0_fop_type m0_fop_listxattr_rep_fopt;
extern struct m0_fop_type m0_fop_statfs_rep_fopt;
extern struct m0_fop_type m0_fop_rename_rep_fopt;
extern struct m0_fop_type m0_fop_readdir_rep_fopt;
extern struct m0_fop_type m0_fop_layout_rep_fopt;

struct m0_fop_str {
        uint32_t s_len;
        uint8_t *s_buf;
} M0_XCA_SEQUENCE;

struct m0_fop_cob {
        uint32_t      b_rc;
        uint64_t      b_index;
        uint64_t      b_version;
        uint32_t      b_flags;
        uint32_t      b_valid;
        uint32_t      b_mode;
        uint64_t      b_size;
        uint64_t      b_blksize;
        uint64_t      b_blocks;
        uint32_t      b_nlink;
        uint32_t      b_uid;
        uint32_t      b_gid;
        uint32_t      b_sid;
        uint64_t      b_nid;
        uint32_t      b_rdev;
        uint32_t      b_atime;
        uint32_t      b_mtime;
        uint32_t      b_ctime;
        uint64_t      b_lid;
        struct m0_fid b_pfid;
        struct m0_fid b_tfid;
} M0_XCA_RECORD;

struct m0_fop_buf {
        uint32_t b_count;
        uint8_t *b_addr;
} M0_XCA_SEQUENCE;

struct m0_fop_create {
        struct m0_fop_cob c_body;
        struct m0_fop_str c_target;
        struct m0_fop_str c_path;
        struct m0_fop_str c_name;
} M0_XCA_RECORD;

struct m0_fop_create_rep {
        struct m0_fop_cob c_body;
} M0_XCA_RECORD;

struct m0_fop_lookup {
        struct m0_fop_cob l_body;
        struct m0_fop_str l_path;
        struct m0_fop_str l_name;
} M0_XCA_RECORD;

struct m0_fop_lookup_rep {
        struct m0_fop_cob l_body;
} M0_XCA_RECORD;

struct m0_fop_link {
        struct m0_fop_cob l_body;
        struct m0_fop_str l_spath;
        struct m0_fop_str l_tpath;
        struct m0_fop_str l_name;
} M0_XCA_RECORD;

struct m0_fop_link_rep {
        struct m0_fop_cob l_body;
} M0_XCA_RECORD;

struct m0_fop_unlink {
        struct m0_fop_cob u_body;
        struct m0_fop_str u_path;
        struct m0_fop_str u_name;
} M0_XCA_RECORD;

struct m0_fop_unlink_rep {
        struct m0_fop_cob u_body;
} M0_XCA_RECORD;

struct m0_fop_rename {
        struct m0_fop_cob r_sbody;
        struct m0_fop_cob r_tbody;
        struct m0_fop_str r_spath;
        struct m0_fop_str r_tpath;
        struct m0_fop_str r_sname;
        struct m0_fop_str r_tname;
} M0_XCA_RECORD;

struct m0_fop_rename_rep {
        struct m0_fop_cob r_body;
} M0_XCA_RECORD;

struct m0_fop_open {
        struct m0_fop_str o_path;
        struct m0_fop_cob o_body;
} M0_XCA_RECORD;

struct m0_fop_open_rep {
        struct m0_fop_cob o_body;
} M0_XCA_RECORD;

struct m0_fop_close {
        struct m0_fop_cob c_body;
        struct m0_fop_str c_path;
} M0_XCA_RECORD;

struct m0_fop_close_rep {
        struct m0_fop_cob c_body;
} M0_XCA_RECORD;

struct m0_fop_setattr {
        struct m0_fop_cob s_body;
        struct m0_fop_str s_path;
} M0_XCA_RECORD;

struct m0_fop_setattr_rep {
        struct m0_fop_cob s_body;
} M0_XCA_RECORD;

struct m0_fop_getattr {
        struct m0_fop_cob g_body;
        struct m0_fop_str g_path;
} M0_XCA_RECORD;

struct m0_fop_getattr_rep {
        struct m0_fop_cob g_body;
} M0_XCA_RECORD;

struct m0_fop_getxattr {
        struct m0_fop_cob g_body;
        struct m0_fop_str g_key;
} M0_XCA_RECORD;

struct m0_fop_getxattr_rep {
        struct m0_fop_cob g_body;
        struct m0_fop_str g_value;
} M0_XCA_RECORD;

struct m0_fop_setxattr {
        struct m0_fop_cob s_body;
        struct m0_fop_str s_key;
        struct m0_fop_str s_value;
} M0_XCA_RECORD;

struct m0_fop_setxattr_rep {
        struct m0_fop_cob s_body;
} M0_XCA_RECORD;

struct m0_fop_delxattr {
        struct m0_fop_cob d_body;
        struct m0_fop_str d_key;
} M0_XCA_RECORD;

struct m0_fop_delxattr_rep {
        struct m0_fop_cob d_body;
} M0_XCA_RECORD;

struct m0_fop_listxattr {
        struct m0_fop_cob l_body;
} M0_XCA_RECORD;

struct m0_fop_listxattr_rep {
        struct m0_fop_str l_end;
        struct m0_fop_cob l_body;
        struct m0_fop_buf l_buf;
} M0_XCA_RECORD;

struct m0_fop_readdir {
        struct m0_fop_cob r_body;
        struct m0_fop_str r_path;
        struct m0_fop_str r_pos;
} M0_XCA_RECORD;

struct m0_fop_readdir_rep {
        struct m0_fop_str r_end;
        struct m0_fop_cob r_body;
        struct m0_fop_buf r_buf;
} M0_XCA_RECORD;

struct m0_fop_statfs {
        uint64_t          f_flags;
} M0_XCA_RECORD;

struct m0_fop_statfs_rep {
        uint32_t          f_rc;
        uint64_t          f_type;
        uint32_t          f_bsize;
        uint64_t          f_blocks;
        uint64_t          f_bfree;
        uint64_t          f_bavail;
        uint64_t          f_files;
        uint64_t          f_ffree;
        uint32_t          f_namelen;
        struct m0_fid     f_root;
} M0_XCA_RECORD;

enum m0_layout_opcode {
        M0_LAYOUT_OP_NOOP   = 0,
        M0_LAYOUT_OP_ADD    = 1,
        M0_LAYOUT_OP_DELETE = 2,
        M0_LAYOUT_OP_LOOKUP = 3,
        M0_LAYOUT_OP_UPDATE = 4
};

struct m0_fop_layout {
        uint32_t          l_op; /*< m0_layout_opcode */
        uint64_t          l_lid;
        struct m0_fop_buf l_buf;
} M0_XCA_RECORD;

struct m0_fop_layout_rep {
        uint32_t          lr_rc;
        struct m0_fop_buf lr_buf;
} M0_XCA_RECORD;

/**
   Init and fini of mdservice fops code.
 */
M0_INTERNAL int m0_mdservice_fop_init(void);
M0_INTERNAL void m0_mdservice_fop_fini(void);

#endif /* __MERO_MDSERVICE_MD_FOMS_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
