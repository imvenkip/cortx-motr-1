#include "lib/cdefs.h"
#include "session_rpc.h"

bool_t
xdr_c2_session_cmd (XDR *xdrs, c2_session_cmd *objp)
{
	register int32_t *buf;

	 if (!xdr_enum (xdrs, (enum_t *) objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_client_id (XDR *xdrs, client_id *objp)
{
	register int32_t *buf;

	int i;
	 if (!xdr_vector (xdrs, (char *)objp->uuid, 40,
		sizeof (char), (xdrproc_t) xdr_char))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_session_id (XDR *xdrs, session_id *objp)
{
	register int32_t *buf;

	 if (!xdr_uint64_t (xdrs, &objp->id))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_session_create_arg (XDR *xdrs, session_create_arg *objp)
{
	register int32_t *buf;

	 if (!xdr_client_id (xdrs, &objp->sca_client))
		 return FALSE;
	 if (!xdr_client_id (xdrs, &objp->sca_server))
		 return FALSE;
	 if (!xdr_uint32_t (xdrs, &objp->sca_high_slot_id))
		 return FALSE;
	 if (!xdr_uint32_t (xdrs, &objp->cca_max_rpc_size))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_session_create_out (XDR *xdrs, session_create_out *objp)
{
	register int32_t *buf;

	 if (!xdr_session_id (xdrs, &objp->sco_session_id))
		 return FALSE;
	 if (!xdr_uint32_t (xdrs, &objp->sco_high_slot_id))
		 return FALSE;
	 if (!xdr_uint32_t (xdrs, &objp->sco_max_rpc_size))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_session_create_ret (XDR *xdrs, session_create_ret *objp)
{
	register int32_t *buf;

	 if (!xdr_int (xdrs, &objp->errno))
		 return FALSE;
	switch (objp->errno) {
	case 0:
		 if (!xdr_session_create_out (xdrs, &objp->session_create_ret_u.reply))
			 return FALSE;
		break;
	default:
		break;
	}
	return TRUE;
}

bool_t
xdr_session_destroy_arg (XDR *xdrs, session_destroy_arg *objp)
{
	register int32_t *buf;

	 if (!xdr_session_id (xdrs, &objp->da_session_id))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_session_destroy_ret (XDR *xdrs, session_destroy_ret *objp)
{
	register int32_t *buf;

	 if (!xdr_int32_t (xdrs, &objp->sda_errno))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_c2_session_adjust_in (XDR *xdrs, c2_session_adjust_in *objp)
{
	register int32_t *buf;

	 if (!xdr_session_id (xdrs, &objp->sr_session_id))
		 return FALSE;
	 if (!xdr_int32_t (xdrs, &objp->sr_new_high_slot_id))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_c2_session_adjust_rep (XDR *xdrs, c2_session_adjust_rep *objp)
{
	register int32_t *buf;

	 if (!xdr_uint32_t (xdrs, &objp->sr_new_high_slot_id))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_c2_session_adjust_out (XDR *xdrs, c2_session_adjust_out *objp)
{
	register int32_t *buf;

	 if (!xdr_int (xdrs, &objp->errno))
		 return FALSE;
	switch (objp->errno) {
	case 0:
		 if (!xdr_c2_session_adjust_rep (xdrs, &objp->c2_session_adjust_out_u.session_adjust_reply))
			 return FALSE;
		break;
	default:
		break;
	}
	return TRUE;
}

bool_t
xdr_c2_session_compound_op (XDR *xdrs, c2_session_compound_op *objp)
{
	register int32_t *buf;

	 if (!xdr_enum (xdrs, (enum_t *) objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_session_sequence_args (XDR *xdrs, session_sequence_args *objp)
{
	register int32_t *buf;

	 if (!xdr_uint32_t (xdrs, &objp->ssa_slot_id))
		 return FALSE;
	 if (!xdr_uint32_t (xdrs, &objp->ssa_sequence_id))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_compound_op_arg (XDR *xdrs, compound_op_arg *objp)
{
	register int32_t *buf;

	 if (!xdr_c2_session_compound_op (xdrs, &objp->c2op))
		 return FALSE;
	switch (objp->c2op) {
	case session_sequence_op:
		 if (!xdr_session_sequence_args (xdrs, &objp->compound_op_arg_u.sess_args))
			 return FALSE;
		break;
	case session_null_op:
		break;
	case session_commit:
		break;
	default:
		break;
	}
	return TRUE;
}

bool_t
xdr_compound_args (XDR *xdrs, compound_args *objp)
{
	register int32_t *buf;

	 if (!xdr_session_id (xdrs, &objp->ca_session_id))
		 return FALSE;
	 if (!xdr_array (xdrs, (char **)&objp->ca_oparray.ca_oparray_val, (u_int *) &objp->ca_oparray.ca_oparray_len, ~0,
		sizeof (compound_op_arg), (xdrproc_t) xdr_compound_op_arg))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_session_sequence_reply (XDR *xdrs, session_sequence_reply *objp)
{
	register int32_t *buf;

	 if (!xdr_int32_t (xdrs, &objp->errno))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_c2_session_resop (XDR *xdrs, c2_session_resop *objp)
{
	register int32_t *buf;

	 if (!xdr_c2_session_compound_op (xdrs, &objp->c2op))
		 return FALSE;
	switch (objp->c2op) {
	case session_null_op:
		break;
	case session_sequence_op:
		 if (!xdr_c2_sequence_reply (xdrs, &objp->c2_session_resop_u.c2seq_reply))
			 return FALSE;
		break;
	case session_commit:
		break;
	default:
		break;
	}
	return TRUE;
}

bool_t
xdr_c2_compound_reply (XDR *xdrs, c2_compound_reply *objp)
{
	register int32_t *buf;

	 if (!xdr_uint32_t (xdrs, &objp->status))
		 return FALSE;
	 if (!xdr_array (xdrs, (char **)&objp->resarray.resarray_val, (u_int *) &objp->resarray.resarray_len, ~0,
		sizeof (c2_session_resop), (xdrproc_t) xdr_c2_session_resop))
		 return FALSE;
	return TRUE;
}
