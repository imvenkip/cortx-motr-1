/* -*- C -*- */

#include "lib/types.h"            /* uint64_t */

#include "lib/user_space/assert.h"
#include "lib/ut.h"
#include "lib/ub.h"
#include "lib/memory.h"
#include "lib/thread.h"
#include "rm/rm.h"

#include "rmproto.h"
#include "rings.h"

#include <unistd.h>
/**
   @addtogroup rm

   @{
 */

/**
   @name Use cases and unit tests
 */

/** @{ */


static struct c2_rm_domain dom;

/*
 * A simple resource type for testing.
 */

static struct rings {
	struct c2_rm_resource rs_resource;
	uint64_t              rs_id;
} R;

/* from http://en.wikipedia.org/wiki/Rings_of_Power */
enum {
	/* Three Rings for the Elven-kings under the sky... */
	/* Narya, the Ring of Fire (set with a ruby) */
	NARYA = 1 << 0,
	/* Nenya, the Ring of Water or Ring of Adamant (made of mithril and set
	   with a "white stone") */
	NENYA = 1 << 1,
	/* and Vilya, the Ring of Air, the "mightiest of the Three" (made of
	   gold and set with a sapphire)*/
	VILYA = 1 << 2,

	/* Seven for the Dwarf-lords in their halls of stone... */
	/* Ring of Durin */
	DURIN = 1 << 3,
	/*Ring of Thror */
	THROR = 1 << 4,
	/* Unnamed gnome rings... */
	GR_2  = 1 << 5,
	GR_3  = 1 << 6,
	GR_4  = 1 << 7,
	GR_5  = 1 << 8,
	GR_6  = 1 << 9,

	/* Nine for Mortal Men doomed to die... */
	/* Witch-king of Angmar */
	ANGMAR = 1 << 10,
	/* Khamul */
	KHAMUL = 1 << 11,
	/* unnamed man rings... */
	MR_2   = 1 << 12,
	MR_3   = 1 << 13,
	MR_4   = 1 << 14,
	MR_5   = 1 << 15,
	MR_6   = 1 << 16,
	MR_7   = 1 << 17,
	MR_8   = 1 << 18,

	/* One for the Dark Lord on his dark throne */
	THE_ONE = 1 << 19,

	ALLRINGS = NARYA | NENYA | VILYA | DURIN | THROR | GR_2 |
	GR_3 | GR_4 | GR_5 | GR_6 | ANGMAR | KHAMUL | MR_2 | MR_3 | MR_4 |
	MR_5 | MR_6 | MR_7 | MR_8 | THE_ONE

};

static struct c2_rm_right everything;
static struct c2_rm_owner Sauron; /* owns all rings initially. */
static struct c2_rm_owner elves;
static struct c2_rm_owner dwarves;
static struct c2_rm_owner men;

static int                   result;
static struct c2_rm_incoming in;
static struct c2_rm_incoming inother;
static struct c2_rm_incoming inreq;
static struct c2_rm_incoming inrep;
static struct c2_rm_incoming inconflict;

static struct c2_rm_domain server;
static struct c2_rm_domain client;
static struct c2_rm_domain client1;
static struct rings RC0;
static struct rings RC1;

static struct c2_rm_resource_type rt = {
	.rt_ops  = &rings_rtype_ops,
	.rt_name = "rm ut resource type",
	.rt_id   = 1
};


static struct c2_rm_resource_type rts = {
        .rt_ops  = &rings_rtype_ops,
        .rt_name = "rm ut resource type",
        .rt_id   = 1
};

static struct c2_rm_resource_type rtc = {
        .rt_ops  = &rings_rtype_ops,
        .rt_name = "rm ut resource type",
        .rt_id   = 1
};

static struct c2_rm_resource_type rtc1 = {
        .rt_ops  = &rings_rtype_ops,
        .rt_name = "rm ut resource type",
        .rt_id   = 1
};

enum {
	UT_SERVER,
	UT_CLIENT0,
	UT_CLIENT1,
	UT_CLIENT2,
};


static void rm_init(void)
{
	rt.rt_id = 1;
	c2_rm_domain_init(&dom);
	c2_rm_type_register(&dom, &rt);
	c2_rm_resource_add(&rt, &R.rs_resource);
	c2_rm_right_init(&everything);
	everything.ri_ops = &rings_right_ops;
	everything.ri_datum = ALLRINGS;
	Sauron.ro_state = ROS_FINAL;
	c2_rm_owner_init_with(&Sauron, &R.rs_resource, &everything);
	elves.ro_state = ROS_FINAL;
	c2_rm_owner_init(&elves, &R.rs_resource);
	dwarves.ro_state = ROS_FINAL;
	c2_rm_owner_init(&dwarves, &R.rs_resource);
	men.ro_state = ROS_FINAL;
	c2_rm_owner_init(&men, &R.rs_resource);
	c2_rm_incoming_init(&in);
	c2_rm_incoming_init(&inother);
	c2_rm_incoming_init(&inreq);
	c2_rm_incoming_init(&inrep);
	c2_rm_incoming_init(&inconflict);
}

static void rm_fini(void)
{
	struct c2_rm_right *right;
	struct c2_rm_right *tmp_right;

	c2_rm_incoming_fini(&inother);
	c2_rm_incoming_fini(&in);
	c2_rm_incoming_fini(&inreq);
	c2_rm_incoming_fini(&inrep);
	c2_rm_incoming_fini(&inconflict);
	men.ro_state = ROS_FINAL;
	c2_rm_owner_fini(&men);
	dwarves.ro_state = ROS_FINAL;
	c2_rm_owner_fini(&dwarves);
	elves.ro_state = ROS_FINAL;
	c2_rm_owner_fini(&elves);
	Sauron.ro_state = ROS_FINAL;
	c2_list_del(&everything.ri_linkage);
	c2_list_for_each_entry_safe(&Sauron.ro_sublet, right, tmp_right,
				    struct c2_rm_right, ri_linkage) {
		c2_list_del(&right->ri_linkage);
	}

	c2_list_for_each_entry_safe(&Sauron.ro_owned[OWOS_CACHED], right,
				    tmp_right, struct c2_rm_right, ri_linkage) {
		c2_list_del(&right->ri_linkage);
	}
	c2_rm_owner_fini(&Sauron);
	c2_rm_right_fini(&everything);
	c2_rm_resource_del(&R.rs_resource);
	c2_rm_type_deregister(&rt);
	c2_rm_domain_fini(&dom);

}

