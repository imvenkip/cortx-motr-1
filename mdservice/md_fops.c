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

#include <string.h>
#include "fop/fop.h"
#include "fop/fom_generic.h"
#include "rpc/rpc_opcodes.h"
#include "mdservice/md_foms.h"
#include "mdservice/md_fops.h"
#include "mdservice/md_fops_ff.h"

static size_t c2_md_fol_pack_size(struct c2_fol_rec_desc *desc)
{
        struct c2_fop *fop = desc->rd_type_private;
        size_t len = fop->f_type->ft_xt->xct_sizeof;
        void *data = c2_fop_data(fop);

        switch (fop->f_type->ft_rpc_item_type.rit_opcode) {
        case C2_MDSERVICE_CREATE_OPCODE:
                len += ((struct c2_fop_create *)data)->c_name.s_len;
                len += ((struct c2_fop_create *)data)->c_target.s_len;
                len += ((struct c2_fop_create *)data)->c_path.s_len;
                break;
        case C2_MDSERVICE_LINK_OPCODE:
                len += ((struct c2_fop_link *)data)->l_name.s_len;
                len += ((struct c2_fop_link *)data)->l_spath.s_len;
                len += ((struct c2_fop_link *)data)->l_tpath.s_len;
                break;
        case C2_MDSERVICE_UNLINK_OPCODE:
                len += ((struct c2_fop_unlink *)data)->u_name.s_len;
                len += ((struct c2_fop_unlink *)data)->u_path.s_len;
                break;
        case C2_MDSERVICE_RENAME_OPCODE:
                len += ((struct c2_fop_rename *)data)->r_sname.s_len;
                len += ((struct c2_fop_rename *)data)->r_tname.s_len;
                len += ((struct c2_fop_rename *)data)->r_spath.s_len;
                len += ((struct c2_fop_rename *)data)->r_tpath.s_len;
                break;
        case C2_MDSERVICE_SETATTR_OPCODE:
                len += ((struct c2_fop_setattr *)data)->s_path.s_len;
                break;
        case C2_MDSERVICE_GETATTR_OPCODE:
                len += ((struct c2_fop_getattr *)data)->g_path.s_len;
                break;
        case C2_MDSERVICE_OPEN_OPCODE:
                len += ((struct c2_fop_open *)data)->o_path.s_len;
                break;
        case C2_MDSERVICE_CLOSE_OPCODE:
                len += ((struct c2_fop_close *)data)->c_path.s_len;
                break;
        case C2_MDSERVICE_READDIR_OPCODE:
                len += ((struct c2_fop_readdir *)data)->r_path.s_len;
                break;
        default:
                break;
        }

        return (len + 7) & ~7;
}

static void copy(char **buf, struct c2_fop_str *str)
{
        if (str->s_len > 0) {
                memcpy(*buf, (char *)str->s_buf, str->s_len);
                *buf += str->s_len;
        }
}

