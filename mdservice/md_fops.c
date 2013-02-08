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
 * Original creation date: 03/29/2011
 */

#include "lib/string.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "fop/fop.h"
#include "fop/fom_generic.h"
#include "fop/fop_item_type.h"
#include "rpc/rpc_opcodes.h"
#include "mdservice/md_foms.h"
#include "mdservice/md_fops.h"
#include "mdservice/md_fops_xc.h"

static size_t m0_md_fol_pack_size(struct m0_fol_rec_desc *desc)
{
        struct m0_fop *fop = desc->rd_type_private;
        size_t len = fop->f_type->ft_xt->xct_sizeof;
        void *data = m0_fop_data(fop);

        switch (m0_fop_opcode(fop)) {
        case M0_MDSERVICE_LOOKUP_OPCODE:
                len += ((struct m0_fop_lookup *)data)->l_name.s_len;
                len += ((struct m0_fop_lookup *)data)->l_path.s_len;
                break;
        case M0_MDSERVICE_CREATE_OPCODE:
                len += ((struct m0_fop_create *)data)->c_name.s_len;
                len += ((struct m0_fop_create *)data)->c_target.s_len;
                len += ((struct m0_fop_create *)data)->c_path.s_len;
                break;
        case M0_MDSERVICE_LINK_OPCODE:
                len += ((struct m0_fop_link *)data)->l_name.s_len;
                len += ((struct m0_fop_link *)data)->l_spath.s_len;
                len += ((struct m0_fop_link *)data)->l_tpath.s_len;
                break;
        case M0_MDSERVICE_UNLINK_OPCODE:
                len += ((struct m0_fop_unlink *)data)->u_name.s_len;
                len += ((struct m0_fop_unlink *)data)->u_path.s_len;
                break;
        case M0_MDSERVICE_RENAME_OPCODE:
                len += ((struct m0_fop_rename *)data)->r_sname.s_len;
                len += ((struct m0_fop_rename *)data)->r_tname.s_len;
                len += ((struct m0_fop_rename *)data)->r_spath.s_len;
                len += ((struct m0_fop_rename *)data)->r_tpath.s_len;
                break;
        case M0_MDSERVICE_SETATTR_OPCODE:
                len += ((struct m0_fop_setattr *)data)->s_path.s_len;
                break;
        case M0_MDSERVICE_GETATTR_OPCODE:
                len += ((struct m0_fop_getattr *)data)->g_path.s_len;
                break;
        case M0_MDSERVICE_OPEN_OPCODE:
                len += ((struct m0_fop_open *)data)->o_path.s_len;
                break;
        case M0_MDSERVICE_CLOSE_OPCODE:
                len += ((struct m0_fop_close *)data)->c_path.s_len;
                break;
        case M0_MDSERVICE_READDIR_OPCODE:
                len += ((struct m0_fop_readdir *)data)->r_path.s_len;
                break;
        case M0_LAYOUT_OPCODE:
                len += ((struct m0_fop_layout *)data)->l_buf.b_count;
                break;
        default:
                break;
        }

        return (len + 7) & ~7;
}

static void copy(char **buf, struct m0_fop_str *str)
{
#ifndef __KERNEL__
        if (str->s_len > 0) {
                memcpy(*buf, (char *)str->s_buf, str->s_len);
                *buf += str->s_len;
        }
#endif
}

