/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original creation date: 08/17/2011
 */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "lib/memory.h"		  /* C2_ALLOC_ARR */
#include "fop/fop.h"		  /* c2_fop */

#include "console/console.h"	  /* verbose */
#include "console/console_it.h"	  /* c2_fit */
#include "console/console_yaml.h" /* c2_cons_yaml_get_value */
#include "console/console_u.h"
/**
   @addtogroup console_it
   @{
 */

static struct c2_cons_atom_ops atom_ops[FPF_NR];
static struct c2_cons_aggr_ops aggr_ops[FFA_NR];

static void depth_print(int depth)
{
	static const char ruler[] = "\t\t\t\t\t.....\t";
	printf("%*.*s", depth, depth, ruler);
}

static void default_show(const struct c2_fop_field_type *ftype,
		     const char *name, void *data)
{
	printf("%s:%s\n", name, ftype->fft_name);
}


static void void_get(const struct c2_fop_field_type *ftype,
		     const char *name, void *data)
{
}

static void void_set(const struct c2_fop_field_type *ftype,
		     const char *name, void *data)
{
}

static void byte_get(const struct c2_fop_field_type *ftype,
		     const char *name, void *data)
{
	char value = *(char *)data;

	if (verbose)
		printf("%s(%s) = %c\n", name, ftype->fft_name, value);
}

static void byte_set(const struct c2_fop_field_type *ftype,
		     const char *name, void *data)
{
	void *tmp_value;
	char value;

	if (yaml_support) {
		tmp_value = c2_cons_yaml_get_value(name);
		C2_ASSERT(tmp_value != NULL);
		*(char *)data = *(char *)tmp_value;
		if (verbose)
			printf("%s(%s) = %c\n", name, ftype->fft_name,
						  *(char *)data);
	} else {
		printf("%s(%s) = ", name, ftype->fft_name);
		if (scanf("\r%c", &value) != EOF)
			*(char *)data = value;
	}
}


static void u32_get(const struct c2_fop_field_type *ftype,
		    const char *name, void *data)
{
	uint32_t value = *(uint32_t *)data;

	if (verbose)
		printf("%s(%s) = %d\n", name, ftype->fft_name, value);
}

static void u32_set(const struct c2_fop_field_type *ftype,
		    const char *name, void *data)
{
	uint32_t  value;
	void	 *tmp_value;

	if (yaml_support) {
		tmp_value = c2_cons_yaml_get_value(name);
		C2_ASSERT(tmp_value != NULL);
		*(uint32_t *)data = atoi((const char *)tmp_value);
		if (verbose)
			printf("%s(%s) = %u\n", name, ftype->fft_name,
						  *(uint32_t *)data);
	} else {
		printf("%s(%s) = ", name, ftype->fft_name);
		if (scanf("%u", &value) != EOF)
			*(uint32_t *)data = value;
	}
}

static void u64_get(const struct c2_fop_field_type *ftype,
		    const char *name, void *data)
{
	uint64_t value = *(uint64_t *)data;

	if (verbose)
		printf("%s(%s) = %ld\n", name, ftype->fft_name, value);
}

static void u64_set(const struct c2_fop_field_type *ftype,
		    const char *name, void *data)
{
	void *tmp_value;
	uint64_t value;

	if (yaml_support) {
		tmp_value = c2_cons_yaml_get_value(name);
		C2_ASSERT(tmp_value != NULL);
		*(uint64_t *)data = atol((const char *)tmp_value);
		if (verbose)
			printf("%s(%s) = %ld\n", name, ftype->fft_name,
						   *(uint64_t *)data);
	} else {
		printf("%s(%s) = ", name, ftype->fft_name);
		if (scanf("%lu", &value) != EOF)
			*(uint64_t *)data = value;
	}
}


/**
 * @brief Methods to hadle U64, U32, etc.
 */
static struct c2_cons_atom_ops atom_ops[FPF_NR] = {
        [FPF_VOID] = { void_get, void_set, default_show },
        [FPF_BYTE] = { byte_get, byte_set, default_show },
        [FPF_U32]  = { u32_get, u32_set, default_show },
        [FPF_U64]  = { u64_get, u64_set, default_show }
};

