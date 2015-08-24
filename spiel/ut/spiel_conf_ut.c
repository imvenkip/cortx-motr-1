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
 * Original author: Mikhail Antropov <mikhail.v.antropov@seagate.com>
 * Original creation date: 24-Feb-2015
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SPIEL
#include "lib/trace.h"

#include "lib/memory.h"                 /* M0_ALLOC_PTR */
#include "lib/errno.h"
#include "lib/string.h"
#include "lib/finject.h"
#include "ut/ut.h"
#include "conf/cache.h"
#include "conf/load_fom.h"              /* m0_conf_cache_from_string */
#include "conf/obj_ops.h"               /* m0_conf_obj_create */
#include "conf/onwire.h"                /* m0_confx */
#include "conf/onwire_xc.h"             /* m0_confx_xc */
#include "conf/preload.h"               /* m0_confx_free */
#include "spiel/spiel.h"
#include "spiel/ut/spiel_ut_common.h"
#include "ut/file_helpers.h"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

struct m0_spiel spiel;

enum {
	SPIEL_UT_OBJ_DIR,
	SPIEL_UT_OBJ_PROFILE,
	SPIEL_UT_OBJ_FILESYSTEM,
	SPIEL_UT_OBJ_POOL,
	SPIEL_UT_OBJ_PVER,
	SPIEL_UT_OBJ_NODE,
	SPIEL_UT_OBJ_PROCESS,
	SPIEL_UT_OBJ_SERVICE,
	SPIEL_UT_OBJ_SDEV,
	SPIEL_UT_OBJ_RACK,
	SPIEL_UT_OBJ_ENCLOSURE,
	SPIEL_UT_OBJ_CONTROLLER,
	SPIEL_UT_OBJ_DISK,
	SPIEL_UT_OBJ_RACK_V,
	SPIEL_UT_OBJ_ENCLOSURE_V,
	SPIEL_UT_OBJ_CONTROLLER_V,
	SPIEL_UT_OBJ_DISK_V,
	SPIEL_UT_OBJ_COUNT,
};

#define	SPIEL_UT_OBJ_ROOT   SPIEL_UT_OBJ_COUNT


static struct m0_fid spiel_obj_fid[] = {
	M0_FID_TINIT('D', 1, 1 ),
	M0_FID_TINIT('p', 2, 2 ),
	M0_FID_TINIT('f', 3, 3 ),
	M0_FID_TINIT('o', 4, 4 ),
	M0_FID_TINIT('v', 5, 5 ),
	M0_FID_TINIT('n', 6, 6 ),
	M0_FID_TINIT('r', 7, 7 ),
	M0_FID_TINIT('s', 8, 8 ),
	M0_FID_TINIT('d', 9, 9 ),
	M0_FID_TINIT('a', 10, 10),
	M0_FID_TINIT('e', 11, 11),
	M0_FID_TINIT('c', 12, 12),
	M0_FID_TINIT('k', 13, 13),
	M0_FID_TINIT('j', 14, 14),
	M0_FID_TINIT('j', 15, 15),
	M0_FID_TINIT('j', 16, 16),
	M0_FID_TINIT('j', 17, 17),
	M0_FID_TINIT(0, 00, 00),
};

static int spiel_copy_file(const char *source, const char* dest)
{
	char buf[1024];
	int in_fd;
	int out_fd;
	size_t result;
	int rc = 0;

	in_fd = open(source, O_RDONLY);
	if (in_fd < 0)
		return -EINVAL;

	out_fd = open(dest, O_WRONLY | O_CREAT, 0666);
	if (in_fd < 0) {
		close(in_fd);
		return -EINVAL;
	}

	while (1) {
		rc = (result = read(in_fd, &buf[0], sizeof(buf))) >= 0 ?
		     0 : -EINVAL;
		if (result == 0 || rc != 0)
			break;
		rc = write(out_fd, &buf[0], result) == result ? 0 : -EINVAL;
		if (rc != 0)
			break;
	}
	close(in_fd);
	close(out_fd);

	return rc;
}

static int spiel_conf_ut_init()
{
	spiel_copy_file(M0_UT_PATH("conf-str.txt"),
			M0_UT_PATH("conf-str-tmp.txt"));

	/* Use copy of conf-str file as confd path - file may be changed */
	int rc = m0_spiel__ut_init(&spiel, M0_UT_PATH("conf-str-tmp.txt"));
	M0_UT_ASSERT(rc == 0);

	return 0;
}

static int spiel_conf_ut_fini()
{
	int rc;

	m0_spiel__ut_fini(&spiel);

	rc = system("rm -rf "M0_UT_PATH("confd"));
	M0_ASSERT(rc != -1);
	unlink(M0_UT_PATH("conf-str-tmp.txt"));
	return 0;
}

static void spiel_conf_create_configuration(struct m0_spiel    *spiel,
					    struct m0_spiel_tx *tx)
{
	int                           rc;
	struct m0_pdclust_attr        pdclust_attr = { .pa_N=0,
						       .pa_K=0,
						       .pa_P=0};
	const char                   *fs_param[] = { "11111", "22222", NULL };
	struct m0_spiel_service_info  service_info = {.svi_endpoints=fs_param };
	struct m0_bitmap              bitmap;

	m0_bitmap_init(&bitmap, 32);
	m0_bitmap_set(&bitmap, 0, true);
	m0_bitmap_set(&bitmap, 1, true);

	m0_spiel_tx_open(spiel, tx);
	M0_UT_ASSERT(tx->spt_version != M0_CONF_VER_UNKNOWN);

	rc = m0_spiel_profile_add(tx, &spiel_obj_fid[SPIEL_UT_OBJ_PROFILE]);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_filesystem_add(tx,
				     &spiel_obj_fid[SPIEL_UT_OBJ_FILESYSTEM],
				     &spiel_obj_fid[SPIEL_UT_OBJ_PROFILE],
				     10,
				     &spiel_obj_fid[SPIEL_UT_OBJ_PROFILE],
				     &spiel_obj_fid[SPIEL_UT_OBJ_POOL],
				     fs_param);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_pool_add(tx,
			       &spiel_obj_fid[SPIEL_UT_OBJ_POOL],
			       &spiel_obj_fid[SPIEL_UT_OBJ_FILESYSTEM],
			       2);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_rack_add(tx,
			       &spiel_obj_fid[SPIEL_UT_OBJ_RACK],
			       &spiel_obj_fid[SPIEL_UT_OBJ_FILESYSTEM]);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_enclosure_add(tx,
				    &spiel_obj_fid[SPIEL_UT_OBJ_ENCLOSURE],
				    &spiel_obj_fid[SPIEL_UT_OBJ_RACK]);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_node_add(tx,
			       &spiel_obj_fid[SPIEL_UT_OBJ_NODE],
			       &spiel_obj_fid[SPIEL_UT_OBJ_FILESYSTEM],
			       256,
			       2,
			       10,
			       0xff00ff00,
			       &spiel_obj_fid[SPIEL_UT_OBJ_POOL]);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_controller_add(tx,
				     &spiel_obj_fid[SPIEL_UT_OBJ_CONTROLLER],
				     &spiel_obj_fid[SPIEL_UT_OBJ_ENCLOSURE],
				     &spiel_obj_fid[SPIEL_UT_OBJ_NODE]);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_disk_add(tx,
			       &spiel_obj_fid[SPIEL_UT_OBJ_DISK],
			       &spiel_obj_fid[SPIEL_UT_OBJ_CONTROLLER]);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_pool_version_add(tx,
				       &spiel_obj_fid[SPIEL_UT_OBJ_PVER],
				       &spiel_obj_fid[SPIEL_UT_OBJ_POOL],
				       &pdclust_attr);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_rack_v_add(tx,
				 &spiel_obj_fid[SPIEL_UT_OBJ_RACK_V],
				 &spiel_obj_fid[SPIEL_UT_OBJ_PVER],
				 &spiel_obj_fid[SPIEL_UT_OBJ_RACK]);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_enclosure_v_add(tx,
				      &spiel_obj_fid[SPIEL_UT_OBJ_ENCLOSURE_V],
				      &spiel_obj_fid[SPIEL_UT_OBJ_RACK_V],
				      &spiel_obj_fid[SPIEL_UT_OBJ_ENCLOSURE]);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_controller_v_add(tx,
				      &spiel_obj_fid[SPIEL_UT_OBJ_CONTROLLER_V],
				      &spiel_obj_fid[SPIEL_UT_OBJ_ENCLOSURE_V],
				      &spiel_obj_fid[SPIEL_UT_OBJ_CONTROLLER]);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_disk_v_add(tx,
				 &spiel_obj_fid[SPIEL_UT_OBJ_DISK_V],
				 &spiel_obj_fid[SPIEL_UT_OBJ_CONTROLLER_V],
				 &spiel_obj_fid[SPIEL_UT_OBJ_DISK]);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_process_add(tx,
				  &spiel_obj_fid[SPIEL_UT_OBJ_PROCESS],
				  &spiel_obj_fid[SPIEL_UT_OBJ_NODE],
				  &bitmap, 4000, 1, 2, 3);
	M0_UT_ASSERT(rc == 0);

