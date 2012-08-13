/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 01/04/2011
 */

#pragma once

#ifndef __COLIBRI_UDB_UDB_H__
#define __COLIBRI_UDB_UDB_H__

/**
   @defgroup udb Enterprise User Daba-base

   Please see the HLD at:
https://docs.google.com/a/xyratex.com/Doc?docid=0AYiCgZNYbBLAZGhrZ3p2emRfM2Z0NHZkM2Nj&hl=en

   @{
 */

/**
   User Credential Domain.

   A domain is a collection of nodes. All nodes in the domain share some
   common ways, e.g. all user credentials from this domain are mapped into the
   same user credential.
*/
struct c2_udb_domain {

};


/**
   Colibri User Credential.

   There are two categories of user credentials: internal and external.
   <li> The internal user credentials are used on servers and are stored in
   persistent storage. All permission checking on server side are based
   on this.
   <li> The external user credentials are used on clients. They are visiable
   to user applications. These user credentials are mapped into internals ones
   before the system permission checking. At the same time, when internal user
   credentials in the system are returned to clients, they are mapped into
   externals ones.
*/
enum c2_udb_cred_type {
	C2_UDB_CRED_INTERNAL,
	C2_UDB_CRED_EXTERNAL
};

struct c2_udb_cred {
	enum c2_udb_cred_type uc_type;
	struct c2_udb_domain *uc_domain;
};


/**
   User Data-base Context
*/
struct c2_udb_ctxt {

};

/**
   Init a Colibri User Data-base Context.

   @param ctxt the user db context
   @return 0 means success. Otherwise failure.
*/
int c2_udb_ctxt_init(struct c2_udb_ctxt *ctxt);

/**
   Fini a Colibri User Data-base Context

   @param ctxt the udb context
*/
void c2_udb_ctxt_fini(struct c2_udb_ctxt *ctxt);

/**
   add a cred mapping into udb

   @param ctxt [in]the udb context.
   @param edomain [in]external credential domain.
   @param external [in]external credential.
   @param internal [in]internal credential.
   @return 0 means success. otherwise failure.

   If the external domain is valid and external credential is CRED_ANY,
   that means to mapping any credentials from this domain to the specified
   internal credential.
*/
int c2_udb_add(struct c2_udb_ctxt *ctxt,
	       const struct c2_udb_domain *edomain,
	       const struct c2_udb_cred *external,
	       const struct c2_udb_cred *internal);

/**
   delete a cred mapping into udb

   @param ctxt [in]the udb context.
   @param edomain [in]external credential domain.
   @param external [in]external credential.
   @param internal [in]internal credential.
   @return 0 means success. otherwise failure.
*/
int c2_udb_del(struct c2_udb_ctxt *ctxt,
	       const struct c2_udb_domain *edomain,
	       const struct c2_udb_cred *external,
	       const struct c2_udb_cred *internal);


/**
   map an external cred to internal cred

   @param ctxt [in]the udb context.
   @param external [in]external credential.
   @param internal [out]internal credential.
   @return 0 means success. otherwise failure.
*/
int c2_udb_e2i(struct c2_udb_ctxt *ctxt,
	       const struct c2_udb_cred *external,
	       struct c2_udb_cred *internal);

/**
   map an internal cred to external cred

   @param ctxt [in]the udb context.
   @param internal [in]internal credential.
   @param external [out]external credential.
   @return 0 means success. otherwise failure.
*/
int c2_udb_i2e(struct c2_udb_ctxt *ctxt,
	       const struct c2_udb_cred *internal,
	       struct c2_udb_cred *external);

/** @} end group udb */

/* __COLIBRI_UDB_UDB_H__ */
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
