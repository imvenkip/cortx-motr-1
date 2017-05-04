#!/usr/bin/env bash
set -eu

# IP address of a network interface.
# It should be configured to be used with LNET
# (it should be in `lctl list_nids`).
# It is also used by halond and halonctl.
IP=${IP:-$(ip -4 address show label eth\* up |
		sed -rn 's:^ +inet ([^/]+).*:\1:p')}

HALON_BIN=${HALON_BIN:-$HOME/.local/bin}

# Path to halond binary.
HALOND=${HALOND:-$HALON_BIN/halond}

# Path to halonctl binary.
HALONCTL=${HALONCTL:-$HALON_BIN/halonctl}

# Path to the Halon source tree.
HALON_SOURCES=${HALON_SOURCES:-/work/halon}

# These variables can be used to run this test if Halon is installed from rpm.
MERO_ROLE_MAPPINGS=${MERO_ROLE_MAPPINGS:-$HALON_SOURCES/mero-halon/scripts/mero_provisioner_role_mappings.ede}
HALON_ROLES=${HALON_ROLE_MAPPINGS:-$HALON_SOURCES/mero-halon/scripts/halon_roles.yaml}

# Path to the halon_facts.yaml.
# The script overwrites the file with it's own configuration
# (see halon_facts_yaml() function) and then uses the file
# in `halonctl cluster load` command.
HALON_FACTS_YAML=${HALON_FACTS_YAML:-halon_facts.yaml}

# Use Mero libraries installed from rpm.
# Set to non-emptry value if you want this.
USE_SYSTEM_MERO=

# "m0t1fs" for normal storage and "c0cat" for DIX
MROLE=${MROLE:-"m0t1fs"}

die() { echo "$@" >&2; exit 1; }

show_help() {
	cat << EOF
Usage: $0 [-h] [-?] [-s] [-c COMMAND] [-c COMMAND] ...

-s            - Use system-installed mero.

COMMAND - one of the following:

help          - print this help
cluster_start - start local cluster with single node configuration
cluster_stop  - stop the cluster

All commands are synchronous.
EOF
}

function run_command() {
	command=$1
	case "$COMMAND" in
	"help")
		show_help
		;;
	"cluster_start")
		cluster_start
		;;
	"cluster_stop")
		cluster_stop
		;;
	*)
		echo "Unknown command $1"
		show_help
		exit 1
	esac
}

function stop_everything() {
	sudo killall halond halonctl || true
	sleep 1
	sudo systemctl stop mero-kernel &
	sudo killall -9 lt-m0d m0d lt-m0mkfs m0mkfs || true
	wait
	if systemctl is-failed mero-kernel; then
		sudo systemctl start mero-kernel || true
		sudo systemctl stop mero-kernel || true
	fi
	sudo rmmod m0mero m0gf || true
}

function cluster_start() {
	set -x
	sudo rm -rf halon-persistence

	[ -z $USE_SYSTEM_MERO ] && {
		sudo scripts/install-mero-service -u
		sudo scripts/install-mero-service -l
	}
	sudo utils/m0setup -v -P 8 -N 2 -K 1 -i 1 -d /var/mero/img -s 128 -c
	sudo utils/m0setup -v -P 8 -N 2 -K 1 -i 1 -d /var/mero/img -s 128

	sudo rm -vf /etc/mero/genders
	sudo rm -vf /etc/mero/conf.xc
	sudo rm -vf /etc/mero/disks*.conf

	stop_everything

	halon_facts_yaml > $HALON_FACTS_YAML

	ulimit -c unlimited
	sudo $HALOND -l $IP:9000 &> /tmp/halond.log &
	true
	sleep 2
	sudo $HALONCTL -l $IP:9010 -a $IP:9000 mero bootstrap \
					--facts $HALON_FACTS_YAML \
					--roles $MERO_ROLE_MAPPINGS \
					--halonroles $HALON_ROLES \
					--verbose
	sleep 120
	sudo $HALONCTL -l $IP:9010 -a $IP:9000 mero status
}