static int rm_reset(void)
{
	return 0;
}

/**
   <b>Basic infrastructure setup.</b>
 */
static void basic_test(void)
{
	rm_init();
	rm_fini();
}

/**
   <b>right_get 0.</b>

   Simple get a right and immediately release it.
 */
static void right_get_test0(void)
{
	rm_init();

	c2_chan_init(&in.rin_signal);
	c2_rm_right_init(&in.rin_want);
	in.rin_state = RI_INITIALISED;
	in.rin_owner = &Sauron;
	in.rin_priority = 0;
	in.rin_ops = &rings_incoming_ops;
	in.rin_want.ri_ops = &rings_right_ops;
	in.rin_type = RIT_LOAN;
	in.rin_policy = RIP_INPLACE;
	in.rin_flags |= RIF_LOCAL_WAIT;

	in.rin_want.ri_datum = NARYA;
	result = c2_rm_right_get_wait(&Sauron, &in);
	C2_ASSERT(result == 0);
	C2_ASSERT(in.rin_state == RI_SUCCESS);

	c2_rm_right_put(&in);
	c2_list_del(&in.rin_want.ri_linkage);
	c2_rm_right_fini(&in.rin_want);
	c2_chan_fini(&in.rin_signal);

	rm_fini();
}

/**
   <b>right_get 1.</b>

   Get a right and then get a non-conflicting right.
 */
static void right_get_test1(void)
{
	rm_init();

	c2_chan_init(&in.rin_signal);
	c2_rm_right_init(&in.rin_want);
	in.rin_state = RI_INITIALISED;
	in.rin_owner = &Sauron;
	in.rin_priority = 0;
	in.rin_ops = &rings_incoming_ops;
	in.rin_want.ri_ops = &rings_right_ops;
	in.rin_type = RIT_LOAN;
	in.rin_policy = RIP_INPLACE;
	in.rin_flags |= RIF_LOCAL_WAIT;

	in.rin_want.ri_datum = NARYA;
	result = c2_rm_right_get_wait(&Sauron, &in);
	C2_ASSERT(result == 0);

	c2_chan_init(&inother.rin_signal);
	c2_rm_right_init(&inother.rin_want);
	inother.rin_state = RI_INITIALISED;
	inother.rin_owner = &Sauron;
	inother.rin_priority = 0;
	inother.rin_ops = &rings_incoming_ops;
	inother.rin_want.ri_ops = &rings_right_ops;
	inother.rin_type = RIT_LOAN;
	inother.rin_policy = RIP_INPLACE;
	inother.rin_flags |= RIF_LOCAL_WAIT;

	inother.rin_want.ri_datum = KHAMUL;
	result = c2_rm_right_get_wait(&Sauron, &inother);
	C2_ASSERT(result == 0);

	c2_rm_right_put(&in);
	c2_list_del(&in.rin_want.ri_linkage);
	c2_rm_right_fini(&in.rin_want);
	c2_chan_fini(&in.rin_signal);

	c2_rm_right_put(&inother);
	c2_list_del(&inother.rin_want.ri_linkage);
	c2_rm_right_fini(&inother.rin_want);
	c2_chan_fini(&inother.rin_signal);


	rm_fini();
}

/**
   <b>right_get 2.</b>

   Get a right and get a conflicting right. This should succeed, because local
   requests do not conflict by default.

   @see RIF_LOCAL_WAIT
 */
static void right_get_test2(void)
{
	rm_init();

	c2_chan_init(&in.rin_signal);
	c2_rm_right_init(&in.rin_want);
	in.rin_state = RI_INITIALISED;
	in.rin_owner = &Sauron;
	in.rin_priority = 0;
	in.rin_ops = &rings_incoming_ops;
	in.rin_want.ri_ops = &rings_right_ops;
	in.rin_type = RIT_LOCAL;
	in.rin_policy = RIP_INPLACE;
	in.rin_flags |= RIF_LOCAL_WAIT;

	in.rin_want.ri_datum = NARYA;
	result = c2_rm_right_get_wait(&Sauron, &in);
	C2_ASSERT(result == 0);

	c2_chan_init(&inother.rin_signal);
	c2_rm_right_init(&inother.rin_want);
	inother.rin_state = RI_INITIALISED;
	inother.rin_owner = &Sauron;
	inother.rin_priority = 0;
	inother.rin_ops = &rings_incoming_ops;
	inother.rin_want.ri_ops = &rings_right_ops;
	inother.rin_type = RIT_LOCAL;
	inother.rin_policy = RIP_INPLACE;
	inother.rin_flags |= RIF_LOCAL_WAIT;

	inother.rin_want.ri_datum = NARYA;
	result = c2_rm_right_get_wait(&Sauron, &inother);
	C2_ASSERT(result == 0);

	c2_rm_right_put(&in);
	c2_list_del(&in.rin_want.ri_linkage);
	c2_rm_right_fini(&in.rin_want);
	c2_chan_fini(&in.rin_signal);

	c2_rm_right_put(&inother);
	c2_list_del(&inother.rin_want.ri_linkage);
	c2_rm_right_fini(&inother.rin_want);
	c2_chan_fini(&inother.rin_signal);

	rm_fini();
}

/**
   <b>right_get 3.</b>

   Get a right and get a conflicting right with RIF_LOCAL_TRY flag. This should
   fail.

   @see RIF_LOCAL_TRY
 */
