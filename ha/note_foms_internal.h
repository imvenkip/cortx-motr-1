/* -*- C -*- */
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
 * Original author: Nicholas Clarke <nicholas_clarke@xyratex.com>
 * Original creation date: 2014-05-22
 */

 /* Exported solely for the purpose of unit test */

#pragma once

#ifndef __MERO___HA_NOTE_FOMS_INTERNAL_H__
#define __MERO___HA_NOTE_FOMS_INTERNAL_H__

struct m0_ha_state_set_fom {
	/** Generic m0_fom object. */
	struct m0_fom  fp_gen;
	/** FOP associated with this FOM. */
	struct m0_fop *fp_fop;
};

M0_INTERNAL void m0_ha_state_set_fom_fini(struct m0_fom *fom);
M0_INTERNAL size_t m0_ha_state_set_fom_home_locality(const struct m0_fom *fom);
M0_INTERNAL void m0_ha_state_set_fom_addb_init(struct m0_fom *fom,
					       struct m0_addb_mc *mc);

#endif /* __MERO___HA_NOTE_FOMS_INTERNAL_H__ */