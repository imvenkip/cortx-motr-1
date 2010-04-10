#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <colibri/colibri.h>

static int 
c2_cm_storage_in_agent_init(struct c2_cm_agent *self, struct c2_cm *parent)
{
	struct c2_cm_storage_in_agent *ci_agent = container_of(self,
					struct c2_cm_storage_in_agent, ci_agent);
	self->ag_parent = parent;
	self->ag_quit = 0;
	return 0;
}

static int c2_cm_storage_in_agent_stop(struct c2_cm_agent *self, int force)
{
	struct c2_cm_storage_in_agent *ci_agent = container_of(self,
					struct c2_cm_storage_in_agent, ci_agent);
	
	self->ag_quit = 1;
	return 0;
}

static int c2_cm_storage_in_agent_config(struct c2_cm_agent *self,
					 struct c2_cm_agent_config *config)
{
	struct c2_cm_storage_in_agent *ci_agent = container_of(self,
					struct c2_cm_storage_in_agent, ci_agent);
	return 0;
}

static int c2_cm_ci_dlm_completion(void *cb_data)
{
	struct c2_ext *chunk = cb_data;

	c2_cm_ci_add_extent_to_ready_queue(chunk);
}

static int c2_cm_storage_in_agent_enqueue(struct c2_cm_storage_in_agent *agent)
{
	struct c2_cm_iset        *iset = self->ag_parent->cm_iset;
	struct c2_device         *d = agent->ci_device;
	struct c2_priority       *p;
	struct c2_container      *c;	
	struct c2_extent         *ext;
	struct c2_extent          sub_ext;
	struct c2_ext 		  chunk;
	struct c2_cm_aggrg_group group;
	struct c2_dlm	         *lock;

	for (p = c2_get_highest_priority(); p != NULL; p = p->lower()) {
		for (c = d->first_container(); c!= NULL; c = c->next_container()) {
			struct c2_priority *cp = c->get_priority();
			
			if (c2_pri_cmp(p, cp) == 0 && c2_container_cover(c, iset)) {
				for (ext = c->first_extent(); ext != NULL; ext->next()) {
					if (c2_extent_cover(ext, iset)) {
						struct c2_cm_iset_cursor *cur = XXX /* TODO */;
						
						cag_group_get(self, cur, &sub_ext, &group);
						if (group is on this node) {
							
							while (cut_ext_from(&sub_ext, &chunk, RPC_SIZE)) {
								int rc;
								struct dlm_res_id id;
								
								dlm_create_res_id(&id, &chunk, c2_cm_ci_dlm_completion);
								
								if (agent->ci_agent->ag_quit)
									goto out_quit;
								rc = dlm_enqueue_async(&id);

							} /* for each chunk smaller than rpc size */
						}
					} /* extent is covered by the iset */
				} /* for extent */
			} /* same priority and container is in the input set */
		} /* for container */
	} /* for priority */

out_quit:
	return 0;
}

static int c2_cm_storage_in_agent_packet_completion(struct c2_cm_copy_packet *cp)
{
	c2_cm_cp_refdel(cp);
}

static int c2_cm_storage_in_agent_io_completion(void * data)
{
	struct c2_ext *chunk = (struct c2_ext*)data;
	struct c2_cm_copy_packet *cp = c2_cm_copy_packet_alloc();
	cp->completion = c2_cm_storage_in_agent_packet_completion;

	c2_cm_copy_packet_set_data(cp, chunk);
	ag_parent->queue_this_copy_packet(cp);
}

static int c2_cm_storage_in_agent_submitting(struct c2_cm_storage_in_agent *agent)
{
	struct c2_ext chunk;
	while (1) {
		wait_event((rlimit <= threshold && !buffer_pool_empty() && !ready_queue_empty()) || agent->ci_agent->ag_quit);
	
		if (agent->ci_agent->ag_quit)
			break;
		get_a_chunk_from_ready_queue(&chunk);
		submit_aio_read(agent->ci_device, &chunk, c2_cm_storage_in_agent_io_completion);
	}
	return 0;
}

static int c2_cm_storage_in_agent_run(struct c2_cm_agent *self)
{
	struct c2_cm_storage_in_agent *ci_agent = container_of(self,
					struct c2_cm_storage_in_agent, ci_agent);
	struct c2_thread *t1;
	struct c2_thread *t2;

	t1 = c2_thread_create(c2_cm_storage_in_agent_enqueue, ci_agent);
	t2 = c2_thread_create(c2_cm_storage_in_agent_submitting, ci_agent);

	c2_thread_wait(t1);
	c2_thread_wait(t2);
	return 0;
}