static void c2_md_fol_pack(struct c2_fol_rec_desc *desc, void *buf)
{
        struct c2_fop *fop = desc->rd_type_private;
        char *data = c2_fop_data(fop), *ptr;
        size_t size = fop->f_type->ft_xt->xct_sizeof;

        memcpy(buf, data, size);
        ptr = (char *)buf + size;

        switch (fop->f_type->ft_rpc_item_type.rit_opcode) {
        case C2_MDSERVICE_CREATE_OPCODE:
                copy(&ptr, &((struct c2_fop_create *)data)->c_name);
                copy(&ptr, &((struct c2_fop_create *)data)->c_target);
                copy(&ptr, &((struct c2_fop_create *)data)->c_path);
                break;
        case C2_MDSERVICE_LINK_OPCODE:
                copy(&ptr, &((struct c2_fop_link *)data)->l_name);
                copy(&ptr, &((struct c2_fop_link *)data)->l_spath);
                copy(&ptr, &((struct c2_fop_link *)data)->l_tpath);
                break;
        case C2_MDSERVICE_UNLINK_OPCODE:
                copy(&ptr, &((struct c2_fop_unlink *)data)->u_name);
                copy(&ptr, &((struct c2_fop_unlink *)data)->u_path);
                break;
        case C2_MDSERVICE_RENAME_OPCODE:
                copy(&ptr, &((struct c2_fop_rename *)data)->r_sname);
                copy(&ptr, &((struct c2_fop_rename *)data)->r_tname);
                copy(&ptr, &((struct c2_fop_rename *)data)->r_spath);
                copy(&ptr, &((struct c2_fop_rename *)data)->r_tpath);
                break;
        case C2_MDSERVICE_SETATTR_OPCODE:
                copy(&ptr, &((struct c2_fop_setattr *)data)->s_path);
                break;
        case C2_MDSERVICE_GETATTR_OPCODE:
                copy(&ptr, &((struct c2_fop_getattr *)data)->g_path);
                break;
        case C2_MDSERVICE_OPEN_OPCODE:
                copy(&ptr, &((struct c2_fop_open *)data)->o_path);
                break;
        case C2_MDSERVICE_CLOSE_OPCODE:
                copy(&ptr, &((struct c2_fop_close *)data)->c_path);
                break;
        case C2_MDSERVICE_READDIR_OPCODE:
                copy(&ptr, &((struct c2_fop_readdir *)data)->r_path);
                break;
        default:
                break;
        }
}

static void map(char **buf, struct c2_fop_str *str)
{
        if (str->s_len > 0) {
                str->s_buf = (uint8_t *)*buf;
                *buf += str->s_len;
        }
}

static int c2_md_fol_open(const struct c2_fol_rec_type *type,
                          struct c2_fol_rec_desc *desc)
{
        struct c2_fop *fop = desc->rd_type_private;
        void *data = desc->rd_data;
        char *ptr;

        switch (fop->f_type->ft_rpc_item_type.rit_opcode) {
        case C2_MDSERVICE_CREATE_OPCODE:
                ptr = (char *)((struct c2_fop_create *)data + 1);
                map(&ptr, &((struct c2_fop_create *)data)->c_name);
                map(&ptr, &((struct c2_fop_create *)data)->c_target);
                map(&ptr, &((struct c2_fop_create *)data)->c_path);
                break;
        case C2_MDSERVICE_LINK_OPCODE:
                ptr = (char *)((struct c2_fop_link *)data + 1);
                map(&ptr, &((struct c2_fop_link *)data)->l_name);
                map(&ptr, &((struct c2_fop_link *)data)->l_spath);
                map(&ptr, &((struct c2_fop_link *)data)->l_tpath);
                break;
        case C2_MDSERVICE_UNLINK_OPCODE:
                ptr = (char *)((struct c2_fop_unlink *)data + 1);
                map(&ptr, &((struct c2_fop_unlink *)data)->u_name);
                map(&ptr, &((struct c2_fop_unlink *)data)->u_path);
                break;
        case C2_MDSERVICE_RENAME_OPCODE:
                ptr = (char *)((struct c2_fop_rename *)data + 1);
                map(&ptr, &((struct c2_fop_rename *)data)->r_sname);
                map(&ptr, &((struct c2_fop_rename *)data)->r_tname);
                map(&ptr, &((struct c2_fop_rename *)data)->r_spath);
                map(&ptr, &((struct c2_fop_rename *)data)->r_tpath);
                break;
        case C2_MDSERVICE_SETATTR_OPCODE:
                ptr = (char *)((struct c2_fop_setattr *)data + 1);
                map(&ptr, &((struct c2_fop_setattr *)data)->s_path);
                break;
        case C2_MDSERVICE_GETATTR_OPCODE:
                ptr = (char *)((struct c2_fop_getattr *)data + 1);
                map(&ptr, &((struct c2_fop_getattr *)data)->g_path);
                break;
        case C2_MDSERVICE_OPEN_OPCODE:
                ptr = (char *)((struct c2_fop_open *)data + 1);
                map(&ptr, &((struct c2_fop_open *)data)->o_path);
                break;
        case C2_MDSERVICE_CLOSE_OPCODE:
                ptr = (char *)((struct c2_fop_close *)data + 1);
                map(&ptr, &((struct c2_fop_close *)data)->c_path);
                break;
        case C2_MDSERVICE_READDIR_OPCODE:
                ptr = (char *)((struct c2_fop_readdir *)data + 1);
                map(&ptr, &((struct c2_fop_readdir *)data)->r_path);
                break;
        default:
                break;
        }