static void m0_md_fol_pack(struct m0_fol_rec_desc *desc, void *buf)
{
        struct m0_fop *fop = desc->rd_type_private;
        size_t size = fop->f_type->ft_xt->xct_sizeof;
        char *data = m0_fop_data(fop);
        char *ptr;

        memcpy(buf, data, size);
        ptr = (char *)buf + size;

        switch (m0_fop_opcode(fop)) {
        case M0_MDSERVICE_LOOKUP_OPCODE:
                copy(&ptr, &((struct m0_fop_lookup *)data)->l_name);
                copy(&ptr, &((struct m0_fop_lookup *)data)->l_path);
                break;
        case M0_MDSERVICE_CREATE_OPCODE:
                copy(&ptr, &((struct m0_fop_create *)data)->c_name);
                copy(&ptr, &((struct m0_fop_create *)data)->c_target);
                copy(&ptr, &((struct m0_fop_create *)data)->c_path);
                break;
        case M0_MDSERVICE_LINK_OPCODE:
                copy(&ptr, &((struct m0_fop_link *)data)->l_name);
                copy(&ptr, &((struct m0_fop_link *)data)->l_spath);
                copy(&ptr, &((struct m0_fop_link *)data)->l_tpath);
                break;
        case M0_MDSERVICE_UNLINK_OPCODE:
                copy(&ptr, &((struct m0_fop_unlink *)data)->u_name);
                copy(&ptr, &((struct m0_fop_unlink *)data)->u_path);
                break;
        case M0_MDSERVICE_RENAME_OPCODE:
                copy(&ptr, &((struct m0_fop_rename *)data)->r_sname);
                copy(&ptr, &((struct m0_fop_rename *)data)->r_tname);
                copy(&ptr, &((struct m0_fop_rename *)data)->r_spath);
                copy(&ptr, &((struct m0_fop_rename *)data)->r_tpath);
                break;
        case M0_MDSERVICE_SETATTR_OPCODE:
                copy(&ptr, &((struct m0_fop_setattr *)data)->s_path);
                break;
        case M0_MDSERVICE_GETATTR_OPCODE:
                copy(&ptr, &((struct m0_fop_getattr *)data)->g_path);
                break;
        case M0_MDSERVICE_OPEN_OPCODE:
                copy(&ptr, &((struct m0_fop_open *)data)->o_path);
                break;
        case M0_MDSERVICE_CLOSE_OPCODE:
                copy(&ptr, &((struct m0_fop_close *)data)->c_path);
                break;
        case M0_MDSERVICE_READDIR_OPCODE:
                copy(&ptr, &((struct m0_fop_readdir *)data)->r_path);
                break;
        case M0_LAYOUT_OPCODE:
                copy(&ptr, (struct m0_fop_str*)
				&((struct m0_fop_layout *)data)->l_buf);
                break;
        default:
                break;
        }
}

static void map(char **buf, struct m0_fop_str *str)
{
#ifndef __KERNEL__
        if (str->s_len > 0) {
                str->s_buf = (uint8_t *)*buf;
                *buf += str->s_len;
        }
#endif
}

static int m0_md_fol_open(const struct m0_fol_rec_type *type,
                          struct m0_fol_rec_desc *desc)
{
        struct m0_fop *fop = desc->rd_type_private;
        void *data = desc->rd_data;
        char *ptr;