static int 
c2_cm_storage_out_agent_init(struct c2_cm_agent *self, struct c2_cm *parent)
{
	struct c2_cm_storage_out_agent *co_agent = container_of(self,
					struct c2_cm_storage_out_agent, co_agent);
	self->ag_parent = parent;
	thia->ag_quit = 0;
	return 0;
}

static int c2_cm_storage_out_agent_stop(struct c2_cm_agent *self, int force)
{
	struct c2_cm_storage_out_agent *co_agent = container_of(self,
					struct c2_cm_storage_out_agent, co_agent);
	
	self->ag_quit = 1;
	return 0;
}

static int c2_cm_storage_out_agent_config(struct c2_cm_agent *self,
				  	  struct c2_cm_agent_config *config)
{
	struct c2_cm_storage_out_agent *co_agent = container_of(self,
					struct c2_cm_storage_out_agent, co_agent);
	return 0;
}

static int c2_cm_storage_out_agent_io_completion(void * data)
{
	struct c2_cm_copy_packet *cp = (struct c2_cm_copy_packet *)data;
	c2_cm_cp_refdel(cp);
}

static int c2_cm_storage_out_agent_submitting(struct c2_cm_storage_out_agent *agent)
{
	struct c2_cm_iset        *iset = self->ag_parent->cm_iset;
	struct c2_device         *d = agent->co_device;
	struct c2_cm_copy_packet *cp;

	while (!agent->co_agent->ag_quit) {
		c2_wait_event( (!agent->co_incoming_queue_empty() && rlimit <= thread) || !agent->co_agent->ag_quit);
		if (agent->co_quit)
			break;	

		cp = agent->get_cp_from_queue();
		submit_aio_write(d, cp, c2_cm_storage_out_agent_io_completion);
	}

	return 0;
}

static int c2_cm_storage_out_agent_handle_clio(struct c2_cm_storage_out_agent *agent)
{
	struct c2_cm_iset        *iset = self->ag_parent->cm_iset;
	struct c2_device         *d = agent->co_device;
	struct c2_cm_copy_packet *cp;

	while (!agent->co_agent->ag_quit) {
		/* TODO handle normal IO here */
	}

	return 0;
}

static int c2_cm_storage_out_agent_run(struct c2_cm_agent *self)
{
	struct c2_cm_storage_out_agent *co_agent = container_of(self,
					struct c2_cm_storage_out_agent, co_agent);
	struct c2_thread *t1;
	struct c2_thread *t2;

	t1 = c2_thread_create(c2_cm_storage_out_agent_submitting, co_agent);
	t2 = c2_thread_create(c2_cm_storage_out_agent_handle_clio, co_agent);

	c2_thread_wait(t1);
	c2_thread_wait(t2);
	return 0;
}


static int 
c2_cm_network_in_agent_init(struct c2_cm_agent *self, struct c2_cm *parent)
{
	struct c2_cm_network_in_agent *ni_agent = container_of(self,
					struct c2_cm_network_in_agent, ni_agent);
	self->ag_parent = parent;
	self->co_quit = 0;
	return 0;
}

static int c2_cm_network_in_agent_stop(struct c2_cm_agent *self, int force)
{
	struct c2_cm_network_in_agent *ni_agent = container_of(self,
					struct c2_cm_network_in_agent, ni_agent);
	
	self->co_quit = 1;
	return 0;
}

static int c2_cm_network_in_agent_config(struct c2_cm_agent *self,
				  	  struct c2_cm_agent_config *config)
{
	struct c2_cm_network_in_agent *ni_agent = container_of(self,
					struct c2_cm_network_in_agent, ni_agent);
	return 0;
}

static int c2_cm_network_in_agent_completion(void * data)
{
	struct c2_cm_copy_packet *cp = (struct c2_cm_copy_packet *)data;
	c2_cm_cp_refdel(cp);
}

static int c2_cm_network_in_agent_receiver(struct c2_cm_network_in_agent *agent)
{
	struct c2_cm_iset        *iset = self->ag_parent->cm_iset;
	struct c2_device         *d = agent->co_device;
	struct c2_cm_copy_packet *cp;

	while (!agent->ni_agent->ag_quit) {
		c2_wait_event( (!agent->ni_incoming_queue_empty() && rlimit <= thread) || !agent->ni_agent_ag_quit);
		if (agent->ni_agent->ag_quit)
			break;

		cp = agent->get_cp_from_network();
		agent->ni_agent.queue(cp, c2_cm_network_in_agent_completion);
	}

	return 0;
}

static int c2_cm_network_in_agent_run(struct c2_cm_agent *self)
{
	struct c2_cm_storage_out_agent *ni_agent = container_of(self,
					struct c2_cm_network_in_agent, ni_agent);
	struct c2_thread *t1;

	t1 = c2_thread_create(c2_network_in_agent_receiver, ni_agent);

	c2_thread_wait(t1);
	return 0;
}


