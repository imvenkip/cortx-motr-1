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
 * Original author: Huang Hua <hua_huang@xyratex.com>
 * Original creation date: 01/04/2011
 */

#ifndef __COLIBRI_CAPA_CAPA_H__
#define __COLIBRI_CAPA_CAPA_H__

#include "lib/atomic.h"
#include "net/net.h"

/**
   @defgroup capa Colibri Capability

Colibri Capabilities are an implementation of Capability-based security as
described here:
http://en.wikipedia.org/wiki/Capability-based_security

The idea is that an authority managing some object (e.g., a lock, a file, a
layout, etc., basically, a resource), issues a capability together with this
object. Other parties can verify that a capability was issued by the authority
but cannot forge capabilities. A typical use case is that a client receives a
capability attached to some piece of file system state and then forwards the
capability together with the state to another node. For example, a capability
attached to a fid and sent back to the server which produced the fid and the
capability, can be used to deal with fid-guessing attack. Capabilities can be
forwarded to the nodes different from ones where they originated.

Please refer to the capability HLD at:
https://docs.google.com/a/xyratex.com/Doc?docid=0AYiCgZNYbBLAZGhrZ3p2emRfMmhyZm45dGdx&hl=en

   @{
 */

/**
   Capability Protected Entity Type
*/
enum c2_capa_entity_type {
	C2_CAPA_ENTITY_OBJECT,
	C2_CAPA_ENTITY_LOCKS,
	C2_CAPA_ENTITY_LAYOUT,
};

/**
   Capability Operations
*/
enum c2_capa_operation {
	C2_CAPA_OP_DATA_READ,
	C2_CAPA_OP_DATA_WRITE,
};

enum {
	C2_CAPA_HMAC_MAX_LEN = 64
};

/**
   Capability issuer.
   @todo Use proper capability issuer
*/

struct c2_capa_issuer {

};

struct c2_capa_ctxt;
/**
   Colibri Object Capability
*/
struct c2_object_capa {
	/** the context in which this capability is issued. */
	struct c2_capa_ctxt     *oc_ctxt;
	/** an authority who issues the capability */
	struct c2_capa_issuer   *oc_owner;
	enum c2_capa_entity_type oc_type;
	enum c2_capa_operation   oc_opcode;
	struct c2_atomic64       oc_ref;

	/** an entity protectd by this capability. Data type depends on the
	    type and operation.
	*/
	void 		        *oc_data;
	char			 oc_opaque[C2_CAPA_HMAC_MAX_LEN];
};


/**
   Colibri Capability Context

   This is the context in which the capability credentials are issued,
   authorized, checked, etc.
*/
struct c2_capa_ctxt {
	/** more fields go here */
};

/**
   Init a Colibri Capability Context

   @param ctxt the execution context
   @return 0 means success. Otherwise failure.
*/
int c2_capa_ctxt_init(struct c2_capa_ctxt *ctxt);

/**
   Fini a Colibri Capability Context

   @param ctxt the execution context
*/
void c2_capa_ctxt_fini(struct c2_capa_ctxt *ctxt);

/**
   New Capability for an object for specified operation

   @param capa [in][out]result will be stored here.
   @param type [in] type of the capability.
   @param opcode [in] operation code.
   @param data [in] opaque object that this capability protects.
   @return 0 means success. Otherwise failure.

   Reference count will be initialzed to zero.
*/
int c2_capa_new(struct c2_object_capa *capa,
	         enum c2_capa_entity_type type,
	         enum c2_capa_operation opcode,
		 void *data);

/**
   Get Capability for an object for specified operation

   @param ctxt [in]the execution context.
   @param owner [in] owner of this capa.
   @param capa [in][out]result will be stored here.
   @return 0 means success. Otherwise failure.

   @pre c2_capa_new() should be called successfully.
   Reference count will be bumped.
*/
int c2_capa_get(struct c2_capa_ctxt *ctxt, struct c2_capa_issuer *owner,
		struct c2_object_capa *capa);

/*
   Put Capability for an object

   @param ctxt [in]the execution context.
   @param capa [in]capa to be released.

   Reference count will be decreased. When reference count drops to zero,
   it will be finalized and can not be used any more.
*/
void c2_capa_put(struct c2_capa_ctxt *ctxt, struct c2_object_capa *capa);


/**
   Authenticate an operation

   @param ctxt [in]the execution context.
   @param capa [in]capability to be authenticated.
   @param op [in] target operation.
   @return 0 means permission is granted. -EPERM means access denied, and
           others mean error.
*/
int c2_capa_auth(struct c2_capa_ctxt *ctxt, struct c2_object_capa *capa,
		 enum c2_capa_operation op);


/** @} end group capa */

/* __COLIBRI_CAPA_CAPA_H__ */
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
