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
 * Original author: Alexey Lyashkov
 * Original creation date: 05/18/2010
 */

#ifndef __COLIBRI_SESSION_XDR_H__
#define __COLIBRI_SESSION_XDR_H__

/** 
 the xdr functions 

 each function is converted one type from network to local representations.
 function must return true iif conversion successuly 
*/


struct c2_session_create_arg;
extern  bool c2_xdr_session_create_arg (void *, struct c2_session_create_arg *);

struct c2_session_create_ret;
extern  bool c2_xdr_session_create_ret (void *, struct c2_session_create_ret *);

struct c2_session_destroy_arg;
extern  bool c2_xdr_session_destroy_arg (void *, struct c2_session_destroy_arg *);

struct c2_session_destroy_ret;
extern  bool c2_xdr_session_destroy_ret (void *, struct c2_session_destroy_ret *);

#endif
