/* -*- C -*- */
/*
 * COPYRIGHT 2017 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF SEAGATE LLC,
 * ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF SEAGATE TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF SEAGATE LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF SEAGATE'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A SEAGATE REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author:  Pratik Shinde <pratik.shinde@seagate.com>
 * Original creation date: 20-Jun-2016
 */

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <assert.h>
#include <stdarg.h>
#include <unistd.h>

#include "lib/finject.h"
#include "lib/trace.h"
#include "clovis/clovis.h"
#include "clovis/clovis_internal.h"
#include "clovis/clovis_idx.h"

#include "clovis/m0crate/logger.h"
#include "clovis/m0crate/workload.h"
#include "clovis/m0crate/crate_clovis.h"
#include "clovis/m0crate/crate_clovis_utils.h"


void integrity(struct m0_uint128 object_id, unsigned char **md5toverify,
		int block_count, int idx_op);
int index_operation(struct workload *wt);
void list_index_return(struct workload *w);


static struct m0_clovis_op_ops *cbs;


static int create_object(struct m0_uint128 id)
{
	int                  rc;
	struct m0_clovis_obj *obj;
	struct m0_clovis_op *ops[1] = {NULL};

	if (NULL == M0_ALLOC_PTR(obj))
		return -ENOMEM;

	m0_clovis_obj_init(obj, crate_clovis_uber_realm(), &id, 1);

	m0_clovis_entity_create(&obj->ob_entity, &ops[0]);

	m0_fi_enable("clovis_cob_mds_send", "Performance");
	m0_clovis_op_launch(ops, ARRAY_SIZE(ops));

	rc = m0_clovis_op_wait(
		ops[0], M0_BITS(M0_CLOVIS_OS_FAILED, M0_CLOVIS_OS_STABLE),
		m0_time_from_now(3,0));

	if (rc != 0)
		return rc;

	rc = ops[0]->op_sm.sm_rc;
	if (M0_BITS(ops[0]->op_sm.sm_state) & M0_BITS(M0_CLOVIS_OS_FAILED)) {
		printf("Clovis object create failed. %d\n", rc);
	}
	m0_clovis_op_fini(ops[0]);
	m0_clovis_op_free(ops[0]);
	m0_clovis_obj_fini(obj);
	m0_free(obj);
	return rc;
}

int create_objects(struct workload *load)
{
	int i;
	int rc;
	struct clovis_workload_io *w = load->u.cw_clovis_io;

	M0_ALLOC_ARR(w->ids, w->num_objs);
	if (w->ids == NULL)
		return -ENOMEM;

	for(i = 0; i < w->num_objs; i++) {

		/* Create the object */
		generate_fid(load->cw_rstate, &w->ids[i].u_lo, &w->ids[i].u_hi);
		if (w->opcode) {
			rc = create_object(w->ids[i]);
			if (rc != 0 && rc != -EEXIST) {
				printf("Object create failed..");
				return rc;
			}
		}
	}

	return 0;
}
static int prepare_data_buffer(char *filename, struct m0_bufvec *data,
			       unsigned char *md5sum[])
{
	int i;
	int rc;
	int nr_blocks;
	FILE *fp;
	nr_blocks = data->ov_vec.v_nr;

	fp = fopen(filename, "r");
	if (fp == NULL) {
		cr_log(CLL_ERROR, "Unable to open file:%d:%s\n", errno,
				strerror(errno));
		return -EINVAL;
	}

	for (i = 0; i < nr_blocks; i++) {
		rc = fread(data->ov_buf[i], clovis_block_size, 1, fp);
		if (rc != 1) {
			printf("File read failed:%d:%s\n", ferror(fp),
					filename);
			break;
		}
		if (md5sum) {
			md5sum[i] = calc_md5sum(data->ov_buf[i],
					clovis_block_size);
		}
		if (feof(fp))
			break;
	}

	fclose(fp);
	return i;
}

int allocate_buffers(struct m0_bufvec *data, struct m0_indexvec *ext,
		     struct m0_bufvec *attr, int block_count)
{
	int rc = 0;
	/* Allocate block_count * 4K data buffer. */
	rc = m0_bufvec_alloc_aligned(data, block_count,
			clovis_block_size, 12);

	/* Allocate bufvec and indexvec for write. */
	rc = m0_bufvec_alloc(attr, block_count, 1);
	if(rc != 0)
		return rc;

	rc = m0_indexvec_alloc(ext, block_count);
	if (rc != 0)
		return rc;
	return rc;
}

int prepare_idx_vector(struct m0_indexvec *ext, struct m0_bufvec *attr,
		       int block_count, int op_count)
{
	long unsigned last_index = clovis_block_size*op_count*block_count;
	/*
	 * Prepare indexvec for write: <clovis_block_count> from the
	 * beginning of the object.
	 */
	int ext_count = 0;
	for (ext_count = 0; ext_count < block_count;
			ext_count++) {
		ext->iv_index[ext_count] = last_index;
		ext->iv_vec.v_count[ext_count] = clovis_block_size;
		last_index += clovis_block_size;
		/* we don't want any attributes */
		attr->ov_vec.v_count[ext_count] = 0;
	}
	return 0;
}

void free_md5sum(unsigned char **md5sum, int clovis_block_count)
{
	int i = 0;
	for(i = 0; i < clovis_block_count; i++)
		free(md5sum[i]);
	free(md5sum);
}

struct clovis_op_context {
	struct timeval op_finish;
	int index;
	int *op_status;
	unsigned char **md5sum;
	int block_count;
	struct m0_indexvec ext;
	struct m0_bufvec   data;
	struct m0_bufvec   attr;
};

void free_buffers(struct m0_indexvec ext, struct m0_bufvec data,
		  struct m0_bufvec   attr)
{
	/* Free bufvec's and indexvec's */
	m0_indexvec_free(&ext);
	m0_bufvec_free(&data);
	m0_bufvec_free(&attr);

}
void clean_workload(struct workload *load, struct clovis_workload_task *task)
{
	int i;
	struct clovis_workload_io *wt = load->u.cw_clovis_io;
	for(i = 0; i < load->cw_ops; i++) {
		struct clovis_op_context *context = task->ops[i]->op_datum;
		/* fini and release */
		m0_clovis_op_fini(task->ops[i]);
		m0_clovis_op_free(task->ops[i]);
		m0_clovis_obj_fini(&task->objs[i]);

		free_buffers(context->ext, context->data, context->attr);

		if (wt->integrity)
			free_md5sum(context->md5sum, context->block_count);

		m0_free(context);

	}
	m0_free(task->objs);
	m0_free(task->op_status);
	m0_free(task->ops);
}


int get_object_id_idx(struct workload *load, int idx)
{
	struct clovis_workload_io *w = load->u.cw_clovis_io;
	if (w->num_objs == load->cw_nr_thread)
		return idx;
	return 0;
}

void clovis_op_cb_executed(struct m0_clovis_op *op)
{
	return;
}

void clovis_op_cb_stable(struct m0_clovis_op *op)
{
	struct clovis_op_context *op_context = op->op_datum;
	gettimeofday(&op_context->op_finish, NULL);
	op_context->op_status[op_context->index] = OP_FINISHED;
}

void clovis_op_cb_failed(struct m0_clovis_op *op)
{
	struct clovis_op_context *op_context = op->op_datum;
	op_context->op_status[op_context->index] = OP_FAILED;
}

int clovis_op_setup()
{
	cbs = m0_alloc(sizeof(struct m0_clovis_op_ops));
	cbs->oop_executed = clovis_op_cb_executed;
	cbs->oop_stable = clovis_op_cb_stable;
	cbs->oop_failed = clovis_op_cb_failed;
	return 0;
}