static void right_get_test3(void)
{
	rt.rt_id = 1;
	rm_init();

	c2_chan_init(&in.rin_signal);
	c2_rm_right_init(&in.rin_want);
	in.rin_state = RI_INITIALISED;
	in.rin_owner = &Sauron;
	in.rin_priority = 0;
	in.rin_ops = &rings_incoming_ops;
	in.rin_want.ri_ops = &rings_right_ops;
	in.rin_type = RIT_LOCAL;
	in.rin_policy = RIP_INPLACE;

	in.rin_flags = RIF_LOCAL_TRY;
	in.rin_want.ri_datum = NARYA;
	result = c2_rm_right_get_wait(&Sauron, &in);
	C2_ASSERT(result == 0);

	c2_chan_init(&inother.rin_signal);
	c2_rm_right_init(&inother.rin_want);
	inother.rin_state = RI_INITIALISED;
	inother.rin_owner = &Sauron;
	inother.rin_priority = 0;
	inother.rin_ops = &rings_incoming_ops;
	inother.rin_want.ri_ops = &rings_right_ops;
	inother.rin_type = RIT_LOAN;
	inother.rin_policy = RIP_INPLACE;

	inother.rin_want.ri_datum = NARYA;
	inother.rin_flags = RIF_LOCAL_TRY;
	result = c2_rm_right_get_wait(&Sauron, &inother);
	C2_ASSERT(result == -EWOULDBLOCK);

	//c2_rm_right_put(&inother);
	c2_list_del(&inother.rin_want.ri_linkage);
	c2_rm_right_fini(&inother.rin_want);
	c2_chan_fini(&inother.rin_signal);

	c2_rm_right_put(&in);
	c2_list_del(&in.rin_want.ri_linkage);
	c2_rm_right_fini(&in.rin_want);
	c2_chan_fini(&in.rin_signal);

	rm_fini();
}

static void rm_server_init(void)
{
	rts.rt_id = 1;
	strcpy(rm_info[UT_SERVER].name, "SERVER");
	rm_info[UT_SERVER].owner_id = UT_SERVER;
	rm_info[UT_SERVER].owner = &Sauron;
	rm_info[UT_SERVER].res = &R.rs_resource;
	rm_info[UT_SERVER].req_owner_id = UT_CLIENT0;
	c2_queue_init(&rm_info[UT_SERVER].owner_queue);
	c2_mutex_init(&rm_info[UT_SERVER].oq_lock);

        c2_rm_domain_init(&server);
        c2_rm_type_register(&server, &rts);
        c2_rm_resource_add(&rts, &R.rs_resource);
        c2_rm_right_init(&everything);
        everything.ri_ops = &rings_right_ops;
        everything.ri_datum = ALLRINGS;
        Sauron.ro_state = ROS_FINAL;
        c2_rm_owner_init_with(&Sauron, &R.rs_resource, &everything);
}

static void rm_server_fini(void)
{
	struct c2_rm_right *right;
	struct c2_rm_right *tmp_right;

        Sauron.ro_state = ROS_FINAL;
        c2_list_del(&everything.ri_linkage);

	c2_list_for_each_entry_safe(&Sauron.ro_sublet, right, tmp_right,
				    struct c2_rm_right, ri_linkage) {
		c2_list_del(&right->ri_linkage);
	}

	c2_list_for_each_entry_safe(&Sauron.ro_owned[OWOS_CACHED], right,
				    tmp_right, struct c2_rm_right, ri_linkage) {
		c2_list_del(&right->ri_linkage);
	}

        c2_rm_owner_fini(&Sauron);
        c2_rm_right_fini(&everything);
        c2_rm_resource_del(&R.rs_resource);
        c2_rm_type_deregister(&rts);
        c2_rm_domain_fini(&server);

	c2_queue_fini(&rm_info[UT_SERVER].owner_queue);
	c2_mutex_fini(&rm_info[UT_SERVER].oq_lock);
}

static void rm_client_init(void)
{
	rtc.rt_id = 1;
	strcpy(rm_info[UT_CLIENT0].name, "CLIENT0");
	rm_info[UT_CLIENT0].owner_id = UT_CLIENT0;
	rm_info[UT_CLIENT0].owner = &elves;
	rm_info[UT_CLIENT0].res = &RC0.rs_resource;
	rm_info[UT_CLIENT0].req_owner_id = UT_SERVER;
	c2_queue_init(&rm_info[UT_CLIENT0].owner_queue);
	c2_mutex_init(&rm_info[UT_CLIENT0].oq_lock);

        c2_rm_domain_init(&client);
        c2_rm_type_register(&client, &rtc);
        c2_rm_resource_add(&rtc, &RC0.rs_resource);
        elves.ro_state = ROS_FINAL;
        c2_rm_owner_init(&elves, &RC0.rs_resource);
}

static void rm_client_fini(void)
{
	struct c2_rm_right *right;
	struct c2_rm_right *tmp_right;

	c2_list_for_each_entry_safe(&elves.ro_sublet, right, tmp_right,
				    struct c2_rm_right, ri_linkage) {
		c2_list_del(&right->ri_linkage);
	}

	c2_list_for_each_entry_safe(&elves.ro_owned[OWOS_CACHED], right,
				    tmp_right, struct c2_rm_right, ri_linkage) {
		c2_list_del(&right->ri_linkage);
	}

	c2_list_for_each_entry_safe(&elves.ro_borrowed, right,
				    tmp_right, struct c2_rm_right, ri_linkage) {
		c2_list_del(&right->ri_linkage);
	}

        elves.ro_state = ROS_FINAL;
        c2_rm_owner_fini(&elves);
        c2_rm_resource_del(&RC0.rs_resource);
        c2_rm_type_deregister(&rtc);
        c2_rm_domain_fini(&client);

	c2_queue_fini(&rm_info[UT_CLIENT0].owner_queue);
	c2_mutex_fini(&rm_info[UT_CLIENT0].oq_lock);
}