	service_info.svi_type = M0_CST_IOS;
	service_info.svi_u.repair_limits = 10;
			rc = m0_spiel_service_add(tx,
				  &spiel_obj_fid[SPIEL_UT_OBJ_SERVICE],
				  &spiel_obj_fid[SPIEL_UT_OBJ_PROCESS],
				  &service_info);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_device_add(tx,
				 &spiel_obj_fid[SPIEL_UT_OBJ_SDEV],
				 &spiel_obj_fid[SPIEL_UT_OBJ_SERVICE],
				 &spiel_obj_fid[SPIEL_UT_OBJ_DISK],
				 M0_CFG_DEVICE_INTERFACE_SCSI,
				 M0_CFG_DEVICE_MEDIA_SSD,
				 1024, 512, 123, 0x55, "fake_filename");
	M0_UT_ASSERT(rc == 0);

	m0_bitmap_fini(&bitmap);
}

/*
 * spiel-conf-create-ok test
 */
static void spiel_conf_create_ok(void)
{
	struct m0_spiel_tx tx;

	spiel_conf_ut_init();
	spiel_conf_create_configuration(&spiel, &tx);
	m0_spiel_tx_close(&tx);
	spiel_conf_ut_fini();
}

/*
 * spiel-conf-create-fail test
 */
