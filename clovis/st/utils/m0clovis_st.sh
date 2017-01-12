#!/bin/bash

# System test based on m0clovis tool.
# Checks clovis index commands only for now.

set -x

SANDBOX_DIR=${SANDBOX_DIR:-/var/mero/sandbox.m0clovis-st}

emsg="---"
M0_SRC_DIR=`readlink -f $0`
M0_SRC_DIR=${M0_SRC_DIR%/*/*/*/*}
num=10
large_size=30 #131072
small_size=20

. $M0_SRC_DIR/utils/functions # die, sandbox_init, report_and_exit
# following scripts are mandatory for start the mero service (mero_start/stop)
. $M0_SRC_DIR/m0t1fs/linux_kernel/st/common.sh
. $M0_SRC_DIR/m0t1fs/linux_kernel/st/m0t1fs_common_inc.sh
. $M0_SRC_DIR/m0t1fs/linux_kernel/st/m0t1fs_server_inc.sh

out_file="${SANDBOX_DIR}/m0clovis_st.log"
rc_file="${SANDBOX_DIR}/m0clovis_st.cod.log"
erc_file="${SANDBOX_DIR}/m0clovis_st.ecod.log"
res_out_file="${SANDBOX_DIR}/m0clovis_st.results.log"

keys_file="${SANDBOX_DIR}/keys.txt"
keys_bulk_file="${SANDBOX_DIR}/keys_bulk.txt"
vals_file="${SANDBOX_DIR}/vals.txt"
vals_bulk_file="${SANDBOX_DIR}/vals_bulk.txt"
fids_file="${SANDBOX_DIR}/fids.txt"

clovistool="$M0_SRC_DIR/clovis/st/utils/m0clovis_start.sh local index"

genf="genf ${num} ${fids_file}"
genv_bulk="genv ${num} ${large_size} ${vals_bulk_file}"
genv="genv ${num} ${small_size} ${vals_file}"

do_mero_start=1
verbose=0

NODE_UUID=`uuidgen`

function usage()
{
	echo "m0clovis_st.sh [--no-setup --verbose --help] all|test1 test2 ..."
	echo "Options:"
	echo "    '--no-setup'        don't start mero service"
	echo "    '--verbose'         output additional info into console"
	echo "    '--help'            show this help"
	echo "Available tests:"
	echo "    'creates'       create index"
	echo "    'createm'       create several indices"
	echo "    'drops'         drop index"
	echo "    'dropm'         drop several indices"
	echo "    'list'          get list of indices"
	echo "    'lookup'        lookup indices"
	echo "    'puts1'         put key into index"
	echo "    'putsN_exist'   put existing key into index"
	echo "    'bputsN_exist'  put existing key (batch)"
	echo "    'putsN'         put several keys into index"
	echo "    'putmN'         put several keys into several indices"
	echo "    'putsNb'        put several keys into index (use bulk)"
	echo "    'gets1'         get key-value from index"
	echo "    'bgets1'        get key-value from index (batch)"
	echo "    'getsN'         get several key-values from index"
	echo "    'bgetsN'        get several key-values from index (batch)"
	echo "    'nextsN'        get next key-values from index"
	echo "    'bnextsN'       get next key-values from index (batch)"
	echo "    'dels1'         delete key from index"
	echo "    'bdels1'        delete key from index (batch)"
	echo "    'delsN'         delete keys from index"
	echo "    'bdelsN'        delete keys from index (batch)"
	echo "    'delmN'         delete keys from several indices"
	echo "    'bdelmN'        delete keys from several indices (batch)"
	echo "    empty list or 'all'  enable all tests"
	echo "Examples:"
	echo "./m0clovis_st.sh --no-setup all"
	echo "./m0clovis_st.sh creates putsN --verbose"
}

function load_item()
{
    local resultvar=$3
    local fil=$1
    local n=$2
    local t=${n}p

    local  myresult=$(sed -n "${t}" < ${fil})
    if [[ "$resultvar" ]]; then
        eval $resultvar="'$myresult'"
    else
        echo "$myresult"
    fi
}

function rm_logs()
{
	rm -rf ${out_file} ${rc_file} ${erc_file} ${res_out_file} >/dev/null
}

creates()
{
	local rc=0
	echo "Test:Create"
	emsg="FAILED to create index by fid:${fid}"
	${clovistool} ${drop} >/dev/null
	${clovistool} ${create} >${out_file}
	rc=$?
	[ $rc != 0 ] && return $rc
	#check output: should be rc code 0
	exec grep "operation rc:" ${out_file} | grep -v "operation rc: 0" >${erc_file}
	[ -s "${erc_file}" ] && return 1
	rm_logs
	return $rc
}

createm()
{
	local rc=0
	echo "Test:Create FIDS"
	emsg="FAILED to create index by @fids:${fids_file}"
	${clovistool} ${dropf} >/dev/null
	${clovistool} ${create} >/dev/null
	rc=$?
	[ $rc != 0 ] && return $rc
	${clovistool} ${createf} >>${out_file}
	rc=$?
	[ $rc != 0 ] && return $rc
	grep "operation rc:" ${out_file} >${rc_file}
	grep -v "operation rc: 0" ${rc_file} >${erc_file}
	[ $(cat ${erc_file} | wc -l) == 1 ] || return 1
	rm_logs
	return $rc
}

drops()
{
	local rc=0
	echo "Test:Drop"
	emsg="FAILED to drop index by fid:${fid}"
	${clovistool} ${create} >/dev/null
	rc=$?
	[ $rc != 0 ] && return $rc
	${clovistool} ${drop} >${out_file}
	rc=$?
	[ $rc != 0 ] && return $rc
	grep "operation rc:" ${out_file} >${rc_file}
	grep -v "operation rc: 0" ${rc_file} >${erc_file}
	[ -s "${erc_file}" ] && return 1
	rm_logs
	return $rc
}

dropm()
{
	local rc=0
	echo "Test:Drop FIDS"
	emsg="FAILED to drop index by @fids:${fids_file}"
	${clovistool} ${createf} >/dev/null
	rc=$?
	[ $rc != 0 ] && return $rc
	${clovistool} ${drop} >${out_file}
	[ $rc != 0 ] && return $rc
	grep "operation rc:" ${out_file} >${rc_file}
	grep -v "operation rc: 0" ${rc_file} >${erc_file}
	[ -s "${erc_file}" ] && return 1
	rm_logs
	${clovistool} ${dropf} >${out_file}
	rc=$?
	[ $rc != 0 ] && return $rc
	grep "operation rc:" ${out_file} >${rc_file}
	grep -v "operation rc: 0" ${rc_file} >${erc_file}
	[ $(cat ${erc_file} | wc -l) == 1 ] || return 1
	rm_logs
	return $rc
}

list()
{
	local rc=0
	echo "Test:List"
	emsg="FAILED to get List"
	${clovistool} ${dropf} >/dev/null
	${clovistool} ${createf} >/dev/null
	${clovistool} ${list3} >${out_file}
	grep "<" ${out_file} >${res_out_file}
	[ $(cat ${res_out_file} | wc -l) == 3 ] || return 1
	${clovistool} ${list} >${out_file}
	grep "<" ${out_file} >${res_out_file}
	[ $(cat ${res_out_file} | wc -l) == ${num} ] || return 1
	rm_logs
	return $rc
}

lookup()
{
	local rc=0
	echo "Test:Lookup"
	emsg="FAILED to check lookup functionality"
	${clovistool} ${dropf} >/dev/null
	${clovistool} ${create} >/dev/null
	${clovistool} ${lookup} >${out_file}
	grep "found rc" ${out_file} >${rc_file}
	grep -v "found rc 0" ${rc_file} >${erc_file}
	[ -s "${erc_file}" ] && return 1

	${clovistool} ${drop} >/dev/null
	${clovistool} ${lookup} >${out_file}
	grep "found rc" ${out_file} >${rc_file}
	grep -v "found rc -2" ${rc_file} >${erc_file}
	[ -s "${erc_file}" ] && return 1
	return $rc
}

puts1()
{
	local rc=0
	echo "Test:Put KEY"
	emsg="FAILED to put key ${key} val ${val} into ${fid}"
	${clovistool} ${dropf} >/dev/null
	rc=$?
	[ $rc != 0 ] && return $rc
	${clovistool} ${create} >/dev/null
	rc=$?
	[ $rc != 0 ] && return $rc
	${clovistool} ${put_fid_key} >${out_file}
	[ $rc != 0 ] && return $rc
	grep "operation rc:" ${out_file} >${rc_file}
	grep -v "operation rc: 0" ${rc_file} >${erc_file}
	[ -s "${erc_file}" ] && return 1
	rm_logs
	return $rc
}

putsN_exist()
{
	local rc=0
	echo "Test:Put existing KEYS"
	emsg="FAILED to put existing @keys:${keys_file} @vals:${vals_file} into ${fid}"
	${clovistool} ${dropf} >/dev/null
	rc=$?
	[ $rc != 0 ] && return $rc
	${clovistool} ${create} >/dev/null
	[ $rc != 0 ] && return $rc
	${clovistool} ${put_fid_key} >/dev/null
	[ $rc != 0 ] && return $rc
	${clovistool} ${put_fid_keys} >${out_file}
	grep "operation rc:" ${out_file} >${rc_file}
	# TODO: Check that -17 is returned
	rm_logs
	return $rc
}

bputsN_exist()
{
	local rc=0
	echo "Test:Put existing KEYS by one command"
	emsg="FAILED to put existing @keys:${keys_file} @vals:${vals_file} into ${fid}"
	${clovistool} ${dropf} ${create} ${put_fid_key} ${put_fid_keys} >${out_file}
	grep -B 1 "put done:" ${out_file} >${rc_file}
	grep -v ": 0" ${rc_file} >${erc_file}
	# TODO: Check that -17 is returned
	rm_logs
	return $rc
}

putsN()
{
	local rc=0
	echo "Test:Put KEYS"
	emsg="FAILED to put new @keys:${keys_file} @vals:${vals_file} into ${fid}"
	${clovistool} ${dropf} >/dev/null
	rc=$?
	[ $rc != 0 ] && return $rc
	${clovistool} ${create} >/dev/null
	rc=$?
	[ $rc != 0 ] && return $rc
	${clovistool} ${put_fid_keys} >${out_file}
	rc=$?
	[ $rc != 0 ] && return $rc
	grep "operation rc:" ${out_file} >${rc_file}
	grep -v "operation rc: 0" ${rc_file} >${erc_file}
	[ -s "${erc_file}" ] && return 1
	rm_logs
	return $rc
}

putsNb()
{
	local rc=0
	echo "Test:Put KEYS (bulk)"
	emsg="FAILED to put new @keys:${keys_bulk_file} @vals:${vals_bulk_file} into ${fid}"
	${clovistool} ${dropf} >/dev/null
	rc=$?
	[ $rc != 0 ] && return $rc
	${clovistool} ${create} >/dev/null
	rc=$?
	[ $rc != 0 ] && return $rc
	${clovistool} ${put_fid_keys_bulk} >${out_file}
	rc=$?
	[ $rc != 0 ] && return $rc
	grep "operation rc:" ${out_file} >${rc_file}
	grep -v "operation rc: 0" ${rc_file} >${erc_file}
	[ -s "${erc_file}" ] && return 1
	rm_logs
	return $rc
}

putmN()
{
	local rc=0
	echo "Test:Put KEYS into FIDS"
	emsg="FAILED to put new @keys:${keys_file} @vals:${vals_file} into @fids:${fid_file}"
	${clovistool} ${dropf} >/dev/null
	rc=$?
	[ $rc != 0 ] && return $rc
	${clovistool} ${createf} >/dev/null
	rc=$?
	[ $rc != 0 ] && return $rc
	${clovistool} ${put_fids_keys} >${out_file}
	[ $rc != 0 ] && return $rc
	grep "operation rc:" ${out_file} >${rc_file}
	grep -v "operation rc: 0" ${rc_file} >${erc_file}
	[ -s "${erc_file}" ] && return 1
	rm_logs
	return $rc
}

gets1()
{
	local rc=0
	echo "Test:Get KEY"
	emsg="FAILED to get key:${key} from fid:${fid}"
	${clovistool} ${dropf} >/dev/null
	rc=$?
	[ $rc != 0 ] && return $rc
	${clovistool} ${create} >/dev/null
	rc=$?
	[ $rc != 0 ] && return $rc
	${clovistool} ${put_fid_key} >/dev/null
	rc=$?
	[ $rc != 0 ] && return $rc
	${clovistool} ${get_fid_key} >${out_file}
	rc=$?
	[ $rc != 0 ] && return $rc
	grep "operation rc:" ${out_file} >${rc_file}
	grep -v "operation rc: 0" ${rc_file} >${erc_file}
	[ -s "${erc_file}" ] && return 1
	grep -A 1 "KEY" ${out_file} > ${res_out_file}
	key_line=$(load_item ${res_out_file} 1)
	val_line=$(load_item ${res_out_file} 2)
	key_line=${key_line#"/KEY/"}
	val_line=${val_line#"/VAL/"}
	[ "${val_line}" != "${key_line}" ] && return 1
	[ $(cat ${res_out_file} | wc -l) == 2 ] || return 1
	rm_logs
	return $rc
}

bgets1()
{
	local rc=0
	echo "Test:Get KEY by one command"
	emsg="FAILED to get key:${key} from fid:${fid} by one command"
	${clovistool} ${dropf} ${create} ${put_fid_key} ${get_fid_key} >${out_file}
	rc=$?
	[ $rc != 0 ] && return $rc
	grep -A 1 "KEY" ${out_file} > ${res_out_file}
	key_line=$(load_item ${res_out_file} 1)
	val_line=$(load_item ${res_out_file} 2)
	key_line=${key_line#"/KEY/"}
	val_line=${val_line#"/VAL/"}
	[ "${val_line}" != "${key_line}" ] && return 1
	[ $(cat ${res_out_file} | wc -l) == 2 ] || return 1
	rm_logs
	return $rc
}

getsN()
{
	local rc=0
	echo "Test:Get KEYS"
	emsg="FAILED to get @keys:${keys_file} from fid:${fid}"
	${clovistool} ${dropf} ${create} ${put_fid_keys} >/dev/null
	rc=$?
	[ $rc != 0 ] && return $rc
	${clovistool} ${get_fid_keys} >${out_file}
	grep -A 1 "KEY" ${out_file} > ${res_out_file}
	local lines
	let lines=$num*3-1
	[ $(cat ${res_out_file} | wc -l) == $lines ] || return 1
	while read key_line ; do
		read val_line
		key_line=${key_line#"/KEY/"}
		val_line=${val_line#"/VAL/"}
		[ "${val_line}" != "${key_line}" ] && return 1
		read tmp
	done < ${res_out_file}
	rm_logs
	return $rc
}

bgetsN()
{
	local rc=0
	echo "Test:Get KEYS by one command"
	emsg="FAILED to get @keys:${keys_file} from fid:${fid} by one command"
	${clovistool} ${dropf} ${create} ${put_fid_keys} ${get_fid_keys} >${out_file}
	rc=$?
	[ $rc != 0 ] && return $rc
	grep -A 1 "KEY" ${out_file} > ${res_out_file}
	local lines
	let lines=$num*3-1
	[ $(cat ${res_out_file} | wc -l) == $lines ] || return 1
	while read key_line ; do
		read val_line
		key_line=${key_line#"/KEY/"}
		val_line=${val_line#"/VAL/"}
		[ "${val_line}" != "${key_line}" ] && return 1
		read tmp
	done < ${res_out_file}
	rm_logs
	return $rc
}

nextsN()
{
	local rc=0
	echo "Test:Get next keys"
	emsg="FAILED to get next keys from ${fid} start key ${key}"
	${clovistool} ${dropf} ${create} ${put_fid_keys} >/dev/null
	rc=$?
	[ $rc != 0 ] && return $rc
	${clovistool} ${next} >${out_file}
	rc=$?
	[ $rc != 0 ] && return $rc
	grep -A 1 "KEY" ${out_file} > ${res_out_file}
	while read key_line ; do
		read val_line
		key_line=${key_line#"/KEY/"}
		val_line=${val_line#"/VAL/"}
		[ "${val_line}" != "${key_line}" ] && return 1
		read tmp
	done < ${res_out_file}
	return $rc
}

bnextsN()
{
	local rc=0
	echo "Test:Get next keys by one command"
	emsg="FAILED to get next keys from ${fid} start key ${key} by one command"
	${clovistool} ${dropf} ${create} ${put_fid_keys} ${next} >${out_file}
	rc=$?
	[ $rc != 0 ] && return $rc
	grep -A 1 "KEY" ${out_file} > ${res_out_file}
	while read key_line ; do
		read val_line
		key_line=${key_line#"/KEY/"}
		val_line=${val_line#"/VAL/"}
		[ "${val_line}" != "${key_line}" ] && return 1
		read tmp
	done < ${res_out_file}
	return $rc
}

dels1()
{
	local rc=0
	echo "Test:Del existing KEY"
	emsg="FAILED to del key:${key} from ${fid}"
	${clovistool} ${dropf} ${create} ${put_fid_key} >/dev/null
	rc=$?
	[ $rc != 0 ] && return $rc
	${clovistool} $del_fid_key >${out_file}
	rc=$?
	[ $rc != 0 ] && return $rc
	grep "operation rc:" ${out_file} >${rc_file}
	grep -v "operation rc: 0" ${rc_file} >${erc_file}
	[ -s "${erc_file}" ] && return 1
	rm_logs
	return $rc
}

bdels1()
{
	local rc=0
	echo "Test:Del existing KEY by one command"
	emsg="FAILED to del key:${key} from ${fid} by one command"
	${clovistool} ${dropf} ${create} ${put_fid_key} $del_fid_key>${out_file}
	rc=$?
	[ $rc != 0 ] && return $rc
	grep -B 1 "del done:" ${out_file} >${rc_file}
	grep -v ": 0" ${rc_file} >${erc_file}
	[ -s "${erc_file}" ] && return 1
	rm_logs
	return $rc
}

delsN()
{
	local rc=0
	echo "Test:Del existing KEYS"
	emsg="FAILED to del @keys:${keys_file} from ${fid}"
	${clovistool} ${dropf} ${create} ${put_fid_keys} >/dev/null
	rc=$?
	[ $rc != 0 ] && return $rc
	${clovistool} ${del_fid_keys} >${out_file}
	rc=$?
	[ $rc != 0 ] && return $rc
	grep "operation rc:" ${out_file} >${rc_file}
	grep -v "operation rc: 0" ${rc_file} >${erc_file}
	[ -s "${erc_file}" ] && return 1
	rm_logs
	return $rc
}

bdelsN()
{
	local rc=0
	echo "Test:Del existing KEYS by one command"
	emsg="FAILED to del keys:@${keys_file} from ${fid} by one command"
	${clovistool} ${dropf} ${create} ${put_fid_key} $del_fid_keys >${out_file}
	rc=$?
	[ $rc != 0 ] && return $rc
	grep -B 1 "del done:" ${out_file} >${rc_file}
	grep -v ": 0" ${rc_file} >${erc_file}
	[ -s "${erc_file}" ] && return 1
	rm_logs
	return $rc
}

delmN()
{
	local rc=0
	echo "Test:Del existing KEYS from several indices"
	emsg="FAILED to del @keys:${keys_file} @vals:${vals_file} from @${fid_file}"
	${clovistool} ${dropf} ${createf} ${put_fids_keys} >/dev/null
	rc=$?
	[ $rc != 0 ] && return $rc
	${clovistool} ${del_fids_keys} >${out_file}
	[ $rc != 0 ] && return $rc
	grep "operation rc:" ${out_file} >${rc_file}
	grep -v "operation rc: 0" ${rc_file} >${erc_file}
	[ -s "${erc_file}" ] && return 1
	rm_logs
	return $rc
}

bdelmN()
{
	local rc=0
	echo "Test:Del existing KEYS in several indices by one command"
	emsg="FAILED to del keys:@${keys_file} from @${fid_file} by one command"
	${clovistool} ${dropf} ${createf} ${put_fids_keys} $del_fids_keys >${out_file}
	rc=$?
	[ $rc != 0 ] && return $rc
	grep -B 1 "del done:" ${out_file} >${rc_file}
	grep -v ": 0" ${rc_file} >${erc_file}
	[ -s "${erc_file}" ] && return 1
	rm_logs
	return $rc
}

st_init()
{
	# generate source files for KEYS, VALS, FIDS
	${clovistool} ${genf} ${genv} ${genv_bulk} >/dev/null
	[ $? != 0 ] && return 1
	cp ${vals_file} ${keys_file}
	cp ${vals_bulk_file} ${keys_bulk_file}
	fid=$(load_item $fids_file 1)
	key=$(load_item $keys_file 1)
	val=$(load_item $vals_file 1)
	t=${key#* }
	key=$t
	t=${val#* }
	val=$t
	dropf="drop @${fids_file}"
	drop="drop \"${fid}\""
	createf="create @${fids_file}"
	create="create \"${fid}\""

	put_fids_keys="put @${fids_file} @${keys_file} @${vals_file}"
	put_fids_key="put @${fids_file} ${key} ${val}"
	put_fid_keys="put \"${fid}\" @${keys_file} @${vals_file}"
	put_fid_key="put \"${fid}\" ${key} ${val}"
	put_fid_keys_bulk="put \"${fid}\" @${keys_bulk_file} @${vals_bulk_file}"

	del_fids_keys="del @${fids_file} @${keys_file}"
	del_fids_key="del @${fids_file} ${key}"
	del_fid_keys="del \"${fid}\" @${keys_file}"
	del_fid_key="del \"${fid}\" ${key}"

	get_fid_keys="get \"${fid}\" @${keys_file}"
	get_fid_key="get \"${fid}\" ${key}"

	next="next \"${fid}\" ${key} 10"

	lookup="lookup \"${fid}\""
	lookups="lookup @${fids_file}"

	list="list \"${fid}\" ${num}"
	list3="list \"${fid}\" 3"
}


start_mero()
{
	local rc=0
	local multiple_pools=0
	echo "Mero service starting..."
	if [ $verbose == 1 ]; then
		mero_service start $multiple_pools
	else
		mero_service start $multiple_pools >/dev/null 2>&1
	fi
	rc=$?
	if [ $rc -ne "0" ]
	then
		echo "Failed to start Mero Service."
	fi
	return $rc
}

stop_mero()
{
	local rc=0
	echo "Mero service stopping..."
	if [ $verbose == 1 ]; then
		mero_service stop
	else
		mero_service stop >/dev/null 2>&1
	fi
	rc=$?
	if [ $rc -ne "0" ]
	then
		echo "Failed to stop Mero Service."
	fi
	return $rc
}

start_tests()
{
	local rc
	st_init
	rc=$?
	if [ $rc -ne 0 ]; then
		echo "Failed to init test $rc"
		return $rc
	fi
	for f in ${tests_list[@]}; do
		(${f})
		rc=$?
		if [ $rc != 0 ]; then
			echo "Failed: $f"
			echo "MSG   : ${emsg}"
			return $rc
		fi
	done
	return $rc
}

main()
{
	local rc
	sandbox_init
	if [ ${do_mero_start} == 1 ]; then
		start_mero
		rc=$?
		if [ $rc -ne 0 ]; then
			echo "Failed to start Mero Service."
			return 1
		fi
	fi

	start_tests
	rc=$?
	if [ $do_mero_start == 1 ]; then
		stop_mero
	fi
	[ $rc -ne 0 ] || sandbox_fini
	return $rc
}


arg_list=("$@")

[ `id -u` -eq 0 ] || die 'Must be run by superuser'

OPTS=`getopt -o vhn --long verbose,no-setup,help -n 'parse-options' -- "$@"`
if [ $? != 0 ] ; then echo "Failed parsing options." >&2 ; exit 1 ; fi
eval set -- "$OPTS"
while true; do
	case "$1" in
	-v | --verbose ) verbose=1; shift ;;
	-h | --help )    usage ; exit 0; shift ;;
	-n | --no-setup ) do_mero_start=0; shift ;;
	-- ) shift; break ;;
	* ) break ;;
	esac
done

for arg in "${arg_list[@]}"; do
	if [ "${arg:0:2}" != "--" ]; then
		tests_list+=(${arg})
	fi
done

if [ ${#tests_list[@]} -eq 0 ]; then
	declare -a tests_list=(creates
				createm
				drops
				dropm
				list
				lookup
				puts1
				putsN_exist
				bputsN_exist
				putsN
				putmN
				putsNb
				gets1
				bgets1
				getsN
				bgetsN
				nextsN
				bnextsN
				dels1
				bdels1
				delsN
				bdelsN
				delmN
				bdelmN)
fi

main
rc=$?
report_and_exit m0clovis $rc


# Local variables:
# sh-basic-offset: 8
# sh-indentation: 8
# tab-width: 8
# End:
