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
 * Original author: Dave Cohrs <dave_cohrs@xyratex.com>
 * Original creation date: 2-Apr-2013
 */

#include "lib/misc.h"
#include "lib/ut.h"
#include "mgmt/mgmt.h"

#include <sys/stat.h> /* mkdir */
#include <limits.h>   /* HOST_NAME_MAX */
#include <unistd.h>   /* gethostname */

static char mgmt_conf_hostname[HOST_NAME_MAX];

static char *genders_proto[] = {
	"h[00-10]     m0_lnet_if=o2ib0,m0_lnet_pid=12345\n",
	"h[00-10],%s  m0_lnet_kernel_portal=34\n",
	"h[00-10],%s  m0_lnet_m0d_portal=35\n",
	"h[00-10],%s  m0_lnet_client_portal=36\n",
	"h00          m0_lnet_host=192.168.1.2\n",
	"h[01-10]     m0_lnet_host=l%%n\n",
	"%s           m0_lnet_if=tcp,m0_lnet_pid=12121\n",
	"%s           m0_lnet_host=localhost\n",
	"h[00-10],%s  m0_var=var_mero\n",
	"h[00-10],%s  m0_max_rpc_msg=163840\n",
	"h[00-10],%s  m0_min_recv_q=2\n",
	"h00          m0_s_confd=-c:/var/mero/confd/confdb.txt\n",
	"h00          m0_s_rm\n",
	"h00          m0_s_mdservice=-p\n",
	"h[00-10],%s  m0_s_addb=-A:/etc/mero/stobs\n",
	"h[00-10],%s  m0_s_ioservice=-T:AD:-S:/etc/mero/stobs\n",
	"h[00-10],%s  m0_s_sns\n",
	"h00 m0_uuid=b47539c2-143e-44e8-9594-a8f6e09bfec0\n",
	"h01 m0_uuid=6d5ddc53-b1b6-43ae-9c7c-16c227b2ea5a\n",
	"h02 m0_uuid=26a17da7-d5f2-462d-960d-205334adb028\n",
	"h03 m0_uuid=68b617e1-097a-4e46-8d16-3e202628c568\n",
	"%s  m0_uuid=4b7539c2-143e-44e8-9594-a8f6e09bfec0\n",
	"h00 m0_HA-PROXY\n",
	"h00 m0_u_confd=d2655b68-f578-45cb-bbb9-c1495e083074\n",
	"h00 m0_u_rm=a19d7247-cd7b-4f7e-b55a-b5c31e181b7d\n",
	"h00 m0_u_mdservice=a55763b7-c115-40b6-ad50-941c96847f72\n",
	"h00 m0_u_addb=c228f8fc-356a-4963-9b56-34b7773ba09d\n",
	"h00 m0_u_ioservice=00ca6771-757e-4824-8059-88151a1ed996\n",
	"h00 m0_u_sns=44e35d39-18d8-4a9a-a975-40645a7b7ec2\n",
	"%s  m0_u_addb=a1c18e3c-76a5-482a-a52c-10f91f65f399\n",
	"%s  m0_u_ioservice=f595564a-20ca-4b12-8f4b-0d2f82726d61\n",
	"%s  m0_u_sns=97b8a598-9377-4d8e-af1c-f1c74640df99\n",
};

static int mgmt_conf_ut_init(void)
{
	FILE *g;
	char *ptr;
	int   i;
	int   rc;

	rc = mkdir("var_mero", 0777);
	if (rc != 0)
		return -errno;

	rc = gethostname(mgmt_conf_hostname, sizeof mgmt_conf_hostname);
	if (rc != 0)
		return -errno;
	ptr = strchr(mgmt_conf_hostname, '.');
	if (ptr != NULL)		/* want short name only */
		*ptr = 0;

	g = fopen("test-genders", "w");
	if (g == NULL)
		return -errno;

	for (i = 0; i < ARRAY_SIZE(genders_proto); ++i) {
		ptr = genders_proto[i];
		rc = fprintf(g, ptr, mgmt_conf_hostname);
		if (rc < 0) {
			rc = -errno;
			break;
		}
	}
	rc = fclose(g);
	if (rc != 0)
		rc = -errno;

	return rc;
}

static int mgmt_conf_ut_fini(void)
{
	return 0;
}

static void test_genders_parse(void)
{
	struct m0_mgmt_conf conf;
	int                 rc;

	rc = m0_mgmt_conf_init(&conf, "test-genders", NULL);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(strcmp(conf.mnc_name, mgmt_conf_hostname) == 0);
	M0_UT_ASSERT(strcmp(conf.mnc_uuid,
			    "4b7539c2-143e-44e8-9594-a8f6e09bfec0") == 0);
	M0_UT_ASSERT(strcmp(conf.mnc_m0d_ep, "127.0.0.1@tcp:12121:35:0") == 0);
	M0_UT_ASSERT(strcmp(conf.mnc_client_ep,
			    "127.0.0.1@tcp:12121:36:*") == 0);
	M0_UT_ASSERT(strcmp(conf.mnc_client_uuid, conf.mnc_uuid) == 0);
	M0_UT_ASSERT(strcmp(conf.mnc_var, "var_mero") == 0);
	M0_UT_ASSERT(m0_mgmt_conf_tlist_length(&conf.mnc_svc) == 3);
	m0_mgmt_conf_fini(&conf);
}

const struct m0_test_suite m0_mgmt_svc_ut = {
	.ts_name = "mgmt-conf-ut",
	.ts_init = mgmt_conf_ut_init,
	.ts_fini = mgmt_conf_ut_fini,
	.ts_tests = {
		{ "genders-parse", test_genders_parse },
		{ NULL, NULL }
	}
};
M0_EXPORTED(m0_mgmt_svc_ut);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