static void spiel_conf_create_fail(void)
{
	struct m0_spiel_tx            tx;
	const char                   *ep[] = { SERVER_ENDPOINT_ADDR, NULL };
	int                           rc;
	struct m0_pdclust_attr        pdclust_attr = { .pa_N=0,
						       .pa_K=0,
						       .pa_P=0};
	const char                   *fs_param[] = { "11111", "22222", NULL };
	struct m0_spiel_service_info  service_info = {
		.svi_endpoints = fs_param };
	struct m0_fid                 fake_profile_fid =
					spiel_obj_fid[SPIEL_UT_OBJ_NODE];
	struct m0_fid                 fake_fid =
					spiel_obj_fid[SPIEL_UT_OBJ_PROFILE];
	struct m0_bitmap              bitmap;

	spiel_conf_ut_init();
	m0_bitmap_init(&bitmap, 32);
	m0_bitmap_set(&bitmap, 0, true);
	m0_bitmap_set(&bitmap, 1, true);

	m0_spiel_tx_open(&spiel, &tx);
	M0_UT_ASSERT(tx.spt_version != M0_CONF_VER_UNKNOWN);

	/* Profile */
	rc = m0_spiel_profile_add(&tx, &fake_profile_fid);
	M0_UT_ASSERT(rc == -EINVAL);

	m0_fi_enable_once("m0_alloc", "fail_allocation");
	rc = m0_spiel_profile_add(&tx, &spiel_obj_fid[SPIEL_UT_OBJ_PROFILE]);
	M0_UT_ASSERT(rc == -ENOMEM);

	rc = m0_spiel_profile_add(&tx, &spiel_obj_fid[SPIEL_UT_OBJ_PROFILE]);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_profile_add(&tx, &spiel_obj_fid[SPIEL_UT_OBJ_PROFILE]);
	M0_UT_ASSERT(rc == -EEXIST);

	/* Filesystem */
	rc = m0_spiel_filesystem_add(&tx,
				     &fake_fid,
				     &spiel_obj_fid[SPIEL_UT_OBJ_PROFILE],
				     10,
				     &spiel_obj_fid[SPIEL_UT_OBJ_PROFILE],
				     &spiel_obj_fid[SPIEL_UT_OBJ_POOL],
				     fs_param);
	M0_UT_ASSERT(rc == -EINVAL);

	/* alloc fail for cf_params */
	m0_fi_enable_once("m0_alloc", "fail_allocation");
	rc = m0_spiel_filesystem_add(&tx,
				     &spiel_obj_fid[SPIEL_UT_OBJ_FILESYSTEM],
				     &spiel_obj_fid[SPIEL_UT_OBJ_PROFILE],
				     10,
				     &spiel_obj_fid[SPIEL_UT_OBJ_PROFILE],
				     &spiel_obj_fid[SPIEL_UT_OBJ_POOL],
				     fs_param);
	M0_UT_ASSERT(rc == -ENOMEM);

	/* alloc fail for cf_params */
	m0_fi_enable_off_n_on_m("m0_strings_dup",
				"strdup_failed", 1, 1);
	rc = m0_spiel_filesystem_add(&tx,
				     &spiel_obj_fid[SPIEL_UT_OBJ_FILESYSTEM],
				     &spiel_obj_fid[SPIEL_UT_OBJ_PROFILE],
				     10,
				     &spiel_obj_fid[SPIEL_UT_OBJ_PROFILE],
				     &spiel_obj_fid[SPIEL_UT_OBJ_POOL],
				     fs_param);
	m0_fi_disable("m0_strings_dup", "strdup_failed");
	M0_UT_ASSERT(rc == -ENOMEM);

	rc = m0_spiel_filesystem_add(&tx,
				     &spiel_obj_fid[SPIEL_UT_OBJ_FILESYSTEM],
				     &spiel_obj_fid[SPIEL_UT_OBJ_PROFILE],
				     10,
				     &spiel_obj_fid[SPIEL_UT_OBJ_PROFILE],
				     &spiel_obj_fid[SPIEL_UT_OBJ_POOL],
				     fs_param);
	M0_UT_ASSERT(rc == 0);

	/* Pool */
	rc = m0_spiel_pool_add(&tx,
			       &fake_fid,
			       &spiel_obj_fid[SPIEL_UT_OBJ_FILESYSTEM],
			       2);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_pool_add(&tx,
			       &spiel_obj_fid[SPIEL_UT_OBJ_POOL],
			       &fake_fid,
			       2);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_pool_add(&tx,
			       &spiel_obj_fid[SPIEL_UT_OBJ_POOL],
			       &spiel_obj_fid[SPIEL_UT_OBJ_FILESYSTEM],
			       2);
	M0_UT_ASSERT(rc == 0);

	/* Rack */
	rc = m0_spiel_rack_add(&tx,
			       &fake_fid,
			       &spiel_obj_fid[SPIEL_UT_OBJ_FILESYSTEM]);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_rack_add(&tx,
			       &spiel_obj_fid[SPIEL_UT_OBJ_RACK],
			       &fake_fid);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_rack_add(&tx,
			       &spiel_obj_fid[SPIEL_UT_OBJ_RACK],
			       &spiel_obj_fid[SPIEL_UT_OBJ_FILESYSTEM]);
	M0_UT_ASSERT(rc == 0);

	/* Enclosure */
	rc = m0_spiel_enclosure_add(&tx,
				    &fake_fid,
				    &spiel_obj_fid[SPIEL_UT_OBJ_RACK]);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_enclosure_add(&tx,
				    &spiel_obj_fid[SPIEL_UT_OBJ_ENCLOSURE],
				    &fake_fid);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_enclosure_add(&tx,
				    &spiel_obj_fid[SPIEL_UT_OBJ_ENCLOSURE],
				    &spiel_obj_fid[SPIEL_UT_OBJ_RACK]);
	M0_UT_ASSERT(rc == 0);

	/* Node */
	rc = m0_spiel_node_add(&tx,
			       &spiel_obj_fid[SPIEL_UT_OBJ_NODE],
			       &spiel_obj_fid[SPIEL_UT_OBJ_FILESYSTEM],
			       256, 2, 10, 0xff00ff00,
			       &fake_fid);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_node_add(&tx,
			       &fake_fid,
			       &spiel_obj_fid[SPIEL_UT_OBJ_FILESYSTEM],
			       256, 2, 10, 0xff00ff00,
			       &spiel_obj_fid[SPIEL_UT_OBJ_POOL]);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_node_add(&tx,
			       &spiel_obj_fid[SPIEL_UT_OBJ_NODE],
			       &fake_fid,
			       256, 2, 10, 0xff00ff00,
			       &spiel_obj_fid[SPIEL_UT_OBJ_POOL]);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_node_add(&tx,
			       &spiel_obj_fid[SPIEL_UT_OBJ_NODE],
			       &spiel_obj_fid[SPIEL_UT_OBJ_FILESYSTEM],
			       256, 2, 10, 0xff00ff00,
			       &spiel_obj_fid[SPIEL_UT_OBJ_POOL]);
	M0_UT_ASSERT(rc == 0);

	/* Controller */
	rc = m0_spiel_controller_add(&tx,
				     &spiel_obj_fid[SPIEL_UT_OBJ_CONTROLLER],
				     &spiel_obj_fid[SPIEL_UT_OBJ_ENCLOSURE],
				     &fake_fid);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_controller_add(&tx,
				     &fake_fid,
				     &spiel_obj_fid[SPIEL_UT_OBJ_ENCLOSURE],
				     &spiel_obj_fid[SPIEL_UT_OBJ_NODE]);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_controller_add(&tx,
				     &spiel_obj_fid[SPIEL_UT_OBJ_CONTROLLER],
				     &fake_fid,
				     &spiel_obj_fid[SPIEL_UT_OBJ_NODE]);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_controller_add(&tx,
				     &spiel_obj_fid[SPIEL_UT_OBJ_CONTROLLER],
				     &spiel_obj_fid[SPIEL_UT_OBJ_ENCLOSURE],
				     &spiel_obj_fid[SPIEL_UT_OBJ_NODE]);
	M0_UT_ASSERT(rc == 0);

	/* Disk */
	rc = m0_spiel_disk_add(&tx,
			       &fake_fid,
			       &spiel_obj_fid[SPIEL_UT_OBJ_CONTROLLER]);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_disk_add(&tx,
			       &spiel_obj_fid[SPIEL_UT_OBJ_DISK],
			       &fake_fid);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_disk_add(&tx,
			       &spiel_obj_fid[SPIEL_UT_OBJ_DISK],
			       &spiel_obj_fid[SPIEL_UT_OBJ_CONTROLLER]);
	M0_UT_ASSERT(rc == 0);

	/* Pool version */
	rc = m0_spiel_pool_version_add(&tx,
				       &fake_fid,
				       &spiel_obj_fid[SPIEL_UT_OBJ_POOL],
				       &pdclust_attr);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_pool_version_add(&tx,
				       &spiel_obj_fid[SPIEL_UT_OBJ_PVER],
				       &fake_fid,
				       &pdclust_attr);
	M0_UT_ASSERT(rc == -EINVAL);

	pdclust_attr.pa_K = 10;
	rc = m0_spiel_pool_version_add(&tx,
				       &spiel_obj_fid[SPIEL_UT_OBJ_PVER],
				       &fake_fid,
				       &pdclust_attr);
	M0_UT_ASSERT(rc == -EINVAL);

	pdclust_attr.pa_K = 0;
	rc = m0_spiel_pool_version_add(&tx,
				       &spiel_obj_fid[SPIEL_UT_OBJ_PVER],
				       &spiel_obj_fid[SPIEL_UT_OBJ_POOL],
				       &pdclust_attr);
	M0_UT_ASSERT(rc == 0);

	/* Rack version */
	rc = m0_spiel_rack_v_add(&tx,
				 &spiel_obj_fid[SPIEL_UT_OBJ_RACK_V],
				 &spiel_obj_fid[SPIEL_UT_OBJ_PVER],
				 &fake_fid);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_rack_v_add(&tx,
				 &fake_fid,
				 &spiel_obj_fid[SPIEL_UT_OBJ_PVER],
				 &spiel_obj_fid[SPIEL_UT_OBJ_RACK]);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_rack_v_add(&tx,
				 &spiel_obj_fid[SPIEL_UT_OBJ_RACK_V],
				 &fake_fid,
				 &spiel_obj_fid[SPIEL_UT_OBJ_RACK]);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_rack_v_add(&tx,
				 &spiel_obj_fid[SPIEL_UT_OBJ_RACK_V],
				 &spiel_obj_fid[SPIEL_UT_OBJ_PVER],
				 &spiel_obj_fid[SPIEL_UT_OBJ_RACK]);
	M0_UT_ASSERT(rc == 0);

	/* Enclosure version */
	rc = m0_spiel_enclosure_v_add(&tx,
				      &spiel_obj_fid[SPIEL_UT_OBJ_ENCLOSURE_V],
				      &spiel_obj_fid[SPIEL_UT_OBJ_RACK_V],
				      &fake_fid);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_enclosure_v_add(&tx,
				      &fake_fid,
				      &spiel_obj_fid[SPIEL_UT_OBJ_RACK_V],
				      &spiel_obj_fid[SPIEL_UT_OBJ_ENCLOSURE]);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_enclosure_v_add(&tx,
				      &spiel_obj_fid[SPIEL_UT_OBJ_ENCLOSURE_V],
				      &fake_fid,
				      &spiel_obj_fid[SPIEL_UT_OBJ_ENCLOSURE]);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_enclosure_v_add(&tx,
				      &spiel_obj_fid[SPIEL_UT_OBJ_ENCLOSURE_V],
				      &spiel_obj_fid[SPIEL_UT_OBJ_RACK_V],
				      &spiel_obj_fid[SPIEL_UT_OBJ_ENCLOSURE]);
	M0_UT_ASSERT(rc == 0);

	/* Controller version */
	rc = m0_spiel_controller_v_add(&tx,
				      &spiel_obj_fid[SPIEL_UT_OBJ_CONTROLLER_V],
				      &spiel_obj_fid[SPIEL_UT_OBJ_ENCLOSURE_V],
				      &fake_fid);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_controller_v_add(&tx,
				      &fake_fid,
				      &spiel_obj_fid[SPIEL_UT_OBJ_ENCLOSURE_V],
				      &spiel_obj_fid[SPIEL_UT_OBJ_CONTROLLER]);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_controller_v_add(&tx,
				      &spiel_obj_fid[SPIEL_UT_OBJ_CONTROLLER_V],
				      &fake_fid,
				      &spiel_obj_fid[SPIEL_UT_OBJ_CONTROLLER]);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_controller_v_add(&tx,
				      &spiel_obj_fid[SPIEL_UT_OBJ_CONTROLLER_V],
				      &spiel_obj_fid[SPIEL_UT_OBJ_ENCLOSURE_V],
				      &spiel_obj_fid[SPIEL_UT_OBJ_CONTROLLER]);
	M0_UT_ASSERT(rc == 0);

	/* Disk version */
	rc = m0_spiel_disk_v_add(&tx,
				 &spiel_obj_fid[SPIEL_UT_OBJ_DISK_V],
				 &spiel_obj_fid[SPIEL_UT_OBJ_CONTROLLER_V],
				 &fake_fid);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_disk_v_add(&tx,
				 &fake_fid,
				 &spiel_obj_fid[SPIEL_UT_OBJ_CONTROLLER_V],
				 &spiel_obj_fid[SPIEL_UT_OBJ_DISK]);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_disk_v_add(&tx,
				 &spiel_obj_fid[SPIEL_UT_OBJ_DISK_V],
				 &fake_fid,
				 &spiel_obj_fid[SPIEL_UT_OBJ_DISK]);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_disk_v_add(&tx,
				 &spiel_obj_fid[SPIEL_UT_OBJ_DISK_V],
				 &spiel_obj_fid[SPIEL_UT_OBJ_CONTROLLER_V],
				 &spiel_obj_fid[SPIEL_UT_OBJ_DISK]);
	M0_UT_ASSERT(rc == 0);

	/* Process */
	rc = m0_spiel_process_add(&tx,
				  &fake_fid,
				  &spiel_obj_fid[SPIEL_UT_OBJ_NODE],
				  &bitmap, 4000, 1, 2, 3);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_process_add(&tx,
				  &spiel_obj_fid[SPIEL_UT_OBJ_PROCESS],
				  &fake_fid,
				  &bitmap, 4000, 1, 2, 3);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_process_add(&tx,
				  &spiel_obj_fid[SPIEL_UT_OBJ_PROCESS],
				  &spiel_obj_fid[SPIEL_UT_OBJ_NODE],
				  &bitmap, 4000, 1, 2, 3);
	M0_UT_ASSERT(rc == 0);

	/* Service */
	service_info.svi_endpoints = ep;
	service_info.svi_type = M0_CST_MGS;
	service_info.svi_u.confdb_path = ep[0];
	rc = m0_spiel_service_add(&tx,
				  &fake_fid,
				  &spiel_obj_fid[SPIEL_UT_OBJ_PROCESS],
				  &service_info);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_service_add(&tx,
				  &spiel_obj_fid[SPIEL_UT_OBJ_SERVICE],
				  &fake_fid,
				  &service_info);
	M0_UT_ASSERT(rc == -EINVAL);

	/* Check copy endpoints parameter */
	m0_fi_enable_once("m0_alloc", "fail_allocation");
	rc = m0_spiel_service_add(&tx,
				  &spiel_obj_fid[SPIEL_UT_OBJ_SERVICE],
				  &spiel_obj_fid[SPIEL_UT_OBJ_PROCESS],
				  &service_info);
	M0_UT_ASSERT(rc == -ENOMEM);

	/* Check copy cs_u parameter & switch by type */
	service_info.svi_type = M0_CST_MDS;
	service_info.svi_u.repair_limits = 5;
	rc = m0_spiel_service_add(&tx,
				  &spiel_obj_fid[SPIEL_UT_OBJ_SERVICE],
				  &spiel_obj_fid[SPIEL_UT_OBJ_PROCESS],
				  &service_info);
	M0_UT_ASSERT(rc == 0);

	service_info.svi_type = M0_CST_IOS;
	service_info.svi_u.repair_limits = 10;
	spiel_obj_fid[SPIEL_UT_OBJ_SERVICE].f_container += 0x0100;
	spiel_obj_fid[SPIEL_UT_OBJ_SERVICE].f_key += 0x0100;
	rc = m0_spiel_service_add(&tx,
				  &spiel_obj_fid[SPIEL_UT_OBJ_SERVICE],
				  &spiel_obj_fid[SPIEL_UT_OBJ_PROCESS],
				  &service_info);
	M0_UT_ASSERT(rc == 0);

	service_info.svi_type = M0_CST_MGS;
	service_info.svi_u.confdb_path = ep[0];
	spiel_obj_fid[SPIEL_UT_OBJ_SERVICE].f_container += 0x0100;
	spiel_obj_fid[SPIEL_UT_OBJ_SERVICE].f_key += 0x0100;
	rc = m0_spiel_service_add(&tx,
				  &spiel_obj_fid[SPIEL_UT_OBJ_SERVICE],
				  &spiel_obj_fid[SPIEL_UT_OBJ_PROCESS],
				  &service_info);
	M0_UT_ASSERT(rc == 0);

	service_info.svi_type = M0_CST_RMS;
	service_info.svi_u.confdb_path = ep[0];
	spiel_obj_fid[SPIEL_UT_OBJ_SERVICE].f_container += 0x0100;
	spiel_obj_fid[SPIEL_UT_OBJ_SERVICE].f_key += 0x0100;
	rc = m0_spiel_service_add(&tx,
				  &spiel_obj_fid[SPIEL_UT_OBJ_SERVICE],
				  &spiel_obj_fid[SPIEL_UT_OBJ_PROCESS],
				  &service_info);
	M0_UT_ASSERT(rc == 0);

	service_info.svi_type = M0_CST_SSS;
	service_info.svi_u.addb_stobid = fake_fid;
	spiel_obj_fid[SPIEL_UT_OBJ_SERVICE].f_container += 0x0100;
	spiel_obj_fid[SPIEL_UT_OBJ_SERVICE].f_key += 0x0100;
	rc = m0_spiel_service_add(&tx,
				  &spiel_obj_fid[SPIEL_UT_OBJ_SERVICE],
				  &spiel_obj_fid[SPIEL_UT_OBJ_PROCESS],
				  &service_info);
	M0_UT_ASSERT(rc == 0);

	service_info.svi_type = M0_CST_HA;
	service_info.svi_u.addb_stobid = fake_fid;
	spiel_obj_fid[SPIEL_UT_OBJ_SERVICE].f_container += 0x0100;
	spiel_obj_fid[SPIEL_UT_OBJ_SERVICE].f_key += 0x0100;
	rc = m0_spiel_service_add(&tx,
				  &spiel_obj_fid[SPIEL_UT_OBJ_SERVICE],
				  &spiel_obj_fid[SPIEL_UT_OBJ_PROCESS],
				  &service_info);
	M0_UT_ASSERT(rc == 0);

	service_info.svi_type = M0_CST_NR;
	service_info.svi_u.addb_stobid = fake_fid;
	spiel_obj_fid[SPIEL_UT_OBJ_SERVICE].f_container += 0x0100;
	spiel_obj_fid[SPIEL_UT_OBJ_SERVICE].f_key += 0x0100;
	rc = m0_spiel_service_add(&tx,
				  &spiel_obj_fid[SPIEL_UT_OBJ_SERVICE],
				  &spiel_obj_fid[SPIEL_UT_OBJ_PROCESS],
				  &service_info);
	M0_UT_ASSERT(rc == -EINVAL);

	/* Normal */
	service_info.svi_type = M0_CST_MGS;
	service_info.svi_u.confdb_path = ep[0];
	spiel_obj_fid[SPIEL_UT_OBJ_SERVICE].f_container += 0x0100;
	spiel_obj_fid[SPIEL_UT_OBJ_SERVICE].f_key += 0x0100;
	rc = m0_spiel_service_add(&tx,
				  &spiel_obj_fid[SPIEL_UT_OBJ_SERVICE],
				  &spiel_obj_fid[SPIEL_UT_OBJ_PROCESS],
				  &service_info);
	M0_UT_ASSERT(rc == 0);

	/* Device */
	rc = m0_spiel_device_add(&tx,
				 &fake_fid,
				 &spiel_obj_fid[SPIEL_UT_OBJ_SERVICE],
				 &spiel_obj_fid[SPIEL_UT_OBJ_DISK],
				 M0_CFG_DEVICE_INTERFACE_SCSI,
				 M0_CFG_DEVICE_MEDIA_SSD,
				 1024, 512, 123, 0x55, "fake_filename");
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_device_add(&tx,
				 &spiel_obj_fid[SPIEL_UT_OBJ_SDEV],
				 &fake_fid,
				 &fake_fid,
				 M0_CFG_DEVICE_INTERFACE_SCSI,
				 M0_CFG_DEVICE_MEDIA_SSD,
				 1024, 512, 123, 0x55, "fake_filename");
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_device_add(&tx,
				 &spiel_obj_fid[SPIEL_UT_OBJ_SDEV],
				 &spiel_obj_fid[SPIEL_UT_OBJ_SERVICE],
				 &spiel_obj_fid[SPIEL_UT_OBJ_DISK],
				 M0_CFG_DEVICE_INTERFACE_SCSI,
				 M0_CFG_DEVICE_MEDIA_SSD,
				 1024, 512, 123, 0x55, "fake_filename");
	M0_UT_ASSERT(rc == 0);

	m0_spiel_tx_close(&tx);

	m0_bitmap_fini(&bitmap);

	spiel_conf_ut_fini();
}

