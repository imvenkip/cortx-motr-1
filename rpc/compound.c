
int compound_send_noseq(const struct rpc_client *cli,
		       unsigned int num_ops, void *ops);

int compound_send_seq(const struct rpc_client *cli, struct c2_cli_slot *slot,
		      unsigned int num_ops, void *ops);
