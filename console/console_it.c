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
		printf("\t%s(%s) = %c\n", name, ftype->fft_name, value);
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
			printf("\t%s(%s) = %c\n", name, ftype->fft_name,
						  *(char *)data);
	} else {
		printf("\t%s(%s) = ", name, ftype->fft_name);
		scanf("\r%c", &value);
		*(char *)data = value;
	}
}

static void u32_get(const struct c2_fop_field_type *ftype,
		    const char *name, void *data)
{
	uint32_t value = *(uint32_t *)data;

	if (verbose)
		printf("\t%s(%s) = %d\n", name, ftype->fft_name, value);
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
			printf("\t%s(%s) = %u\n", name, ftype->fft_name,
						  *(uint32_t *)data);
	} else {
		printf("\t%s(%s) = ", name, ftype->fft_name);
		scanf("%u", &value);
		*(uint32_t *)data = value;
	}
}

static void u64_get(const struct c2_fop_field_type *ftype,
		    const char *name, void *data)
{
	uint64_t value = *(uint64_t *)data;

	if (verbose)
		printf("\t%s(%s) = %ld\n", name, ftype->fft_name, value);
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
			printf("\t%s(%s) = %ld\n", name, ftype->fft_name,
						   *(uint64_t *)data);
	} else {
		printf("\t%s(%s) = ", name, ftype->fft_name);
		scanf("%lu", &value);
		*(uint64_t *)data = value;
	}
}


/**
 * @brief Methods to hadle U64, U32, etc.
 */
static struct c2_cons_atom_ops atom_ops[FPF_NR] = {
        [FPF_VOID] = { void_get, void_set },
        [FPF_BYTE] = { byte_get, byte_set },
        [FPF_U32]  = { u32_get, u32_set },
        [FPF_U64]  = { u64_get, u64_set }
};

static void record_process(const struct c2_fop_field_type *ftype,
			   const char *name, void *data, bool output)
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
		if (cgtype == FFA_ATOM && verbose)
			printf("\t");
		aggr_ops[cgtype].caggr_process_val(cftype, child->ff_name,
						   data, output);
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
			  const char *name, void *data, bool output)
{
	struct c2_fop_field	 *child;
        struct c2_fop_field_type *cftype;
        enum c2_fop_field_aggr    cgtype;

	child = ftype->fft_child[0];
	cftype = child->ff_type;
	cgtype = cftype->fft_aggr;
	aggr_ops[cgtype].caggr_process_val(cftype, child->ff_name,
					   data, output);

}

static void sequence_process(const struct c2_fop_field_type *ftype,
			     const char *name, void *data, bool output)
{
	struct c2_fop_field	 *child;
        struct c2_fop_field_type *cftype;
        enum c2_fop_field_aggr    cgtype;
	void			 *tmp_value;
	struct c2_fop_vec	 *vec_data;


	if (!yaml_support)
		printf("Enter Count :");
	child = ftype->fft_child[0];
	cftype = child->ff_type;
	cgtype = cftype->fft_aggr;
	aggr_ops[cgtype].caggr_process_val(cftype, child->ff_name,
					   data, output);
	vec_data = (struct c2_fop_vec *)data;
	C2_ALLOC_ARR(vec_data->fv_seg, vec_data->fv_count);
	C2_ASSERT(vec_data->fv_seg != NULL);
	if (yaml_support) {
		tmp_value = c2_cons_yaml_get_value(name);
		C2_ASSERT(tmp_value != NULL);
		memcpy(vec_data->fv_seg, tmp_value, vec_data->fv_count);
	} else {
		printf("Enter value :");
		scanf("%s", vec_data->fv_seg);
	}
}

static void typedef_process(const struct c2_fop_field_type *ftype,
			    const char *name, void *data, bool output)
{
}

static void atom_process(const struct c2_fop_field_type *ftype,
			 const char *name, void *data, bool output)
{
	enum c2_fop_field_primitive_type  atype;

	atype = ftype->fft_u.u_atom.a_type;
	if (output)
		atom_ops[atype].catom_get_val(ftype, name, data);
	else
		atom_ops[atype].catom_set_val(ftype, name, data);
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

void c2_cons_fop_fields_show(struct c2_fit *it)
{
	struct c2_fop_field	  *child;
        struct c2_fit_yield	   yield;
	struct c2_fop_field_type  *ftype;
	struct c2_fop_type_format *fop_format;
	enum c2_fop_field_aggr     gtype;
	int			   i;

	fop_format = it->fi_fop->f_type->ft_fmt;
	C2_ASSERT(fop_format != NULL);
	printf("\n%s {\n", fop_format->ftf_name);
        while(c2_fit_yield(it, &yield) > 0) {
		ftype = yield.fy_val.ffi_field->ff_type;
		child = yield.fy_val.ffi_field;
		gtype = ftype->fft_aggr;
		printf("\t%s:%s",child->ff_name, child->ff_type->fft_name);
		if (gtype != FFA_ATOM) {
			printf(" {\n");
			for (i = 0; i < ftype->fft_nr; i++) {
				child = ftype->fft_child[i];
				printf("\t\t%s:%s\n",child->ff_name,
						  child->ff_type->fft_name);
			}
			printf("\t}");
		}
		printf("\n");
	}
	printf("}\n");
}

void c2_cons_fop_obj_input_output(struct c2_fit *it, bool output)
{
        struct c2_fit_yield	   yield;
	struct c2_fop_field	  *field;
	struct c2_fop_field_type  *ftype;
	struct c2_fop_type_format *fop_format;
	enum c2_fop_field_aggr     gtype;

	fop_format = it->fi_fop->f_type->ft_fmt;
	C2_ASSERT(fop_format != NULL);
	if (verbose)
		printf("\n%s {", fop_format->ftf_name);
        while(c2_fit_yield(it, &yield) > 0) {
		field = yield.fy_val.ffi_field;
		ftype = field->ff_type;
		gtype = ftype->fft_aggr;
		if (gtype != FFA_ATOM && verbose)
			printf("\n\t%s:%s {\n", field->ff_name,
						field->ff_type->fft_name);
		aggr_ops[gtype].caggr_process_val(ftype, field->ff_name,
						  yield.fy_val.ffi_val, output);
		if (gtype != FFA_ATOM && verbose)
			printf("\t}\n");
        }
	if (verbose)
		printf("}\n");
}

void c2_cons_fop_obj_input(struct c2_fit *it)
{
	c2_cons_fop_obj_input_output(it, false);
}

void c2_cons_fop_obj_output(struct c2_fit *it)
{
	c2_cons_fop_obj_input_output(it, true);
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