        return 0;
}

static const struct c2_fol_rec_type_ops c2_md_fop_fol_ops = {
        .rto_commit     = NULL,
        .rto_abort      = NULL,
        .rto_persistent = NULL,
        .rto_cull       = NULL,
        .rto_open       = c2_md_fol_open,
        .rto_fini       = NULL,
        .rto_pack_size  = c2_md_fol_pack_size,
        .rto_pack       = c2_md_fol_pack
};

const struct c2_fop_type_ops c2_md_fop_ops = {
        .fto_rec_ops    = &c2_md_fop_fol_ops
};

static struct c2_fom_type_ops c2_md_fom_ops = {
        .fto_create   = c2_md_req_fom_create
};

extern struct c2_reqh_service_type c2_mds_type;

struct c2_fop_type c2_fop_create_fopt;
struct c2_fop_type c2_fop_link_fopt;
struct c2_fop_type c2_fop_unlink_fopt;
struct c2_fop_type c2_fop_open_fopt;
struct c2_fop_type c2_fop_close_fopt;
struct c2_fop_type c2_fop_setattr_fopt;
struct c2_fop_type c2_fop_getattr_fopt;
struct c2_fop_type c2_fop_rename_fopt;
struct c2_fop_type c2_fop_readdir_fopt;

struct c2_fop_type c2_fop_create_rep_fopt;
struct c2_fop_type c2_fop_link_rep_fopt;
struct c2_fop_type c2_fop_unlink_rep_fopt;
struct c2_fop_type c2_fop_open_rep_fopt;
struct c2_fop_type c2_fop_close_rep_fopt;
struct c2_fop_type c2_fop_setattr_rep_fopt;
struct c2_fop_type c2_fop_getattr_rep_fopt;
struct c2_fop_type c2_fop_rename_rep_fopt;
struct c2_fop_type c2_fop_readdir_rep_fopt;

