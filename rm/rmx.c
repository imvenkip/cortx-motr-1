/*
 * RM fop-fom pseudo-code.
 */

/*
 * FOP part. rm-fop has to implement the following functions:
 */

int c2_rm_borrow_out(struct c2_rm_incoming *in, struct c2_rm_right *right);
int c2_rm_revoke_out(struct c2_rm_incoming *in,
		     struct c2_rm_loan *loan, struct c2_rm_right *right);

/* defined in rm_{u,k}.h, duplicated for completeness.
   This is the on-wire format of BORROW fop.
 */
struct c2_fop_rm_borrow {
	uint64_t             bo_rtype;
	struct c2_fop_rm_owner bo_owner;
	struct c2_fop_rm_right bo_right;
	struct c2_fop_rm_cookie bo_loan;
	uint64_t             bo_flags;
};

/* defined in rm_{u,k}.h, duplicated for completeness.
   This is the on-wire format of BORROW reply fop.
 */
struct c2_fop_rm_borrow_rep {
	uint32_t             br_rc;
	struct c2_fop_rm_owner bo_owner;
	struct c2_fop_rm_cookie bo_loan;
	struct c2_fop_rm_right bo_right;
	struct c2_fop_rm_opaque bo_lvb;
};

/* defined in rm_{u,k}.h, duplicated for completeness.
   This is the on-wire format of CANcel and revOKE fops.
 */
struct c2_fop_rm_canoke {
	char                 cr_dir;
	struct c2_fop_rm_cookie cr_loan;
	struct c2_fop_rm_right cr_right;
};

/* common part of rm_borrow and rm_revoke */
struct rm_out {
	struct c2_rm_incoming *ou_incoming;
	struct c2_rm_outgoing  ou_req;
	struct c2_fop          ou_fop;
};

/* structure to keep track of outgoing BORROW fop */
struct rm_borrow {
	struct rm_out            bo_out;
	struct c2_fop_rm_borrow  bo_data;
	struct c2_rm_loan       *bo_loan;
};

/* structure to keep track of outgoing REVOKE fop */
struct rm_revoke {
	struct rm_out           re_out;
	struct c2_fop_rm_canoke re_data;
};

/*
 * This is called from borrow_send().
 */
int c2_rm_borrow_out(struct c2_rm_incoming *in, struct c2_rm_right *right)
{
	struct rm_borrow *borrow;

	C2_ALLOC_PTR(borrow);
	req = &borrow->bo_out.ou_req.rog_type
	req->rog_type  = ROT_BORROW;
	req->rog_owner = right->ri_owner;
	right_copy(&req->rog_want.rl_right, right);
	req->rog_want.rl_other = right->ri_owner->ro_creditor;
	C2_ALLOC_PRE(borrow->bo_loan);
	c2_fop_init(&out->ou_fop, &c2_fop_rm_borrow_fopt, data);
	pin_add(in, &req->rog_want.rl_right, RPF_TRACK);
	/* fill borrow->bo_data */
	fop = &borrow->bo_data;
	fop->bo_rtype = right->ri_owner->ro_resource->rt_id;
	fop->bo_owner = right->ri_owner->ro_creditor->rem_id;
	c2_rm_loan_cookie(borrow->bo_loan, &fop->bo_loan.ow_cookie);
	... encode right to fop->bo_right, using c2_rm_right_ops::rro_encode();
	borrow->bo_out.ou_fop.f_item->ri_ops = &borrow_ops;
	c2_rpc_post(&borrow->bo_out.ou_fop.f_item);
}


/*
 * This is called from revoke_send().
 */
int c2_rm_revoke_out(struct c2_rm_incoming *in,
		     struct c2_rm_loan *loan, struct c2_rm_right *right);
{
	struct rm_revoke *revoke;

	C2_ALLOC_PTR(revoke);
	req = &revoke->re_out.ou_req;
	req->rog_type  = ROT_REVOKE;
	req->rog_owner = right->ri_owner;
	loan_copy(&req->rog_want, loan);
	c2_fop_init(&out->ou_fop, &c2_fop_rm_canoke_fopt, data);
	pin_add(in, &req->rog_want.rl_right, RPF_TRACK);
	/* fill revoke->re_data */
	fop = &revoke->re_data;
	fop->cr_dir = 1; /* 1 means "revoke", 0 means "cancel". */
	fop->cr_loan = loan->rl_cookie;
	... encode right to fop->cr_right, using c2_rm_right_ops::rro_encode();
	borrow->bo_out.ou_fop.f_item->ri_ops = &revoke_ops;
	c2_rpc_post(&revoke->re_out.ou_fop.f_item);
}

