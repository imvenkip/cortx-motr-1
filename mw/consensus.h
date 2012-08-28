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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 04/20/2010
 */

#pragma once

#ifndef __COLIBRI_MW_CONSENSUS_H__
#define __COLIBRI_MW_CONSENSUS_H__


struct c2_m_container;
struct c2_id_factory;
struct c2_tx;

/**
   @defgroup consensus Consensus
   @{
*/

struct c2_consensus_domain;
struct c2_consensus_proposer;
struct c2_consensus_acceptor;
struct c2_consensus;

struct c2_consensus_acceptor_ops {
	int  (*cacc_is_value_ok)(struct c2_consensus_acceptor *acc,
				 const struct c2_consensus *cons);
	void (*cacc_reached)(struct c2_consensus_acceptor *acc,
			     struct c2_tx *tx, const struct c2_consensus *cons);
};

int  c2_consensus_domain_init(struct c2_consensus_domain **domain);
void c2_consensus_domain_fini(struct c2_consensus_domain *domain);
int  c2_consensus_domain_add(struct c2_consensus_domain *domain,
			     struct c2_server *server);

int  c2_consensus_proposer_init(struct c2_consensus_proposer **proposer,
				struct c2_id_factory *idgen);
void c2_consensus_proposer_fini(struct c2_consensus_proposer *proposer);

int  c2_consensus_acceptor_init(struct c2_consensus_acceptor **acceptor,
				struct c2_m_container *store,
				const struct c2_consensus_acceptor_ops *ops);
void c2_consensus_acceptor_fini(struct c2_consensus_acceptor *acceptor);

int  c2_consensus_init(struct c2_consensus **cons,
		       struct c2_consensus_proposer *prop,
		       const struct c2_buf *val);
void c2_consensus_fini(struct c2_consensus *cons);

int  c2_consensus_establish(struct c2_consensus_proposer *proposer,
			    struct c2_consensus *cons);

struct c2_buf *c2_consensus_value(const struct c2_consensus *cons);

/** @} end of consensus group */

/* __COLIBRI_MW_CONSENSUS_H__ */
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
