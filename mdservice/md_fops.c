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

static int md_fol_rec_part_undo(struct m0_fop_fol_rec_part *fpart,
			        struct m0_fol *fol);
static int md_fol_rec_part_redo(struct m0_fop_fol_rec_part *fpart,
				struct m0_fol *fol);

const struct m0_fop_type_ops m0_md_fop_ops = {
	.fto_undo = md_fol_rec_part_undo,
	.fto_redo = md_fol_rec_part_redo,
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
struct m0_fop_type m0_fop_setxattr_fopt;
struct m0_fop_type m0_fop_getxattr_fopt;
struct m0_fop_type m0_fop_delxattr_fopt;
struct m0_fop_type m0_fop_listxattr_fopt;
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
struct m0_fop_type m0_fop_setxattr_rep_fopt;
struct m0_fop_type m0_fop_getxattr_rep_fopt;
struct m0_fop_type m0_fop_delxattr_rep_fopt;
struct m0_fop_type m0_fop_listxattr_rep_fopt;
struct m0_fop_type m0_fop_statfs_rep_fopt;
struct m0_fop_type m0_fop_rename_rep_fopt;
struct m0_fop_type m0_fop_readdir_rep_fopt;
struct m0_fop_type m0_fop_layout_rep_fopt;

M0_INTERNAL int m0_mdservice_fopts_init(void)
{
        M0_FOP_TYPE_INIT(&m0_fop_create_fopt,
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
			 .sm        = &m0_generic_conf);
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
			 .sm        = &m0_generic_conf);
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
			 .sm        = &m0_generic_conf);
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
			 .sm        = &m0_generic_conf);
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
			 .sm        = &m0_generic_conf);
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
			 .sm        = &m0_generic_conf);
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
			 .sm        = &m0_generic_conf);
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
			 .sm        = &m0_generic_conf);
	M0_FOP_TYPE_INIT(&m0_fop_setxattr_fopt,
			 .name      = "Setxattr request",
			 .opcode    = M0_MDSERVICE_SETXATTR_OPCODE,
			 .xt        = m0_fop_setxattr_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST |
			 M0_RPC_ITEM_TYPE_MUTABO,
			 .fop_ops   = &m0_md_fop_ops,
#ifndef __KERNEL__
			 .fom_ops   = &m0_md_fom_ops,
			 .svc_type  = &m0_mds_type,
#endif
			 .sm        = &m0_generic_conf);
	M0_FOP_TYPE_INIT(&m0_fop_getxattr_fopt,
			 .name      = "Getxattr request",
			 .opcode    = M0_MDSERVICE_GETXATTR_OPCODE,
			 .xt        = m0_fop_getxattr_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST,
			 .fop_ops   = &m0_md_fop_ops,
#ifndef __KERNEL__
			 .fom_ops   = &m0_md_fom_ops,
			 .svc_type  = &m0_mds_type,
#endif
			 .sm        = &m0_generic_conf);
	M0_FOP_TYPE_INIT(&m0_fop_delxattr_fopt,
			 .name      = "Delxattr request",
			 .opcode    = M0_MDSERVICE_DELXATTR_OPCODE,
			 .xt        = m0_fop_delxattr_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST |
			 M0_RPC_ITEM_TYPE_MUTABO,
			 .fop_ops   = &m0_md_fop_ops,
#ifndef __KERNEL__
			 .fom_ops   = &m0_md_fom_ops,
			 .svc_type  = &m0_mds_type,
#endif
			 .sm        = &m0_generic_conf);
	M0_FOP_TYPE_INIT(&m0_fop_listxattr_fopt,
			 .name      = "Listxattr request",
			 .opcode    = M0_MDSERVICE_LISTXATTR_OPCODE,
			 .xt        = m0_fop_listxattr_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST,
			 .fop_ops   = &m0_md_fop_ops,
#ifndef __KERNEL__
			 .fom_ops   = &m0_md_fom_ops,
			 .svc_type  = &m0_mds_type,
#endif
			 .sm        = &m0_generic_conf);
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
			 .sm        = &m0_generic_conf);
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
			 .sm        = &m0_generic_conf);
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
			 .sm        = &m0_generic_conf);
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
			 .sm        = &m0_generic_conf);
	return 0;
}

