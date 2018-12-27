/* -*- C -*- */
/*
 * COPYRIGHT 2018 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Maxim Medved <max.medved@seagate.com>
 * Original creation date: 26-Apr-2018
 */

#pragma once

#ifndef __MERO_BE_REG_H__
#define __MERO_BE_REG_H__

/**
 * @defgroup be
 *
 * @{
 */

struct m0_be_reg;
struct m0_be_tx;

M0_INTERNAL void m0_be_reg_get(const struct m0_be_reg *reg,
			       struct m0_be_tx        *tx);
M0_INTERNAL void m0_be_reg_put(const struct m0_be_reg *reg,
			       struct m0_be_tx        *tx);

#define M0_BE_REG_GET_PTR(ptr, seg, tx) \
	m0_be_reg_get(&M0_BE_REG_PTR((seg), (ptr)), (tx))
#define M0_BE_REG_PUT_PTR(ptr, seg, tx) \
	m0_be_reg_put(&M0_BE_REG_PTR((seg), (ptr)), (tx))

#define M0_BE_REG_GET_BUF(buf, seg, tx) \
	m0_be_reg_get(&M0_BE_REG_BUF((seg), (buf)), (tx))
#define M0_BE_REG_PUT_BUF(buf, seg, tx) \
	m0_be_reg_put(&M0_BE_REG_BUF((seg), (buf)), (tx))


/** @} end of be group */
#endif /* __MERO_BE_REG_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