static void rm_client1_init(void)
{
	rtc1.rt_id = 1;
	strcpy(rm_info[UT_CLIENT1].name, "CLIENT1");
	rm_info[UT_CLIENT1].owner_id = UT_CLIENT1;
	rm_info[UT_CLIENT1].owner = &dwarves;
	rm_info[UT_CLIENT1].res = &RC1.rs_resource;
	rm_info[UT_CLIENT1].req_owner_id = UT_SERVER;
	c2_queue_init(&rm_info[UT_CLIENT1].owner_queue);
	c2_mutex_init(&rm_info[UT_CLIENT1].oq_lock);

        c2_rm_domain_init(&client1);
        c2_rm_type_register(&client1, &rtc1);
        c2_rm_resource_add(&rtc1, &RC1.rs_resource);
        dwarves.ro_state = ROS_FINAL;
        c2_rm_owner_init(&dwarves, &RC1.rs_resource);
}

static void rm_client1_fini(void)
{
	struct c2_rm_right *right;
	struct c2_rm_right *tmp_right;

	c2_list_for_each_entry_safe(&dwarves.ro_sublet, right, tmp_right,
				    struct c2_rm_right, ri_linkage) {
		c2_list_del(&right->ri_linkage);
	}

	c2_list_for_each_entry_safe(&dwarves.ro_owned[OWOS_CACHED], right,
				    tmp_right, struct c2_rm_right, ri_linkage) {
		c2_list_del(&right->ri_linkage);
	}

	c2_list_for_each_entry_safe(&dwarves.ro_borrowed, right,
				    tmp_right, struct c2_rm_right, ri_linkage) {
		c2_list_del(&right->ri_linkage);
	}

        dwarves.ro_state = ROS_FINAL;
        c2_rm_owner_fini(&dwarves);
        c2_rm_resource_del(&RC1.rs_resource);
        c2_rm_type_deregister(&rtc1);
        c2_rm_domain_fini(&client1);

	c2_queue_fini(&rm_info[UT_CLIENT1].owner_queue);
	c2_mutex_fini(&rm_info[UT_CLIENT1].oq_lock);
}

static void intent_server(int id)
{
        c2_chan_init(&in.rin_signal);
        c2_rm_right_init(&in.rin_want);
        in.rin_state = RI_INITIALISED;
        in.rin_owner = &Sauron;
        in.rin_priority = 0;
        in.rin_ops = &rings_incoming_ops;
        in.rin_want.ri_ops = &rings_right_ops;
        in.rin_type = RIT_LOAN;
        in.rin_policy = RIP_INPLACE;

        in.rin_flags |= RIF_LOCAL_TRY;
        in.rin_want.ri_datum = NARYA;
        result = c2_rm_right_get_wait(&Sauron, &in);
        C2_ASSERT(result == 0);

        c2_rm_right_put(&in);
        c2_list_del(&in.rin_want.ri_linkage);
        c2_rm_right_fini(&in.rin_want);
        c2_chan_fini(&in.rin_signal);

}

static void intent_client(int id)
{
	struct c2_rm_req_reply *req = NULL;
	struct c2_queue_link *link = NULL;
	struct c2_rm_proto_info *info = &rm_info[id];

	while (link == NULL)
	{
		c2_mutex_lock(&info->oq_lock);
        	link = c2_queue_get(&info->owner_queue);
        	c2_mutex_unlock(&info->oq_lock);
		req = container_of(link, struct c2_rm_req_reply, rq_link);
	}

        req->in.rin_owner = &elves;
        req->in.rin_priority = 0;
        req->in.rin_ops = &rings_incoming_ops;
        req->in.rin_want.ri_ops = &rings_right_ops;
        req->in.rin_type = RIT_LOAN;
        req->in.rin_policy = RIP_INPLACE;
        req->in.rin_want.ri_datum = NARYA;
        req->in.rin_flags |= RIF_LOCAL_TRY;
        req->in.rin_state = RI_SUCCESS;

	c2_rm_right_put(&req->in);
        c2_list_del(&req->in.rin_want.ri_linkage);
        c2_rm_right_fini(&req->in.rin_want);
        c2_chan_fini(&req->in.rin_signal);

	c2_mutex_lock(&rpc_lock);
	req->type = PRO_REQ_FINISH;
        c2_queue_put(&rpc_queue, &req->rq_link);
        c2_mutex_unlock(&rpc_lock);
}

/**
   <b>Intent mode.</b>

   - setup two resource domains CLIENT and SERVER;

   - acquire a right on SERVER (on client's behalf) and return it to CLIENT;

   - release the right on CLIENT.
 */
static void intent_mode_test(void)
{
	int i;

	c2_rm_rpc_init();
	rm_server_init();
	rm_client_init();

	rpc_signal = 0;

	result = C2_THREAD_INIT(&rpc_handle, int,
				NULL, &rpc_process, UT_SERVER);
	C2_ASSERT(result == 0);

	result = C2_THREAD_INIT(&rm_info[UT_SERVER].rm_handle, int,
				NULL, &intent_server, UT_SERVER);
	C2_ASSERT(result == 0);

	result = C2_THREAD_INIT(&rm_info[UT_CLIENT0].rm_handle, int,
				NULL, &intent_client, UT_CLIENT0);
	C2_ASSERT(result == 0);

	for (i = 0; i < 2; ++i) {
		c2_thread_join(&rm_info[i].rm_handle);
		c2_thread_fini(&rm_info[i].rm_handle);
	}

	rpc_signal = 1;
	c2_thread_join(&rpc_handle);
	c2_thread_fini(&rpc_handle);

	rm_client_fini();
	rm_server_fini();
	c2_rm_rpc_fini();
}