int c2_mdservice_fop_init(void)
{
        /*
         * Provided by ff2c compiler after parsing io_fops_xc.ff
         */
        c2_xc_md_fops_init();

        return  C2_FOP_TYPE_INIT(&c2_fop_create_fopt,
                                 .name      = "Create request",
                                 .opcode    = C2_MDSERVICE_CREATE_OPCODE,
                                 .xt        = c2_fop_create_xc,
                                 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST |
                                              C2_RPC_ITEM_TYPE_MUTABO,
                                 .fop_ops   = &c2_md_fop_ops,
#ifndef __KERNEL__
                                 .fom_ops   = &c2_md_fom_ops,
                                 .sm        = &c2_generic_conf,
                                 .svc_type  = &c2_mds_type,
#endif
                                 .rpc_ops   = NULL) ?:
                C2_FOP_TYPE_INIT(&c2_fop_link_fopt,
                                 .name      = "Hardlink request",
                                 .opcode    = C2_MDSERVICE_LINK_OPCODE,
                                 .xt        = c2_fop_link_xc,
                                 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST |
                                              C2_RPC_ITEM_TYPE_MUTABO,
                                 .fop_ops   = &c2_md_fop_ops,
#ifndef __KERNEL__
                                 .fom_ops   = &c2_md_fom_ops,
                                 .sm        = &c2_generic_conf,
                                 .svc_type  = &c2_mds_type,
#endif
                                 .rpc_ops   = NULL) ?:
                C2_FOP_TYPE_INIT(&c2_fop_unlink_fopt,
                                 .name      = "Unlink request",
                                 .opcode    = C2_MDSERVICE_UNLINK_OPCODE,
                                 .xt        = c2_fop_unlink_xc,
                                 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST |
                                              C2_RPC_ITEM_TYPE_MUTABO,
                                 .fop_ops   = &c2_md_fop_ops,
#ifndef __KERNEL__
                                 .fom_ops   = &c2_md_fom_ops,
                                 .sm        = &c2_generic_conf,
                                 .svc_type  = &c2_mds_type,
#endif
                                 .rpc_ops   = NULL) ?:
                C2_FOP_TYPE_INIT(&c2_fop_open_fopt,
                                 .name      = "Open request",
                                 .opcode    = C2_MDSERVICE_OPEN_OPCODE,
                                 .xt        = c2_fop_open_xc,
                                 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST |
                                              C2_RPC_ITEM_TYPE_MUTABO,
                                 .fop_ops   = &c2_md_fop_ops,
#ifndef __KERNEL__
                                 .fom_ops   = &c2_md_fom_ops,
                                 .sm        = &c2_generic_conf,
                                 .svc_type  = &c2_mds_type,
#endif
                                 .rpc_ops   = NULL) ?:
                C2_FOP_TYPE_INIT(&c2_fop_close_fopt,
                                 .name      = "Close request",
                                 .opcode    = C2_MDSERVICE_CLOSE_OPCODE,
                                 .xt        = c2_fop_close_xc,
                                 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST |
                                              C2_RPC_ITEM_TYPE_MUTABO,
                                 .fop_ops   = &c2_md_fop_ops,
#ifndef __KERNEL__
                                 .fom_ops   = &c2_md_fom_ops,
                                 .sm        = &c2_generic_conf,
                                 .svc_type  = &c2_mds_type,
#endif
                                 .rpc_ops   = NULL) ?:
                C2_FOP_TYPE_INIT(&c2_fop_setattr_fopt,
                                 .name      = "Setattr request",
                                 .opcode    = C2_MDSERVICE_SETATTR_OPCODE,
                                 .xt        = c2_fop_setattr_xc,
                                 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST |
                                              C2_RPC_ITEM_TYPE_MUTABO,
                                 .fop_ops   = &c2_md_fop_ops,
#ifndef __KERNEL__
                                 .fom_ops   = &c2_md_fom_ops,
                                 .sm        = &c2_generic_conf,
                                 .svc_type  = &c2_mds_type,
#endif
                                 .rpc_ops   = NULL) ?:
                C2_FOP_TYPE_INIT(&c2_fop_getattr_fopt,
                                 .name      = "Getattr request",
                                 .opcode    = C2_MDSERVICE_GETATTR_OPCODE,
                                 .xt        = c2_fop_setattr_xc,
                                 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST,
                                 .fop_ops   = &c2_md_fop_ops,
#ifndef __KERNEL__
                                 .fom_ops   = &c2_md_fom_ops,
                                 .sm        = &c2_generic_conf,
                                 .svc_type  = &c2_mds_type,
#endif
                                 .rpc_ops   = NULL) ?:
                C2_FOP_TYPE_INIT(&c2_fop_rename_fopt,
                                 .name      = "Rename request",
                                 .opcode    = C2_MDSERVICE_RENAME_OPCODE,
                                 .xt        = c2_fop_rename_xc,
                                 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST |
                                              C2_RPC_ITEM_TYPE_MUTABO,
                                 .fop_ops   = &c2_md_fop_ops,
#ifndef __KERNEL__
                                 .fom_ops   = &c2_md_fom_ops,
                                 .sm        = &c2_generic_conf,
                                 .svc_type  = &c2_mds_type,
#endif
                                 .rpc_ops   = NULL) ?:
                C2_FOP_TYPE_INIT(&c2_fop_readdir_fopt,
                                 .name      = "Readdir request",
                                 .opcode    = C2_MDSERVICE_READDIR_OPCODE,
                                 .xt        = c2_fop_readdir_xc,
                                 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST,
                                 .fop_ops   = &c2_md_fop_ops,
#ifndef __KERNEL__
                                 .fom_ops   = &c2_md_fom_ops,
                                 .sm        = &c2_generic_conf,
                                 .svc_type  = &c2_mds_type,
#endif
                                 .rpc_ops   = NULL) ?:
                C2_FOP_TYPE_INIT(&c2_fop_create_rep_fopt,
                                 .name      = "Create reply",
                                 .opcode    = C2_MDSERVICE_CREATE_REP_OPCODE,
                                 .xt        = c2_fop_create_rep_xc,
                                 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY) ?:
                C2_FOP_TYPE_INIT(&c2_fop_link_rep_fopt,
                                 .name      = "Hardlink reply",
                                 .opcode    = C2_MDSERVICE_LINK_REP_OPCODE,
                                 .xt        = c2_fop_link_rep_xc,
                                 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY) ?:
                C2_FOP_TYPE_INIT(&c2_fop_unlink_rep_fopt,
                                 .name      = "Unlink reply",
                                 .opcode    = C2_MDSERVICE_UNLINK_REP_OPCODE,
                                 .xt        = c2_fop_unlink_rep_xc,
                                 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY) ?:
                C2_FOP_TYPE_INIT(&c2_fop_open_rep_fopt,
                                 .name      = "Open reply",
                                 .opcode    = C2_MDSERVICE_OPEN_REP_OPCODE,
                                 .xt        = c2_fop_open_rep_xc,
                                 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY) ?:
                C2_FOP_TYPE_INIT(&c2_fop_close_rep_fopt,
                                 .name      = "Close reply",
                                 .opcode    = C2_MDSERVICE_CLOSE_REP_OPCODE,
                                 .xt        = c2_fop_close_rep_xc,
                                 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY) ?:
                C2_FOP_TYPE_INIT(&c2_fop_setattr_rep_fopt,
                                 .name      = "Setattr reply",
                                 .opcode    = C2_MDSERVICE_SETATTR_REP_OPCODE,
                                 .xt        = c2_fop_setattr_rep_xc,
                                 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY) ?:
                C2_FOP_TYPE_INIT(&c2_fop_getattr_rep_fopt,
                                 .name      = "Getattr reply",
                                 .opcode    = C2_MDSERVICE_GETATTR_REP_OPCODE,
                                 .xt        = c2_fop_setattr_rep_xc,
                                 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY) ?:
                C2_FOP_TYPE_INIT(&c2_fop_rename_rep_fopt,
                                 .name      = "Rename reply",
                                 .opcode    = C2_MDSERVICE_RENAME_REP_OPCODE,
                                 .xt        = c2_fop_rename_rep_xc,
                                 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY) ?:
                C2_FOP_TYPE_INIT(&c2_fop_readdir_rep_fopt,
                                 .name      = "Readdir reply",
                                 .opcode    = C2_MDSERVICE_READDIR_REP_OPCODE,
                                 .xt        = c2_fop_readdir_rep_xc,
                                 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY);
}
C2_EXPORTED(c2_mdservice_fop_init);