const struct c2_rpc_item_ops borrow_ops = {
	.rio_replied = borrow_reply
};

const struct c2_rpc_item_ops revoke_ops = {
	.rio_replied = out_reply
};

/*
  This is called when a reply or timeout is received for a borrow fop.
 */
void borrow_reply(struct c2_rpc_item *item)
{
	c2_rm_borrow_done(&out->ou_req, out->ou_loan);
	out_reply(item);
}

/*
  This is called when a reply or timeout is received for a revoke fop.
 */
void revoke_reply(struct c2_rpc_item *item)
{
	c2_rm_revoke_done(&out->ou_req);
	out_reply(item);
}

void out_reply(struct c2_rpc_item *item)
{
	struct rm_out *out;

	out = container_of(item, struct rm_out, ou_fop.f_item);
	c2_rm_outgoing_complete(&out->ou_req, item->ri_rc);
	c2_free(out); /* assumes that rm_out is the first field in
			 rm_{borrow,revoke}. */
}

/*
 * Fom part.
 */

struct rm_borrow_fom {
	struct c2_fom                bom_fom;
	struct c2_rm_borrow_incoming bom_incoming;
};

/* one part of ->fo_state() for borrow fom. This is called when an incoming
   borrow fom is received. */
int borrow_state0(struct c2_fom *fom)
{
	struct rm_borrow_fom    *b;
	struct c2_fop_rm_borrow *data;
	struct c2_rm_right       right;

	b = container_of(fom, struct rm_borrow_fom, bom_fom);
	data = c2_fop_data(b->bom_fom.fo_fop);
	in = &b->bom_incoming.bi_incoming;
	C2_ALLOC_PTR(b->bom_incoming.bi_loan);
	c2_rm_incoming_init(in, c2_rm_owner_find(data->bo_owner.ow_cookie),
			    RIT_BORROW, RIP_NONE,
			    RIF_MAY_REVOKE|RIF_MAY_BORROW|RIF_LOCAL_WAIT);
	... decode right from data->bo_right;
	c2_rm_right_get(in);
	c2_fom_block_at(fom, &in->rin_signal);
	return FSO_WAIT;
}

/* second part of ->fo_state() for borrow fom. This is called when incoming
   request, created by borrow_state0() completes. */
int borrow_state0(struct c2_fom *fom)
{
	struct c2_fop_rm_borrow_rep *rep;

	if (in->rin_state != RI_SUCCESS) {
		... handle error, send error reply...;
	} else {
		c2_rm_borrow_commit(&b->bom_incoming);
		rep->br_rc = 0;
		c2_rm_owner_cookie(in->rin_want.ri_owner,
				   &rep->br_owner.ow_cookie);
		c2_rm_loan_cookie(b->bom_incoming.bi_loan,
				  &rep->br_loan.ow_cookie);
		... encode right to rep->br_right;
	}
	post reply;
}

static int out_request_send(struct c2_rm_outgoing *out)
{
	return 0;
}

/**
   Sends an outgoing request of type "otype" to a remote owner specified by
   "other" and with requested (or cancelled) right @right.
 */
int go_out(struct c2_rm_incoming *in, enum c2_rm_outgoing_type otype,
	   struct c2_rm_loan *loan, struct c2_rm_right *right)
{
	struct c2_rm_right    *scan;
	struct c2_rm_owner    *owner = in->rin_owner;
	struct c2_rm_outgoing *out;
	int		    result = 0;

	if (!right_is_empty(right) && result == 0) {
		C2_ALLOC_PTR(out);
		if (out != NULL) {
			out->rog_type  = otype;
			out->rog_owner = owner;
			scan = &loan->rl_right;
			c2_rm_right_init(scan, owner);
			result = right_copy(scan, right);
			result = result ?: pin_add(in, scan, RPF_TRACK);
			if (result == 0)
				ur_tlist_add(&owner->ro_outgoing[OQS_GROUND],
					     scan);
			else {
				c2_rm_right_fini(scan);
				c2_free(out);
			}
		} else
			result = -ENOMEM;
	}

	c2_rm_right_fini(right);

	return result ?: out_request_send(out);
}

static bool right_check(const struct c2_rm_right *right, void *datum)
{
	struct owner_invariant_state *s = datum;

	if (s->is_phase == OIS_BORROWED)
		s->is_credit.ri_ops->rro_join(&s->is_credit, right);
	if (s->is_phase == OIS_SUBLET || s->is_phase == OIS_OWNED)
		s->is_debit.ri_ops->rro_join(&s->is_credit, right);

	return s->is_owner == right->ri_owner &&
		right_invariant(right, s);
}


/* and similarly for revoke fom. */