static void wbc_server(int id)
{
	struct c2_rm_req_reply *req = NULL;
	struct c2_queue_link *link = NULL;

	while (link == NULL)
	{
		c2_mutex_lock(&rm_info[UT_SERVER].oq_lock);
        	link = c2_queue_get(&rm_info[UT_SERVER].owner_queue);
        	c2_mutex_unlock(&rm_info[UT_SERVER].oq_lock);
		req = container_of(link, struct c2_rm_req_reply, rq_link);
	}

        c2_chan_init(&req->in.rin_signal);
        c2_rm_right_init(&req->in.rin_want);
        req->in.rin_state = RI_INITIALISED;
        req->in.rin_owner = &Sauron;
        req->in.rin_priority = 0;
        req->in.rin_ops = &rings_incoming_ops;
        req->in.rin_want.ri_ops = &rings_right_ops;
        req->in.rin_type = RIT_LOAN;
        req->in.rin_policy = RIP_INPLACE;

        req->in.rin_flags = RIF_LOCAL_WAIT;
        req->in.rin_want.ri_datum = VILYA;
        result = c2_rm_right_get_wait(&Sauron, &req->in);
        C2_ASSERT(result == 0);

	req->sig_id = UT_SERVER;
	req->reply_id = UT_CLIENT0;
	req->type = PRO_LOAN_REPLY;	
	req->right.ri_datum = VILYA;
	req->right.ri_ops = &rings_right_ops;
	c2_mutex_lock(&rpc_lock);
	c2_queue_put(&rpc_queue, &req->rq_link);
	c2_mutex_unlock(&rpc_lock);
	printf("Wbc Server\n");
}

static void wbc_client(int id)
{
	/* Prepare incoming request for client which will eventually
	 * barrow rights from server */
	c2_chan_init(&inother.rin_signal);
        c2_rm_right_init(&inother.rin_want);
        inother.rin_state = RI_INITIALISED;
        inother.rin_owner = &elves;
        inother.rin_priority = 0;
        inother.rin_ops = &rings_incoming_ops;
        inother.rin_want.ri_ops = &rings_right_ops;
        inother.rin_type = RIT_LOCAL;
        inother.rin_policy = RIP_INPLACE;
        inother.rin_want.ri_datum = VILYA;
        inother.rin_flags = RIF_MAY_BORROW;
        result = c2_rm_right_get_wait(&elves, &inother);
        C2_ASSERT(result == 0);

	printf("Wbc client\n");
	c2_rm_right_put(&inother);
        c2_list_del(&inother.rin_want.ri_linkage);
        c2_rm_right_fini(&inother.rin_want);
        c2_chan_fini(&inother.rin_signal);
}

/**
   <b>WBC mode.</b>

   - setup two resource domains CLIENT and SERVER;

   - borrow a right from SERVER to client. Use it;

   - release the right.
 */
static void wbc_mode_test(void)
{
	int i;

	c2_rm_rpc_init();
	rm_server_init();
	rm_client_init();

	rpc_signal = 0;

	result = C2_THREAD_INIT(&rpc_handle, int,
				NULL, &rpc_process, UT_SERVER);
	C2_ASSERT(result == 0);

	result = C2_THREAD_INIT(&rm_info[UT_SERVER].rm_handle, int,
				NULL, &wbc_server, UT_SERVER);
	C2_ASSERT(result == 0);

	result = C2_THREAD_INIT(&rm_info[UT_CLIENT0].rm_handle, int,
				NULL, &wbc_client, UT_CLIENT0);
	C2_ASSERT(result == 0);

	for (i = 0; i < 2; ++i) {
		c2_thread_join(&rm_info[i].rm_handle);
		c2_thread_fini(&rm_info[i].rm_handle);
	}
	
	rpc_signal = 1;
	c2_thread_join(&rpc_handle);
	c2_thread_fini(&rpc_handle);

	rm_client_fini();
	rm_server_fini();
	c2_rm_rpc_fini();
}

/**
   <b>Resource manager separate from resource provider.<b>
 */
static void separate_test(void)
{
}

/**
   <b>Network partitioning, leases.</b>
 */
static void lease_test(void)
{
}

/**
   <b>Optimistic concurrency.</b>
 */
static void optimist_test(void)
{
}

/**
   <b>A use case for each resource type.</b>
 */
static void resource_type_test(void)
{
}

/**
   <b>Use cases involving multiple resources acquired.</b>
 */
static void multiple_test(void)
{
}

static void cancel_server(int id)
{
	struct c2_rm_req_reply *req = NULL;
	struct c2_queue_link *link = NULL;

	while (link == NULL)
	{
		c2_mutex_lock(&rm_info[id].oq_lock);
        	link = c2_queue_get(&rm_info[id].owner_queue);
        	c2_mutex_unlock(&rm_info[id].oq_lock);
		req = container_of(link, struct c2_rm_req_reply, rq_link);
	}

        c2_chan_init(&req->in.rin_signal);
        c2_rm_right_init(&req->in.rin_want);
        req->in.rin_state = RI_INITIALISED;
        req->in.rin_owner = &Sauron;
        req->in.rin_priority = 0;
        req->in.rin_ops = &rings_incoming_ops;
        req->in.rin_want.ri_ops = &rings_right_ops;
        req->in.rin_type = RIT_LOAN;
        req->in.rin_policy = RIP_INPLACE;

        req->in.rin_flags = RIF_LOCAL_WAIT;
        req->in.rin_want.ri_datum = DURIN;
        result = c2_rm_right_get_wait(&Sauron, &req->in);
        C2_ASSERT(result == 0);

	req->type = PRO_LOAN_REPLY;
	req->right.ri_datum = DURIN;
	req->right.ri_ops = &rings_right_ops;
	c2_mutex_lock(&rpc_lock);
	c2_queue_put(&rpc_queue, &req->rq_link);
	c2_mutex_unlock(&rpc_lock);

	link = NULL;
	while (link == NULL)
	{
		c2_mutex_lock(&rm_info[id].oq_lock);
        	link = c2_queue_get(&rm_info[id].owner_queue);
        	c2_mutex_unlock(&rm_info[id].oq_lock);
		req = container_of(link, struct c2_rm_req_reply, rq_link);
	}

	/* Ask for same right which will be revoked */
        c2_chan_init(&req->in.rin_signal);
        c2_rm_right_init(&req->in.rin_want);
        req->in.rin_state = RI_INITIALISED;
        req->in.rin_owner = &Sauron;
        req->in.rin_priority = 0;
        req->in.rin_ops = &rings_incoming_ops;
        req->in.rin_want.ri_ops = &rings_right_ops;
        req->in.rin_type = RIT_REVOKE;
        req->in.rin_policy = RIP_INPLACE;

        req->in.rin_flags = RIF_MAY_REVOKE;
        req->in.rin_want.ri_datum = DURIN;
        result = c2_rm_right_get_wait(&Sauron, &req->in);
        C2_ASSERT(result == 0);
}

