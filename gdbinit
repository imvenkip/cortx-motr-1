#
# c2-list-print command is defined in core/gdbinit.py
#
# It is mandatory that core/gdbinit.py is loaded before executing
# commands(that use c2-list-print) from this file.
#

printf "Loading gdb commands for Colibri\n"
printf "NOTE: Do not forget to load gdbinit.py\n"

define __visit_ft
	set $ft = (struct c2_fop_type *)$arg0
	printf "%p %s %d \n", $ft, $ft->ft_name, $ft->ft_rpc_item_type.rit_opcode
end

define c2-fop-types-list-print
	printf "address name opcode\n"

	# __visit_ft command will be called for each fop type in fop_types_list
	c2-list-print fop_types_list struct c2_fop_type ft_linkage __visit_ft
end
document c2-fop-types-list-print
	Prints global list of registered fop types
end

define __visit_session
	set $s = (struct c2_rpc_session *)$arg0
	printf "%p 0x%lx %u \n", $s, $s->s_session_id, $s->s_state
end

define c2-rpc-conn-print-sessions
	set $conn = (struct c2_rpc_conn *)$arg0
	printf "address session-id state\n"
	c2-list-print $conn->c_sessions struct c2_rpc_session s_link __visit_session
end
document c2-rpc-conn-print-sessions
	Prints list of sessions within connection

	Usage: c2-rpc-conn-print-sessions &conn
end
