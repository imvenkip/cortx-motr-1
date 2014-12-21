/* -*- C -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <nikita.danilov@seagate.com>
 * Original creation date: 06-Mar-2015
 */


/**
 * @addtogroup addb2
 *
 * @{
 */

#include <err.h>
#include <sys/stat.h>
#include <stdio.h>
#include <sysexits.h>
#include <stdlib.h>                   /* system */

#include "lib/assert.h"
#include "lib/errno.h"

#include "stob/domain.h"
#include "stob/stob.h"
#include "mero/init.h"
#include "module/instance.h"

#include "addb2/identifier.h"
#include "addb2/consumer.h"
#include "addb2/storage.h"

static void rec_dump(const struct m0_addb2_record *rec);
static void val_dump(const struct m0_addb2_value *val, int indent);

#define DOM "./_addb2-dump"

int main(int argc, char **argv)
{
	struct m0_stob_domain  *dom;
	struct m0_stob         *stob;
	const char             *fname = argv[1];
	struct m0_addb2_sit    *sit;
	struct stat             buf;
	struct m0_addb2_record *rec;
	struct m0               instance;
	int                     result;

	static struct m0_addb2_value_descr descr[] = {
		{ M0_AVI_NODE,                 "node" },
		{ M0_AVI_LOCALITY,             "locality" },
		{ M0_AVI_THREAD,               "thread" },
		{ M0_AVI_SERVICE,              "service" },
		{ M0_AVI_FOM,                  "fom" },
		{ M0_AVI_NULL,                 "null" },
		{ M0_AVI_CLOCK,                "clock" },
		{ M0_AVI_PHASE,                "fom-phase" },
		{ M0_AVI_STATE,                "fom-state" },
		{ M0_AVI_ALLOC,                "alloc" },
		{ M0_AVI_FOM_DESCR,            "fom-descr" },
		{ M0_AVI_FOM_ACTIVE,           "fom-active" },
		{ M0_AVI_RUNQ,                 "runq" },
		{ M0_AVI_WAIL,                 "wail" },
		{ M0_AVI_NODATA,               "nodata" },
		{ 0,                           NULL }
	};

	result = m0_init(&instance);
	if (result != 0)
		err(EX_CONFIG, "Cannot initialise mero: %d", result);
	result = m0_stob_domain_init("linuxstob:"DOM, "directio=true", &dom);
	if (result == 0)
		m0_stob_domain_destroy(dom);
	else if (result != -ENOENT)
		err(EX_CONFIG, "Cannot destroy domain: %d", result);
	result = m0_stob_domain_create_or_init("linuxstob:"DOM, "directio=true",
					       /* domain key, not important */
					       8, NULL, &dom);
	if (result != 0)
		err(EX_CANTCREAT, "Cannot create domain: %d", result);
	result = m0_stob_find_by_key(dom, 1 /* stob key, any */, &stob);
	if (result != 0)
		err(EX_CANTCREAT, "Cannot find stob: %d", result);
	result = m0_stob_locate(stob);
	if (result != 0)
		err(EX_CANTCREAT, "Cannot locate stob: %d", result);
	result = m0_stob_create(stob, NULL, fname);
	if (result != 0)
		err(EX_NOINPUT, "Cannot create stob: %d", result);
	M0_ASSERT(m0_stob_state_get(stob) == CSS_EXISTS);
	result = stat(fname, &buf);
	if (result != 0)
		err(EX_NOINPUT, "Cannot stat: %d", result);

	m0_addb2_value_id_set_nr(descr);
	/** @todo XXX size parameter copied from m0_reqh_addb2_config(). */
	result = m0_addb2_sit_init(&sit, stob, 128ULL << 30, NULL);
	if (result != 0)
		err(EX_DATAERR, "Cannot initialise iterator: %d", result);
	while ((result = m0_addb2_sit_next(sit, &rec)) > 0)
		rec_dump(rec);
	if (result != 0)
		err(EX_DATAERR, "Iterator error: %d", result);
	m0_addb2_sit_fini(sit);
	m0_stob_destroy(stob, NULL);
	m0_stob_domain_destroy(dom);
	m0_fini();
	return EX_OK;
}

#define U64 "%16"PRIx64

static void rec_dump(const struct m0_addb2_record *rec)
{
	int i;

	val_dump(&rec->ar_val, 0);
	for (i = 0; i < rec->ar_label_nr; ++i)
		val_dump(&rec->ar_label[i], 8);
}

static void val_dump(const struct m0_addb2_value *val, int indent)
{
	static const char ruler[] = "                                "
		"                                                ";
	struct m0_addb2_value_descr *descr = m0_addb2_value_id_get(val->va_id);
	int                          i;

	printf("%*.*s", indent, indent, ruler);
	if (descr != NULL)
		printf("%-16s", descr->vd_name);
	else
		printf(U64, val->va_id);
	for (i = 0; i < val->va_nr; ++i)
		printf("%s "U64, i > 0 ? "," : "", val->va_data[i]);
	printf("\n");
}

/** @} end of addb2 group */

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