static void cancel_client(int id)
{
	struct c2_rm_req_reply *req = NULL;
	struct c2_queue_link *link = NULL;

	/* Prepare incoming request for client which will eventually
	 * barrow rights from server */
	c2_chan_init(&inother.rin_signal);
        c2_rm_right_init(&inother.rin_want);
        inother.rin_state = RI_INITIALISED;
        inother.rin_owner = &elves;
        inother.rin_priority = 0;
        inother.rin_ops = &rings_incoming_ops;
        inother.rin_want.ri_ops = &rings_right_ops;
        inother.rin_type = RIT_LOCAL;
        inother.rin_policy = RIP_INPLACE;
        inother.rin_want.ri_datum = DURIN;
        inother.rin_flags = RIF_MAY_BORROW;
        result = c2_rm_right_get_wait(&elves, &inother);
        C2_ASSERT(result == 0);

	while (link == NULL)
	{
		c2_mutex_lock(&rm_info[id].oq_lock);
        	link = c2_queue_get(&rm_info[id].owner_queue);
        	c2_mutex_unlock(&rm_info[id].oq_lock);
		req = container_of(link, struct c2_rm_req_reply, rq_link);
	}

        c2_chan_init(&req->in.rin_signal);
        c2_rm_right_init(&req->in.rin_want);
        req->in.rin_state = RI_INITIALISED;
        req->in.rin_owner = &elves;
        req->in.rin_priority = 0;
        req->in.rin_ops = &rings_incoming_ops;
        req->in.rin_want.ri_ops = &rings_right_ops;
        req->in.rin_type = RIT_REVOKE;
        req->in.rin_policy = RIP_INPLACE;

        req->in.rin_flags = RIF_LOCAL_WAIT;
        req->in.rin_want.ri_datum = DURIN;
        result = c2_rm_right_get_wait(&elves, &req->in);
        C2_ASSERT(result == 0);

	req->type = PRO_LOAN_REPLY;
	req->right.ri_datum = DURIN;
	req->right.ri_ops = &rings_right_ops;
	c2_mutex_lock(&rpc_lock);
	c2_queue_put(&rpc_queue, &req->rq_link);
	c2_mutex_unlock(&rpc_lock);
}

/**
   <b>Cancellation.</b>

   - setup two resource domains CLIENT and SERVER;

   - borrow a right R from SERVER to CLIENT;

   - cancel R;

   - witness SERVER possesses R.
 */
static void cancel_test(void)
{
	int i;

	c2_rm_rpc_init();
	rm_server_init();
	rm_client_init();

	rpc_signal = 0;

	result = C2_THREAD_INIT(&rpc_handle, int,
				NULL, &rpc_process, UT_SERVER);
	C2_ASSERT(result == 0);

	result = C2_THREAD_INIT(&rm_info[UT_SERVER].rm_handle, int,
				NULL, &cancel_server, UT_SERVER);
	C2_ASSERT(result == 0);

	result = C2_THREAD_INIT(&rm_info[UT_CLIENT0].rm_handle, int,
				NULL, &cancel_client, UT_CLIENT0);
	C2_ASSERT(result == 0);

	for (i = 0; i < 2; ++i) {
		c2_thread_join(&rm_info[i].rm_handle);
		c2_thread_fini(&rm_info[i].rm_handle);
	}

	rpc_signal = 1;
	c2_thread_join(&rpc_handle);
	c2_thread_fini(&rpc_handle);
	
	rm_client_fini();
	rm_server_fini();
	c2_rm_rpc_fini();
}

static void caching_server(int id)
{
	struct c2_rm_req_reply *req = NULL;
	struct c2_queue_link *link = NULL;

	while (link == NULL)
	{
		c2_mutex_lock(&rm_info[id].oq_lock);
        	link = c2_queue_get(&rm_info[id].owner_queue);
        	c2_mutex_unlock(&rm_info[id].oq_lock);
		req = container_of(link, struct c2_rm_req_reply, rq_link);
	}

        c2_chan_init(&req->in.rin_signal);
        c2_rm_right_init(&req->in.rin_want);
        req->in.rin_state = RI_INITIALISED;
        req->in.rin_owner = &Sauron;
        req->in.rin_priority = 0;
        req->in.rin_ops = &rings_incoming_ops;
        req->in.rin_want.ri_ops = &rings_right_ops;
        req->in.rin_type = RIT_LOAN;
        req->in.rin_policy = RIP_INPLACE;

        req->in.rin_flags = RIF_LOCAL_WAIT;
        req->in.rin_want.ri_datum = THROR;
        result = c2_rm_right_get_wait(&Sauron, &req->in);
        C2_ASSERT(result == 0);

	req->type = PRO_LOAN_REPLY;
	req->right.ri_datum = THROR;
	req->right.ri_ops = &rings_right_ops;
	c2_mutex_lock(&rpc_lock);
	c2_queue_put(&rpc_queue, &req->rq_link);
	c2_mutex_unlock(&rpc_lock);
}