M0_INTERNAL int m0_mdservice_rep_fopts_init(void)
{
	M0_FOP_TYPE_INIT(&m0_fop_create_rep_fopt,
			 .name      = "Create reply",
			 .opcode    = M0_MDSERVICE_CREATE_REP_OPCODE,
			 .xt        = m0_fop_create_rep_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY);
	M0_FOP_TYPE_INIT(&m0_fop_lookup_rep_fopt,
			 .name      = "Lookup reply",
			 .opcode    = M0_MDSERVICE_LOOKUP_REP_OPCODE,
			 .xt        = m0_fop_lookup_rep_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY);
	M0_FOP_TYPE_INIT(&m0_fop_link_rep_fopt,
			 .name      = "Hardlink reply",
			 .opcode    = M0_MDSERVICE_LINK_REP_OPCODE,
			 .xt        = m0_fop_link_rep_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY);
	M0_FOP_TYPE_INIT(&m0_fop_unlink_rep_fopt,
			 .name      = "Unlink reply",
			 .opcode    = M0_MDSERVICE_UNLINK_REP_OPCODE,
			 .xt        = m0_fop_unlink_rep_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY);
	M0_FOP_TYPE_INIT(&m0_fop_open_rep_fopt,
			 .name      = "Open reply",
			 .opcode    = M0_MDSERVICE_OPEN_REP_OPCODE,
			 .xt        = m0_fop_open_rep_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY);
	M0_FOP_TYPE_INIT(&m0_fop_close_rep_fopt,
			 .name      = "Close reply",
			 .opcode    = M0_MDSERVICE_CLOSE_REP_OPCODE,
			 .xt        = m0_fop_close_rep_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY);
	M0_FOP_TYPE_INIT(&m0_fop_setattr_rep_fopt,
			 .name      = "Setattr reply",
			 .opcode    = M0_MDSERVICE_SETATTR_REP_OPCODE,
			 .xt        = m0_fop_setattr_rep_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY);
	M0_FOP_TYPE_INIT(&m0_fop_getattr_rep_fopt,
			 .name      = "Getattr reply",
			 .opcode    = M0_MDSERVICE_GETATTR_REP_OPCODE,
			 .xt        = m0_fop_getattr_rep_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY);
	M0_FOP_TYPE_INIT(&m0_fop_setxattr_rep_fopt,
			 .name      = "Setxattr reply",
			 .opcode    = M0_MDSERVICE_SETXATTR_REP_OPCODE,
			 .xt        = m0_fop_setxattr_rep_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY);
	M0_FOP_TYPE_INIT(&m0_fop_getxattr_rep_fopt,
			 .name      = "Getxattr reply",
			 .opcode    = M0_MDSERVICE_GETXATTR_REP_OPCODE,
			 .xt        = m0_fop_getxattr_rep_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY);
	M0_FOP_TYPE_INIT(&m0_fop_delxattr_rep_fopt,
			 .name      = "Delxattr reply",
			 .opcode    = M0_MDSERVICE_DELXATTR_REP_OPCODE,
			 .xt        = m0_fop_delxattr_rep_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY);
	M0_FOP_TYPE_INIT(&m0_fop_listxattr_rep_fopt,
			 .name      = "Listxattr reply",
			 .opcode    = M0_MDSERVICE_LISTXATTR_REP_OPCODE,
			 .xt        = m0_fop_listxattr_rep_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY);
	M0_FOP_TYPE_INIT(&m0_fop_statfs_rep_fopt,
			 .name      = "Statfs reply",
			 .opcode    = M0_MDSERVICE_STATFS_REP_OPCODE,
			 .xt        = m0_fop_statfs_rep_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY);
	M0_FOP_TYPE_INIT(&m0_fop_rename_rep_fopt,
			 .name      = "Rename reply",
			 .opcode    = M0_MDSERVICE_RENAME_REP_OPCODE,
			 .xt        = m0_fop_rename_rep_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY);
	M0_FOP_TYPE_INIT(&m0_fop_readdir_rep_fopt,
			 .name      = "Readdir reply",
			 .opcode    = M0_MDSERVICE_READDIR_REP_OPCODE,
			 .xt        = m0_fop_readdir_rep_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY);
	M0_FOP_TYPE_INIT(&m0_fop_layout_rep_fopt,
			 .name      = "Layout reply",
			 .opcode    = M0_LAYOUT_REP_OPCODE,
			 .xt        = m0_fop_layout_rep_xc,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY);
	return 0;
}

M0_INTERNAL int m0_mdservice_fop_init(void)
{
        /*
         * Provided by gccxml2xcode after parsing md_fops.h
         */
        m0_xc_md_fops_init();

        return	m0_mdservice_fopts_init() ?:
		m0_mdservice_rep_fopts_init();
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
        m0_fop_type_fini(&m0_fop_setxattr_fopt);
        m0_fop_type_fini(&m0_fop_getxattr_fopt);
        m0_fop_type_fini(&m0_fop_delxattr_fopt);
        m0_fop_type_fini(&m0_fop_listxattr_fopt);
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
        m0_fop_type_fini(&m0_fop_setxattr_rep_fopt);
        m0_fop_type_fini(&m0_fop_getxattr_rep_fopt);
        m0_fop_type_fini(&m0_fop_delxattr_rep_fopt);
        m0_fop_type_fini(&m0_fop_listxattr_rep_fopt);
        m0_fop_type_fini(&m0_fop_statfs_rep_fopt);
        m0_fop_type_fini(&m0_fop_rename_rep_fopt);
        m0_fop_type_fini(&m0_fop_readdir_rep_fopt);
        m0_fop_type_fini(&m0_fop_layout_rep_fopt);
        m0_xc_md_fops_fini();
}
M0_EXPORTED(m0_mdservice_fop_fini);

static int md_fol_rec_part_undo(struct m0_fop_fol_rec_part *fpart,
			        struct m0_fol *fol)
{
	/**
	 * @todo Perform the undo operation for meta-data
	 * updates using the generic fop fol record part.
	 */
	return 0;
}

static int md_fol_rec_part_redo(struct m0_fop_fol_rec_part *fpart,
			        struct m0_fol *fol)
{
	/**
	 * @todo Perform the redo operation for meta-data
	 * updates using the generic fop fol record part.
	 */
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