/*
 * spiel-conf-delete test
 */
static void spiel_conf_delete(void)
{
	struct m0_spiel_tx tx;
	int                rc;

	spiel_conf_ut_init();

	spiel_conf_create_configuration(&spiel, &tx);
	rc = m0_spiel_element_del(&tx, &spiel_obj_fid[SPIEL_UT_OBJ_CONTROLLER]);
	M0_UT_ASSERT(rc == 0);
	m0_spiel_tx_close(&tx);

	spiel_conf_ut_fini();
}


/*
  spiel-conf-file create tree

  Create conf file equvalent  conf/ut/conf-str.txt

[20:
# profile:      ('p', 1,  0)
   {0x70| (((0x7000000000000001, 0)), (0x6600000000000001, 1))},
# filesystem:   ('f', 1,  1)
   {0x66| (((0x6600000000000001, 1)),
	   (11, 22), 41212, [3: "param-0", "param-1", "param-2"],
	   (0x6f00000000000001, 4),
	   [1: (0x6e00000000000001, 2)],
	   [1: (0x6f00000000000001, 4)],
	   [1: (0x6100000000000001, 3)])},
# node:         ('n', 1,  2)
   {0x6e| (((0x6e00000000000001, 2)), 16000, 2, 3, 2, (0x6f00000000000001, 4),
	   [2: (0x7200000000000001, 5), (0x7200000000000001, 6)])},
# process "p0": ('r', 1,  5)
   {0x72| (((0x7200000000000001, 5)), [1:3], 0, 0, 0, 0, [2: (0x7300000000000001, 9),
						   (0x7300000000000001, 10)])},
# process "p1": ('r', 1,  6)
   {0x72| (((0x7200000000000001, 6)), [1:3], 0, 0, 0, 0, [0])},
# service "s0": ('s', 1,  9)
   {0x73| (((0x7300000000000001, 9)), 3, [3: "addr-0", "addr-1", "addr-2"],
	   [2: (0x6400000000000001, 13), (0x6400000000000001, 14)])},
# service "s1": ('s', 1, 10)
   {0x73| (((0x7300000000000001, 10)), 1, [1: "addr-3"],
	   [1: (0x6400000000000001, 15)])},
# sdev "d0":    ('d', 1, 13)
   {0x64| (((0x6400000000000001, 13)), 4, 1, 4096, 596000000000, 3, 4, "/dev/sdev0")},
# sdev "d1":    ('d', 1, 14)
   {0x64| (((0x6400000000000001, 14)), 4, 1, 4096, 596000000000, 3, 4, "/dev/sdev1")},
# sdev "d2":    ('d', 1, 15)
   {0x64| (((0x6400000000000001, 15)), 7, 2, 8192, 320000000000, 2, 4, "/dev/sdev2")},
# rack:         ('a', 1,  3)
   {0x61| (((0x6100000000000001, 3)),
	   [1: (0x6500000000000001, 7)], [1: (0x7600000000000001, 8)])},
# enclosure:    ('e', 1,  7)
   {0x65| (((0x6500000000000001, 7)),
	   [1: (0x6300000000000001, 11)], [1: (0x7600000000000001, 8)])},
# controller:   ('c', 1, 11) --> node
   {0x63| (((0x6300000000000001, 11)), (0x6e00000000000001, 2),
	   [1: (0x6b00000000000001, 16)], [1: (0x7600000000000001, 8)])},
# disk:         ('k', 1, 16) --> sdev "d2"
   {0x6b| (((0x6b00000000000001, 16)), (0x6400000000000001, 15))},
# pool:         ('o', 1,  4)
   {0x6f| (((0x6f00000000000001, 4)), 0, [1: (0x7600000000000001, 8)])},
# pver:         ('v', 1,  8)
   {0x76| (((0x7600000000000001, 8)), 0, 8, 2, [3: 1,2,4], [0],
	   [1: (0x6a00000000000001, 12)])},
# rack-v:       ('j', 1, 12) --> rack
   {0x6a| (((0x6a00000000000001, 12)), (0x6100000000000001, 3),
	   [1: (0x6a00000000000001, 17)])},
# enclosure-v:  ('j', 1, 17) --> enclosure
   {0x6a| (((0x6a00000000000001, 17)), (0x6500000000000001, 7),
	   [1: (0x6a00000000000001, 18)])},
# controller-v: ('j', 1, 18) --> controller
   {0x6a| (((0x6a00000000000001, 18)), (0x6300000000000001, 11),
	   [1: (0x6a00000000000001, 19)])},
# disk-v:       ('j', 1, 19) --> disk
   {0x6a| (((0x6a00000000000001, 19)), (0x6b00000000000001, 16), [0])}]

# digraph conf {
#     profile [label="profile\n('p', 0)"];
#     filesystem [label="filesystem\n('f', 1)"];
#     "node" [label="node\n('n', 2)"];
#     rack [label="rack\n('a', 3)"];
#     pool [label="pool\n('o', 4)"];
#     process_5 [label="process\n('r', 5)"];
#     process_6 [label="process\n('r', 6)"];
#     encl [label="encl\n('e', 7)"];
#     pver [label="pver\n('v', 8)"];
#     service_9 [label="service\n('s', 9)"];
#     service_10 [label="service\n('s', 10)"];
#     ctrl [label="ctrl\n('c', 11)"];
#     rack_v [label="rack-v\n('j', 12)"];
#     sdev_13 [label="sdev\n('d', 13)"];
#     sdev_14 [label="sdev\n('d', 14)"];
#     sdev_15 [label="sdev\n('d', 15)"];
#     disk [label="disk\n('k', 16)"];
#     encl_v [label="encl-v\n('j', 17)"];
#     ctrl_v [label="ctrl-v\n('j', 18)"];
#     disk_v [label="disk-v\n('j', 19)"];
#
#     profile -> filesystem;
#     filesystem -> "node";
#     filesystem -> rack -> encl -> ctrl -> disk;
#     filesystem -> pool -> pver -> rack_v -> encl_v -> ctrl_v -> disk_v;
#     "node" -> process_5 -> service_9 -> sdev_13;
#     service_9 -> sdev_14;
#     "node" -> process_6 -> service_10 -> sdev_15;
#
#     "node" -> ctrl [dir=back, style=dashed, weight=0];
#     sdev_15 -> disk [dir=back, style=dashed, weight=0];
#     rack -> rack_v [dir=back, style=dashed, weight=0];
#     encl -> encl_v [dir=back, style=dashed, weight=0];
#     ctrl -> ctrl_v [dir=back, style=dashed, weight=0];
#     disk -> disk_v [dir=back, style=dashed, weight=0];
# }

  */

