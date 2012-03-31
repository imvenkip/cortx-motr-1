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
 * Original author: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 02/29/2012
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef ENABLE_FAULT_INJECTION

#include <linux/kernel.h>    /* pr_info */
#include <linux/debugfs.h>   /* debugfs_create_dir */
#include <linux/module.h>    /* THIS_MODULE */
#include <linux/seq_file.h>  /* seq_read */
#include <linux/uaccess.h>   /* strncpy_from_user */
#include <linux/string.h>    /* strncmp */
#include <linux/ctype.h>     /* isprint */

#include "lib/mutex.h"       /* c2_mutex */
#include "lib/time.h"        /* c2_time_now */
#include "lib/finject.h"
#include "lib/finject_internal.h"


/**
 * @addtogroup kc2ctl
 *
 * @{
 *
 * Fault injection control interface.
 *
 * @li colibri/finject_stat   Provides information about all registered fault
 *                            points.
 *
 * @li colibri/finject_ctl    Allows to change state of existing fault points
 *                            (enable/disable).
 *
 * finject_ctl accepts commands in the following format:
 *
 * @verbatim
 *
 *     COMMAND = ACTION function_name fp_tag [ ACTION_ARGUMENTS ]
 *
 *     ACTION = enable | disable
 *
 *     ACTION_ARGUMENTS = FP_TYPE [ FP_DATA ]
 *
 *     FP_TYPE = always | oneshot | random | off_n_on_m
 *
 *     FP_DATA = integer { integer }
 *
 * @endverbatim
 *
 * Here some examples:
 *
 * @verbatim
 *
 *     enable c2_alloc fake_failure oneshot
 *     enable c2_alloc fake_failure random 30
 *     enable c2_rpc_conn_start fake_success always
 *     enable c2_net_buffer_del need_fail off_n_on_m 2 5
 *
 *     disable c2_alloc fake_failure
 *     disable c2_net_buffer_del need_fail
 *
 * @endverbatim
 *
 * The easiest way to send a command is to use `echo`:
 *
 * @verbatim
 *
 *     $ echo 'enable c2_init need_fail always' > /sys/kernel/debug/colibri/finject_ctl
 *
 * @endverbatim
 */

extern struct dentry  *dfs_root_dir;
extern const char     dfs_root_name[];

static struct dentry  *fi_stat_file;
static struct dentry  *fi_ctl_file;
static bool           fi_ctl_is_opened = false;


static void *fi_stat_start(struct seq_file *seq, loff_t *pos)
{
	u32 *idx;

	if (*pos == 0) {
		idx = SEQ_START_TOKEN;
		return SEQ_START_TOKEN;
	} else if (*pos >= c2_fi_states_get_free_idx()) {
		/* indicate beyond end of file position */
		idx = NULL;
		return NULL;
	}

	idx = kmalloc(sizeof(u32), GFP_KERNEL);

	if (idx == NULL) {
		pr_err("Failed to alloc seq_file iterator\n");
		return NULL;
	}

	*idx = *pos;

	return idx;
}

static void *fi_stat_next(struct seq_file *seq, void *v, loff_t *pos)
{
	u32 *idx = 0;

	++(*pos);

	if (v == SEQ_START_TOKEN) {
		idx = kmalloc(sizeof(u32), GFP_KERNEL);
		if (idx == NULL) {
			pr_err("Failed to alloc seq_file iterator\n");
			return NULL;
		}
	} else {
		idx = v;
	}

	*idx = *pos - 1;

	if (*idx >= c2_fi_states_get_free_idx()) {
		/* indicate end of sequence */
		idx = 0;
	}

	return idx;
}

static void fi_stat_stop(struct seq_file *seq, void *v)
{
	kfree(v);
}

/**
 * Extracts a "colibri core" file name from a full-path file name.
 *
 * For example, given the following full-path file name:
 *
 *     /data/colibri/core/build_kernel_modules/lib/ut/finject.c
 *
 * The "colibri core" file name is:
 *
 *     build_kernel_modules/lib/ut/finject.c
 */
static inline const char *core_file_name(const char *fname)
{
	static const char core[] = "core/";
	const char *cfn;

	cfn = strstr(fname, core);
	if (cfn == NULL)
		return NULL;

	return cfn + strlen(core);
}

static const char *fi_type_names[C2_FI_TYPES_NR] = {
	[C2_FI_ALWAYS]     = "always",
	[C2_FI_ONESHOT]    = "oneshot",
	[C2_FI_RANDOM]     = "random",
	[C2_FI_OFF_N_ON_M] = "off_n_on_m",
	[C2_FI_FUNC]       = "user_func",
};

static inline const char *fi_type_name(enum c2_fi_fpoint_type type)
{
	C2_PRE(IS_IN_ARRAY(type, fi_type_names));
	return fi_type_names[type];
}

