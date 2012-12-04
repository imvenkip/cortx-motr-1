/* -*- c -*- */
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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 25-Sep-2012
 */

#include "conf/obj.h"
#include "conf/obj_ops.h" /* m0_conf_dir_tl */
#include "conf/reg.h"
#include "lib/memory.h"
#include "lib/arith.h"
#ifndef __KERNEL__
#  include <stdio.h>      /* printf */
#  include <string.h>     /* memcpy */
#  define DBG(fmt, ...) printf("@@@CONF@@@" fmt, ## __VA_ARGS__)
#  define DBGS DBG
#  define PLU "%lu"
#else
#  include <linux/module.h>
#  include <linux/kernel.h>
#  define DBGS(fmt, ...) printk(KERN_INFO "@@@CONF@@@" fmt, ## __VA_ARGS__)
#  define DBG(fmt, ...) printk("@@@CONF@@@" fmt, ## __VA_ARGS__)
#  define PLU "%llu"
#endif

static void identity_print(struct m0_conf_obj *obj);

static void m0_buf_print(struct m0_buf *b)
{
	char	   *buf;       /* allocated, due to 4K kernel stack */
	m0_bcount_t len = 1024;

	M0_ALLOC_ARR(buf, len);
	M0_ASSERT(buf != NULL);	/* for debug purposes it's allowed */

	memcpy(buf, b->b_addr, min_check(len-1, b->b_nob));
	buf[min_check(len-1, b->b_nob)] = '\0';

	DBG("%s", buf);

	m0_free(buf);
}

static void identity_print_quoted(struct m0_conf_obj *obj)
{
	DBG("\"");
	identity_print(obj);
	DBG("\"");
}

static void dir_print(struct m0_conf_obj *obj)
{
	struct m0_conf_dir *o = M0_CONF_CAST(obj, m0_conf_dir);
	struct m0_conf_obj *child;

	identity_print_quoted(obj);
	DBG(" [label=\"");
	identity_print(obj);
	DBG("\"]");

	m0_tl_for(m0_conf_dir, &o->cd_items, child) {
		DBG("\n");
		identity_print_quoted(obj);
		DBG(" -> ");
		identity_print_quoted(child);
	} m0_tl_endfor;
}

static void profile_print(struct m0_conf_obj *obj)
{
	struct m0_conf_profile *o = M0_CONF_CAST(obj, m0_conf_profile);
	struct m0_conf_obj     *child = &o->cp_filesystem->cf_obj;

	identity_print_quoted(obj);
	DBG(" [label=\"");
	identity_print(obj);
	DBG("\"]\n");
	identity_print_quoted(obj);
	DBG(" -> ");
	identity_print_quoted(child);
}

static void filesystem_print(struct m0_conf_obj *obj)
{
	struct m0_conf_filesystem *o = M0_CONF_CAST(obj, m0_conf_filesystem);
	struct m0_conf_obj        *child = &o->cf_services->cd_obj;

	identity_print_quoted(obj);
	DBG(" [label=\"");
	identity_print(obj); DBG("\\n");
	DBG("fid:[" PLU ", " PLU "]", o->cf_rootfid.f_key,
	    o->cf_rootfid.f_container);
	DBG("\"]\n");
	identity_print_quoted(obj);
	DBG(" -> ");
	identity_print_quoted(child);
}

static void service_print(struct m0_conf_obj *obj)
{
	int                     i;
	struct m0_conf_service *o = M0_CONF_CAST(obj, m0_conf_service);
	struct m0_conf_obj     *child = &o->cs_node->cn_obj;

	identity_print_quoted(obj);
	DBG(" [label=\"");
	identity_print(obj); DBG("\\n");
	DBG("type:%d\\n", o->cs_type);
	for (i = 0; o->cs_endpoints[i] != NULL; ++i)
		DBG("ep:%s\\n", o->cs_endpoints[i]);
	DBG("\"]\n");
	identity_print_quoted(obj);
	DBG(" -> ");
	identity_print_quoted(child);
}