static void spiel_conf_file_create_tree(struct m0_spiel_tx *tx)
{
	int                           rc;
	const char                   *ep[] = { SERVER_ENDPOINT_ADDR,
					       NULL };
	const char                   *ep_service1[] = { "addr-0",
						        "addr-1",
						        "addr-2",
						        NULL };
	const char                   *ep_service2[] = { "addr-3", NULL };
	struct m0_pdclust_attr        pdclust_attr = { 0, 0, 0, 0 };
	const char                   *fs_param[] = { "param-0",
						     "param-1",
						     "param-2",
						     NULL };
	struct m0_spiel_service_info  service_info1 = {
		.svi_endpoints = ep_service1 };
	struct m0_spiel_service_info  service_info2 = {
		.svi_endpoints = ep_service2 };
	struct m0_fid                 fid_profile   = M0_FID_TINIT( 0x70,1, 0 );
	struct m0_fid                 fid_filesystem= M0_FID_TINIT( 0x66,1, 1 );
	struct m0_fid                 fid_node      = M0_FID_TINIT( 0x6e,1, 2 );
	struct m0_fid                 fid_rack      = M0_FID_TINIT( 0x61,1, 3 );
	struct m0_fid                 fid_pool      = M0_FID_TINIT( 0x6f,1, 4 );
	struct m0_fid                 fid_process1  = M0_FID_TINIT( 0x72,1, 5 );
	struct m0_fid                 fid_process2  = M0_FID_TINIT( 0x72,1, 6 );
	struct m0_fid                 fid_enclosure = M0_FID_TINIT( 0x65,1, 7 );
	struct m0_fid                 fid_pver      = M0_FID_TINIT( 0x76,1, 8 );
	struct m0_fid                 fid_service1  = M0_FID_TINIT( 0x73,1, 9 );
	struct m0_fid                 fid_service2  = M0_FID_TINIT( 0x73,1,10 );
	struct m0_fid                 fid_controller= M0_FID_TINIT( 0x63,1,11 );
	struct m0_fid                 fid_disk1     = M0_FID_TINIT( 0x6b,1,12 );
	struct m0_fid                 fid_disk2     = M0_FID_TINIT( 0x6b,1,13 );
	struct m0_fid                 fid_disk3     = M0_FID_TINIT( 0x6b,1,14 );
	struct m0_fid                 fid_rack_v    = M0_FID_TINIT( 0x6a,1,15 );
	struct m0_fid                 fid_sdev1     = M0_FID_TINIT( 0x64,1,16 );
	struct m0_fid                 fid_sdev2     = M0_FID_TINIT( 0x64,1,17 );
	struct m0_fid                 fid_sdev3     = M0_FID_TINIT( 0x64,1,18 );
	struct m0_fid                 fid_enclosure_v=M0_FID_TINIT( 0x6a,1,19 );
	struct m0_fid                 fid_controller_v=M0_FID_TINIT(0x6a,1,20 );
	struct m0_fid                 fid_disk_v1     =M0_FID_TINIT(0x6a,1,21 );
	struct m0_fid                 fid_disk_v2     =M0_FID_TINIT(0x6a,1,22 );
	struct m0_fid                 fid_disk_v3     =M0_FID_TINIT(0x6a,1,23 );
	struct m0_bitmap              bitmap;

	m0_bitmap_init(&bitmap, 32);
	m0_bitmap_set(&bitmap, 0, true);
	m0_bitmap_set(&bitmap, 1, true);


	rc = m0_spiel_profile_add(tx, &fid_profile);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_filesystem_add(tx, &fid_filesystem, &fid_profile, 41212,
				     &fid_profile, &fid_pool, fs_param);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_pool_add(tx, &fid_pool, &fid_filesystem, 0);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_rack_add(tx, &fid_rack, &fid_filesystem);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_enclosure_add(tx, &fid_enclosure, &fid_rack);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_node_add(tx, &fid_node, &fid_filesystem,
			       16000, 2, 3, 2, &fid_pool);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_controller_add(tx, &fid_controller, &fid_enclosure,
				     &fid_node);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_disk_add(tx, &fid_disk1, &fid_controller);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_disk_add(tx, &fid_disk2, &fid_controller);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_disk_add(tx, &fid_disk3, &fid_controller);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_pool_version_add(tx, &fid_pver, &fid_pool, &pdclust_attr);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_rack_v_add(tx, &fid_rack_v, &fid_pver, &fid_rack);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_enclosure_v_add(tx, &fid_enclosure_v, &fid_rack_v,
				      &fid_enclosure);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_controller_v_add(tx, &fid_controller_v, &fid_enclosure_v,
				       &fid_controller);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_disk_v_add(tx, &fid_disk_v1, &fid_controller_v,
				 &fid_disk1);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_disk_v_add(tx, &fid_disk_v2, &fid_controller_v,
				 &fid_disk2);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_disk_v_add(tx, &fid_disk_v3, &fid_controller_v,
				 &fid_disk3);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_process_add(tx, &fid_process1, &fid_node,
				  &bitmap, 4000, 1, 2, 3);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_process_add(tx, &fid_process2, &fid_node,
				  &bitmap, 4000, 1, 2, 3);
	M0_UT_ASSERT(rc == 0);

	service_info1.svi_type = 3;
	service_info1.svi_u.confdb_path = ep[0];
	rc = m0_spiel_service_add(tx, &fid_service1, &fid_process1,
				  &service_info1);
	M0_UT_ASSERT(rc == 0);

	service_info2.svi_type = 1;
	service_info2.svi_u.repair_limits = 10;
	rc = m0_spiel_service_add(tx, &fid_service2, &fid_process2,
				  &service_info2);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_device_add(tx, &fid_sdev1, &fid_service1, &fid_disk1,
				 4, 1, 4096, 596000000000, 3, 4, "/dev/sdev0");
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_device_add(tx, &fid_sdev2, &fid_service1, &fid_disk2,
				 4, 1, 4096, 596000000000, 3, 4, "/dev/sdev1");
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_device_add(tx, &fid_sdev3, &fid_service2, &fid_disk3,
				 7, 2, 8192, 320000000000, 2, 4, "/dev/sdev2");
	M0_UT_ASSERT(rc == 0);

	m0_bitmap_fini(&bitmap);
}

