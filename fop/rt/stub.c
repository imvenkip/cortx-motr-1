/* -*- C -*- */
/**
   @addtogroup fop

   @{
 */

struct c2_fop_type;
struct c2_rpc_item_type;

int c2_rpc_item_type_register(struct c2_rpc_item_type *item_type)
{
	return 0;
}

void c2_rpc_item_type_deregister(struct c2_rpc_item_type *item_type)
{
}

int fop_fol_type_init(struct c2_fop_type *fopt)
{
	return 0;
}

void fop_fol_type_fini(struct c2_fop_type *fopt)
{
}

void c2_fits_init(void)
{
}

void c2_fits_fini(void)
{
}

struct c2_fop_field_type;

int c2_fop_field_type_fit(struct c2_fop_field_type *fieldt)
{
	return 0;
}

void c2_fom_ll_global_init(void)
{
}

/** @} end of fop group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