        switch (m0_fop_opcode(fop)) {
        case M0_MDSERVICE_LOOKUP_OPCODE:
                ptr = (char *)((struct m0_fop_lookup *)data + 1);
                map(&ptr, &((struct m0_fop_lookup *)data)->l_name);
                map(&ptr, &((struct m0_fop_lookup *)data)->l_path);
                break;
        case M0_MDSERVICE_CREATE_OPCODE:
                ptr = (char *)((struct m0_fop_create *)data + 1);
                map(&ptr, &((struct m0_fop_create *)data)->c_name);
                map(&ptr, &((struct m0_fop_create *)data)->c_target);
                map(&ptr, &((struct m0_fop_create *)data)->c_path);
                break;
        case M0_MDSERVICE_LINK_OPCODE:
                ptr = (char *)((struct m0_fop_link *)data + 1);
                map(&ptr, &((struct m0_fop_link *)data)->l_name);
                map(&ptr, &((struct m0_fop_link *)data)->l_spath);
                map(&ptr, &((struct m0_fop_link *)data)->l_tpath);
                break;
        case M0_MDSERVICE_UNLINK_OPCODE:
                ptr = (char *)((struct m0_fop_unlink *)data + 1);
                map(&ptr, &((struct m0_fop_unlink *)data)->u_name);
                map(&ptr, &((struct m0_fop_unlink *)data)->u_path);
                break;
        case M0_MDSERVICE_RENAME_OPCODE:
                ptr = (char *)((struct m0_fop_rename *)data + 1);
                map(&ptr, &((struct m0_fop_rename *)data)->r_sname);
                map(&ptr, &((struct m0_fop_rename *)data)->r_tname);
                map(&ptr, &((struct m0_fop_rename *)data)->r_spath);
                map(&ptr, &((struct m0_fop_rename *)data)->r_tpath);
                break;
        case M0_MDSERVICE_SETATTR_OPCODE:
                ptr = (char *)((struct m0_fop_setattr *)data + 1);
                map(&ptr, &((struct m0_fop_setattr *)data)->s_path);
                break;
        case M0_MDSERVICE_GETATTR_OPCODE:
                ptr = (char *)((struct m0_fop_getattr *)data + 1);
                map(&ptr, &((struct m0_fop_getattr *)data)->g_path);
                break;
        case M0_MDSERVICE_OPEN_OPCODE:
                ptr = (char *)((struct m0_fop_open *)data + 1);
                map(&ptr, &((struct m0_fop_open *)data)->o_path);
                break;
        case M0_MDSERVICE_CLOSE_OPCODE:
                ptr = (char *)((struct m0_fop_close *)data + 1);
                map(&ptr, &((struct m0_fop_close *)data)->c_path);
                break;
        case M0_MDSERVICE_READDIR_OPCODE:
                ptr = (char *)((struct m0_fop_readdir *)data + 1);
                map(&ptr, &((struct m0_fop_readdir *)data)->r_path);
                break;
        case M0_LAYOUT_OPCODE:
                ptr = (char *)((struct m0_fop_layout *)data + 1);
                map(&ptr, (struct m0_fop_str *)
				&((struct m0_fop_layout *)data)->l_buf);
                break;
        default:
                break;
        }

        return 0;
}

static const struct m0_fol_rec_type_ops m0_md_fop_fol_ops = {
        .rto_commit     = NULL,
        .rto_abort      = NULL,
        .rto_persistent = NULL,
        .rto_cull       = NULL,
        .rto_open       = m0_md_fol_open,
        .rto_fini       = NULL,
        .rto_pack_size  = m0_md_fol_pack_size,
        .rto_pack       = m0_md_fol_pack
};

const struct m0_fop_type_ops m0_md_fop_ops = {
        .fto_rec_ops    = &m0_md_fop_fol_ops
};

#ifndef __KERNEL__
static const struct m0_fom_type_ops m0_md_fom_ops = {
        .fto_create   = m0_md_req_fom_create
};

extern struct m0_reqh_service_type m0_mds_type;
#endif

struct m0_fop_type m0_fop_create_fopt;
struct m0_fop_type m0_fop_lookup_fopt;
struct m0_fop_type m0_fop_link_fopt;
struct m0_fop_type m0_fop_unlink_fopt;
struct m0_fop_type m0_fop_open_fopt;
struct m0_fop_type m0_fop_close_fopt;
struct m0_fop_type m0_fop_setattr_fopt;
struct m0_fop_type m0_fop_getattr_fopt;
struct m0_fop_type m0_fop_statfs_fopt;
struct m0_fop_type m0_fop_rename_fopt;
struct m0_fop_type m0_fop_readdir_fopt;
struct m0_fop_type m0_fop_layout_fopt;

struct m0_fop_type m0_fop_create_rep_fopt;
struct m0_fop_type m0_fop_lookup_rep_fopt;
struct m0_fop_type m0_fop_link_rep_fopt;
struct m0_fop_type m0_fop_unlink_rep_fopt;
struct m0_fop_type m0_fop_open_rep_fopt;
struct m0_fop_type m0_fop_close_rep_fopt;
struct m0_fop_type m0_fop_setattr_rep_fopt;
struct m0_fop_type m0_fop_getattr_rep_fopt;
struct m0_fop_type m0_fop_statfs_rep_fopt;
struct m0_fop_type m0_fop_rename_rep_fopt;
struct m0_fop_type m0_fop_readdir_rep_fopt;
struct m0_fop_type m0_fop_layout_rep_fopt;

