#
# m0-list-print command is defined in core/gdbinit.py
#
# It is mandatory that core/gdbinit.py is loaded before executing
# commands(that use m0-list-print) from this file.
#

printf "Loading gdb commands for Mero...\n"
printf "NOTE: If not already done, DO load gdbinit.py\n"

define __visit_ft
	set $ft = (struct m0_fop_type *)$arg0
	printf "%p %s %d \n", $ft, $ft->ft_name, $ft->ft_rpc_item_type.rit_opcode
end

define m0-fop-types-list-print
	printf "address name opcode\n"

	# __visit_ft command will be called for each fop type in fop_types_list
	m0-list-print fop_types_list struct m0_fop_type ft_linkage __visit_ft
end
document m0-fop-types-list-print
	Prints global list of registered fop types
end

define __visit_session
	set $s = (struct m0_rpc_session *)$arg0
	printf "%p 0x%lx %u \n", $s, $s->s_session_id, $s->s_state
end

define m0-rpc-conn-print-sessions
	set $conn = (struct m0_rpc_conn *)$arg0
	printf "address session-id state\n"
	m0-list-print $conn->c_sessions struct m0_rpc_session s_link __visit_session
end
document m0-rpc-conn-print-sessions
	Prints list of sessions within connection

	Usage: m0-rpc-conn-print-sessions &conn
end

define frm-item
	set $item = (struct m0_rpc_item *)$arg0
	printf "item: %p deadline: %lu prio: %u\n", $item, $item->ri_deadline, $item->ri_prio
end
define m0-rpc-frm-itemq-print
	m0-list-print $arg0 struct m0_rpc_item ri_iq_link frm-item
end