static int spiel_str_to_file(char *str, const char *filename)
{
	int   rc;
	FILE *file;
	file = fopen(filename, "w+");
	if (file == NULL)
		return errno;
	rc = fwrite(str, strlen(str), 1, file)==1 ? 0 : -EINVAL;
	fclose(file);
	return rc;
}

static int spiel_file_read(const char *filename, char *dest, size_t sz)
{
	FILE  *f;
	size_t n;
	int    rc = 0;

	M0_ENTRY();

	f = fopen(filename, "r");
	if (f == NULL)
		return M0_RC(-errno);

	n = fread(dest, 1, sz - 1, f);
	if (ferror(f))
		rc = -errno;
	else if (!feof(f))
		rc = -EFBIG;
	else
		dest[n] = '\0';

	fclose(f);
	return M0_RC(rc);
}

/*
 * spiel-conf-file test
 */
static void spiel_conf_file(void)
{
	int                   rc;
	struct m0_spiel_tx    tx;
	struct m0_confx      *confx;
	char                 *str;
	const char            filename[] = "/tmp/spiel_conf_file.txt";
	struct m0_conf_cache  cache;
	struct m0_mutex       lock;

	spiel_conf_ut_init();

	m0_spiel_tx_open(&spiel, &tx);
	M0_UT_ASSERT(tx.spt_version != M0_CONF_VER_UNKNOWN);
	spiel_conf_file_create_tree(&tx);

	/* Convert to file */
	M0_ALLOC_PTR(confx);

	m0_mutex_lock(&tx.spt_lock);
	rc = m0_conf_cache_encode(&tx.spt_cache, confx);
	m0_mutex_unlock(&tx.spt_lock);
	M0_UT_ASSERT(rc == 0);

	m0_confx_to_string(confx, &str);

	rc = spiel_str_to_file(str, filename);
	M0_UT_ASSERT(rc == 0);

	m0_confx_free(confx);
	m0_free_aligned(str, strlen(str) + 1, PAGE_SHIFT);
	m0_spiel_tx_close(&tx);

	/* Load file */
	M0_ALLOC_ARR(str, M0_CONF_STR_MAXLEN);
	rc = spiel_file_read(filename, str, M0_CONF_STR_MAXLEN);
	M0_UT_ASSERT(rc == 0);

	m0_mutex_init(&lock);
	m0_conf_cache_init(&cache, &lock);

	m0_mutex_lock(&lock);

	rc = m0_conf_cache_from_string(&cache, str);
	m0_free(str);
	M0_UT_ASSERT(rc == 0);
	m0_mutex_unlock(&lock);

	m0_conf_cache_fini(&cache);
	m0_mutex_fini(&lock);
	spiel_conf_ut_fini();
}