static void caching_client(int id)
{
	/* Prepare incoming request for client which will eventually
	 * barrow rights from server */
	c2_chan_init(&inother.rin_signal);
        c2_rm_right_init(&inother.rin_want);
        inother.rin_state = RI_INITIALISED;
        inother.rin_owner = &elves;
        inother.rin_priority = 0;
        inother.rin_ops = &rings_incoming_ops;
        inother.rin_want.ri_ops = &rings_right_ops;
        inother.rin_type = RIT_LOCAL;
        inother.rin_policy = RIP_INPLACE;
        inother.rin_want.ri_datum = THROR;
        inother.rin_flags = RIF_MAY_BORROW;
        result = c2_rm_right_get_wait(&elves, &inother);
        C2_ASSERT(result == 0);

	c2_rm_right_put(&inother);
        c2_list_del(&inother.rin_want.ri_linkage);
        c2_rm_right_fini(&inother.rin_want);
        c2_chan_fini(&inother.rin_signal);

        c2_rm_right_init(&inrep.rin_want);
        inrep.rin_state = RI_INITIALISED;
        inrep.rin_owner = &elves;
        inrep.rin_priority = 0;
        inrep.rin_ops = &rings_incoming_ops;
        inrep.rin_want.ri_ops = &rings_right_ops;
        inrep.rin_type = RIT_LOCAL;
        inrep.rin_policy = RIP_INPLACE;

        inrep.rin_flags = RIF_LOCAL_WAIT;
        inrep.rin_want.ri_datum = THROR;
        result = c2_rm_right_get_wait(&elves, &inrep);
        C2_ASSERT(result == 0);

        c2_rm_right_put(&inrep);
        c2_list_del(&inrep.rin_want.ri_linkage);
        c2_rm_right_fini(&inrep.rin_want);
        c2_chan_fini(&inrep.rin_signal);
}

/**
   <b>Caching.</b>

   - setup two resource domains CLIENT and SERVER;

   - borrow a right R from SERVER to CLIENT;

   - release R;

   - get R again on CLIENT, witness it is found in cache.
 */
static void caching_test(void)
{
	int i;

	c2_rm_rpc_init();
	rm_server_init();
	rm_client_init();

	rpc_signal = 0;

	result = C2_THREAD_INIT(&rpc_handle, int,
				NULL, &rpc_process, UT_SERVER);
	C2_ASSERT(result == 0);

	result = C2_THREAD_INIT(&rm_info[UT_SERVER].rm_handle, int,
				NULL, &caching_server, UT_SERVER);
	C2_ASSERT(result == 0);

	result = C2_THREAD_INIT(&rm_info[UT_CLIENT0].rm_handle, int,
				NULL, &caching_client, UT_CLIENT0);
	C2_ASSERT(result == 0);

	for (i = 0; i < 2; ++i) {
		c2_thread_join(&rm_info[i].rm_handle);
		c2_thread_fini(&rm_info[i].rm_handle);
	}
	
	rpc_signal = 1;
	c2_thread_join(&rpc_handle);
	c2_thread_fini(&rpc_handle);

	rm_client_fini();
	rm_server_fini();
	c2_rm_rpc_fini();
}

static void callback_server(int id)
{
	struct c2_rm_req_reply *req = NULL;
	struct c2_queue_link *link = NULL;

	while (link == NULL)
	{
		c2_mutex_lock(&rm_info[id].oq_lock);
        	link = c2_queue_get(&rm_info[id].owner_queue);
        	c2_mutex_unlock(&rm_info[id].oq_lock);
		req = container_of(link, struct c2_rm_req_reply, rq_link);
	}

        c2_chan_init(&req->in.rin_signal);
        c2_rm_right_init(&req->in.rin_want);
        req->in.rin_state = RI_INITIALISED;
        req->in.rin_owner = &Sauron;
        req->in.rin_priority = 0;
        req->in.rin_ops = &rings_incoming_ops;
        req->in.rin_want.ri_ops = &rings_right_ops;
        req->in.rin_type = RIT_LOAN;
        req->in.rin_policy = RIP_INPLACE;

        req->in.rin_flags = RIF_LOCAL_WAIT;
        req->in.rin_want.ri_datum = ANGMAR;
        result = c2_rm_right_get_wait(&Sauron, &req->in);
        C2_ASSERT(result == 0);

	req->type = PRO_LOAN_REPLY;
	req->right.ri_datum = ANGMAR;
	req->right.ri_ops = &rings_right_ops;
	c2_mutex_lock(&rpc_lock);
	c2_queue_put(&rpc_queue, &req->rq_link);
	c2_mutex_unlock(&rpc_lock);
	c2_mutex_lock(&rm_info[UT_SERVER].oq_lock);

	link = NULL;
	while (link == NULL)
	{
		c2_mutex_lock(&rm_info[id].oq_lock);
        	link = c2_queue_get(&rm_info[id].owner_queue);
        	c2_mutex_unlock(&rm_info[id].oq_lock);
		req = container_of(link, struct c2_rm_req_reply, rq_link);
	}

	/* Ask for same right which will be revoked */
        c2_chan_init(&req->in.rin_signal);
        c2_rm_right_init(&req->in.rin_want);
        req->in.rin_state = RI_INITIALISED;
        req->in.rin_owner = &Sauron;
        req->in.rin_priority = 0;
        req->in.rin_ops = &rings_incoming_ops;
        req->in.rin_want.ri_ops = &rings_right_ops;
        req->in.rin_type = RIT_REVOKE;
        req->in.rin_policy = RIP_INPLACE;

        req->in.rin_flags = RIF_MAY_REVOKE;
        req->in.rin_want.ri_datum = ANGMAR;
	printf("Server 1\n");
        result = c2_rm_right_get_wait(&Sauron, &req->in);
        C2_ASSERT(result == 0);
	printf("Server 2\n");

        c2_rm_right_put(&req->in);
        c2_list_del(&req->in.rin_want.ri_linkage);
        c2_rm_right_fini(&req->in.rin_want);
        c2_chan_fini(&req->in.rin_signal);
}

