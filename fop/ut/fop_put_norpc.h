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
 * Original author: Andriy Tkachuk <andriy_tkachuk@xyratex.com>
 * Original creation date: 2/21/2013
 */

#pragma once

#ifndef __MERO_FOP_UT_FOP_PUT_NORPC_H__
#define __MERO_FOP_UT_FOP_PUT_NORPC_H__

#include "fop/fom.h"

/**
 * Puts fom's fops without holding rpc machine lock.
 *
 * m0_fom_fini() requires rpc machine (ri_rmachine) to be set
 * for the fops. But some UTs don't create rpc machine and don't
 * submit the fops to the RPC layer, so ri_rmachine is not set there.
 * This routine is called in such cases just before m0_fom_fini().
 */
void fom_fop_put_norpc(struct m0_fom *fom);

#endif /* __MERO_FOP_UT_FOP_PUT_NORPC_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