static int clovis_operation(struct workload *load,
			    struct clovis_workload_task *task,
			    int task_num)
{
	int                  rc = 0;
	int                  i;
	int num_ops;
	int block_count = 0;
	struct clovis_op_context *op_context;
	struct clovis_workload_io *w = load->u.cw_clovis_io;

	setbuf(stdout, NULL);

	if (NULL == M0_ALLOC_ARR(task->ops, load->cw_ops))
		return M0_ERR(-ENOMEM);

	if (NULL == M0_ALLOC_ARR(task->op_list_time, load->cw_ops))
		return M0_ERR(-ENOMEM);

	if (NULL == M0_ALLOC_ARR(task->op_status, load->cw_ops))
		return M0_ERR(-ENOMEM);

	if (NULL == M0_ALLOC_ARR(task->objs, load->cw_ops))
		return M0_ERR(-ENOMEM);

	block_count = w->iosize/clovis_block_size;

	num_ops = load->cw_ops;
	clovis_op_setup();
	for(i = 0; i < num_ops; i++) {

		/* Set the object entity we want to write */
		m0_clovis_obj_init(&task->objs[i], crate_clovis_uber_realm(),
				   &(w->ids[get_object_id_idx(load, task_num)]), 1);

		op_context = m0_alloc(sizeof(struct clovis_op_context));
		if (op_context == NULL)
			return -1;

		allocate_buffers(&op_context->data, &op_context->ext,
				&op_context->attr, block_count);

		op_context->md5sum = NULL;
		if (w->integrity) {
			M0_ALLOC_ARR(op_context->md5sum, block_count);
			if(op_context->md5sum == NULL)
				return -1;
		}

		op_context->op_status = task->op_status;
		op_context->index = i;

		op_context->block_count = block_count;

		prepare_data_buffer(w->src_filename, &op_context->data,
				    op_context->md5sum);

		prepare_idx_vector(&op_context->ext, &op_context->attr,
				   block_count, i);

		/* Create the write request */
		m0_clovis_obj_op(&task->objs[i],
				 w->opcode ? M0_CLOVIS_OC_WRITE :
					M0_CLOVIS_OC_READ, &op_context->ext,
				 &op_context->data, &op_context->attr, 0,
				 &task->ops[i]);

		task->ops[i]->op_datum = op_context;

		if (w->mode) {

			m0_clovis_op_setup(task->ops[i], cbs, 0);
		}
	}

	return rc;
}

struct timeval async_wait(struct m0_clovis_op *op)
{

	struct clovis_op_context *ctx =
	     (struct clovis_op_context *)op->op_datum;

	struct timeval op_finish_time = {};
	if (!ctx)
		goto out;

	while(ctx->op_status[ctx->index] != OP_FINISHED &&
			ctx->op_status[ctx->index] != OP_FAILED)
		;

	op_finish_time = ctx->op_finish;
out:
	return op_finish_time;
}

struct timeval sync_wait(struct m0_clovis_op *op)
{
	int rc = 0;
	struct timeval after;
	struct m0_uint128 id = {};
	rc = m0_clovis_op_wait(op,
			M0_BITS(M0_CLOVIS_OS_FAILED,
			M0_CLOVIS_OS_STABLE), M0_TIME_NEVER);

	rc = op->op_sm.sm_rc;
	if (M0_BITS(op->op_sm.sm_state) &
			M0_BITS(M0_CLOVIS_OS_FAILED)) {
		printf("Clovis object operation failed. %d\n", rc);
		printf("Object operation:<%lu:%lu>\n", (unsigned long)id.u_hi,
					  (unsigned long)id.u_lo);
	}

	id = op->op_entity->en_id;
	gettimeofday(&after, NULL);
	return after;
}

void wait_for_ops(struct workload *load, struct clovis_workload_task *task)
{
	int i;
	struct clovis_workload_io *w = load->u.cw_clovis_io;
	if (w->mode) {
		for(i = 0; i < load->cw_ops; i++)
			task->op_list_time[i] = async_wait(task->ops[i]);
	} else {
		for(i = 0; i < load->cw_ops; i++)
			 task->op_list_time[i] = sync_wait(task->ops[i]);
	}
}

static int launch_clovis_operation(struct workload *load,
				   struct clovis_workload_task *task)
{
	int i;
	struct clovis_workload_io *w = load->u.cw_clovis_io;
	struct timeval before, after;

	gettimeofday(&before, NULL);
	m0_clovis_op_launch(task->ops, load->cw_ops);
	wait_for_ops(load, task);
	gettimeofday(&after, NULL);

	if (w->integrity && w->opcode) {
		for(i = 0; i < load->cw_ops; i++) {
			struct clovis_op_context *datum = task->ops[i]->op_datum;
			printf("Performing intergrity check operation:%d\n", i);
			integrity(w->ids[get_object_id_idx(load, task->task_idx)],
				  datum->md5sum, datum->block_count, i);
			printf("Integrity check is complete....\n");
		}
	}