/**
 * spiel-conf-cancel
 *
 * send Load command to confd
 */
static void spiel_conf_cancel(void)
{
	struct m0_spiel_tx  tx;

	spiel_conf_ut_init();
	spiel_conf_create_configuration(&spiel, &tx);
	m0_spiel_tx_close(&tx);
	spiel_conf_ut_fini();
}

/**
 * spiel-conf-load-send
 *
 * send Load command to confd
 */
static void spiel_conf_load_send(void)
{
	struct m0_spiel_tx tx;
	int                rc;

	spiel_conf_ut_init();
	spiel_conf_create_configuration(&spiel, &tx);
	rc = m0_spiel_tx_commit(&tx);
	M0_UT_ASSERT(rc == 0);
	m0_spiel_tx_close(&tx);
	spiel_conf_ut_fini();
}

static void spiel_conf_big_db(void)
{
	struct m0_spiel_tx  tx;
	int                 rc;
	int                 i;
	int                 svc_nr;
	struct m0_fid       fid = spiel_obj_fid[SPIEL_UT_OBJ_SERVICE];
	const char         *svc_ep[] = {"192.168.252.132@tcp:12345:41:201",
					NULL};
	struct m0_spiel_service_info svc_info = {
			.svi_type = M0_CST_IOS,
			.svi_endpoints = svc_ep
			};
	m0_bcount_t         seg_size;
	char               *cache_str;
	uint32_t            svc_str_size = sizeof(
			"{0x73|(((0x7300000000000001,0)),0x1,"
			"[0x1:[0x20:0x31,0x39,0x32,0x2e,0x31,0x36,0x38,0x2e,"
			"0x32,0x35,0x32,0x2e,0x31,0x33,0x32,0x40,0x74,0x63,"
			"0x70,0x3a,0x31,0x32,0x33,0x34,0x35,0x3a,0x34,0x31,"
			"0x3a,0x32,0x30,0x31]],[0])},") - 1;

	spiel_conf_ut_init();
	spiel_conf_create_configuration(&spiel, &tx);
	/**
	 * Add sufficient number of services, so resulting conf db string
	 * after encoding doesn't fit into one RPC bulk segment.
	 */
	m0_fi_enable("m0_conf_segment_size", "const_size");
	seg_size = m0_conf_segment_size(NULL);
	svc_nr = seg_size/svc_str_size + 1;
	for (i = 0; i < svc_nr; i++) {
		fid.f_key++;
		rc = m0_spiel_service_add(&tx, &fid,
					  &spiel_obj_fid[SPIEL_UT_OBJ_PROCESS],
					  &svc_info);
		M0_UT_ASSERT(rc == 0);
	}
	rc = m0_conf_cache_to_string(&tx.spt_cache, &cache_str);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(strlen(cache_str) > seg_size);
	rc = m0_spiel_tx_commit(&tx);
	M0_UT_ASSERT(rc == 0);
	m0_fi_disable("m0_conf_segment_size", "const_size");
	m0_free_aligned(cache_str, strlen(cache_str)+1, PAGE_SHIFT);
	m0_spiel_tx_close(&tx);
	spiel_conf_ut_fini();
}

static void spiel_conf_flip_fail(void)
{
	struct m0_spiel_tx  tx;
	int                 rc;

	spiel_conf_ut_init();
	spiel_conf_create_configuration(&spiel, &tx);
	m0_fi_enable_once("conf_flip_confd_config_save", "fcreate_failed");
	rc = m0_spiel_tx_commit(&tx);
	M0_UT_ASSERT(rc == -ENOENT);
	m0_spiel_tx_close(&tx);
	spiel_conf_ut_fini();
}

/**
 * spiel-conf-check-fail
 *
 * send Load command to confd
 */
static void spiel_conf_check_fail(void)
{
	struct m0_spiel_tx    tx;
	int                   rc;
	struct m0_conf_obj   *obj = NULL;
	struct m0_conf_obj   *obj_parent;
	struct m0_conf_cache *cache = &tx.spt_cache;
	struct m0_fid         fake_fid = M0_FID_TINIT('n', 6, 600 );

	spiel_conf_ut_init();
	spiel_conf_create_configuration(&spiel, &tx);

	m0_tl_for(m0_conf_cache, &cache->ca_registry, obj) {
		if (m0_conf_obj_type(obj) == &M0_CONF_PROCESS_TYPE)
			break;
	} m0_tl_endfor;

	/* spiel_check_cache(tx) test */
	obj->co_status = M0_CS_MISSING;
	rc = m0_spiel_tx_commit(&tx);
	obj->co_status = M0_CS_READY;
	M0_UT_ASSERT(rc == -EBUSY);

	obj_parent = obj->co_parent;

	obj->co_parent = NULL;
	rc = m0_spiel_tx_commit(&tx);
	M0_UT_ASSERT(rc == -ENOENT);

	obj->co_parent = m0_conf_obj_create(cache, &fake_fid);
	rc = m0_spiel_tx_commit(&tx);
	M0_UT_ASSERT(rc == -ENOENT);

	obj->co_parent->co_ops->coo_delete(obj->co_parent);
	obj->co_parent = obj_parent;

	/* M0_ALLOC_PTR(confx) test */
	m0_fi_enable_once("m0_alloc", "fail_allocation");
	rc = m0_spiel_tx_commit(&tx);
	M0_UT_ASSERT(rc == -ENOMEM);

	/* m0_conf_cache_encode test */
	m0_fi_enable_off_n_on_m("m0_alloc", "fail_allocation", 1, 1);
	rc = m0_spiel_tx_commit(&tx);
	m0_fi_disable("m0_alloc", "fail_allocation");
	M0_UT_ASSERT(rc == -ENOMEM);

	/* sessions & small_allign == OK */
	m0_fi_enable_off_n_on_m("m0_alloc", "fail_allocation", 21, 1);
	rc = m0_spiel_tx_commit(&tx);
	m0_fi_disable("m0_alloc", "fail_allocation");
	M0_UT_ASSERT(rc == -ENOMEM);

	m0_spiel_tx_close(&tx);
	spiel_conf_ut_fini();
}