M0_INTERNAL int m0_mdservice_fop_init(void)
{
        /*
         * Provided by gccxml2xcode after parsing md_fops.h
         */
        m0_xc_md_fops_init();

        return  M0_FOP_TYPE_INIT(&m0_fop_create_fopt,
                                 .name      = "Create request",
                                 .opcode    = M0_MDSERVICE_CREATE_OPCODE,
                                 .xt        = m0_fop_create_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST |
                                              M0_RPC_ITEM_TYPE_MUTABO,
                                 .fop_ops   = &m0_md_fop_ops,
#ifndef __KERNEL__
                                 .fom_ops   = &m0_md_fom_ops,
                                 .svc_type  = &m0_mds_type,
#endif
                                 .sm        = &m0_generic_conf) ?:
                M0_FOP_TYPE_INIT(&m0_fop_lookup_fopt,
                                 .name      = "Lookup request",
                                 .opcode    = M0_MDSERVICE_LOOKUP_OPCODE,
                                 .xt        = m0_fop_lookup_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST,
                                 .fop_ops   = &m0_md_fop_ops,
#ifndef __KERNEL__
                                 .fom_ops   = &m0_md_fom_ops,
                                 .svc_type  = &m0_mds_type,
#endif
                                 .sm        = &m0_generic_conf) ?:
                M0_FOP_TYPE_INIT(&m0_fop_link_fopt,
                                 .name      = "Hardlink request",
                                 .opcode    = M0_MDSERVICE_LINK_OPCODE,
                                 .xt        = m0_fop_link_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST |
                                              M0_RPC_ITEM_TYPE_MUTABO,
                                 .fop_ops   = &m0_md_fop_ops,
#ifndef __KERNEL__
                                 .fom_ops   = &m0_md_fom_ops,
                                 .svc_type  = &m0_mds_type,
#endif
                                 .sm        = &m0_generic_conf) ?:
                M0_FOP_TYPE_INIT(&m0_fop_unlink_fopt,
                                 .name      = "Unlink request",
                                 .opcode    = M0_MDSERVICE_UNLINK_OPCODE,
                                 .xt        = m0_fop_unlink_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST |
                                              M0_RPC_ITEM_TYPE_MUTABO,
                                 .fop_ops   = &m0_md_fop_ops,
#ifndef __KERNEL__
                                 .fom_ops   = &m0_md_fom_ops,
                                 .svc_type  = &m0_mds_type,
#endif
                                 .sm        = &m0_generic_conf) ?:
                M0_FOP_TYPE_INIT(&m0_fop_open_fopt,
                                 .name      = "Open request",
                                 .opcode    = M0_MDSERVICE_OPEN_OPCODE,
                                 .xt        = m0_fop_open_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST |
                                              M0_RPC_ITEM_TYPE_MUTABO,
                                 .fop_ops   = &m0_md_fop_ops,
#ifndef __KERNEL__
                                 .fom_ops   = &m0_md_fom_ops,
                                 .svc_type  = &m0_mds_type,
#endif
                                 .sm        = &m0_generic_conf) ?:
                M0_FOP_TYPE_INIT(&m0_fop_close_fopt,
                                 .name      = "Close request",
                                 .opcode    = M0_MDSERVICE_CLOSE_OPCODE,
                                 .xt        = m0_fop_close_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST |
                                              M0_RPC_ITEM_TYPE_MUTABO,
                                 .fop_ops   = &m0_md_fop_ops,
#ifndef __KERNEL__
                                 .fom_ops   = &m0_md_fom_ops,
                                 .svc_type  = &m0_mds_type,
#endif
                                 .sm        = &m0_generic_conf) ?:
                M0_FOP_TYPE_INIT(&m0_fop_setattr_fopt,
                                 .name      = "Setattr request",
                                 .opcode    = M0_MDSERVICE_SETATTR_OPCODE,
                                 .xt        = m0_fop_setattr_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST |
                                              M0_RPC_ITEM_TYPE_MUTABO,
                                 .fop_ops   = &m0_md_fop_ops,
#ifndef __KERNEL__
                                 .fom_ops   = &m0_md_fom_ops,
                                 .svc_type  = &m0_mds_type,
#endif
                                 .sm        = &m0_generic_conf) ?:
                M0_FOP_TYPE_INIT(&m0_fop_getattr_fopt,
                                 .name      = "Getattr request",
                                 .opcode    = M0_MDSERVICE_GETATTR_OPCODE,
                                 .xt        = m0_fop_getattr_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST,
                                 .fop_ops   = &m0_md_fop_ops,
#ifndef __KERNEL__
                                 .fom_ops   = &m0_md_fom_ops,
                                 .svc_type  = &m0_mds_type,
#endif
                                 .sm        = &m0_generic_conf) ?:
                M0_FOP_TYPE_INIT(&m0_fop_statfs_fopt,
                                 .name      = "Statfs request",
                                 .opcode    = M0_MDSERVICE_STATFS_OPCODE,
                                 .xt        = m0_fop_statfs_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST,
                                 .fop_ops   = &m0_md_fop_ops,
#ifndef __KERNEL__
                                 .fom_ops   = &m0_md_fom_ops,
                                 .svc_type  = &m0_mds_type,
#endif
                                 .sm        = &m0_generic_conf) ?:
                M0_FOP_TYPE_INIT(&m0_fop_rename_fopt,
                                 .name      = "Rename request",
                                 .opcode    = M0_MDSERVICE_RENAME_OPCODE,
                                 .xt        = m0_fop_rename_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST |
                                              M0_RPC_ITEM_TYPE_MUTABO,
                                 .fop_ops   = &m0_md_fop_ops,
#ifndef __KERNEL__
                                 .fom_ops   = &m0_md_fom_ops,
                                 .svc_type  = &m0_mds_type,
#endif
                                 .sm        = &m0_generic_conf) ?:
                M0_FOP_TYPE_INIT(&m0_fop_readdir_fopt,
                                 .name      = "Readdir request",
                                 .opcode    = M0_MDSERVICE_READDIR_OPCODE,
                                 .xt        = m0_fop_readdir_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST,
                                 .fop_ops   = &m0_md_fop_ops,
#ifndef __KERNEL__
                                 .fom_ops   = &m0_md_fom_ops,
                                 .svc_type  = &m0_mds_type,
#endif
                                 .sm        = &m0_generic_conf) ?:
                 M0_FOP_TYPE_INIT(&m0_fop_layout_fopt,
                                 .name      = "Layout request",
                                 .opcode    = M0_LAYOUT_OPCODE,
                                 .xt        = m0_fop_layout_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST,
                                 .fop_ops   = &m0_md_fop_ops,
#ifndef __KERNEL__
                                 .fom_ops   = &m0_md_fom_ops,
                                 .svc_type  = &m0_mds_type,
#endif
                                 .sm        = &m0_generic_conf) ?:
                M0_FOP_TYPE_INIT(&m0_fop_create_rep_fopt,
                                 .name      = "Create reply",
                                 .opcode    = M0_MDSERVICE_CREATE_REP_OPCODE,
                                 .xt        = m0_fop_create_rep_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY) ?:
                M0_FOP_TYPE_INIT(&m0_fop_lookup_rep_fopt,
                                 .name      = "Lookup reply",
                                 .opcode    = M0_MDSERVICE_LOOKUP_REP_OPCODE,
                                 .xt        = m0_fop_lookup_rep_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY) ?:
                M0_FOP_TYPE_INIT(&m0_fop_link_rep_fopt,
                                 .name      = "Hardlink reply",
                                 .opcode    = M0_MDSERVICE_LINK_REP_OPCODE,
                                 .xt        = m0_fop_link_rep_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY) ?:
                M0_FOP_TYPE_INIT(&m0_fop_unlink_rep_fopt,
                                 .name      = "Unlink reply",
                                 .opcode    = M0_MDSERVICE_UNLINK_REP_OPCODE,
                                 .xt        = m0_fop_unlink_rep_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY) ?:
                M0_FOP_TYPE_INIT(&m0_fop_open_rep_fopt,
                                 .name      = "Open reply",
                                 .opcode    = M0_MDSERVICE_OPEN_REP_OPCODE,
                                 .xt        = m0_fop_open_rep_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY) ?:
                M0_FOP_TYPE_INIT(&m0_fop_close_rep_fopt,
                                 .name      = "Close reply",
                                 .opcode    = M0_MDSERVICE_CLOSE_REP_OPCODE,
                                 .xt        = m0_fop_close_rep_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY) ?:
                M0_FOP_TYPE_INIT(&m0_fop_setattr_rep_fopt,
                                 .name      = "Setattr reply",
                                 .opcode    = M0_MDSERVICE_SETATTR_REP_OPCODE,
                                 .xt        = m0_fop_setattr_rep_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY) ?:
                M0_FOP_TYPE_INIT(&m0_fop_getattr_rep_fopt,
                                 .name      = "Getattr reply",
                                 .opcode    = M0_MDSERVICE_GETATTR_REP_OPCODE,
                                 .xt        = m0_fop_getattr_rep_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY) ?:
                M0_FOP_TYPE_INIT(&m0_fop_statfs_rep_fopt,
                                 .name      = "Statfs reply",
                                 .opcode    = M0_MDSERVICE_STATFS_REP_OPCODE,
                                 .xt        = m0_fop_statfs_rep_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY) ?:
                M0_FOP_TYPE_INIT(&m0_fop_rename_rep_fopt,
                                 .name      = "Rename reply",
                                 .opcode    = M0_MDSERVICE_RENAME_REP_OPCODE,
                                 .xt        = m0_fop_rename_rep_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY) ?:
                M0_FOP_TYPE_INIT(&m0_fop_readdir_rep_fopt,
                                 .name      = "Readdir reply",
                                 .opcode    = M0_MDSERVICE_READDIR_REP_OPCODE,
                                 .xt        = m0_fop_readdir_rep_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY) ?:
                M0_FOP_TYPE_INIT(&m0_fop_layout_rep_fopt,
                                 .name      = "layout reply",
                                 .opcode    = M0_LAYOUT_REP_OPCODE,
                                 .xt        = m0_fop_layout_rep_xc,
                                 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY);
}
M0_EXPORTED(m0_mdservice_fop_init);