static int fi_stat_show(struct seq_file *seq, void *v)
{
	const struct c2_fi_fault_point  *fp;
	const struct c2_fi_fpoint_state *state;

	u32         *idx;
	char        enb               = 'n';
	uint32_t    total_hit_cnt     = 0;
	uint32_t    total_trigger_cnt = 0;
	uint32_t    hit_cnt           = 0;
	uint32_t    trigger_cnt       = 0;
	const char  *type             = "";
	char        data[64]          = { 0 };
	const char  *module           = "";
	const char  *file             = "";
	const char  *func;
	const char  *tag;
	uint32_t    line_num          = 0;

	/* print header */
	if (SEQ_START_TOKEN == v) {
		/* keep these long strings on a single line for easier editing */
		seq_puts(seq, " Idx | Enb |TotHits|TotTrig|Hits|Trig|   Type   |   Data   | Module |              File name                 | Line |             Func name             |   Tag\n");
		seq_puts(seq, "-----+-----+-------+-------+----+----+----------+----------+--------+----------------------------------------+------+-----------------------------------+----------\n");
		return 0;
	}

	idx = v;
	state = &c2_fi_states_get()[*idx];

	/* skip disabled states */
	/* TODO: add an option to control this in runtime through debugfs */
	/*if (!fi_state_enabled(state))
		return SEQ_SKIP;*/

	func = state->fps_id.fpi_func;
	tag = state->fps_id.fpi_tag;
	total_hit_cnt = state->fps_total_hit_cnt;
	total_trigger_cnt = state->fps_total_trigger_cnt;
	fp = state->fps_fp;

	/*
	 * fp can be NULL if fault point was enabled but had not been registered
	 * yet
	 */
	if (fp != NULL) {
		module = fp->fp_module;
		file = core_file_name(fp->fp_file);
		line_num = fp->fp_line_num;
	}

	if (fi_state_enabled(state)) {
		enb = 'y';
		type = fi_type_name(state->fps_data.fpd_type);
		switch (state->fps_data.fpd_type) {
		case C2_FI_OFF_N_ON_M:
			snprintf(data, sizeof data, "n=%u,m=%u",
					state->fps_data.u.s1.fpd_n,
					state->fps_data.u.s1.fpd_m);
			break;
		case C2_FI_RANDOM:
			snprintf(data, sizeof data, "p=%u",
					state->fps_data.u.fpd_p);
			break;
		default:
			break; /* leave data string empty */
		}
		hit_cnt = state->fps_data.fpd_hit_cnt;
		trigger_cnt = state->fps_data.fpd_trigger_cnt;
	}

	seq_printf(seq, " %-3u    %c   %-7u %-7u %-4u %-4u %-10s %-10s %-8s"
				" %-40s  %-4u  %-35s  %s\n",
			*idx, enb, total_hit_cnt, total_trigger_cnt, hit_cnt,
			trigger_cnt, type, data, module, file, line_num, func,
			tag);

	return 0;
}

static const struct seq_operations fi_stat_sops = {
	.start = fi_stat_start,
	.stop  = fi_stat_stop,
	.next  = fi_stat_next,
	.show  = fi_stat_show,
};

static int fi_stat_open(struct inode *i, struct file *f)
{
	int rc = 0;

	rc = seq_open(f, &fi_stat_sops);

	if (rc == 0) {
		struct seq_file *sf = f->private_data;
		sf->private = i->i_private;
	}

	return rc;
}