static void record_process(const struct c2_fop_field_type *ftype,
			   const char *name, void *data,
			   enum c2_cons_data_process_type type,
			   int depth)
{
	struct c2_fop_field	 *child;
        struct c2_fop_field_type *cftype;
        enum c2_fop_field_aggr    cgtype;
	int			  i;
	enum c2_fop_field_primitive_type atype;

	for (i = 0; i < ftype->fft_nr; i++) {
		child = ftype->fft_child[i];
		cftype = child->ff_type;
		cgtype = cftype->fft_aggr;
		if (cgtype != FFA_ATOM && verbose) {
			++depth;
			depth_print(depth);
			printf("%s:%s {\n", child->ff_name,
					    child->ff_type->fft_name);
		}
		aggr_ops[cgtype].caggr_val_process(cftype, child->ff_name,
						   data, type, depth);
		if (cgtype != FFA_ATOM && verbose) {
			depth_print(depth);
			printf("}\n");
			--depth;
		}
		atype = cftype->fft_u.u_atom.a_type;

		switch (atype) {
		case FPF_NR:
		case FPF_VOID:
			break;
		case FPF_BYTE:
			data = data + sizeof(char);
			break;
		case FPF_U32:
			data = data + sizeof(uint32_t);
			break;
		case FPF_U64:
			data = data + sizeof(uint64_t);
			break;
		}
	}
}

static void union_process(const struct c2_fop_field_type *ftype,
			  const char *name, void *data,
			  enum c2_cons_data_process_type type,
			  int depth)
{
	struct c2_fop_field	 *child;
        struct c2_fop_field_type *cftype;
        enum c2_fop_field_aggr    cgtype;

	child = ftype->fft_child[0];
	cftype = child->ff_type;
	cgtype = cftype->fft_aggr;
	aggr_ops[cgtype].caggr_val_process(cftype, child->ff_name,
					   data, type, depth);
}

static void byte_pointer_process(const struct c2_fop_field *child, void *data,
				 enum c2_cons_data_process_type type, int depth)
{
	struct c2_cons_fop_buf	*buf;
	void			*tmp_value;

	++depth;
	depth_print(depth);
	buf = (struct c2_cons_fop_buf *)data;
	if (type == CONS_IT_OUTPUT) {
		printf("%s(%s) = %s\n", child->ff_name,
					  child->ff_type->fft_name, buf->cons_buf);
	} else {
		C2_ALLOC_ARR(buf->cons_buf, buf->cons_size);
		C2_ASSERT(buf->cons_buf != NULL);
		if (yaml_support) {
			tmp_value = c2_cons_yaml_get_value(child->ff_name);
			C2_ASSERT(tmp_value != NULL);
			memcpy(buf->cons_buf, tmp_value, buf->cons_size);
			if (verbose)
				printf("%s(%s) = %s", child->ff_name,
						      child->ff_type->fft_name,
						      buf->cons_buf);
		} else {
			printf("%s(%s) = ", child->ff_name,
					    child->ff_type->fft_name);
			if (scanf("%s", buf->cons_buf) == EOF)
				return;
		}
	}
	--depth;
}

static void sequence_process(const struct c2_fop_field_type *ftype,
			     const char *name, void *data,
			     enum c2_cons_data_process_type type,
			     int depth)
{
	struct c2_fop_field		 *child;
        struct c2_fop_field_type	 *cftype;
        enum c2_fop_field_aggr		  cgtype;
	enum c2_fop_field_primitive_type  atype;

	child = ftype->fft_child[0];
	cftype = child->ff_type;
	cgtype = cftype->fft_aggr;
	aggr_ops[cgtype].caggr_val_process(cftype, child->ff_name,
					   data, type, depth);
	child = ftype->fft_child[1];
	cftype = child->ff_type;
	cgtype = cftype->fft_aggr;
	atype = cftype->fft_u.u_atom.a_type;
	if (cgtype == FFA_ATOM && atype == FPF_BYTE && type != CONS_IT_SHOW)
		byte_pointer_process(child, data, type, depth);
	else {
		data = data + sizeof(uint32_t);
		if (cgtype != FFA_ATOM && verbose) {
			++depth;
			depth_print(depth);
			printf("%s:%s {\n", child->ff_name,
					    child->ff_type->fft_name);
		}
		aggr_ops[cgtype].caggr_val_process(cftype, child->ff_name,
						   data, type, depth);
		if (cgtype != FFA_ATOM && verbose) {
			depth_print(depth);
			printf("}\n");
			--depth;
		}
	}
}

