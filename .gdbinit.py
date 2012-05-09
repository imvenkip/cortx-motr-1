# List of macros to be defined
macros = [ \
"offsetof(typ,memb) ((unsigned long)((char *)&(((typ *)0)->memb)))", \
"container_of(ptr, type, member) " + \
	"((type *)((char *)(ptr)-(char *)(&((type *)0)->member)))" \
]

class C2ListPrint(gdb.Command):
	"""Prints c2_list elements.

Usage: c2-list-print [&]c2_list [[struct|union] tag field [visit]]

First argument is only mandatory argument. It can be of type either
"struct c2_list" or "struct c2_list *". If this is the only argument
supplied, then c2-list-print prints address of each of c2_list_link
forming the list.
Example:
(gdb) c2-list-print session->s_slot_table[0]->sl_item_list
0x6257d0
0x7fffd80009d0
Total: 2

Later three arguments can be either all present or all absent.
The three arguments together specify name of c2_list_link field
within ambient object. This form of c2-list-print prints pointers to
ambient objects.
Example:
(gdb) c2-list-print session->s_slot_table[0]->sl_item_list struct c2_rpc_item ri_slot_refs[0].sr_link
0x6256e0
0x7fffd80008e0
Total: 2

Last argument 'visit' if present is name of a user-defined command
that takes one argument(think c2-list-print as a list traversing function
with pointer to function as arguemnt).
The visit command will be executed for each object in list.
The implementation of visit command can decide which objects to print and how.
Example:
(gdb) define session_visit
>set $s = (struct c2_rpc_session *)$arg0
>printf "session %p id %lu state %d\\n", $s, $s->s_session_id, $s->s_state
>end
(gdb) c2-list-print session->s_conn->c_sessions struct c2_rpc_session s_link session_visit
session 0x604c60 id 191837184000000002 state 4
session 0x624e50 id 0 state 4
Total: 2
(gdb) c2-list-print session->s_conn->c_sessions struct c2_rpc_session s_link
0x604c60
0x624e50
Total: 2
"""

	def __init__(self):
		gdb.Command.__init__(self, "c2-list-print", \
				     gdb.COMMAND_SUPPORT, gdb.COMPLETE_SYMBOL)

	def invoke(self, arg, from_tty):
		argv = gdb.string_to_argv(arg)
		argc = len(argv)
		if argc != 1 and argc != 4 and argc != 5:
			print "Error: Usage: c2-list-print [&]c2_list " + \
			      "[[struct|union] tag field] visit"
			return

		vhead, head, ok = self.get_head(argv)
		if not ok:
			return

		offset, ok = self.get_offset(argv)
		if not ok:
			return

		if argc == 5:
			visit = argv[4]
		else:
			visit = ""

		vnd   = vhead['l_head']
		nd    = long(vnd)
		total = 0

		while nd != head:
			obj_addr = nd - offset
			if visit == "":
				print "0x%x" % obj_addr
			else:
				gdb.execute("{0} {1}".format(visit, obj_addr))

			vnd    = vnd['ll_next']
			nd     = long(vnd)
			total += 1

		print "Total: %d" % total

	def get_head(self, argv):
		ok    = True
		head  = 0
		vhead = gdb.parse_and_eval(argv[0])

		if vhead.type.code == gdb.TYPE_CODE_PTR and \
		   vhead.type.target().code == gdb.TYPE_CODE_STRUCT and \
		   vhead.type.target().tag == "c2_list":
			# arg1 is of type "struct c2_list *"
			head = long(vhead)

		elif vhead.type.code == gdb.TYPE_CODE_STRUCT and \
		     vhead.type.tag == "c2_list":
			# arg1 is of type "struct c2_list"
			head = long(vhead.address)

		else:
			print "Error: Argument 1 is not a [&]c2_list"
			ok = False

		return vhead, head, ok

	def get_offset(self, argv):
		argc   = len(argv)
		offset = 0
		if argc == 4 or argc == 5:
			if argv[1] != "struct" and argv[1] != "union":
				print 'Error: Argument 2 must be ' + \
				      'either "struct" or "union"'
				return 0, False

			str_amb_type = "{0} {1}".format(argv[1], argv[2])
			try:
				gdb.lookup_type(str_amb_type)
			except:
				print "Error: type '{0}' does not exists".format(str_amb_type)
				return 0, False

			macro_call = "offsetof({0}, {1})".format(str_amb_type,\
					argv[3])
			offset = long(gdb.parse_and_eval(macro_call))

		return offset, True

# Define macros listed in macros[]
for m in macros:
	gdb.execute("macro define %s" % m)

C2ListPrint()
