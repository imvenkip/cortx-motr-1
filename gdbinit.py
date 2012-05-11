def field_type(container, field):
	cmd = "&((({0} *)0)->{1})".format(container, field)
	tmp = gdb.parse_and_eval(cmd)
	return tmp.type.target()

def offset_of(container, field):
	macro_call = "offsetof({0}, {1})".format(container, field)
	offset = long(gdb.parse_and_eval(macro_call))
	return offset

def human_readable(count):
	k = 1024
	m = 1024 * k
	g = 1024 * m
	saved_count = count
	result = ""
	if count >= g:
		c = count // g
		result += "{0}G".format(c)
		count %= g

	if count >= m:
		c = count // m
		result += "{0}M".format(c)
		count %= m

	if count >= k:
		c = count // k
		result += "{0}K".format(c)
		count %= k

	if count != 0 or (count == 0 and result == ""):
		result += "{0}B".format(count)

	return str(saved_count) + "<" + result.strip() + ">"

def sum(start_addr, count):
	a = gdb.parse_and_eval("(unsigned char *){0:#x}".format(start_addr))
	s = 0
	for i in range(count):
		s += a[i]

	return s

#
#==============================================================================
#
class C2ListPrint(gdb.Command):
	"""Prints c2_list/c2_tl elements.

Usage: c2-list-print [&]list [[struct|union] tag link [visit]]

First argument is only mandatory argument. It can be of type
- struct c2_list or struct c2_list *,
- struct c2_tl or struct c2_tl *
If this is the only argument supplied, then c2-list-print prints
address of each of links forming the list.
Example:
(gdb) c2-list-print session->s_slot_table[0]->sl_item_list
0x6257d0
0x7fffd80009d0
Total: 2

Later three arguments can be either all present or all absent.
The three arguments together specify name of link field
within ambient object. This form of c2-list-print prints pointers to
ambient objects.
Example:
(gdb) c2-list-print session->s_slot_table[0]->sl_item_list struct c2_rpc_item ri_slot_refs[0].sr_link
0x6256e0
0x7fffd80008e0
Total: 2

Last argument 'visit' if present is name of a user-defined command
that takes one argument(think c2-list-print as a list traversing function
with pointer to 'visit' function as arguemnt).
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
			print "Error: Usage: c2-list-print [&]list " + \
			      "[[struct|union] tag link [visit]]"
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
		type  = str(vhead.type)

		if type == "struct c2_list":
			head = long(vhead.address)
		elif type == "struct c2_list *":
			head = long(vhead)
		elif type == "struct c2_tl" or type == "struct c2_tl *":
			vhead = vhead['t_head']
			head = long(vhead.address)
		else:
			print "Error: Argument 1 is not a [&]c2_list or [&]c2_tl"
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
			anchor = argv[3]
			try:
				gdb.lookup_type(str_amb_type)
			except:
				print "Error: type '{0}' does not exists".format(str_amb_type)
				return 0, False

			type = field_type(str_amb_type, anchor)
			if str(type) != "struct c2_list_link" and str(type) != "struct c2_tlink":
				print "Error: Argument 4 must be of type c2_list_link or c2_tlink"
				return 0, False

			if str(type) == "struct c2_tlink":
				anchor = anchor.strip() + ".t_link"

			offset = offset_of(str_amb_type, anchor)

		return offset, True

#
#==============================================================================
#

class C2BufvecPrint(gdb.Command):
	"""Prints segments in c2_bufvec

Usage: c2-bufvec-print [&]c2_bufvec
For each segment, the command prints,
- segment number,
- [start address, end_address),
- offset of first byte of segment, inside entire c2_bufvec, along with its
  human readable representation,
- number of bytes in segment in human readable form,
- sumation of all the bytes in the segment. sum = 0, implies all the bytes in
  the segment are zero.
"""
	def __init__(self):
		gdb.Command.__init__(self, "c2-bufvec-print", \
				     gdb.COMMAND_SUPPORT, gdb.COMPLETE_SYMBOL)

	def invoke(self, arg, from_tty):
		argv = gdb.string_to_argv(arg)
		argc = len(argv)

		if argc != 1:
			print "Error: Usage: c2-bufvec-print [&]c2_bufvec"
			return

		vbufvec = gdb.parse_and_eval(argv[0])
		t = str(vbufvec.type)
		if t != "struct c2_bufvec" and t != "struct c2_bufvec *":
			print "Error: Argument 1 must be either 'struct c2_bufvec' or" + \
					" 'struct c2_bufvec *' type"
			return

		nr_seg   = long(vbufvec['ov_vec']['v_nr'])
		buf_size = 0
		offset   = 0
		sum_of_bytes_in_buf = 0
		print "seg:index start_addr end_addr offset bcount sum"
		for i in range(nr_seg):
			start_addr = long(vbufvec['ov_buf'][i])
			count      = long(vbufvec['ov_vec']['v_count'][i])
			end_addr   = start_addr + count
			sum_of_bytes_in_seg = sum(start_addr, count)
			print "seg:{0} {1:#x} {2:#x} {3} {4} {5}".format(i, \
				start_addr, end_addr, human_readable(offset), \
				human_readable(count), sum_of_bytes_in_seg)
			buf_size += count
			offset   += count
			sum_of_bytes_in_buf += sum_of_bytes_in_seg

		print "nr_seg:", nr_seg
		print "buf_size:", human_readable(buf_size)
		print "sum:", sum_of_bytes_in_buf

#
#==============================================================================
#

class C2IndexvecPrint(gdb.Command):
	"""Prints segments in c2_indexvec

Usage: c2-indexvec-print [&]c2_indexvec
"""
	def __init__(self):
		gdb.Command.__init__(self, "c2-indexvec-print", \
				     gdb.COMMAND_SUPPORT, gdb.COMPLETE_SYMBOL)

	def invoke(self, arg, from_tty):
		argv = gdb.string_to_argv(arg)
		argc = len(argv)

		if argc != 1:
			print "Error: Usage: c2-indexvec-print [&]c2_indexvec"
			return

		v_ivec = gdb.parse_and_eval(argv[0])
		t = str(v_ivec.type)
		if t != "struct c2_indexvec" and t != "struct c2_indexvec *":
			print "Error: Argument 1 must be of either 'struct c2_indexvec' or" + \
					" 'struct c2_indexvec *' type."
			return

		nr_seg      = long(v_ivec['iv_vec']['v_nr'])
		total_count = 0

		print "   :seg_num index count"
		for i in range(nr_seg):
			index = long(v_ivec['iv_index'][i])
			count = long(v_ivec['iv_vec']['v_count'][i])
			print "seg:", i, index, count
			total_count += count

		print "nr_seg:", nr_seg
		print "total:", total_count

# List of macros to be defined
macros = [ \
"offsetof(typ,memb) ((unsigned long)((char *)&(((typ *)0)->memb)))", \
"container_of(ptr, type, member) " + \
	"((type *)((char *)(ptr)-(char *)(&((type *)0)->member)))" \
]

# Define macros listed in macros[]
for m in macros:
	gdb.execute("macro define %s" % m)

C2ListPrint()
C2BufvecPrint()
C2IndexvecPrint()

print "Loading gdb commands for Colibri..."
print "NOTE: You may also want to load core/gdbinit"
