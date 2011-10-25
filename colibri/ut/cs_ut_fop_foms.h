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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 05/08/2011
 */

#ifndef __COLIBRI_COLIBRI_UT_CS_UT_FOP_FOMS_H__
#define __COLIBRI_COLIBRI_UT_CS_UT_FOP_FOMS_H__

/*
  Supported service types.
 */
enum {
        DS_ONE = 1,
        DS_TWO,
};

/*
  Builds ds1 service fop types.
  Invoked from service specific stop function.
 */
int c2_cs_ut_ds1_fop_init(void);

/*
  Finalises ds1 service fop types.
  Invoked from service specific startup function.
 */
void c2_cs_ut_ds1_fop_fini(void);

/*
  Builds ds1 service fop types.
  Invoked from service specific stop function.
 */
int c2_cs_ut_ds2_fop_init(void);

/*
  Finalises ds1 service fop types.
  Invoked from service specific startup function.
 */
void c2_cs_ut_ds2_fop_fini(void);

/*
  Sends fops to server.
 */
void c2_cs_ut_send_fops(struct c2_rpc_session *cl_rpc_session, int dstype);

/* __COLIBRI_COLIBRI_UT_CS_UT_FOP_FOMS_H__ */
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