static void typedef_process(const struct c2_fop_field_type *ftype,
			    const char *name, void *data,
			    enum c2_cons_data_process_type type,
			    int depth)
{
}

static void atom_process(const struct c2_fop_field_type *ftype,
			 const char *name, void *data,
			 enum c2_cons_data_process_type type,
			 int depth)
{
	enum c2_fop_field_primitive_type  atype;

	++depth;
	depth_print(depth);
	atype = ftype->fft_u.u_atom.a_type;
	switch (type) {
	case CONS_IT_INPUT:
		atom_ops[atype].catom_val_set(ftype, name, data);
		break;
	case CONS_IT_OUTPUT:
		atom_ops[atype].catom_val_get(ftype, name, data);
		break;
	case CONS_IT_SHOW:
	default:
		atom_ops[atype].catom_val_show(ftype, name, data);
	}
	--depth;
}

/**
 * @brief Methods to handle RECORD, SEQUENCE, etc.
 */
static struct c2_cons_aggr_ops aggr_ops[FFA_NR] = {
        [FFA_RECORD]   = { record_process   },
        [FFA_UNION]    = { union_process    },
        [FFA_SEQUENCE] = { sequence_process },
        [FFA_TYPEDEF]  = { typedef_process  },
        [FFA_ATOM]     = { atom_process     }
};

void c2_cons_fop_obj_input_output(struct c2_fit *it,
				  enum c2_cons_data_process_type type)
{
        struct c2_fit_yield	   yield;
	struct c2_fop_field	  *field;
	struct c2_fop_field_type  *ftype;
	struct c2_fop_type_format *fop_format;
	enum c2_fop_field_aggr     gtype;
	int			   fop_depth;

	fop_depth = 0;
	fop_format = it->fi_fop->f_type->ft_fmt;
	C2_ASSERT(fop_format != NULL);
	if (verbose)
		printf("\n%s {\n", fop_format->ftf_name);
        while(c2_fit_yield(it, &yield) > 0) {
		field = yield.fy_val.ffi_field;
		ftype = field->ff_type;
		gtype = ftype->fft_aggr;
		if (gtype != FFA_ATOM && verbose) {
			++fop_depth;
			depth_print(fop_depth);
			printf("%s:%s {\n", field->ff_name,
					    field->ff_type->fft_name);
		}
		aggr_ops[gtype].caggr_val_process(ftype, field->ff_name,
						  yield.fy_val.ffi_val,
						  type, fop_depth);
		if (gtype != FFA_ATOM && verbose) {
			depth_print(fop_depth);
			printf("}\n");
			--fop_depth;
		}
        }
	if (verbose)
		printf("}\n");
}

void c2_cons_fop_obj_input(struct c2_fop *fop)
{
	struct c2_fit it;

	/* FOP iterator will prompt for each field in fop. */
	c2_fop_all_object_it_init(&it, fop);
	c2_cons_fop_obj_input_output(&it, CONS_IT_INPUT);
	c2_fop_all_object_it_fini(&it);
}

void c2_cons_fop_obj_output(struct c2_fop *fop)
{
	struct c2_fit it;

	c2_fop_all_object_it_init(&it, fop);
	c2_cons_fop_obj_input_output(&it, CONS_IT_OUTPUT);
	c2_fop_all_object_it_fini(&it);
}

void c2_cons_fop_fields_show(struct c2_fop *fop)
{
	struct c2_fit it;

	verbose = true;
	c2_fop_all_object_it_init(&it, fop);
	c2_cons_fop_obj_input_output(&it, CONS_IT_SHOW);
	c2_fop_all_object_it_fini(&it);
	verbose = false;
}

/** @} end of console_it group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