/**
 * spiel-conf-load-fail
 *
 * send Load command to confd
 */
static void spiel_conf_load_fail(void)
{
	struct m0_spiel_tx  tx;
	int                 rc;

	spiel_conf_ut_init();
	spiel_conf_create_configuration(&spiel, &tx);
	/* M0_ALLOC_PTR(spiel_cmd) */
	m0_fi_enable_off_n_on_m("m0_alloc", "fail_allocation", 21, 1);
	rc = m0_spiel_tx_commit(&tx);
	m0_fi_disable("m0_alloc", "fail_allocation");
	M0_UT_ASSERT(rc == -ENOMEM);

	m0_spiel_tx_close(&tx);
	spiel_conf_ut_fini();
}

/**
 * @todo Restore unit test once spiel can start when rconfc quorum isn't reached
 */
#if 0
static void spiel_conf_force_ut_init(struct m0_spiel_ut_reqh *spl_reqh)
{
	int         rc;
	const char *ep = SERVER_ENDPOINT_ADDR;
	const char *client_ep = CLIENT_ENDPOINT_ADDR;

	spiel_copy_file(M0_UT_PATH("conf-str.txt"),
			M0_UT_PATH("conf-str-tmp.txt"));

	rc = m0_spiel__ut_reqh_init(spl_reqh, client_ep);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel__ut_rpc_server_start(&spl_reqh->sur_confd_srv, ep,
					   M0_UT_PATH("conf-str-tmp.txt"));
	M0_UT_ASSERT(rc == 0);
}

static void spiel_conf_force_ut_fini(struct m0_spiel_ut_reqh *spl_reqh)
{
	int rc;

	m0_spiel__ut_rpc_server_stop(&spl_reqh->sur_confd_srv);
	m0_spiel__ut_reqh_fini(spl_reqh);

	rc = system("rm -rf "M0_UT_PATH("confd"));
	M0_ASSERT(rc != -1);
	unlink(M0_UT_PATH("conf-str-tmp.txt"));
}

static void spiel_conf_force(void)
{
	struct m0_spiel_ut_reqh   spl_reqh;
	struct m0_spiel_tx        tx;
	int                       rc;
	const char               *ep[] = { SERVER_ENDPOINT_ADDR,
			                   "127.0.0.1", /* bad address */
			                   NULL };
	const char               *rm_ep = ep[0];
	struct m0_spiel           spiel;
	uint64_t                  ver_org = M0_CONF_VER_UNKNOWN;
	uint64_t                  ver_new = M0_CONF_VER_UNKNOWN;
	uint32_t                  rquorum;
	const char              **eps_orig;

	spiel_conf_force_ut_init(&spl_reqh);
	/* start with quorum impossible due to second ep being invalid */
	rc = m0_spiel_start(&spiel, &spl_reqh.sur_reqh, ep, rm_ep);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(spiel.spl_rconfc.rc_ver == M0_CONF_VER_UNKNOWN);
	ver_org = m0_rconfc_ver_max_read(&spiel.spl_rconfc);
	M0_UT_ASSERT(ver_org != M0_CONF_VER_UNKNOWN);

	/* imitate correct rconfc init to let transaction to fill in */
	spiel.spl_rconfc.rc_ver = ver_org;
	/* fill transaction normal way */
	spiel_conf_create_configuration(&spiel, &tx);
	/* make sure normal commit impossible */
	rc = m0_spiel_tx_commit(&tx);
	M0_UT_ASSERT(rc == -ENOENT); /* no quorum reached */
	/* try forced commit with all invalid addresses */
	eps_orig = spiel.spl_confd_eps;
	spiel.spl_confd_eps = (const char*[]) { "127.0.0.2", "127.0.0.1", NULL};
	rc = m0_spiel_tx_commit_forced(&tx, true, M0_CONF_VER_UNKNOWN,
				       &rquorum);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rquorum < spiel.spl_rconfc.rc_quorum);
	M0_UT_ASSERT(rquorum == 0);
	/* try to repeat the forced commit with the original set */
	spiel.spl_confd_eps = eps_orig;
	rc = m0_spiel_tx_commit_forced(&tx, true, M0_CONF_VER_UNKNOWN,
				       &rquorum);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rquorum < spiel.spl_rconfc.rc_quorum);
	M0_UT_ASSERT(rquorum == 1);
	m0_spiel_stop(&spiel);

	/* make sure new version applied */
	M0_SET0(&spiel);
	rc = m0_spiel_start(&spiel, &spl_reqh.sur_reqh, ep, rm_ep);
	M0_UT_ASSERT(rc == 0);
	ver_new = m0_rconfc_ver_max_read(&spiel.spl_rconfc);
	M0_UT_ASSERT(ver_new == ver_org + 1); /*
					       * default increment done by
					       * m0_spiel_tx_open() proved
					       */

	/* try to repeat the forced commit with a new version number */
	rc = m0_spiel_tx_commit_forced(&tx, true, ver_org + 10, &rquorum);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rquorum < spiel.spl_rconfc.rc_quorum);
	M0_UT_ASSERT(rquorum == 1);
	m0_spiel_stop(&spiel);

	/* dismiss transaction dataset */
	m0_spiel_tx_close(&tx);

	/* make sure new version applied */
	M0_SET0(&spiel);
	rc = m0_spiel_start(&spiel, &spl_reqh.sur_reqh, ep, rm_ep);
	M0_UT_ASSERT(rc == 0);
	ver_new = m0_rconfc_ver_max_read(&spiel.spl_rconfc);
	M0_UT_ASSERT(ver_new == ver_org + 10); /* ver_forced confirmed */
	m0_spiel_stop(&spiel);
	spiel_conf_force_ut_fini(&spl_reqh);
}
#endif

const struct m0_ut_suite spiel_conf_ut = {
	.ts_name = "spiel-conf-ut",
	.ts_tests = {
		{ "spiel-conf-create-ok",   spiel_conf_create_ok   },
		{ "spiel-conf-create-fail", spiel_conf_create_fail },
		{ "spiel-conf-delete",      spiel_conf_delete      },
		{ "spiel-conf-file",        spiel_conf_file        },
		{ "spiel-conf-cancel",      spiel_conf_cancel      },
		{ "spiel-conf-load-send",   spiel_conf_load_send   },
		{ "spiel-conf-big-db",      spiel_conf_big_db      },
		{ "spiel-conf-flip-fail",   spiel_conf_flip_fail   },
		{ "spiel-conf-check-fail",  spiel_conf_check_fail  },
		{ "spiel-conf-load-fail",   spiel_conf_load_fail   },
		/**
		 * @todo Test is disabled because now spiel can't start
		 * successfully if quorum is not reached in rconfc.
		 */
		/*{ "spiel-conf-force",       spiel_conf_force       },*/
		{ NULL, NULL },
	},
};
M0_EXPORTED(spiel_conf_ut);

#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
