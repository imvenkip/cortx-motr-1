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
 * Original author: Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 06/28/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "lib/misc.h"		/* C2_SET0 */
#include "lib/memory.h"		/* C2_ALLOC_ARR */
#include "lib/errno.h"		/* ENOMEM */

#include "net/test/slist.h"

int c2_net_test_slist_init(struct c2_net_test_slist *slist,
			   char *str,
			   char delim)
{
	char  *str1;
	size_t len;
	size_t i = 0;

	C2_SET0(slist);

	if (str == NULL)
		return 0;
	len = strlen(str);
	if (len == 0)
		return 0;

	str1 = str;
	while (*str1 != '\0')
		slist->ntsl_nr += *str1++ == delim;
	slist->ntsl_nr++;

	C2_ALLOC_ARR(slist->ntsl_str, len + 1);
	if (slist->ntsl_str == NULL)
		return -ENOMEM;
	C2_ALLOC_ARR(slist->ntsl_list, slist->ntsl_nr);
	if (slist->ntsl_list == NULL) {
		c2_free(slist->ntsl_str);
		return -ENOMEM;
	}

	strncpy(slist->ntsl_str, str, len + 1);
	str = slist->ntsl_str;
	slist->ntsl_list[i++] = str;
	for (; *str != '\0'; ++str)
		if (*str == delim) {
			*str = '\0';
			slist->ntsl_list[i++] = str + 1;
		}
	return 0;
}

void c2_net_test_slist_fini(struct c2_net_test_slist *slist)
{
	C2_PRE(slist != NULL);

	if (slist->ntsl_nr > 0) {
		c2_free(slist->ntsl_list);
		c2_free(slist->ntsl_str);
	}
}

bool c2_net_test_slist_unique(struct c2_net_test_slist *slist)
{
	uint32_t i, j;

	for (i = 0; i < slist->ntsl_nr; ++i)
		for (j = i + 1; j < slist->ntsl_nr; ++j)
			if (strcmp(slist->ntsl_list[i],
				   slist->ntsl_list[j]) == 0)
				return false;
	return true;
}

/**
   @} end NetTestCommandsInternals
 */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