static void callback_client(int id)
{
	struct c2_rm_req_reply *req = NULL;
	struct c2_queue_link *link = NULL;

	/* Prepare incoming request for client which will eventually
	 * barrow rights from server */
	c2_chan_init(&inother.rin_signal);
        c2_rm_right_init(&inother.rin_want);
        inother.rin_state = RI_INITIALISED;
        inother.rin_owner = &elves;
        inother.rin_priority = 0;
        inother.rin_ops = &rings_incoming_ops;
        inother.rin_want.ri_ops = &rings_right_ops;
        inother.rin_type = RIT_LOCAL;
        inother.rin_policy = RIP_INPLACE;
        inother.rin_want.ri_datum = ANGMAR;
        inother.rin_flags = RIF_MAY_BORROW;
        result = c2_rm_right_get_wait(&elves, &inother);
        C2_ASSERT(result == 0);

	while (link == NULL)
	{
		c2_mutex_lock(&rm_info[id].oq_lock);
        	link = c2_queue_get(&rm_info[id].owner_queue);
        	c2_mutex_unlock(&rm_info[id].oq_lock);
		req = container_of(link, struct c2_rm_req_reply, rq_link);
	}

        c2_chan_init(&req->in.rin_signal);
        c2_rm_right_init(&req->in.rin_want);
        req->in.rin_state = RI_INITIALISED;
        req->in.rin_owner = &elves;
        req->in.rin_priority = 0;
        req->in.rin_ops = &rings_incoming_ops;
        req->in.rin_want.ri_ops = &rings_right_ops;
        req->in.rin_type = RIT_REVOKE;
        req->in.rin_policy = RIP_INPLACE;

        req->in.rin_flags = RIF_LOCAL_WAIT;
        req->in.rin_want.ri_datum = ANGMAR;
	printf("client 1\n");
        result = c2_rm_right_get_wait(&elves, &req->in);
        C2_ASSERT(result == 0);
	printf("client 2\n");

	req->type = PRO_LOAN_REPLY;
	req->right.ri_datum = ANGMAR;
	req->right.ri_ops = &rings_right_ops;
	c2_mutex_lock(&rpc_lock);
	c2_queue_put(&rpc_queue, &req->rq_link);
	c2_mutex_unlock(&rpc_lock);

	c2_rm_right_put(&inother);
        c2_list_del(&inother.rin_want.ri_linkage);
        c2_rm_right_fini(&inother.rin_want);
        c2_chan_fini(&inother.rin_signal);
}

static void callback_client1(int id)
{
	c2_chan_init(&inconflict.rin_signal);
        c2_rm_right_init(&inconflict.rin_want);
        inconflict.rin_state = RI_INITIALISED;
        inconflict.rin_owner = &dwarves;
        inconflict.rin_priority = 0;
        inconflict.rin_ops = &rings_incoming_ops;
        inconflict.rin_want.ri_ops = &rings_right_ops;
        inconflict.rin_type = RIT_LOCAL;
        inconflict.rin_policy = RIP_INPLACE;
        inconflict.rin_want.ri_datum = ANGMAR;
        inconflict.rin_flags = RIF_MAY_REVOKE;
	printf("client1 1\n");
        result = c2_rm_right_get_wait(&dwarves, &inconflict);
        C2_ASSERT(result == 0);
	printf("client1 2\n");

	c2_rm_right_put(&inconflict);
        c2_list_del(&inconflict.rin_want.ri_linkage);
        c2_rm_right_fini(&inconflict.rin_want);
        c2_chan_fini(&inconflict.rin_signal);
}

/**
   <b>Call-back.</b>

   - setup 3 resource domains: SERVER, CLIENT0 and CLIENT1;

   - borrow a right from SERVER to CLIENT0;

   - request a conflicting right to CLIENT1;

   - witness a REVOKE call-back to CLIENT0;

   - cancel the right on CLIENT0;

   - obtain the right on CLIENT1;

   - release the right on CLIENT1.
 */
static void callback_test(void)
{
	int i;

	c2_rm_rpc_init();
	rm_server_init();
	rm_client_init();
	rm_client1_init();

	rpc_signal = 0;

	result = C2_THREAD_INIT(&rpc_handle, int,
				NULL, &rpc_process, UT_SERVER);
	C2_ASSERT(result == 0);

	result = C2_THREAD_INIT(&rm_info[UT_SERVER].rm_handle, int,
				NULL, &callback_server, UT_SERVER);
	C2_ASSERT(result == 0);

	result = C2_THREAD_INIT(&rm_info[UT_CLIENT0].rm_handle, int,
				NULL, &callback_client, UT_CLIENT0);
	C2_ASSERT(result == 0);

	result = C2_THREAD_INIT(&rm_info[UT_CLIENT1].rm_handle, int,
				NULL, &callback_client1, UT_CLIENT1);
	C2_ASSERT(result == 0);

	for (i = 0; i < 3; ++i) {
		c2_thread_join(&rm_info[i].rm_handle);
		c2_thread_fini(&rm_info[i].rm_handle);
	}
	
	rpc_signal = 1;
	c2_thread_join(&rpc_handle);
	c2_thread_fini(&rpc_handle);

	rm_client1_fini();
	rm_client_fini();
	rm_server_fini();
	c2_rm_rpc_fini();
}

/**
   <b>Hierarchy.</b>
 */
static void hierarchy_test(void)
{
}

/**
   <b>Extents and multiple readers-writers (scalability).</b>
 */
static void ext_multi_test(void)
{
}


const struct c2_test_suite rm_ut = {
	.ts_name = "librm-ut",
	.ts_init = rm_reset,
	.ts_fini = rm_reset,
	.ts_tests = {
		{ "basic", basic_test },
		{ "right_get0", right_get_test0 },
		{ "right_get1", right_get_test1 },
		{ "right_get2", right_get_test2 },
		{ "right_get3", right_get_test3 },
		{ "intent mode", intent_mode_test },
		{ "wbc", wbc_mode_test },
		{ "separate", separate_test },
		{ "lease", lease_test },
		{ "optimist", optimist_test },
		{ "resource type", resource_type_test },
		{ "multiple", multiple_test },
		{ "cancel", cancel_test },
		{ "caching", caching_test },
		{ "callback", callback_test },
		{ "hierarchy", hierarchy_test },
		{ "extents multiple", ext_multi_test },
		{ NULL, NULL }
	}
};

/** @} end of use cases and unit tests */

/*
 * UB
 */

enum {
	UB_ITER = 200000,
};

static void ub_init(void)
{
}

static void ub_fini(void)
{
}

struct c2_ub_set c2_rm_ub = {
	.us_name = "rm-ub",
	.us_init = ub_init,
	.us_fini = ub_fini,
	.us_run  = {
		{ .ut_name = NULL }
	}
};

/** @} end of rm group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