static void node_print(struct m0_conf_obj *obj)
{
	struct m0_conf_node *o = M0_CONF_CAST(obj, m0_conf_node);
	struct m0_conf_obj  *child0 = &o->cn_nics->cd_obj;
	struct m0_conf_obj  *child1 = &o->cn_sdevs->cd_obj;

	identity_print_quoted(obj);
	DBG(" [label=\"");
	identity_print(obj); DBG("\\n");
	DBG("memsize: %u\\n"	, o->cn_memsize);
	DBG("cpu_nr: %u\\n"	, o->cn_nr_cpu);
	DBG("last_state: "PLU"\\n", o->cn_last_state);
	DBG("flags: "PLU"\\n"	, o->cn_flags);
	DBG("pool_id: "PLU"\\n"	, o->cn_pool_id);
	DBG("\"]\n");
	identity_print_quoted(obj);
	DBG(" -> ");
	identity_print_quoted(child0);
	DBG("\n");
	identity_print_quoted(obj);
	DBG(" -> ");
	identity_print_quoted(child1);
}

static void nic_print(struct m0_conf_obj *obj)
{
	struct m0_conf_nic *o = M0_CONF_CAST(obj, m0_conf_nic);

	identity_print_quoted(obj);
	DBG(" [label=\"");
	identity_print(obj); DBG("\\n");
	DBG("iface: %u\\n", o->ni_iface);
	DBG("mtu: %u\\n", o->ni_mtu);
	DBG("speed: "PLU"\\n", o->ni_speed);
	DBG("file: %s\\n", o->ni_filename);
	DBG("last_state: "PLU"\\n", o->ni_last_state);
	DBG("\"]");
}

static void sdev_print(struct m0_conf_obj *obj)
{
	struct m0_conf_sdev *o = M0_CONF_CAST(obj, m0_conf_sdev);
	struct m0_conf_obj *child = &o->sd_partitions->cd_obj;

	identity_print_quoted(obj);
	DBG(" [label=\"");
	identity_print(obj); DBG("\\n");
	DBG("iface: %u\\n"	, o->sd_iface);
	DBG("media: %u\\n"	, o->sd_media);
	DBG("size: "PLU"\\n"	, o->sd_size);
	DBG("last_state: "PLU"\\n", o->sd_last_state);
	DBG("flags: "PLU"\\n"	, o->sd_flags);
	DBG("filename: %s\\n"	, o->sd_filename);
	DBG("\"]");

	DBG("\n");
	identity_print_quoted(obj);
	DBG(" -> ");
	identity_print_quoted(child);
}

static void partition_print(struct m0_conf_obj *obj)
{
	struct m0_conf_partition *o = M0_CONF_CAST(obj, m0_conf_partition);
	identity_print_quoted(obj);
	DBG(" [label=\"");
	identity_print(obj); DBG("\\n");
	DBG("start: "PLU"\\n"	, o->pa_start);
	DBG("size: "PLU"\\n"	, o->pa_size);
	DBG("index: %u\\n"	, o->pa_index);
	DBG("type: %u\\n"	, o->pa_type);
	DBG("filename: %s\\n"	, o->pa_filename);
	DBG("\"]");
}

static const struct {
	const char *name;
	void      (*print)(struct m0_conf_obj *obj);
} objtypes[M0_CO_NR] = {
	[M0_CO_DIR]        = { "dir",        dir_print },
	[M0_CO_PROFILE]    = { "profile",    profile_print },
	[M0_CO_FILESYSTEM] = { "filesystem", filesystem_print },
	[M0_CO_SERVICE]    = { "service",    service_print },
	[M0_CO_NODE]       = { "node",       node_print },
	[M0_CO_NIC]        = { "nic",        nic_print },
	[M0_CO_SDEV]       = { "sdev",       sdev_print },
	[M0_CO_PARTITION]  = { "partition",  partition_print },
};

static void identity_print(struct m0_conf_obj *obj)
{
	M0_ASSERT(IS_IN_ARRAY(obj->co_type, objtypes));
	DBG("%s:", objtypes[obj->co_type].name);
	m0_buf_print(&obj->co_id);
}

/** Prints DAG of configuration objects in dot format. */
M0_INTERNAL void m0_conf__reg2dot(const struct m0_conf_reg *reg)
{
	struct m0_conf_obj *obj;

	DBGS("digraph d {\n");
	DBG("node [shape = box]\n");
	m0_tl_for(m0_conf_reg, &reg->r_objs, obj) {
		M0_ASSERT(IS_IN_ARRAY(obj->co_type, objtypes));
		objtypes[obj->co_type].print(obj);
		DBG("\n\n\n");
	} m0_tl_endfor;
	DBG("}\n");
}