function cluster_stop() {
	set -x
	sudo $HALONCTL -l $IP:9010 -a $IP:9000 mero stop && sleep 5
	sudo $HALONCTL -l $IP:9010 -a $IP:9000 mero status &
	sleep 5
	stop_everything
	wait
}

main() {
	if [ -n $USE_SYSTEM_MERO ]; then
		HALOND="LD_LIBRARY_PATH=mero/.libs nohup $HALOND"
		HALONCTL="LD_LIBRARY_PATH=mero/.libs $HALONCTL"
	fi
	if [ $# -eq 0 ]; then
		show_help
		echo "Running cluster_start command"
		cluster_start
	fi
	while getopts "h?sc:" opt; do
		case "$opt" in
		h|\?)
			show_help
			;;
		c)
			COMMAND=$OPTARG
			run_command $COMMAND
			;;
		s)
			USE_SYSTEM_MERO=y
			;;
		esac
		COMMAND=""
	done
	shift $((OPTIND-1))
}

function halon_facts_yaml() {
	cat << EOF
id_racks:
- rack_idx: 1
  rack_enclosures:
  - enc_idx: 1
    enc_id: enclosure1
    enc_bmc:
    - bmc_user: admin
      bmc_addr: bmc.enclosure1
      bmc_pass: admin
    enc_hosts:
    - h_cpucount: 8
      h_fqdn: `hostname`
      h_memsize: 4096
      h_interfaces:
      - if_network: Data
        if_macAddress: '10-00-00-00-00'
        if_ipAddrs:
        - $IP
      h_halon:
        address: "$IP:9000"
        roles:
        - name: station
        - name: ssu
id_m0_servers:
- host_mem_as: 1
  host_cores:
  - 1
  lnid: $IP@tcp
  host_mem_memlock: 1
  m0h_devices:
  - m0d_serial: serial-1
    m0d_bsize: 4096
    m0d_wwn: wwn-1
    m0d_path: /dev/loop1
    m0d_size: 596000000000
    m0d_slot: 1
  - m0d_serial: serial-2
    m0d_bsize: 4096
    m0d_wwn: wwn-2
    m0d_path: /dev/loop2
    m0d_size: 596000000000
    m0d_slot: 2
  - m0d_serial: serial-3
    m0d_bsize: 4096
    m0d_wwn: wwn-3
    m0d_path: /dev/loop3
    m0d_size: 596000000000
    m0d_slot: 3
  - m0d_serial: serial-4
    m0d_bsize: 4096
    m0d_wwn: wwn-4
    m0d_path: /dev/loop4
    m0d_size: 596000000000
    m0d_slot: 4
  - m0d_serial: serial-5
    m0d_bsize: 4096
    m0d_wwn: wwn-5
    m0d_path: /dev/loop5
    m0d_size: 596000000000
    m0d_slot: 5
  - m0d_serial: serial-6
    m0d_bsize: 4096
    m0d_wwn: wwn-6
    m0d_path: /dev/loop6
    m0d_size: 596000000000
    m0d_slot: 6
  - m0d_serial: serial-7
    m0d_bsize: 4096
    m0d_wwn: wwn-7
    m0d_path: /dev/loop7
    m0d_size: 596000000000
    m0d_slot: 7
  - m0d_serial: serial-8
    m0d_bsize: 4096
    m0d_wwn: wwn-8
    m0d_path: /dev/loop8
    m0d_size: 596000000000
    m0d_slot: 8
  host_mem_rss: 1
  m0h_fqdn: `hostname`
  m0h_roles:
  - name: confd
  - name: ha
  - name: mds
  - name: storage
  - name: $MROLE
  host_mem_stack: 1
id_m0_globals:
  m0_parity_units: 2
  m0_md_redundancy: 1
  m0_data_units: 2
  m0_failure_set_gen:
    tag: Formulaic
    contents:
      - [0,0,0,0,1]
      - [0,0,0,0,2]
EOF
}

[ -x $HALOND ] || die "$HALOND: No such executable"
[ -x $HALONCTL ] || die "$HALONCTL: No such executable"
main "$@"