M0_INTERNAL void m0_mdservice_fop_fini(void)
{
        m0_fop_type_fini(&m0_fop_create_fopt);
        m0_fop_type_fini(&m0_fop_lookup_fopt);
        m0_fop_type_fini(&m0_fop_link_fopt);
        m0_fop_type_fini(&m0_fop_unlink_fopt);
        m0_fop_type_fini(&m0_fop_open_fopt);
        m0_fop_type_fini(&m0_fop_close_fopt);
        m0_fop_type_fini(&m0_fop_setattr_fopt);
        m0_fop_type_fini(&m0_fop_getattr_fopt);
        m0_fop_type_fini(&m0_fop_statfs_fopt);
        m0_fop_type_fini(&m0_fop_rename_fopt);
        m0_fop_type_fini(&m0_fop_readdir_fopt);
        m0_fop_type_fini(&m0_fop_layout_fopt);

        m0_fop_type_fini(&m0_fop_create_rep_fopt);
        m0_fop_type_fini(&m0_fop_lookup_rep_fopt);
        m0_fop_type_fini(&m0_fop_link_rep_fopt);
        m0_fop_type_fini(&m0_fop_unlink_rep_fopt);
        m0_fop_type_fini(&m0_fop_open_rep_fopt);
        m0_fop_type_fini(&m0_fop_close_rep_fopt);
        m0_fop_type_fini(&m0_fop_setattr_rep_fopt);
        m0_fop_type_fini(&m0_fop_getattr_rep_fopt);
        m0_fop_type_fini(&m0_fop_statfs_rep_fopt);
        m0_fop_type_fini(&m0_fop_rename_rep_fopt);
        m0_fop_type_fini(&m0_fop_readdir_rep_fopt);
        m0_fop_type_fini(&m0_fop_layout_rep_fopt);
        m0_xc_md_fops_fini();
}
M0_EXPORTED(m0_mdservice_fop_fini);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