void c2_mdservice_fop_fini(void)
{
        c2_fop_type_fini(&c2_fop_create_fopt);
        c2_fop_type_fini(&c2_fop_link_fopt);
        c2_fop_type_fini(&c2_fop_unlink_fopt);
        c2_fop_type_fini(&c2_fop_rename_fopt);
        c2_fop_type_fini(&c2_fop_readdir_fopt);
        c2_fop_type_fini(&c2_fop_open_fopt);
        c2_fop_type_fini(&c2_fop_close_fopt);
        c2_fop_type_fini(&c2_fop_setattr_fopt);
        c2_fop_type_fini(&c2_fop_getattr_fopt);
        c2_fop_type_fini(&c2_fop_create_rep_fopt);
        c2_fop_type_fini(&c2_fop_link_rep_fopt);
        c2_fop_type_fini(&c2_fop_unlink_rep_fopt);
        c2_fop_type_fini(&c2_fop_rename_rep_fopt);
        c2_fop_type_fini(&c2_fop_readdir_rep_fopt);
        c2_fop_type_fini(&c2_fop_open_rep_fopt);
        c2_fop_type_fini(&c2_fop_close_rep_fopt);
        c2_fop_type_fini(&c2_fop_setattr_rep_fopt);
        c2_fop_type_fini(&c2_fop_getattr_rep_fopt);
        c2_xc_md_fops_fini();
}
C2_EXPORTED(c2_mdservice_fop_fini);

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