	for(i = 0; i < load->cw_ops; i++) {
		timeval_sub(&task->op_list_time[i], &before);
	}

	timeval_sub(&after, &before);
	printf("-------------------------------------------------\n");
	printf("All operations in workload are completed in :%f\n", tsec(&after));

	clean_workload(load, task);
	return 0;
}

void integrity(struct m0_uint128 object_id, unsigned char **md5toverify,
		int clovis_block_count, int idx_op)
{
	struct m0_indexvec ext;
	struct m0_bufvec   data;
	struct m0_bufvec   attr;

	struct m0_clovis_obj obj;
	struct m0_clovis_op *op = NULL;

/* XXX: clovis_io checks are disabled */
#if 0
	unsigned char *md5sum;
#endif

	setbuf(stdout, NULL);



	allocate_buffers(&data, &ext, &attr, clovis_block_count);

	prepare_idx_vector(&ext, &attr, clovis_block_count, idx_op);

	/* Set the object entity we want to write */
	m0_clovis_obj_init(&obj, crate_clovis_uber_realm(),
			&object_id, 1);


	/* Create the write request */
	m0_clovis_obj_op(&obj, M0_CLOVIS_OC_READ, &ext, &data, &attr,
			 0, &op);


	m0_clovis_op_launch(&op, 1);
	sync_wait(op);

/* XXX: clovis_io checks are disabled */
#if 0

	int k;
	for (k = 0; k < data.ov_vec.v_nr; k++) {
		md5sum = calc_md5sum(data.ov_buf[k], clovis_block_size);

		if(strncmp((char *)md5sum, (char*)md5toverify[k],
					MD5_DIGEST_LENGTH)) {
			printf("*******Integrity failed..Block\
			       number:%d\n", k);
			printf("\n----------\n");

		}

		free(md5sum);
	}
#else
    assert(0 && "NotImplemented");
#endif

	/* fini and release */
	m0_clovis_op_fini(op);
	m0_clovis_op_free(op);
	m0_clovis_obj_fini(&obj);

	/* Free bufvec's and indexvec's */
	m0_indexvec_free(&ext);
	m0_bufvec_free(&data);
	m0_bufvec_free(&attr);
}

static void print_detail_output(struct workload *w, struct clovis_workload_task *task)
{
	int i;
	printf("--------------------------------------\n");
	printf("Clovis workload.\n");
	for(i = 0; i < w->cw_ops; i++) {
		printf("Operation %d is completed:%f\n", i,
				tsec(&task->op_list_time[i]));
	}

	if (task->op_list_time)
		m0_free(task->op_list_time);
	m0_free(task);
}


int prepare_task(struct workload *w, struct clovis_workload_task **task,
		 int task_num)
{
	*task = m0_alloc(sizeof(struct clovis_workload_task));
	if (*task == NULL)
		return -ENOMEM;
	(*task)->task_idx = task_num;
	clovis_operation(w, *task, task_num);
	return 0;
}

int prepare_clovis_tasks(struct workload *w, struct workload_task *tasks)
{
	int i;
	int rc;
	int num_tasks = w->cw_nr_thread;
	for(i = 0; i < num_tasks; i++) {
		rc = prepare_task(w,
			((struct clovis_workload_task **)&tasks[i].u.clovis_task), i);
		if (rc != 0)
			return rc;
	}
	return 0;
}

void clovis_run(struct workload *w, struct workload_task *tasks)
{
	int rc;
	struct clovis_workload_io *clovis_workload =
					(struct clovis_workload_io*)w->u.cw_clovis_io;
	create_objects(w);
	rc = prepare_clovis_tasks(w, tasks);
	if (rc != 0)
		return ;
	workload_start(w, tasks);
        workload_join(w, tasks);
	m0_free(clovis_workload->src_filename);
	m0_free(clovis_workload->ids);
	m0_free(w->u.cw_clovis_io);
	m0_free(cbs);
}

void clovis_op_run(struct workload *w, struct workload_task *task, const struct workload_op *op)
{
	struct clovis_workload_task *clovis_task = task->u.clovis_task;
	adopt_mero_thread(clovis_task);
	launch_clovis_operation(w, clovis_task);
	release_mero_thread(clovis_task);
	print_detail_output(w, clovis_task);
}

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