static const struct file_operations fi_stat_fops = {
	.owner		= THIS_MODULE,
	.open		= fi_stat_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int fi_ctl_open(struct inode *i, struct file *f)
{
	if (fi_ctl_is_opened)
		return -EBUSY;

	fi_ctl_is_opened = true;

	return 0;
}

static int fi_ctl_release(struct inode *i, struct file *f)
{
	fi_ctl_is_opened = false;
	return 0;
}

static int fi_ctl_process_cmd(int argc, char *argv[])
{
	/* there should be at least "action", "func" and "tag" */
	if (argc < 3) {
		pr_err(KBUILD_MODNAME ": finject_ctl too few arguments\n");
		return -EINVAL;
	}

	if (strcmp(argv[0], "enable") == 0) {
		int rc;
		char *func;
		char *tag;

		/* "enable" also requires an "fp_type" */
		if (argc < 4) {
			pr_err(KBUILD_MODNAME ": finject_ctl too few arguments"
			       " for command '%s'\n", argv[0]);
			return -EINVAL;
		}

		func = kstrdup(argv[1], GFP_KERNEL);
		if (func == NULL)
			return -ENOMEM;

		rc = c2_fi_add_dyn_id(func);
		if (rc != 0) {
			kfree(func);
			return rc;
		}

		tag = kstrdup(argv[2], GFP_KERNEL);
		if (tag == NULL)
			return -ENOMEM;

		rc = c2_fi_add_dyn_id(tag);
		if (rc != 0) {
			kfree(tag);
			return rc;
		}

		if (strcmp(argv[3], "always") == 0) {
			if (argc > 4) {
				pr_err(KBUILD_MODNAME ": finject_ctl too many"
				       " arguments for command '%s'\n", argv[0]);
				return -EINVAL;
			}
			c2_fi_enable(func, tag);
		} else if (strcmp(argv[3], "oneshot") == 0) {
			if (argc > 4) {
				pr_err(KBUILD_MODNAME ": finject_ctl too many"
				       " arguments for command '%s'\n", argv[0]);
				return -EINVAL;
			}
			c2_fi_enable_once(func, tag);
		} else if (strcmp(argv[3], "random") == 0) {
			unsigned long p;
			if (argc != 5) {
				pr_err(KBUILD_MODNAME ": finject_ctl incorrect"
				       " number of arguments for FP type '%s'\n",
				       argv[3]);
				return -EINVAL;
			}
			rc = strict_strtoul(argv[4], 0, &p);
			if (rc < 0)
				return rc;
			c2_fi_enable_random(func, tag, p);
		} else if (strcmp(argv[3], "off_n_on_m") == 0) {
			unsigned long n;
			unsigned long m;
			if (argc != 6) {
				pr_err(KBUILD_MODNAME ": finject_ctl incorrect"
				       " number of arguments for FP type '%s'\n",
				       argv[3]);
				return -EINVAL;
			}
			rc = strict_strtoul(argv[4], 0, &n);
			if (rc < 0)
				return rc;
			rc = strict_strtoul(argv[5], 0, &m);
			if (rc < 0)
				return rc;
			c2_fi_enable_off_n_on_m(func, tag, n, m);
		} else {
			pr_err(KBUILD_MODNAME ": finject_ctl: invalid or not"
			       " allowed FP type '%s'\n", argv[3]);
			return -EINVAL;
		}
	} else if (strcmp(argv[0], "disable") == 0) {
		c2_fi_disable(argv[1], argv[2]);
	} else {
		pr_err(KBUILD_MODNAME ": finject_ctl: invalid action '%s'\n",
					argv[0]);
		return -EINVAL;
	}

	return 0;
}

static ssize_t fi_ctl_write(struct file *file, const char __user *user_buf,
			    size_t size, loff_t *ppos)
{
	int    rc;
	int    i;
	char   buf[256];
	int    argc;
	char **argv;

	if (size > sizeof(buf) - 1)
		return -EINVAL;

	if (strncpy_from_user(buf, user_buf, size) < 0)
		return -EFAULT;
	buf[size] = 0;

	/*
	 * usually debugfs files are written with `echo` command which appends a
	 * newline character at the end of string, so we need to remove it if it
	 * present
	 */
	if (buf[size - 1] == '\n') {
		buf[size - 1] = 0;
		size--;
	}

	/*
	 * check that buffer contains only printable text data to prevent
	 * user-space from injecting some malicious binary code into kernel in
	 * place of FP identifiers
	 */
	for (i = 0; i < size; ++i)
		if (!isprint(buf[i]))
			return -EINVAL;

	pr_info(KBUILD_MODNAME ": finject_ctl command '%s'\n", buf);

	argv = argv_split(GFP_KERNEL, buf, &argc);
	if (argv == NULL)
		return -ENOMEM;

	rc = fi_ctl_process_cmd(argc, argv);
	argv_free(argv);

	if (rc < 0)
		return rc;

	/* ignore the rest of the buffer, only one command at a time */
	*ppos += size;
	return size;
}

static const struct file_operations fi_ctl_fops = {
	.owner		= THIS_MODULE,
	.open		= fi_ctl_open,
	.release	= fi_ctl_release,
	.write		= fi_ctl_write,
};

int fi_dfs_init(void)
{
	static const char fi_stat_name[]  = "finject_stat";
	static const char fi_ctl_name[]   = "finject_ctl";

	fi_stat_file = debugfs_create_file(fi_stat_name, S_IRUGO, dfs_root_dir,
					   NULL, &fi_stat_fops);
	if (fi_stat_file == NULL) {
		pr_err("Failed to create debugfs file '%s/%s'\n",
			dfs_root_name, fi_stat_name);
		return -EPERM;
	}

	fi_ctl_file = debugfs_create_file(fi_ctl_name, S_IWUSR, dfs_root_dir,
					  NULL, &fi_ctl_fops);
	if (fi_ctl_file == NULL) {
		pr_err("Failed to create debugfs file '%s/%s'\n",
			dfs_root_name, fi_ctl_name);
		debugfs_remove(fi_stat_file);
		return -EPERM;
	}

	return 0;
}

void fi_dfs_cleanup(void)
{
	debugfs_remove(fi_ctl_file);
	debugfs_remove(fi_stat_file);
	fi_stat_file = 0;
}

/** @} end of kc2ctl group */

#else

int fi_dfs_init(void)
{
	pr_warn(KBUILD_MODNAME ": fault injection is not available, because it"
				" was disabled during build\n");
	return 0;
}

void fi_dfs_cleanup(void)
{
}

#endif /* ENABLE_FAULT_INJECTION */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
