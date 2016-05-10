#!/bin/bash
set -eux

configure_beta1() {
	HOSTS_LIST="172.16.1.[1-6]"
	HALOND_TS="172.16.1.1"
	HALON_FACTS_FUNC="halon_facts_yaml_beta1"
}

configure_dev1_1() {
	HOSTS_LIST="172.16.1.[3,5,6,8,9]"
	HALOND_TS="172.16.1.3"
	HALON_FACTS_FUNC="halon_facts_yaml_dev1_1"
}

configure_common() {
	HALON_SOURCES=/root/halon
	MERO_SOURCES=/root/mero
	MERO_RPM_PATH=/root/rpmbuild/RPMS/x86_64
	HALON_RPM_PATH=$HALON_SOURCES/rpmbuild/RPMS/x86_64
	REMOTE_RPM_PATH=/tmp

	HALOND_NODES="$HOSTS_LIST"
	HALOND_SAT="$HOSTS_LIST"
	HALOND_NET_IF=bond1
	HALOND_PORT=9000
	HALONCTL_PORT=9001
	HALOND_LOG=/tmp/halond.log
	HALON_FACTS=./halon_facts.yaml
	ROLE_MAPS=/etc/halon/role_maps/prov.ede

	PDSH="pdsh -w $HOSTS_LIST"
	PDCP="pdcp -w $HOSTS_LIST"
}

function show_help () {
	cat << EOF
Usage: $0 [-h] [-?] [-c COMMAND] [-c COMMAND] ...

Note: you need pdsh installed on each node to be able to use pdcp.

COMMAND - one of the following:

help             - print this help
build_mero       - build Mero rpm from sources
build_halon      - build Halon rpm from sources. build_mero should be called before.
stop             - stop all halond and mero services
uninstall        - uninstall Mero and Halon rpms
install          - install the latest Mero and Halon rpms from rpm build dirs
start_halon      - start TS on the first node and SAT on all nodes
bootstrap        - halonctl cluster load
status           - halonctl status
halon_facts_yaml - print halon_facts.yaml for the cluster
EOF
}

function run_command() {
	command=$1
	case "$COMMAND" in
	"help")
		show_help
		;;
	"build_mero")
		build_mero
		;;
	"build_halon")
		build_halon
		;;
	"stop")
		$PDSH systemctl stop mero-kernel
		$PDSH systemctl stop halond
		$PDSH 'kill `pidof halond` `pidof halonctl`'
		sleep 2
		$PDSH systemctl start mero-kernel
		sleep 2
		$PDSH systemctl stop mero-kernel
		;;
	"uninstall")
		$PDSH yum -y remove mero halon
		;;
	"install")
		local MERO_RPM=$(ls -t $MERO_RPM_PATH | grep mero-0 | head -n1)
		local HALON_RPM=$(ls -t $HALON_RPM_PATH | grep 'halon-.*devel' | head -n1)
		$PDCP $MERO_RPM_PATH/$MERO_RPM $REMOTE_RPM_PATH
		$PDSH yum -y install $REMOTE_RPM_PATH/$MERO_RPM
		$PDCP $HALON_RPM_PATH/$HALON_RPM $REMOTE_RPM_PATH
		$PDSH yum -y install $REMOTE_RPM_PATH/$HALON_RPM
		;;
	"start_halon")
		$PDSH rm -rvf halon-persistence
		pdsh -w $HALOND_NODES \
			"IP=\$(ip -o -4 addr show dev $HALOND_NET_IF | \
			awk -F '[ /]+' '{print \$4}'); \
			halond -l \$IP:$HALOND_PORT >& $HALOND_LOG &"
		sleep 3
		local TS_IP=$(ssh $HALOND_TS \
			      ip -o -4 addr show dev $HALOND_NET_IF | \
			      awk -F '[ /]+' '{print $4}')
		pdsh -w $HALOND_TS \
			halonctl -l $TS_IP:$HALONCTL_PORT -a $TS_IP:$HALOND_PORT \
			bootstrap station
		sleep 3
		pdsh -w $HALOND_SAT \
			"IP=\$(ip -o -4 addr show dev $HALOND_NET_IF | \
			awk -F '[ /]+' '{print \$4}'); \
			halonctl -l \$IP:$HALONCTL_PORT -a \$IP:$HALOND_PORT \
			bootstrap satellite -t $TS_IP:$HALOND_PORT"
		sleep 3
		;;
	"bootstrap")
		local TS_IP=$(ssh $HALOND_TS \
			      ip -o -4 addr show dev $HALOND_NET_IF | \
			      awk -F '[ /]+' '{print $4}')
		${HALON_FACTS_FUNC} | ssh $HALOND_TS "cat > $HALON_FACTS"
		ssh $HALOND_TS \
			halonctl -l $TS_IP:$HALONCTL_PORT -a $TS_IP:$HALOND_PORT \
			cluster load -f $HALON_FACTS -r $ROLE_MAPS
		sleep 1
		ssh $HALOND_TS \
			halonctl -l $TS_IP:$HALONCTL_PORT -a $TS_IP:$HALOND_PORT \
			cluster start
		;;
	"status")
		local TS_IP=$(ssh $HALOND_TS \
			      ip -o -4 addr show dev $HALOND_NET_IF | \
			      awk -F '[ /]+' '{print $4}')
		ssh $HALOND_TS \
			halonctl -l $TS_IP:$HALONCTL_PORT \
			$(pdsh -w $HALOND_NODES \
			  "ip -o -4 addr show dev $HALOND_NET_IF" | sort | \
			  awk -v HALOND_PORT=$HALOND_PORT \
			  -F '[ /]+' '{print "-a "$5":"HALOND_PORT}') \
		        status
		;;
	"halon_facts_yaml")
		${HALON_FACTS_FUNC}
		;;
	*)
		echo "Unknown command $1"
		show_help
		exit 1
	esac
}

main() {
	if [ $# -eq 0 ]; then
		show_help
	fi
	case `hostname` in
	"castor-beta1-cc1.xy01.xyratex.com")
		configure_beta1
		;;
	"castor-dev1-1-cc1.xy01.xyratex.com")
		configure_dev1_1
		;;
	*)
		echo "Unknown cluster"
		exit 1
		;;
	esac
	configure_common
	while getopts "h?c:" opt; do
		case "$opt" in
		h|\?)
			show_help
			;;
		c)
			COMMAND=$OPTARG
			run_command $COMMAND
			;;
		esac
		COMMAND=""
	done
	shift $((OPTIND-1))
}

build_mero() {
        cd $MERO_SOURCES
        git clean -dfx
        sh autogen.sh
        # take it from m0d -v after the first rpm build
        ./configure '--build=x86_64-redhat-linux-gnu' '--host=x86_64-redhat-linux-gnu' '--program-prefix=' '--disable-dependency-tracking' '--prefix=/usr' '--exec-prefix=/usr' '--bindir=/usr/bin' '--sbindir=/usr/sbin' '--sysconfdir=/etc' '--datadir=/usr/share' '--includedir=/usr/include' '--libdir=/usr/lib64' '--libexecdir=/usr/libexec' '--localstatedir=/var' '--sharedstatedir=/var/lib' '--mandir=/usr/share/man' '--infodir=/usr/share/info' '--enable-release' '--with-trace-kbuf-size=256' '--with-trace-ubuf-size=64' 'build_alias=x86_64-redhat-linux-gnu' 'host_alias=x86_64-redhat-linux-gnu' 'CFLAGS=-O2 -g -pipe -Wall -Wp,-D_FORTIFY_SOURCE=2 -fexceptions -fstack-protector-strong --param=ssp-buffer-size=4 -grecord-gcc-switches   -m64 -mtune=generic' 'LDFLAGS=-Wl,-z,relro '
        make rpms-notests
        make -j
}

stack_call() {
	MERO_ROOT=$MERO_SOURCES LD_LIBRARY_PATH=$MERO_SOURCES/mero/.libs stack "$@"
}

build_halon() {
	cd $HALON_SOURCES
	if [ "$(grep $MERO_SOURCES stack.yaml)" == "" ]; then
		sed -i "s:/mero:$MERO_SOURCES:" stack.yaml
	fi
	stack_call setup --no-docker
	stack_call clean rpclite --no-docker
	stack_call build --flag mero-halon:mero --no-docker
	make rpm-dev
}

function halon_facts_yaml_beta1() {
	cat << EOF
---
id_racks:
     - rack_idx: 0
       rack_enclosures:
           - enc_idx: 41
             enc_id: SHX1017161Y0C56
             enc_bmc:
                 - bmc_addr: "10.22.193.174"
                   bmc_user: "admin"
                   bmc_pass: "admin"
                 - bmc_addr: "10.22.193.226"
                   bmc_user: "admin"
                   bmc_pass: "admin"
             enc_hosts:
                 - h_fqdn: "castor-beta1-cc1"
                   h_memsize: 64231.04
                   h_cpucount: 20
                   h_interfaces:
                     - if_macAddress: "00:50:cc:7a:3d:c3"
                       if_network: Management
                       if_ipAddrs: [172.16.0.41]
                     - if_macAddress: "00:50:cc:7a:3d:c3"
                       if_network: Data
                       if_ipAddrs: [172.18.0.41]
     - rack_idx: 1
       rack_enclosures:
           - enc_idx: 1
             enc_id: SHX1017159Y0C3W
             enc_bmc:
                 - bmc_addr: "172.16.1.101"
                   bmc_user: "admin"
                   bmc_pass: "admin"
                 - bmc_addr: "172.16.1.121"
                   bmc_user: "admin"
                   bmc_pass: "admin"
             enc_hosts:
                 - h_fqdn: "castor-beta1-ssu-1-1"
                   h_memsize: 64230.46
                   h_cpucount: 20
                   h_interfaces:
                     - if_macAddress: "00:50:cc:79:be:0f"
                       if_network: Management
                       if_ipAddrs: [172.16.1.1]
                     - if_macAddress: "00:50:cc:79:be:13"
                       if_network: Data
                       if_ipAddrs: [172.18.1.1]
           - enc_idx: 2
             enc_id: SHX1017159Y0C3R
             enc_bmc:
                 - bmc_addr: "172.16.1.102"
                   bmc_user: "admin"
                   bmc_pass: "admin"
                 - bmc_addr: "172.16.1.122"
                   bmc_user: "admin"
                   bmc_pass: "admin"
             enc_hosts:
                 - h_fqdn: "castor-beta1-ssu-1-2"
                   h_memsize: 64230.46
                   h_cpucount: 20
                   h_interfaces:
                     - if_macAddress: "00:50:cc:79:bd:c7"
                       if_network: Management
                       if_ipAddrs: [172.16.1.2]
                     - if_macAddress: "00:50:cc:79:bd:cb"
                       if_network: Data
                       if_ipAddrs: [172.18.1.2]
           - enc_idx: 3
             enc_id: SHX1017159Y0C3X
             enc_bmc:
                 - bmc_addr: "172.16.1.103"
                   bmc_user: "admin"
                   bmc_pass: "admin"
                 - bmc_addr: "172.16.1.123"
                   bmc_user: "admin"
                   bmc_pass: "admin"
             enc_hosts:
                 - h_fqdn: "castor-beta1-ssu-1-3"
                   h_memsize: 64230.46
                   h_cpucount: 20
                   h_interfaces:
                     - if_macAddress: "00:50:cc:79:be:45"
                       if_network: Management
                       if_ipAddrs: [172.16.1.3]
                     - if_macAddress: "00:50:cc:79:be:49"
                       if_network: Data
                       if_ipAddrs: [172.18.1.3]
           - enc_idx: 4
             enc_id: SHX1017159Y0C46
             enc_bmc:
                 - bmc_addr: "172.16.1.104"
                   bmc_user: "admin"
                   bmc_pass: "admin"
                 - bmc_addr: "172.16.1.124"
                   bmc_user: "admin"
                   bmc_pass: "admin"
             enc_hosts:
                 - h_fqdn: "castor-beta1-ssu-1-4"
                   h_memsize: 64230.46
                   h_cpucount: 20
                   h_interfaces:
                     - if_macAddress: "00:50:cc:79:bd:b5"
                       if_network: Management
                       if_ipAddrs: [172.16.1.4]
                     - if_macAddress: "00:50:cc:79:bd:b9"
                       if_network: Data
                       if_ipAddrs: [172.18.1.4]
           - enc_idx: 5
             enc_id: SHX1017159Y0C3Y
             enc_bmc:
                 - bmc_addr: "172.16.1.105"
                   bmc_user: "admin"
                   bmc_pass: "admin"
                 - bmc_addr: "172.16.1.125"
                   bmc_user: "admin"
                   bmc_pass: "admin"
             enc_hosts:
                 - h_fqdn: "castor-beta1-ssu-1-5"
                   h_memsize: 64230.46
                   h_cpucount: 20
                   h_interfaces:
                     - if_macAddress: "00:50:cc:79:b1:b5"
                       if_network: Management
                       if_ipAddrs: [172.16.1.5]
                     - if_macAddress: "00:50:cc:79:b1:b9"
                       if_network: Data
                       if_ipAddrs: [172.18.1.5]
           - enc_idx: 6
             enc_id: SHX1017159Y0C3T
             enc_bmc:
                 - bmc_addr: "172.16.1.106"
                   bmc_user: "admin"
                   bmc_pass: "admin"
                 - bmc_addr: "172.16.1.126"
                   bmc_user: "admin"
                   bmc_pass: "admin"
             enc_hosts:
                 - h_fqdn: "castor-beta1-ssu-1-6"
                   h_memsize: 64230.46
                   h_cpucount: 20
                   h_interfaces:
                     - if_macAddress: "00:50:cc:79:bd:af"
                       if_network: Management
                       if_ipAddrs: [172.16.1.6]
                     - if_macAddress: "00:50:cc:79:bd:b3"
                       if_network: Data
                       if_ipAddrs: [172.18.1.6]

id_m0_servers:
  - m0h_fqdn: "castor-beta1-ssu-1-1"
    host_mem_as: 536870912
    host_mem_rss: 65772544
    host_mem_stack: 65772544
    host_mem_memlock: 65772544
    host_cores: [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]
    lnid: "172.18.1.1@tcp"
    m0h_roles:
      - name: ha
      - name: storage
      - name: mds
      - name: confd
      - name: m0t1fs

    m0h_devices:
      - m0d_wwn: "0x5000c5007bd8a15a"
        m0d_serial: "Z8407RJ1"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8a15a"
      - m0d_wwn: "0x5000c5007bd4c8a5"
        m0d_serial: "Z8407NQS"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4c8a5"
      - m0d_wwn: "0x5000c5007bd4993d"
        m0d_serial: "Z84069AH"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4993d"
      - m0d_wwn: "0x5000c5007bd80b12"
        m0d_serial: "Z8407RYY"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd80b12"
      - m0d_wwn: "0x5000c5007bd8176f"
        m0d_serial: "Z8407RYZ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8176f"
      - m0d_wwn: "0x5000c5007bd88b05"
        m0d_serial: "Z8407R0V"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd88b05"
      - m0d_wwn: "0x5000c5007bd80303"
        m0d_serial: "Z8407S40"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd80303"
      - m0d_wwn: "0x5000c5007bd88c49"
        m0d_serial: "Z8407R0P"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd88c49"
      - m0d_wwn: "0x5000c5007bd8043c"
        m0d_serial: "Z8407S61"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8043c"
      - m0d_wwn: "0x5000c5007bd87a41"
        m0d_serial: "Z8407RB1"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87a41"
      - m0d_wwn: "0x5000c5007bc634e3"
        m0d_serial: "Z8407GQ4"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc634e3"
      - m0d_wwn: "0x5000c5007bd5396e"
        m0d_serial: "Z8407LZV"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd5396e"
      - m0d_wwn: "0x5000c5007bd51dbc"
        m0d_serial: "Z8407M34"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd51dbc"
      - m0d_wwn: "0x5000c5007bc6447f"
        m0d_serial: "Z8407GJE"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc6447f"
      - m0d_wwn: "0x5000c5007bc61df5"
        m0d_serial: "Z8407H61"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc61df5"
      - m0d_wwn: "0x5000c5007bd88589"
        m0d_serial: "Z8407R5H"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd88589"
      - m0d_wwn: "0x5000c5007bc66d41"
        m0d_serial: "Z8407G57"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc66d41"
      - m0d_wwn: "0x5000c5007bc658c0"
        m0d_serial: "Z8407GEA"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc658c0"
      - m0d_wwn: "0x5000c5007bc63583"
        m0d_serial: "Z8407GRZ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc63583"
      - m0d_wwn: "0x5000c5007bd4df85"
        m0d_serial: "Z8407MVV"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4df85"
      - m0d_wwn: "0x5000c5007bd5160d"
        m0d_serial: "Z8407M99"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd5160d"
      - m0d_wwn: "0x5000c5007bd88a57"
        m0d_serial: "Z8407R2E"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd88a57"
      - m0d_wwn: "0x5000c5007bd80762"
        m0d_serial: "Z8407S3Q"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd80762"
      - m0d_wwn: "0x5000c5007bd4d142"
        m0d_serial: "Z8407NFM"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4d142"
      - m0d_wwn: "0x5000c5007bd8d5e4"
        m0d_serial: "Z8407Q38"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8d5e4"
      - m0d_wwn: "0x5000c5007bd88777"
        m0d_serial: "Z8407R5J"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd88777"
      - m0d_wwn: "0x5000c5007bd8a5af"
        m0d_serial: "Z8407QZB"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8a5af"
      - m0d_wwn: "0x5000c5007bd4cf4a"
        m0d_serial: "Z8407N5Z"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4cf4a"
      - m0d_wwn: "0x5000c5007bd89baa"
        m0d_serial: "Z8407QVX"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd89baa"
      - m0d_wwn: "0x5000c5007bd810be"
        m0d_serial: "Z8407RXS"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd810be"
      - m0d_wwn: "0x5000c5007bc65b19"
        m0d_serial: "Z8407GBR"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc65b19"
      - m0d_wwn: "0x5000c5007bd80405"
        m0d_serial: "Z8407S3T"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd80405"
      - m0d_wwn: "0x5000c5007bc63260"
        m0d_serial: "Z8407H0Y"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc63260"
      - m0d_wwn: "0x5000c5007bc642e9"
        m0d_serial: "Z8407GY3"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc642e9"
      - m0d_wwn: "0x5000c5007bc66089"
        m0d_serial: "Z8407GAS"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc66089"
      - m0d_wwn: "0x5000c5007bd86565"
        m0d_serial: "Z8407RLN"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd86565"
      - m0d_wwn: "0x5000c5007bd4db29"
        m0d_serial: "Z8407N0X"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4db29"
      - m0d_wwn: "0x5000c5007bc644bc"
        m0d_serial: "Z8407GKC"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc644bc"
      - m0d_wwn: "0x5000c5007bd4dd9d"
        m0d_serial: "Z8407N05"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4dd9d"
      - m0d_wwn: "0x5000c5007bd8851d"
        m0d_serial: "Z8407R42"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8851d"
      - m0d_wwn: "0x5000c5007bd4df65"
        m0d_serial: "Z8407N34"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4df65"
      - m0d_wwn: "0x5000c5007bd4c57e"
        m0d_serial: "Z8407NHM"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4c57e"
      - m0d_wwn: "0x5000c5007bd4dba1"
        m0d_serial: "Z8407N17"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4dba1"
      - m0d_wwn: "0x5000c5007bd83a63"
        m0d_serial: "Z8407SJH"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd83a63"
      - m0d_wwn: "0x5000c5007bd8219b"
        m0d_serial: "Z8407RSR"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8219b"
      - m0d_wwn: "0x5000c5007bd89b90"
        m0d_serial: "Z8407RHH"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd89b90"
      - m0d_wwn: "0x5000c5007bc66003"
        m0d_serial: "Z8407G9J"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc66003"
      - m0d_wwn: "0x5000c5007bd51283"
        m0d_serial: "Z8407M90"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd51283"
      - m0d_wwn: "0x5000c5007bc63171"
        m0d_serial: "Z8407GSA"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc63171"
      - m0d_wwn: "0x5000c5007bc625ad"
        m0d_serial: "Z8407H39"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc625ad"
      - m0d_wwn: "0x5000c5007bc65888"
        m0d_serial: "Z8407GE9"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc65888"
      - m0d_wwn: "0x5000c5007bd805fa"
        m0d_serial: "Z8407S2S"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd805fa"
      - m0d_wwn: "0x5000c5007bd89cc0"
        m0d_serial: "Z8407RB6"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd89cc0"
      - m0d_wwn: "0x5000c5007bd8c277"
        m0d_serial: "Z8407QDS"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8c277"
      - m0d_wwn: "0x5000c5007bd4f990"
        m0d_serial: "Z8407MV5"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4f990"
      - m0d_wwn: "0x5000c5007bd89ab5"
        m0d_serial: "Z8407QWC"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd89ab5"
      - m0d_wwn: "0x5000c5007bc61f23"
        m0d_serial: "Z8407H95"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc61f23"
      - m0d_wwn: "0x5000c5007bd820b7"
        m0d_serial: "Z8407RNK"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd820b7"
      - m0d_wwn: "0x5000c5007bd87f52"
        m0d_serial: "Z8407RGX"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87f52"
      - m0d_wwn: "0x5000c5007bd8ce10"
        m0d_serial: "Z8407Q8A"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8ce10"
      - m0d_wwn: "0x5000c5007bc63920"
        m0d_serial: "Z8407GN4"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc63920"
      - m0d_wwn: "0x5000c5007bd88b40"
        m0d_serial: "Z8407R10"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd88b40"
      - m0d_wwn: "0x5000c5007bd81aac"
        m0d_serial: "Z8407RP0"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd81aac"
      - m0d_wwn: "0x5000c5007bd8ac0f"
        m0d_serial: "Z8407QX5"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8ac0f"
      - m0d_wwn: "0x5000c5007bc61ece"
        m0d_serial: "Z8407H50"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc61ece"
      - m0d_wwn: "0x5000c5007bd8783b"
        m0d_serial: "Z8407R75"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8783b"
      - m0d_wwn: "0x5000c5007bd88c25"
        m0d_serial: "Z8407R0B"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd88c25"
      - m0d_wwn: "0x5000c5007bd89d84"
        m0d_serial: "Z8407QYV"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd89d84"
      - m0d_wwn: "0x5000c5007bd4c808"
        m0d_serial: "Z8407ND9"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4c808"
      - m0d_wwn: "0x5000c5007bd8b6ae"
        m0d_serial: "Z8407QJZ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8b6ae"
      - m0d_wwn: "0x5000c5007bd8ceee"
        m0d_serial: "Z8407Q82"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8ceee"
      - m0d_wwn: "0x5000c5007bc62b53"
        m0d_serial: "Z8407GYK"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc62b53"
      - m0d_wwn: "0x5000c5007bd50cfa"
        m0d_serial: "Z8407MBH"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd50cfa"
      - m0d_wwn: "0x5000c5007bd8b524"
        m0d_serial: "Z8407QJF"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8b524"
      - m0d_wwn: "0x5000c5007bd8b502"
        m0d_serial: "Z8407QJB"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8b502"
      - m0d_wwn: "0x5000c5007bd4af70"
        m0d_serial: "Z8407NT5"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4af70"
      - m0d_wwn: "0x5000c5007bd8173d"
        m0d_serial: "Z8407RQT"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8173d"
      - m0d_wwn: "0x5000c5007bd81010"
        m0d_serial: "Z8407S78"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd81010"
      - m0d_wwn: "0x5000c5007bc61e3d"
        m0d_serial: "Z8407H5S"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc61e3d"
      - m0d_wwn: "0x5000c5007bd4d2f4"
        m0d_serial: "Z8407N74"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4d2f4"
  - m0h_fqdn: "castor-beta1-ssu-1-2"
    host_mem_as: 536870912
    host_mem_rss: 65772544
    host_mem_stack: 65772544
    host_mem_memlock: 65772544
    host_cores: [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]
    lnid: "172.18.1.2@tcp"
    m0h_roles:
      - name: ha
      - name: storage

    m0h_devices:
      - m0d_wwn: "0x5000c5007bd87fdb"
        m0d_serial: "Z8407R6V"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87fdb"
      - m0d_wwn: "0x5000c5007bd8ba17"
        m0d_serial: "Z8407QYN"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8ba17"
      - m0d_wwn: "0x5000c5007bd8c7a5"
        m0d_serial: "Z8407Q85"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8c7a5"
      - m0d_wwn: "0x5000c5007bd87f2f"
        m0d_serial: "Z8407R6W"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87f2f"
      - m0d_wwn: "0x5000c5007bd80751"
        m0d_serial: "Z8407S50"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd80751"
      - m0d_wwn: "0x5000c5007bd83c04"
        m0d_serial: "Z8407SFX"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd83c04"
      - m0d_wwn: "0x5000c5007bd8cf17"
        m0d_serial: "Z8407Q5L"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8cf17"
      - m0d_wwn: "0x5000c5007bd808d1"
        m0d_serial: "Z8407SJ2"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd808d1"
      - m0d_wwn: "0x5000c5007bd88b42"
        m0d_serial: "Z8407R12"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd88b42"
      - m0d_wwn: "0x5000c5007bd81e1c"
        m0d_serial: "Z8407RN3"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd81e1c"
      - m0d_wwn: "0x5000c5007bd8b079"
        m0d_serial: "Z8407QPQ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8b079"
      - m0d_wwn: "0x5000c5007bd8df03"
        m0d_serial: "Z8407Q03"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8df03"
      - m0d_wwn: "0x5000c5007bd88083"
        m0d_serial: "Z8407RA6"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd88083"
      - m0d_wwn: "0x5000c5007bd8a5d5"
        m0d_serial: "Z8407QRG"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8a5d5"
      - m0d_wwn: "0x5000c5007bd8224a"
        m0d_serial: "Z84069EJ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8224a"
      - m0d_wwn: "0x5000c5007bd817c1"
        m0d_serial: "Z8407S9K"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd817c1"
      - m0d_wwn: "0x5000c5007bd8a194"
        m0d_serial: "Z8407RHC"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8a194"
      - m0d_wwn: "0x5000c5007bd812de"
        m0d_serial: "Z8407RVF"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd812de"
      - m0d_wwn: "0x5000c5007bd8c843"
        m0d_serial: "Z8407QA3"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8c843"
      - m0d_wwn: "0x5000c5007bd889ec"
        m0d_serial: "Z8407R4V"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd889ec"
      - m0d_wwn: "0x5000c5007bd89b4d"
        m0d_serial: "Z8407QW0"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd89b4d"
      - m0d_wwn: "0x5000c5007bd83b25"
        m0d_serial: "Z8407SGF"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd83b25"
      - m0d_wwn: "0x5000c5007bd884ee"
        m0d_serial: "Z8407R5C"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd884ee"
      - m0d_wwn: "0x5000c5007bd86413"
        m0d_serial: "Z8407RJ3"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd86413"
      - m0d_wwn: "0x5000c5007bd8c6ee"
        m0d_serial: "Z8407Q98"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8c6ee"
      - m0d_wwn: "0x5000c5007bd87acc"
        m0d_serial: "Z8407RG6"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87acc"
      - m0d_wwn: "0x5000c5007bd879a3"
        m0d_serial: "Z8407RCY"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd879a3"
      - m0d_wwn: "0x5000c5007bd8dde2"
        m0d_serial: "Z8407PZC"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8dde2"
      - m0d_wwn: "0x5000c5007bd822be"
        m0d_serial: "Z8407SBN"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd822be"
      - m0d_wwn: "0x5000c5007bd89b26"
        m0d_serial: "Z8407RKL"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd89b26"
      - m0d_wwn: "0x5000c5007bd88729"
        m0d_serial: "Z8407R4H"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd88729"
      - m0d_wwn: "0x5000c5007bd8c7ba"
        m0d_serial: "Z8407QAB"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8c7ba"
      - m0d_wwn: "0x5000c5007bd808d4"
        m0d_serial: "Z8407S4R"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd808d4"
      - m0d_wwn: "0x5000c5007bd879eb"
        m0d_serial: "Z8407R7K"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd879eb"
      - m0d_wwn: "0x5000c5007bd817e0"
        m0d_serial: "Z8407RQY"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd817e0"
      - m0d_wwn: "0x5000c5007bd8158e"
        m0d_serial: "Z8407RR7"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8158e"
      - m0d_wwn: "0x5000c5007bd88052"
        m0d_serial: "Z8407RBM"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd88052"
      - m0d_wwn: "0x5000c5007bd811f4"
        m0d_serial: "Z8407RWL"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd811f4"
      - m0d_wwn: "0x5000c5007bd8a128"
        m0d_serial: "Z8407QTE"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8a128"
      - m0d_wwn: "0x5000c5007bd87be8"
        m0d_serial: "Z8407R9C"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87be8"
      - m0d_wwn: "0x5000c5007bd8d5b8"
        m0d_serial: "Z8407Q1X"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8d5b8"
      - m0d_wwn: "0x5000c5007bd8bd59"
        m0d_serial: "Z8407QS6"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8bd59"
      - m0d_wwn: "0x5000c5007bd8a7ea"
        m0d_serial: "Z8407QQ9"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8a7ea"
      - m0d_wwn: "0x5000c5007bd87af0"
        m0d_serial: "Z8407RG3"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87af0"
      - m0d_wwn: "0x5000c5007bd89d50"
        m0d_serial: "Z8407QTF"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd89d50"
      - m0d_wwn: "0x5000c5007bd823e0"
        m0d_serial: "Z8407SD1"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd823e0"
      - m0d_wwn: "0x5000c5007bd81337"
        m0d_serial: "Z8407SEA"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd81337"
      - m0d_wwn: "0x5000c5007bd87dfa"
        m0d_serial: "Z8407RDT"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87dfa"
      - m0d_wwn: "0x5000c5007bd82386"
        m0d_serial: "Z8407SD4"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd82386"
      - m0d_wwn: "0x5000c5007bd82228"
        m0d_serial: "Z8407SDD"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd82228"
      - m0d_wwn: "0x5000c5007bd8ed0b"
        m0d_serial: "Z8407PW9"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8ed0b"
      - m0d_wwn: "0x5000c5007bd8c726"
        m0d_serial: "Z8407QAJ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8c726"
      - m0d_wwn: "0x5000c5007bd8d617"
        m0d_serial: "Z8407Q3P"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8d617"
      - m0d_wwn: "0x5000c5007bd8103f"
        m0d_serial: "Z8407RWT"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8103f"
      - m0d_wwn: "0x5000c5007bd88c53"
        m0d_serial: "Z8407QZR"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd88c53"
      - m0d_wwn: "0x5000c5007bd83938"
        m0d_serial: "Z8407SGN"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd83938"
      - m0d_wwn: "0x5000c5007bd804cc"
        m0d_serial: "Z8407S54"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd804cc"
      - m0d_wwn: "0x5000c5007bd83ea3"
        m0d_serial: "Z8407SFC"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd83ea3"
      - m0d_wwn: "0x5000c5007bd8acc7"
        m0d_serial: "Z8407QQR"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8acc7"
      - m0d_wwn: "0x5000c5007bd8cf14"
        m0d_serial: "Z8407Q5A"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8cf14"
      - m0d_wwn: "0x5000c5007bd806dc"
        m0d_serial: "Z8407S1M"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd806dc"
      - m0d_wwn: "0x5000c5007bd8b96c"
        m0d_serial: "Z8407QX0"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8b96c"
      - m0d_wwn: "0x5000c5007bd8b5b7"
        m0d_serial: "Z8407QJW"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8b5b7"
      - m0d_wwn: "0x5000c5007bd83977"
        m0d_serial: "Z8407SJZ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd83977"
      - m0d_wwn: "0x5000c5007bd81300"
        m0d_serial: "Z8407S8V"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd81300"
      - m0d_wwn: "0x5000c5007bd8ba34"
        m0d_serial: "Z8407QEP"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8ba34"
      - m0d_wwn: "0x5000c5007bd7d114"
        m0d_serial: "Z8407T4Q"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd7d114"
      - m0d_wwn: "0x5000c5007bd8066f"
        m0d_serial: "Z8407S26"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8066f"
      - m0d_wwn: "0x5000c5007bd807d0"
        m0d_serial: "Z8407S23"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd807d0"
      - m0d_wwn: "0x5000c5007bd8096d"
        m0d_serial: "Z8407RYW"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8096d"
      - m0d_wwn: "0x5000c5007bd8c7c5"
        m0d_serial: "Z8407QAT"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8c7c5"
      - m0d_wwn: "0x5000c5007bd7dfec"
        m0d_serial: "Z8407SQ4"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd7dfec"
      - m0d_wwn: "0x5000c5007bd8a5ed"
        m0d_serial: "Z8407QR8"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8a5ed"
      - m0d_wwn: "0x5000c5007bd8b115"
        m0d_serial: "Z8407R25"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8b115"
      - m0d_wwn: "0x5000c5007bd88150"
        m0d_serial: "Z8407R83"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd88150"
      - m0d_wwn: "0x5000c5007bd80497"
        m0d_serial: "Z8407S56"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd80497"
      - m0d_wwn: "0x5000c5007bd8bb1a"
        m0d_serial: "Z8407QNG"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8bb1a"
      - m0d_wwn: "0x5000c5007bd80433"
        m0d_serial: "Z8407SG9"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd80433"
      - m0d_wwn: "0x5000c5007bd813e9"
        m0d_serial: "Z8407RSK"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd813e9"
      - m0d_wwn: "0x5000c5007bd8112c"
        m0d_serial: "Z8407S5G"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8112c"
  - m0h_fqdn: "castor-beta1-ssu-1-3"
    host_mem_as: 536870912
    host_mem_rss: 65772544
    host_mem_stack: 65772544
    host_mem_memlock: 65772544
    host_cores: [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]
    lnid: "172.18.1.3@tcp"
    m0h_roles:
      - name: ha
      - name: storage

    m0h_devices:
      - m0d_wwn: "0x5000c5007bd5237b"
        m0d_serial: "Z8407M7R"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd5237b"
      - m0d_wwn: "0x5000c5007bd4c0cd"
        m0d_serial: "Z8407NBG"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4c0cd"
      - m0d_wwn: "0x5000c5007bd4bf16"
        m0d_serial: "Z8407NBF"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4bf16"
      - m0d_wwn: "0x5000c5007bd50b27"
        m0d_serial: "Z8407MCQ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd50b27"
      - m0d_wwn: "0x5000c5007bd8bd12"
        m0d_serial: "Z8407QMF"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8bd12"
      - m0d_wwn: "0x5000c5007bd50abd"
        m0d_serial: "Z8407MDE"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd50abd"
      - m0d_wwn: "0x5000c5007bd4b020"
        m0d_serial: "Z8407NRX"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4b020"
      - m0d_wwn: "0x5000c5007bd4d9b5"
        m0d_serial: "Z8407N00"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4d9b5"
      - m0d_wwn: "0x5000c5007bd51d00"
        m0d_serial: "Z8407M6X"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd51d00"
      - m0d_wwn: "0x5000c5007bd80931"
        m0d_serial: "Z8407RZ2"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd80931"
      - m0d_wwn: "0x5000c5007bd8b7bd"
        m0d_serial: "Z8407QKV"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8b7bd"
      - m0d_wwn: "0x5000c5007bd4fbf7"
        m0d_serial: "Z8407MGX"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4fbf7"
      - m0d_wwn: "0x5000c5007bd5395d"
        m0d_serial: "Z8407LZN"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd5395d"
      - m0d_wwn: "0x5000c5007bd812fe"
        m0d_serial: "Z8407RTF"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd812fe"
      - m0d_wwn: "0x5000c5007bd8652f"
        m0d_serial: "Z8407RJA"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8652f"
      - m0d_wwn: "0x5000c5007bd81d8a"
        m0d_serial: "Z8407SDR"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd81d8a"
      - m0d_wwn: "0x5000c5007bd88106"
        m0d_serial: "Z8407R5X"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd88106"
      - m0d_wwn: "0x5000c5007bd8b162"
        m0d_serial: "Z8407QPN"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8b162"
      - m0d_wwn: "0x5000c5007bd86707"
        m0d_serial: "Z8407RKV"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd86707"
      - m0d_wwn: "0x5000c5007bd51cce"
        m0d_serial: "Z8407M3V"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd51cce"
      - m0d_wwn: "0x5000c5007bd509e8"
        m0d_serial: "Z8407MBK"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd509e8"
      - m0d_wwn: "0x5000c5007bd5098f"
        m0d_serial: "Z8407MBM"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd5098f"
      - m0d_wwn: "0x5000c5007bd4c765"
        m0d_serial: "Z8407NEE"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4c765"
      - m0d_wwn: "0x5000c5007bd4f89c"
        m0d_serial: "Z8407MJX"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4f89c"
      - m0d_wwn: "0x5000c5007bd51bd1"
        m0d_serial: "Z8407M5L"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd51bd1"
      - m0d_wwn: "0x5000c5007bd8373f"
        m0d_serial: "Z8407SJY"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8373f"
      - m0d_wwn: "0x5000c5007bd4a719"
        m0d_serial: "Z840696T"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4a719"
      - m0d_wwn: "0x5000c5007bd4d12b"
        m0d_serial: "Z8407N6X"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4d12b"
      - m0d_wwn: "0x5000c5007bd4ddd6"
        m0d_serial: "Z8407MZS"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4ddd6"
      - m0d_wwn: "0x5000c5007bd87ba0"
        m0d_serial: "Z8407RCF"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87ba0"
      - m0d_wwn: "0x5000c5007bd89fff"
        m0d_serial: "Z8407RFW"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd89fff"
      - m0d_wwn: "0x5000c5007bd8bdb6"
        m0d_serial: "Z8407QGC"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8bdb6"
      - m0d_wwn: "0x5000c5007bd87900"
        m0d_serial: "Z8407RGC"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87900"
      - m0d_wwn: "0x5000c5007bd87921"
        m0d_serial: "Z8407R8F"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87921"
      - m0d_wwn: "0x5000c5007bd809ea"
        m0d_serial: "Z8407S4C"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd809ea"
      - m0d_wwn: "0x5000c5007bd505dd"
        m0d_serial: "Z8407MED"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd505dd"
      - m0d_wwn: "0x5000c5007bd804c4"
        m0d_serial: "Z8407SJG"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd804c4"
      - m0d_wwn: "0x5000c5007bd8c2e0"
        m0d_serial: "Z8407QD8"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8c2e0"
      - m0d_wwn: "0x5000c5007bd8ba93"
        m0d_serial: "Z8407QP0"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8ba93"
      - m0d_wwn: "0x5000c5007bd55f2e"
        m0d_serial: "Z8407LZF"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd55f2e"
      - m0d_wwn: "0x5000c5007bd523df"
        m0d_serial: "Z8407M1Y"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd523df"
      - m0d_wwn: "0x5000c5007bd4e798"
        m0d_serial: "Z8407N0M"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4e798"
      - m0d_wwn: "0x5000c5007bd81434"
        m0d_serial: "Z8407RWH"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd81434"
      - m0d_wwn: "0x5000c5007bd4d5c8"
        m0d_serial: "Z8407N63"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4d5c8"
      - m0d_wwn: "0x5000c5007bd4da79"
        m0d_serial: "Z8407N2G"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4da79"
      - m0d_wwn: "0x5000c5007bd866dd"
        m0d_serial: "Z8407RK4"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd866dd"
      - m0d_wwn: "0x5000c5007bd4be71"
        m0d_serial: "Z8407NGX"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4be71"
      - m0d_wwn: "0x5000c5007bd813d4"
        m0d_serial: "Z8407RS2"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd813d4"
      - m0d_wwn: "0x5000c5007bd89f35"
        m0d_serial: "Z8407QZH"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd89f35"
      - m0d_wwn: "0x5000c5007bd8c318"
        m0d_serial: "Z8407QCA"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8c318"
      - m0d_wwn: "0x5000c5007bd4fd91"
        m0d_serial: "Z8407MH9"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4fd91"
      - m0d_wwn: "0x5000c5007bd4ef2a"
        m0d_serial: "Z8407MKE"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4ef2a"
      - m0d_wwn: "0x5000c5007bd864c3"
        m0d_serial: "Z8407RHA"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd864c3"
      - m0d_wwn: "0x5000c5007bd4ef74"
        m0d_serial: "Z8407MLP"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4ef74"
      - m0d_wwn: "0x5000c5007bd4beb1"
        m0d_serial: "Z8407NFK"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4beb1"
      - m0d_wwn: "0x5000c5007bd4adaf"
        m0d_serial: "Z8407NRG"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4adaf"
      - m0d_wwn: "0x5000c5007bd81077"
        m0d_serial: "Z8407S5C"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd81077"
      - m0d_wwn: "0x5000c5007bd8184a"
        m0d_serial: "Z8407RSJ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8184a"
      - m0d_wwn: "0x5000c5007bd4e9e5"
        m0d_serial: "Z8407MRT"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4e9e5"
      - m0d_wwn: "0x5000c5007bd4ca70"
        m0d_serial: "Z8407NND"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4ca70"
      - m0d_wwn: "0x5000c5007bd4a7ea"
        m0d_serial: "Z8407NSP"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4a7ea"
      - m0d_wwn: "0x5000c5007bd806f2"
        m0d_serial: "Z8407S03"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd806f2"
      - m0d_wwn: "0x5000c5007bd8064c"
        m0d_serial: "Z8407S36"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8064c"
      - m0d_wwn: "0x5000c5007bd8668e"
        m0d_serial: "Z8407RJ9"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8668e"
      - m0d_wwn: "0x5000c5007bd88a6e"
        m0d_serial: "Z8407R2A"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd88a6e"
      - m0d_wwn: "0x5000c5007bd81553"
        m0d_serial: "Z8407S7C"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd81553"
      - m0d_wwn: "0x5000c5007bd4c1a3"
        m0d_serial: "Z8407NCG"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4c1a3"
      - m0d_wwn: "0x5000c5007bd8bb3d"
        m0d_serial: "Z8407QND"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8bb3d"
      - m0d_wwn: "0x5000c5007bd4cd7c"
        m0d_serial: "Z8407N97"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4cd7c"
      - m0d_wwn: "0x5000c5007bd50ade"
        m0d_serial: "Z8407MMT"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd50ade"
      - m0d_wwn: "0x5000c5007bd81241"
        m0d_serial: "Z8407RTB"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd81241"
      - m0d_wwn: "0x5000c5007bd52138"
        m0d_serial: "Z8407M37"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd52138"
      - m0d_wwn: "0x5000c5007bd8b25e"
        m0d_serial: "Z8407RL7"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8b25e"
      - m0d_wwn: "0x5000c5007bd51c5a"
        m0d_serial: "Z8407M6Z"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd51c5a"
      - m0d_wwn: "0x5000c5007bd83f71"
        m0d_serial: "Z8407SJK"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd83f71"
      - m0d_wwn: "0x5000c5007bd87fd0"
        m0d_serial: "Z8407RFL"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87fd0"
      - m0d_wwn: "0x5000c5007bd81a3a"
        m0d_serial: "Z8407RPX"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd81a3a"
      - m0d_wwn: "0x5000c5007bd88016"
        m0d_serial: "Z8407R5T"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd88016"
      - m0d_wwn: "0x5000c5007bd8110a"
        m0d_serial: "Z8407RWE"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8110a"
      - m0d_wwn: "0x5000c5007bd50acf"
        m0d_serial: "Z8407MAQ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd50acf"
  - m0h_fqdn: "castor-beta1-ssu-1-4"
    host_mem_as: 536870912
    host_mem_rss: 65772544
    host_mem_stack: 65772544
    host_mem_memlock: 65772544
    host_cores: [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]
    lnid: "172.18.1.4@tcp"
    m0h_roles:
      - name: ha
      - name: storage

    m0h_devices:
      - m0d_wwn: "0x5000c50086c1d25e"
        m0d_serial: "Z8407JV8"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086c1d25e"
      - m0d_wwn: "0x5000c50086e2bda7"
        m0d_serial: "Z8408FAC"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e2bda7"
      - m0d_wwn: "0x5000c50086e2b118"
        m0d_serial: "Z8408FCW"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e2b118"
      - m0d_wwn: "0x5000c50086e2c415"
        m0d_serial: "Z8408F9B"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e2c415"
      - m0d_wwn: "0x5000c5007bc5c01e"
        m0d_serial: "Z8406PK9"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc5c01e"
      - m0d_wwn: "0x5000c50086e2a0ad"
        m0d_serial: "Z8408FNE"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e2a0ad"
      - m0d_wwn: "0x5000c50086e274f9"
        m0d_serial: "Z8408G04"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e274f9"
      - m0d_wwn: "0x5000c5007bd51bbb"
        m0d_serial: "Z8407M69"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd51bbb"
      - m0d_wwn: "0x5000c50086e2ddf0"
        m0d_serial: "Z8408EXT"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e2ddf0"
      - m0d_wwn: "0x5000c5007bd4ac2a"
        m0d_serial: "Z8407NTL"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4ac2a"
      - m0d_wwn: "0x5000c5007bd4dc85"
        m0d_serial: "Z8407MZZ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4dc85"
      - m0d_wwn: "0x5000c5007bd4e190"
        m0d_serial: "Z8407MT1"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4e190"
      - m0d_wwn: "0x5000c50086e48f9a"
        m0d_serial: "Z8408R6L"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e48f9a"
      - m0d_wwn: "0x5000c5007bd4c72d"
        m0d_serial: "Z8407NA7"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4c72d"
      - m0d_wwn: "0x5000c5007bd52298"
        m0d_serial: "Z8407M4S"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd52298"
      - m0d_wwn: "0x5000c5007bc59de2"
        m0d_serial: "Z8406PTF"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc59de2"
      - m0d_wwn: "0x5000c5007bd5002e"
        m0d_serial: "Z8407MFS"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd5002e"
      - m0d_wwn: "0x5000c5007bd4e5b4"
        m0d_serial: "Z8407MRD"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4e5b4"
      - m0d_wwn: "0x5000c5007bd4d09d"
        m0d_serial: "Z8407NEN"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4d09d"
      - m0d_wwn: "0x5000c50086e23065"
        m0d_serial: "Z8408GPB"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e23065"
      - m0d_wwn: "0x5000c50086e4b342"
        m0d_serial: "Z8408RBL"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e4b342"
      - m0d_wwn: "0x5000c5007bd819c2"
        m0d_serial: "Z8407RNW"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd819c2"
      - m0d_wwn: "0x5000c50086e084cf"
        m0d_serial: "Z84086JC"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e084cf"
      - m0d_wwn: "0x5000c50086e26484"
        m0d_serial: "Z8408G37"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e26484"
      - m0d_wwn: "0x5000c50086e2c37c"
        m0d_serial: "Z8408FEK"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e2c37c"
      - m0d_wwn: "0x5000c50086e2caeb"
        m0d_serial: "Z8408F43"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e2caeb"
      - m0d_wwn: "0x5000c50086e48ab5"
        m0d_serial: "Z8408R78"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e48ab5"
      - m0d_wwn: "0x5000c50086e2c408"
        m0d_serial: "Z8408F8S"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e2c408"
      - m0d_wwn: "0x5000c50086e28af9"
        m0d_serial: "Z8408FTK"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e28af9"
      - m0d_wwn: "0x5000c5007bd5065e"
        m0d_serial: "Z8407MDW"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd5065e"
      - m0d_wwn: "0x5000c5007bd4a518"
        m0d_serial: "Z8406994"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4a518"
      - m0d_wwn: "0x5000c5007bd4c6e4"
        m0d_serial: "Z8407NCW"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4c6e4"
      - m0d_wwn: "0x5000c5007bd4992c"
        m0d_serial: "Z84069D7"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4992c"
      - m0d_wwn: "0x5000c5007bd4d943"
        m0d_serial: "Z8407N4E"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4d943"
      - m0d_wwn: "0x5000c5007bd4b9a6"
        m0d_serial: "Z8407NES"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4b9a6"
      - m0d_wwn: "0x5000c50086e271ce"
        m0d_serial: "Z8408FVT"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e271ce"
      - m0d_wwn: "0x5000c5007bd4c82e"
        m0d_serial: "Z8407NF1"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4c82e"
      - m0d_wwn: "0x5000c50086e2dfd7"
        m0d_serial: "Z8408F1R"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e2dfd7"
      - m0d_wwn: "0x5000c5007bd4bfc7"
        m0d_serial: "Z8407NBQ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4bfc7"
      - m0d_wwn: "0x5000c50086e01e6c"
        m0d_serial: "Z840882Z"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e01e6c"
      - m0d_wwn: "0x5000c50086e2a52e"
        m0d_serial: "Z8408FLD"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e2a52e"
      - m0d_wwn: "0x5000c50086e482c6"
        m0d_serial: "Z8408RAA"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e482c6"
      - m0d_wwn: "0x5000c5007bc5d5b6"
        m0d_serial: "Z8406P96"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc5d5b6"
      - m0d_wwn: "0x5000c50086e48274"
        m0d_serial: "Z8408RC7"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e48274"
      - m0d_wwn: "0x5000c50086dfdb51"
        m0d_serial: "Z8408880"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086dfdb51"
      - m0d_wwn: "0x5000c5007bd52350"
        m0d_serial: "Z8407M4Y"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd52350"
      - m0d_wwn: "0x5000c5007bd4e80a"
        m0d_serial: "Z8407MQ3"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4e80a"
      - m0d_wwn: "0x5000c5007bd4ca56"
        m0d_serial: "Z8407NKF"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4ca56"
      - m0d_wwn: "0x5000c5007bd4c703"
        m0d_serial: "Z8407NL8"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4c703"
      - m0d_wwn: "0x5000c5007bd4fc66"
        m0d_serial: "Z8407MG2"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4fc66"
      - m0d_wwn: "0x5000c50086e342d0"
        m0d_serial: "Z8408DRN"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e342d0"
      - m0d_wwn: "0x5000c50086e49005"
        m0d_serial: "Z8408R72"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e49005"
      - m0d_wwn: "0x5000c5007bc5b1cd"
        m0d_serial: "Z8406PNW"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc5b1cd"
      - m0d_wwn: "0x5000c50086e18168"
        m0d_serial: "Z84083YZ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e18168"
      - m0d_wwn: "0x5000c50086dfd4b1"
        m0d_serial: "Z840888G"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086dfd4b1"
      - m0d_wwn: "0x5000c50086e4b2f8"
        m0d_serial: "Z8408RCM"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e4b2f8"
      - m0d_wwn: "0x5000c5007bd4cb7f"
        m0d_serial: "Z8407NAS"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4cb7f"
      - m0d_wwn: "0x5000c5007bd51362"
        m0d_serial: "Z8407M74"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd51362"
      - m0d_wwn: "0x5000c50086e01ee2"
        m0d_serial: "Z8408839"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e01ee2"
      - m0d_wwn: "0x5000c50086e23927"
        m0d_serial: "Z8408GL7"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e23927"
      - m0d_wwn: "0x5000c50086e298ec"
        m0d_serial: "Z8408FQG"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e298ec"
      - m0d_wwn: "0x5000c5007bd4baf5"
        m0d_serial: "Z8407NHK"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4baf5"
      - m0d_wwn: "0x5000c50086e25374"
        m0d_serial: "Z8408G9F"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e25374"
      - m0d_wwn: "0x5000c5007bc5b9ba"
        m0d_serial: "Z8406PJR"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc5b9ba"
      - m0d_wwn: "0x5000c5007bd4ee45"
        m0d_serial: "Z8407MNW"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4ee45"
      - m0d_wwn: "0x5000c5007bd515b1"
        m0d_serial: "Z8407M9L"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd515b1"
      - m0d_wwn: "0x5000c50086e28e3f"
        m0d_serial: "Z8408FSV"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e28e3f"
      - m0d_wwn: "0x5000c5007bd4993e"
        m0d_serial: "Z84069AM"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4993e"
      - m0d_wwn: "0x5000c50086e20060"
        m0d_serial: "Z8408HJM"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e20060"
      - m0d_wwn: "0x5000c50086e250c3"
        m0d_serial: "Z8408G7J"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e250c3"
      - m0d_wwn: "0x5000c5007bc6261e"
        m0d_serial: "Z8407H2H"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc6261e"
      - m0d_wwn: "0x5000c5007bc58138"
        m0d_serial: "Z8406Q7Z"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc58138"
      - m0d_wwn: "0x5000c5007bd4fbe6"
        m0d_serial: "Z8407MGJ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4fbe6"
      - m0d_wwn: "0x5000c50086e1f8b4"
        m0d_serial: "Z8408HHZ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e1f8b4"
      - m0d_wwn: "0x5000c50086e483ea"
        m0d_serial: "Z8408RCF"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e483ea"
      - m0d_wwn: "0x5000c5007bd4e040"
        m0d_serial: "Z8407NGF"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4e040"
      - m0d_wwn: "0x5000c50086e4804a"
        m0d_serial: "Z8408RDP"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e4804a"
      - m0d_wwn: "0x5000c5007bd4dc47"
        m0d_serial: "Z8407MZN"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4dc47"
      - m0d_wwn: "0x5000c5007bd4f0d1"
        m0d_serial: "Z8407MN5"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4f0d1"
      - m0d_wwn: "0x5000c50086e005e3"
        m0d_serial: "Z840886R"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50086e005e3"
  - m0h_fqdn: "castor-beta1-ssu-1-5"
    host_mem_as: 536870912
    host_mem_rss: 65772544
    host_mem_stack: 65772544
    host_mem_memlock: 65772544
    host_cores: [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]
    lnid: "172.18.1.5@tcp"
    m0h_roles:
      - name: ha
      - name: storage

    m0h_devices:
      - m0d_wwn: "0x5000c5007bd87d2f"
        m0d_serial: "Z8407R96"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87d2f"
      - m0d_wwn: "0x5000c5007bba0c1f"
        m0d_serial: "Z840781M"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bba0c1f"
      - m0d_wwn: "0x5000c5007bd87c82"
        m0d_serial: "Z8407R9E"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87c82"
      - m0d_wwn: "0x5000c5007bd80ffb"
        m0d_serial: "Z8407S5W"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd80ffb"
      - m0d_wwn: "0x5000c5007bc63234"
        m0d_serial: "Z8407GVA"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc63234"
      - m0d_wwn: "0x5000c5007bd8667b"
        m0d_serial: "Z8407RHZ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8667b"
      - m0d_wwn: "0x5000c5007bd80908"
        m0d_serial: "Z8407RZ8"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd80908"
      - m0d_wwn: "0x5000c5007bd7b5e5"
        m0d_serial: "Z8407TBL"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd7b5e5"
      - m0d_wwn: "0x5000c5007bd8062c"
        m0d_serial: "Z8407S1W"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8062c"
      - m0d_wwn: "0x5000c5007bd808b2"
        m0d_serial: "Z8407S32"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd808b2"
      - m0d_wwn: "0x5000c5007bd879d9"
        m0d_serial: "Z8407R95"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd879d9"
      - m0d_wwn: "0x5000c5007bd8c4a0"
        m0d_serial: "Z8407QBX"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8c4a0"
      - m0d_wwn: "0x5000c5007bd89ba9"
        m0d_serial: "Z8407R29"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd89ba9"
      - m0d_wwn: "0x5000c5007bc6327d"
        m0d_serial: "Z8407GSN"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc6327d"
      - m0d_wwn: "0x5000c5007bc66c04"
        m0d_serial: "Z8407G5D"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc66c04"
      - m0d_wwn: "0x5000c5007bd8c341"
        m0d_serial: "Z8407QBW"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8c341"
      - m0d_wwn: "0x5000c5007bc62331"
        m0d_serial: "Z8407H86"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc62331"
      - m0d_wwn: "0x5000c5007bd83991"
        m0d_serial: "Z8407SH8"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd83991"
      - m0d_wwn: "0x5000c5007bd81cc5"
        m0d_serial: "Z8407S9W"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd81cc5"
      - m0d_wwn: "0x5000c5007bc62564"
        m0d_serial: "Z8407H3P"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc62564"
      - m0d_wwn: "0x5000c5007bd8c219"
        m0d_serial: "Z8407QDN"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8c219"
      - m0d_wwn: "0x5000c5007bd8bb54"
        m0d_serial: "Z8407QEA"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8bb54"
      - m0d_wwn: "0x5000c5007bd886af"
        m0d_serial: "Z8407R49"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd886af"
      - m0d_wwn: "0x5000c5007bd8ae4c"
        m0d_serial: "Z8407QQH"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8ae4c"
      - m0d_wwn: "0x5000c5007bd880fa"
        m0d_serial: "Z8407R53"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd880fa"
      - m0d_wwn: "0x5000c5007bd8c194"
        m0d_serial: "Z8407QB1"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8c194"
      - m0d_wwn: "0x5000c5007bd8c272"
        m0d_serial: "Z8407QCK"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8c272"
      - m0d_wwn: "0x5000c5007bd87a84"
        m0d_serial: "Z8407RB2"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87a84"
      - m0d_wwn: "0x5000c5007bd83892"
        m0d_serial: "Z8407SG3"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd83892"
      - m0d_wwn: "0x5000c5007bc6158b"
        m0d_serial: "Z8407H91"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc6158b"
      - m0d_wwn: "0x5000c5007bd87cca"
        m0d_serial: "Z8407R9F"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87cca"
      - m0d_wwn: "0x5000c5007bc65872"
        m0d_serial: "Z8407GD2"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc65872"
      - m0d_wwn: "0x5000c5007bd817e3"
        m0d_serial: "Z8407RQ9"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd817e3"
      - m0d_wwn: "0x5000c5007bd7b54d"
        m0d_serial: "Z8407TD9"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd7b54d"
      - m0d_wwn: "0x5000c5007bd87f4b"
        m0d_serial: "Z8407R6F"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87f4b"
      - m0d_wwn: "0x5000c5007bc64476"
        m0d_serial: "Z8407GMJ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc64476"
      - m0d_wwn: "0x5000c5007bd8ccc1"
        m0d_serial: "Z8407Q64"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8ccc1"
      - m0d_wwn: "0x5000c5007bd83b02"
        m0d_serial: "Z8407SGS"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd83b02"
      - m0d_wwn: "0x5000c5007bd80884"
        m0d_serial: "Z8407S3K"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd80884"
      - m0d_wwn: "0x5000c5007bd81364"
        m0d_serial: "Z8407RSF"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd81364"
      - m0d_wwn: "0x5000c5007bd8042e"
        m0d_serial: "Z8407S42"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8042e"
      - m0d_wwn: "0x5000c5007bd88b36"
        m0d_serial: "Z8407R1J"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd88b36"
      - m0d_wwn: "0x5000c5007bd7b7e2"
        m0d_serial: "Z8407T8L"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd7b7e2"
      - m0d_wwn: "0x5000c5007bd88001"
        m0d_serial: "Z8407R5Y"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd88001"
      - m0d_wwn: "0x5000c5007bd880be"
        m0d_serial: "Z8407R6S"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd880be"
      - m0d_wwn: "0x5000c5007bd80f59"
        m0d_serial: "Z8407RYX"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd80f59"
      - m0d_wwn: "0x5000c5007bd81b8f"
        m0d_serial: "Z8407RQ4"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd81b8f"
      - m0d_wwn: "0x5000c5007bd80859"
        m0d_serial: "Z8407S1D"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd80859"
      - m0d_wwn: "0x5000c5007bd87ab0"
        m0d_serial: "Z8407RBL"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87ab0"
      - m0d_wwn: "0x5000c5007bd816b7"
        m0d_serial: "Z8407S8W"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd816b7"
      - m0d_wwn: "0x5000c5007bd88626"
        m0d_serial: "Z8407RF8"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd88626"
      - m0d_wwn: "0x5000c5007bd8cf96"
        m0d_serial: "Z8407Q8Q"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8cf96"
      - m0d_wwn: "0x5000c5007bc661b1"
        m0d_serial: "Z8407G92"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc661b1"
      - m0d_wwn: "0x5000c5007bd8c789"
        m0d_serial: "Z8407Q8N"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8c789"
      - m0d_wwn: "0x5000c5007bd87e4e"
        m0d_serial: "Z8407RA4"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87e4e"
      - m0d_wwn: "0x5000c5007bc6354f"
        m0d_serial: "Z8407GPD"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc6354f"
      - m0d_wwn: "0x5000c5007bd8069d"
        m0d_serial: "Z8407S2J"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8069d"
      - m0d_wwn: "0x5000c5007bd884a4"
        m0d_serial: "Z8407R2Y"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd884a4"
      - m0d_wwn: "0x5000c5007bd88502"
        m0d_serial: "Z8407R2V"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd88502"
      - m0d_wwn: "0x5000c5007bd887d5"
        m0d_serial: "Z8407R54"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd887d5"
      - m0d_wwn: "0x5000c5007bd8c254"
        m0d_serial: "Z8407QD9"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8c254"
      - m0d_wwn: "0x5000c5007bd8be60"
        m0d_serial: "Z8407QFP"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8be60"
      - m0d_wwn: "0x5000c5007bd80f87"
        m0d_serial: "Z8407RYE"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd80f87"
      - m0d_wwn: "0x5000c5007bd8648f"
        m0d_serial: "Z8407RG1"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8648f"
      - m0d_wwn: "0x5000c5007bc63177"
        m0d_serial: "Z8407GW3"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc63177"
      - m0d_wwn: "0x5000c5007bd8a6a1"
        m0d_serial: "Z8407QQT"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8a6a1"
      - m0d_wwn: "0x5000c5007bd8078a"
        m0d_serial: "Z8407S10"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8078a"
      - m0d_wwn: "0x5000c5007bd8df2e"
        m0d_serial: "Z8407Q1H"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8df2e"
      - m0d_wwn: "0x5000c5007bd885ca"
        m0d_serial: "Z8407R4D"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd885ca"
      - m0d_wwn: "0x5000c5007bc6367d"
        m0d_serial: "Z8407GQM"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc6367d"
      - m0d_wwn: "0x5000c5007bc623d1"
        m0d_serial: "Z8407H7L"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc623d1"
      - m0d_wwn: "0x5000c5007bc6658b"
        m0d_serial: "Z8407G9A"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc6658b"
      - m0d_wwn: "0x5000c5007bd83cea"
        m0d_serial: "Z8407SGZ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd83cea"
      - m0d_wwn: "0x5000c5007bd8ae34"
        m0d_serial: "Z8407QV0"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8ae34"
      - m0d_wwn: "0x5000c5007bd8de32"
        m0d_serial: "Z8407Q27"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8de32"
      - m0d_wwn: "0x5000c5007bd83c9d"
        m0d_serial: "Z8407SH0"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd83c9d"
      - m0d_wwn: "0x5000c5007bd87811"
        m0d_serial: "Z8407R77"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87811"
      - m0d_wwn: "0x5000c5007bc651a9"
        m0d_serial: "Z8407GF6"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc651a9"
      - m0d_wwn: "0x5000c5007bc63918"
        m0d_serial: "Z8407H1J"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc63918"
      - m0d_wwn: "0x5000c5007bd80f4e"
        m0d_serial: "Z8407SHR"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd80f4e"
  - m0h_fqdn: "castor-beta1-ssu-1-6"
    host_mem_as: 536870912
    host_mem_rss: 65772544
    host_mem_stack: 65772544
    host_mem_memlock: 65772544
    host_cores: [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]
    lnid: "172.18.1.6@tcp"
    m0h_roles:
      - name: ha
      - name: storage

    m0h_devices:
      - m0d_wwn: "0x5000c5007bd4e6c5"
        m0d_serial: "Z8407MV1"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4e6c5"
      - m0d_wwn: "0x5000c5007bd4fd76"
        m0d_serial: "Z8407MHB"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4fd76"
      - m0d_wwn: "0x5000c5007bd7b71d"
        m0d_serial: "Z8407T7X"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd7b71d"
      - m0d_wwn: "0x5000c5007bd7b491"
        m0d_serial: "Z8407TAF"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd7b491"
      - m0d_wwn: "0x5000c5007bd8dd5b"
        m0d_serial: "Z8407PZE"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8dd5b"
      - m0d_wwn: "0x5000c5007bd7ae9c"
        m0d_serial: "Z8407TB7"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd7ae9c"
      - m0d_wwn: "0x5000c5007bd522e1"
        m0d_serial: "Z8407M2P"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd522e1"
      - m0d_wwn: "0x5000c5007bd87acb"
        m0d_serial: "Z8407RCV"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87acb"
      - m0d_wwn: "0x5000c5007bd50588"
        m0d_serial: "Z8407MF6"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd50588"
      - m0d_wwn: "0x5000c5007bd87f7d"
        m0d_serial: "Z8407RA2"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87f7d"
      - m0d_wwn: "0x5000c5007bd8ec72"
        m0d_serial: "Z8407PYQ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8ec72"
      - m0d_wwn: "0x5000c5007bd89e6c"
        m0d_serial: "Z8407RJF"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd89e6c"
      - m0d_wwn: "0x5000c5007bd80880"
        m0d_serial: "Z8407S27"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd80880"
      - m0d_wwn: "0x5000c5007bd87d14"
        m0d_serial: "Z8407R9T"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87d14"
      - m0d_wwn: "0x5000c5007bd8d5ec"
        m0d_serial: "Z8407Q2K"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8d5ec"
      - m0d_wwn: "0x5000c5007bd887be"
        m0d_serial: "Z8407R59"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd887be"
      - m0d_wwn: "0x5000c5007bd8cc38"
        m0d_serial: "Z8407Q70"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8cc38"
      - m0d_wwn: "0x5000c5007bd89df3"
        m0d_serial: "Z8407QZM"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd89df3"
      - m0d_wwn: "0x5000c5007bd8a18f"
        m0d_serial: "Z8407QT3"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8a18f"
      - m0d_wwn: "0x5000c5007bd51fab"
        m0d_serial: "Z8407M5X"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd51fab"
      - m0d_wwn: "0x5000c5007bd50c82"
        m0d_serial: "Z8407MAZ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd50c82"
      - m0d_wwn: "0x5000c5007bd524f7"
        m0d_serial: "Z8407M3N"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd524f7"
      - m0d_wwn: "0x5000c5007bd4fb9e"
        m0d_serial: "Z8407MGL"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4fb9e"
      - m0d_wwn: "0x5000c5007bd7b481"
        m0d_serial: "Z8407TBD"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd7b481"
      - m0d_wwn: "0x5000c5007bd87922"
        m0d_serial: "Z8407RD8"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87922"
      - m0d_wwn: "0x5000c5007bd4c854"
        m0d_serial: "Z8407NP6"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4c854"
      - m0d_wwn: "0x5000c5007bd7b97f"
        m0d_serial: "Z8407T92"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd7b97f"
      - m0d_wwn: "0x5000c5007bc664ce"
        m0d_serial: "Z8407G5S"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bc664ce"
      - m0d_wwn: "0x5000c5007bd8b026"
        m0d_serial: "Z8407QQ6"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8b026"
      - m0d_wwn: "0x5000c5007bd89e09"
        m0d_serial: "Z8407QZJ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd89e09"
      - m0d_wwn: "0x5000c5007bd8bdd0"
        m0d_serial: "Z8407QFQ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8bdd0"
      - m0d_wwn: "0x5000c5007bd89e05"
        m0d_serial: "Z8407RHR"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd89e05"
      - m0d_wwn: "0x5000c5007bd8bca0"
        m0d_serial: "Z8407QH4"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8bca0"
      - m0d_wwn: "0x5000c5007bd8b045"
        m0d_serial: "Z8407QPS"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8b045"
      - m0d_wwn: "0x5000c5007bd8ceeb"
        m0d_serial: "Z8407Q5J"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8ceeb"
      - m0d_wwn: "0x5000c5007bd4dc9a"
        m0d_serial: "Z8407NNK"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4dc9a"
      - m0d_wwn: "0x5000c5007bd89e27"
        m0d_serial: "Z8407RB0"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd89e27"
      - m0d_wwn: "0x5000c5007bd8669a"
        m0d_serial: "Z8407RKZ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8669a"
      - m0d_wwn: "0x5000c5007bd8b237"
        m0d_serial: "Z8407QWP"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8b237"
      - m0d_wwn: "0x5000c5007bd50ec6"
        m0d_serial: "Z8407MC8"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd50ec6"
      - m0d_wwn: "0x5000c5007bd7b7a7"
        m0d_serial: "Z8407T8X"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd7b7a7"
      - m0d_wwn: "0x5000c5007bd5111d"
        m0d_serial: "Z8407M7X"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd5111d"
      - m0d_wwn: "0x5000c5007bd8c8f4"
        m0d_serial: "Z8407Q8T"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8c8f4"
      - m0d_wwn: "0x5000c5007bd813be"
        m0d_serial: "Z8407RW7"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd813be"
      - m0d_wwn: "0x5000c5007bd51e34"
        m0d_serial: "Z8407M46"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd51e34"
      - m0d_wwn: "0x5000c5007bd802b2"
        m0d_serial: "Z8407S4A"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd802b2"
      - m0d_wwn: "0x5000c5007bd8d622"
        m0d_serial: "Z8407Q30"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8d622"
      - m0d_wwn: "0x5000c5007bd8dfa2"
        m0d_serial: "Z8407Q28"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8dfa2"
      - m0d_wwn: "0x5000c5007bd87965"
        m0d_serial: "Z8407R8G"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87965"
      - m0d_wwn: "0x5000c5007bd81df9"
        m0d_serial: "Z8407RRK"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd81df9"
      - m0d_wwn: "0x5000c5007bd86407"
        m0d_serial: "Z8407RJ5"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd86407"
      - m0d_wwn: "0x5000c5007bd516fc"
        m0d_serial: "Z8407M9W"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd516fc"
      - m0d_wwn: "0x5000c5007bd8bc38"
        m0d_serial: "Z8407QET"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8bc38"
      - m0d_wwn: "0x5000c5007bd7b73c"
        m0d_serial: "Z8407T9G"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd7b73c"
      - m0d_wwn: "0x5000c5007bd8bb95"
        m0d_serial: "Z8407QN3"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8bb95"
      - m0d_wwn: "0x5000c5007bd8afa1"
        m0d_serial: "Z8407RJN"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8afa1"
      - m0d_wwn: "0x5000c5007bd8f170"
        m0d_serial: "Z8407PWH"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8f170"
      - m0d_wwn: "0x5000c5007bd4e1a0"
        m0d_serial: "Z8407N02"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4e1a0"
      - m0d_wwn: "0x5000c5007bd53e78"
        m0d_serial: "Z8407LY2"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd53e78"
      - m0d_wwn: "0x5000c5007bd80732"
        m0d_serial: "Z8407S11"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd80732"
      - m0d_wwn: "0x5000c5007bd8c2ed"
        m0d_serial: "Z8407QDJ"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8c2ed"
      - m0d_wwn: "0x5000c5007bd55ee0"
        m0d_serial: "Z8407M05"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd55ee0"
      - m0d_wwn: "0x5000c5007bd89d97"
        m0d_serial: "Z8407QYE"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd89d97"
      - m0d_wwn: "0x5000c5007bd81763"
        m0d_serial: "Z8407RWN"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd81763"
      - m0d_wwn: "0x5000c5007bd87dc0"
        m0d_serial: "Z8407R86"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87dc0"
      - m0d_wwn: "0x5000c5007bd80f54"
        m0d_serial: "Z8407S7A"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd80f54"
      - m0d_wwn: "0x5000c5007bd87ca9"
        m0d_serial: "Z8407R92"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87ca9"
      - m0d_wwn: "0x5000c5007bd81540"
        m0d_serial: "Z8407S79"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd81540"
      - m0d_wwn: "0x5000c5007bd865e6"
        m0d_serial: "Z8407RLL"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd865e6"
      - m0d_wwn: "0x5000c5007bd87de3"
        m0d_serial: "Z8407R67"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd87de3"
      - m0d_wwn: "0x5000c5007bd8ccf4"
        m0d_serial: "Z8407Q5Q"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8ccf4"
      - m0d_wwn: "0x5000c5007bd8bbff"
        m0d_serial: "Z8407QEM"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8bbff"
      - m0d_wwn: "0x5000c5007bd4858b"
        m0d_serial: "Z84069KF"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4858b"
      - m0d_wwn: "0x5000c5007bd4a69c"
        m0d_serial: "Z8407NTC"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4a69c"
      - m0d_wwn: "0x5000c5007bd4dee7"
        m0d_serial: "Z8407MXN"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4dee7"
      - m0d_wwn: "0x5000c5007bd8c2c7"
        m0d_serial: "Z8407QDV"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8c2c7"
      - m0d_wwn: "0x5000c5007bd4cf69"
        m0d_serial: "Z8407N6T"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd4cf69"
      - m0d_wwn: "0x5000c5007bd8be09"
        m0d_serial: "Z8407QFL"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd8be09"
      - m0d_wwn: "0x5000c5007bd81d29"
        m0d_serial: "Z8407SDE"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd81d29"
      - m0d_wwn: "0x5000c5007bd7ab85"
        m0d_serial: "Z8407TA5"
        m0d_bsize: 4096
        m0d_size: 8001563222016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007bd7ab85"
id_m0_globals:
  m0_data_units: 8
  m0_parity_units: 2
  m0_md_redundancy: 3
  m0_failure_set_gen:
    tag: Dynamic
    contents: []
EOF
}

halon_facts_yaml_dev1_1() {
	cat << EOF
---
id_racks:
     - rack_idx: 0
       rack_enclosures:
           - enc_idx: 41
             enc_id: SHX0951731XXXXX
             enc_bmc:
                 - bmc_addr: "10.22.193.100"
                   bmc_user: "admin"
                   bmc_pass: "admin"
                 - bmc_addr: "10.22.193.101"
                   bmc_user: "admin"
                   bmc_pass: "admin"
             enc_hosts:
                 - h_fqdn: "castor-dev1-1-cc1"
                   h_memsize: 64231.09
                   h_cpucount: 20
                   h_interfaces:
                     - if_macAddress: "00:50:cc:79:b2:4f"
                       if_network: Management
                       if_ipAddrs: [172.16.0.41]
                     - if_macAddress: "00:50:cc:79:b2:4f"
                       if_network: Data
                       if_ipAddrs: [172.18.0.41]
     - rack_idx: 1
       rack_enclosures:
           - enc_idx: 11
             enc_id: HLM1002010G286X
             enc_bmc:
                 - bmc_addr: "172.16.1.111"
                   bmc_user: "admin"
                   bmc_pass: "admin"
                 - bmc_addr: "172.16.1.131"
                   bmc_user: "admin"
                   bmc_pass: "admin"
             enc_hosts:
                 - h_fqdn: "castor-dev1-1-ssu-1-11"
                   h_memsize: 64230.46
                   h_cpucount: 20
                   h_interfaces:
                     - if_macAddress: "00:50:cc:79:b1:df"
                       if_network: Management
                       if_ipAddrs: [172.16.1.11]
                     - if_macAddress: "00:50:cc:79:b1:e4"
                       if_network: Data
                       if_ipAddrs: [172.18.1.11]
           - enc_idx: 3
             enc_id: HLM1002010G287C
             enc_bmc:
                 - bmc_addr: "172.16.1.103"
                   bmc_user: "admin"
                   bmc_pass: "admin"
                 - bmc_addr: "172.16.1.123"
                   bmc_user: "admin"
                   bmc_pass: "admin"
             enc_hosts:
                 - h_fqdn: "castor-dev1-1-ssu-1-3"
                   h_memsize: 64230.46
                   h_cpucount: 20
                   h_interfaces:
                     - if_macAddress: "00:50:cc:79:b2:03"
                       if_network: Management
                       if_ipAddrs: [172.16.1.3]
                     - if_macAddress: "00:50:cc:79:b2:07"
                       if_network: Data
                       if_ipAddrs: [172.18.1.3]
           - enc_idx: 5
             enc_id: HLM1002010G2870
             enc_bmc:
                 - bmc_addr: "172.16.1.105"
                   bmc_user: "admin"
                   bmc_pass: "admin"
                 - bmc_addr: "172.16.1.125"
                   bmc_user: "admin"
                   bmc_pass: "admin"
             enc_hosts:
                 - h_fqdn: "castor-dev1-1-ssu-1-5"
                   h_memsize: 64230.46
                   h_cpucount: 20
                   h_interfaces:
                     - if_macAddress: "00:50:cc:79:b1:c7"
                       if_network: Management
                       if_ipAddrs: [172.16.1.5]
                     - if_macAddress: "00:50:cc:79:b1:cb"
                       if_network: Data
                       if_ipAddrs: [172.18.1.5]
           - enc_idx: 6
             enc_id: HLM1002010G287V
             enc_bmc:
                 - bmc_addr: "172.16.1.106"
                   bmc_user: "admin"
                   bmc_pass: "admin"
                 - bmc_addr: "172.16.1.126"
                   bmc_user: "admin"
                   bmc_pass: "admin"
             enc_hosts:
                 - h_fqdn: "castor-dev1-1-ssu-1-6"
                   h_memsize: 64230.46
                   h_cpucount: 20
                   h_interfaces:
                     - if_macAddress: "00:50:cc:79:b1:d3"
                       if_network: Management
                       if_ipAddrs: [172.16.1.6]
                     - if_macAddress: "00:50:cc:79:b1:d8"
                       if_network: Data
                       if_ipAddrs: [172.18.1.6]
           - enc_idx: 8
             enc_id: HLM1002010G2873
             enc_bmc:
                 - bmc_addr: "172.16.1.108"
                   bmc_user: "admin"
                   bmc_pass: "admin"
                 - bmc_addr: "172.16.1.128"
                   bmc_user: "admin"
                   bmc_pass: "admin"
             enc_hosts:
                 - h_fqdn: "castor-dev1-1-ssu-1-8"
                   h_memsize: 64230.46
                   h_cpucount: 20
                   h_interfaces:
                     - if_macAddress: "00:50:cc:79:b1:bb"
                       if_network: Management
                       if_ipAddrs: [172.16.1.8]
                     - if_macAddress: "00:50:cc:79:b1:bf"
                       if_network: Data
                       if_ipAddrs: [172.18.1.8]
           - enc_idx: 9
             enc_id: HLM1002010G2281
             enc_bmc:
                 - bmc_addr: "172.16.1.109"
                   bmc_user: "admin"
                   bmc_pass: "admin"
                 - bmc_addr: "172.16.1.129"
                   bmc_user: "admin"
                   bmc_pass: "admin"
             enc_hosts:
                 - h_fqdn: "castor-dev1-1-ssu-1-9"
                   h_memsize: 64230.46
                   h_cpucount: 20
                   h_interfaces:
                     - if_macAddress: "00:50:cc:79:b1:f7"
                       if_network: Management
                       if_ipAddrs: [172.16.1.9]
                     - if_macAddress: "00:50:cc:79:b1:fb"
                       if_network: Data
                       if_ipAddrs: [172.18.1.9]

id_m0_servers:
  - m0h_fqdn: "castor-dev1-1-ssu-1-3"
    host_mem_as: 536870912
    host_mem_rss: 65772544
    host_mem_stack: 65772544
    host_mem_memlock: 65772544
    host_cores: [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]
    lnid: "172.18.1.3@tcp"
    m0h_roles:
      - name: ha
      - name: storage
      - name: mds
      - name: confd
      - name: m0t1fs

    m0h_devices:
      - m0d_wwn: "0x5000c50078d0622e"
        m0d_serial: "Z3021LBD"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d0622e"
      - m0d_wwn: "0x5000c50078d022c1"
        m0d_serial: "Z3021LWQ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d022c1"
      - m0d_wwn: "0x5000c50078cf8c31"
        m0d_serial: "Z3021NQB"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8c31"
      - m0d_wwn: "0x5000c50078c128f2"
        m0d_serial: "Z3020CG0"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078c128f2"
      - m0d_wwn: "0x5000c50078cfc280"
        m0d_serial: "Z3021MF9"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfc280"
      - m0d_wwn: "0x5000c50078cfc43a"
        m0d_serial: "Z3021MEP"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfc43a"
      - m0d_wwn: "0x5000c50078cfbc48"
        m0d_serial: "Z3021NBQ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfbc48"
      - m0d_wwn: "0x5000c50078cfa207"
        m0d_serial: "Z3021MRE"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfa207"
      - m0d_wwn: "0x5000c50078cfa377"
        m0d_serial: "Z3021MQK"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfa377"
      - m0d_wwn: "0x5000c50078cfc7c3"
        m0d_serial: "Z3021MET"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfc7c3"
      - m0d_wwn: "0x5000c50078cf5f77"
        m0d_serial: "Z3021NJC"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf5f77"
      - m0d_wwn: "0x5000c50078cfa51f"
        m0d_serial: "Z3021MQ7"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfa51f"
      - m0d_wwn: "0x5000c50078d06198"
        m0d_serial: "Z3021LEP"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d06198"
      - m0d_wwn: "0x5000c50078d02569"
        m0d_serial: "Z3021LMC"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d02569"
      - m0d_wwn: "0x5000c50078c12935"
        m0d_serial: "Z3020CH5"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078c12935"
      - m0d_wwn: "0x5000c50078c13990"
        m0d_serial: "Z3020CAE"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078c13990"
      - m0d_wwn: "0x5000c50078c13e1c"
        m0d_serial: "Z3020C6G"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078c13e1c"
      - m0d_wwn: "0x5000c50078cf8c83"
        m0d_serial: "Z3021N0E"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8c83"
      - m0d_wwn: "0x5000c50078cf9c15"
        m0d_serial: "Z3021MVH"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf9c15"
      - m0d_wwn: "0x5000c50078d025ac"
        m0d_serial: "Z3021LKR"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d025ac"
      - m0d_wwn: "0x5000c50078d02c2e"
        m0d_serial: "Z3021LMJ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d02c2e"
      - m0d_wwn: "0x5000c50078c138b6"
        m0d_serial: "Z3020CDK"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078c138b6"
      - m0d_wwn: "0x5000c50078d01529"
        m0d_serial: "Z3021LQJ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d01529"
      - m0d_wwn: "0x5000c50078c12576"
        m0d_serial: "Z3020CL1"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078c12576"
      - m0d_wwn: "0x5000c50078cf3d78"
        m0d_serial: "Z3021P0L"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf3d78"
      - m0d_wwn: "0x5000c50078cf9ebf"
        m0d_serial: "Z3021MNV"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf9ebf"
      - m0d_wwn: "0x5000c50078cf850a"
        m0d_serial: "Z3021MZK"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf850a"
      - m0d_wwn: "0x5000c50078d01e7c"
        m0d_serial: "Z3021M0H"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d01e7c"
      - m0d_wwn: "0x5000c50078d0146e"
        m0d_serial: "Z3021LZK"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d0146e"
      - m0d_wwn: "0x5000c50078cf9a35"
        m0d_serial: "Z3021MTM"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf9a35"
      - m0d_wwn: "0x5000c50078d02cc1"
        m0d_serial: "Z3021LMM"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d02cc1"
      - m0d_wwn: "0x5000c50078cf9d50"
        m0d_serial: "Z3021MTB"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf9d50"
      - m0d_wwn: "0x5000c50078cfb8ff"
        m0d_serial: "Z3021MMG"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfb8ff"
      - m0d_wwn: "0x5000c50078cf8ac1"
        m0d_serial: "Z3021NPR"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8ac1"
      - m0d_wwn: "0x5000c50078d02cce"
        m0d_serial: "Z3021LMY"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d02cce"
      - m0d_wwn: "0x5000c50078cfb327"
        m0d_serial: "Z3021MM5"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfb327"
      - m0d_wwn: "0x5000c50078cf8463"
        m0d_serial: "Z3021N0N"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8463"
      - m0d_wwn: "0x5000c50078cf8a48"
        m0d_serial: "Z3021NP7"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8a48"
      - m0d_wwn: "0x5000c50078c12aff"
        m0d_serial: "Z3020CHT"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078c12aff"
      - m0d_wwn: "0x5000c50078cf8ef4"
        m0d_serial: "Z3021N1H"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8ef4"
      - m0d_wwn: "0x5000c50078d060a9"
        m0d_serial: "Z3021LG8"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d060a9"
      - m0d_wwn: "0x5000c50078d03515"
        m0d_serial: "Z3021LHH"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d03515"
      - m0d_wwn: "0x5000c50078d011bd"
        m0d_serial: "Z3021LYK"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d011bd"
      - m0d_wwn: "0x5000c50078d012ec"
        m0d_serial: "Z3021LYN"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d012ec"
      - m0d_wwn: "0x5000c50078d009cf"
        m0d_serial: "Z3021LY2"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d009cf"
      - m0d_wwn: "0x5000c50078cf6340"
        m0d_serial: "Z3021NJG"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf6340"
      - m0d_wwn: "0x5000c50078c15181"
        m0d_serial: "Z3020C27"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078c15181"
      - m0d_wwn: "0x5000c50078c12056"
        m0d_serial: "Z3020CQ0"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078c12056"
      - m0d_wwn: "0x5000c50078cf8a29"
        m0d_serial: "Z3021N1V"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8a29"
      - m0d_wwn: "0x5000c50078c132be"
        m0d_serial: "Z3020CE4"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078c132be"
      - m0d_wwn: "0x5000c50078cf7e91"
        m0d_serial: "Z3021N6A"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf7e91"
      - m0d_wwn: "0x5000c50078c128d5"
        m0d_serial: "Z3020CFR"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078c128d5"
      - m0d_wwn: "0x5000c50078d02920"
        m0d_serial: "Z3021LKX"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d02920"
      - m0d_wwn: "0x5000c50078d01b8b"
        m0d_serial: "Z3021M1E"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d01b8b"
      - m0d_wwn: "0x5000c50078c16095"
        m0d_serial: "Z3020C0R"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078c16095"
      - m0d_wwn: "0x5000c50078c15575"
        m0d_serial: "Z3020C0V"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078c15575"
      - m0d_wwn: "0x5000c50078d015fa"
        m0d_serial: "Z3021MJZ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d015fa"
      - m0d_wwn: "0x5000c50078cf9e2b"
        m0d_serial: "Z3021MNB"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf9e2b"
      - m0d_wwn: "0x5000c50078cf7e5b"
        m0d_serial: "Z3021N61"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf7e5b"
      - m0d_wwn: "0x5000c50078c15fb5"
        m0d_serial: "Z3020C0P"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078c15fb5"
      - m0d_wwn: "0x5000c50078cf61b5"
        m0d_serial: "Z3021NS1"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf61b5"
      - m0d_wwn: "0x5000c50078cf79d0"
        m0d_serial: "Z3021N19"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf79d0"
      - m0d_wwn: "0x5000c50078cfa1f8"
        m0d_serial: "Z3021MRA"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfa1f8"
      - m0d_wwn: "0x5000c50078c12937"
        m0d_serial: "Z3020CFK"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078c12937"
      - m0d_wwn: "0x5000c50078cfc5bd"
        m0d_serial: "Z3021MEH"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfc5bd"
      - m0d_wwn: "0x5000c50078cf5e5d"
        m0d_serial: "Z3021NK5"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf5e5d"
      - m0d_wwn: "0x5000c50078cfbb1e"
        m0d_serial: "Z3021NCR"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfbb1e"
      - m0d_wwn: "0x5000c50078cf867a"
        m0d_serial: "Z3021MZH"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf867a"
      - m0d_wwn: "0x5000c50078c128b6"
        m0d_serial: "Z3020CG8"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078c128b6"
      - m0d_wwn: "0x5000c50078cf6332"
        m0d_serial: "Z3021NRL"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf6332"
      - m0d_wwn: "0x5000c50078c12884"
        m0d_serial: "Z3020CJX"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078c12884"
      - m0d_wwn: "0x5000c50078cfab72"
        m0d_serial: "Z3021MXM"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfab72"
      - m0d_wwn: "0x5000c50078cfc183"
        m0d_serial: "Z3021MDZ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfc183"
      - m0d_wwn: "0x5000c50078d02531"
        m0d_serial: "Z3021LLC"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d02531"
      - m0d_wwn: "0x5000c50078c12486"
        m0d_serial: "Z3020CJN"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078c12486"
      - m0d_wwn: "0x5000c50078cfc0ce"
        m0d_serial: "Z3021MBX"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfc0ce"
      - m0d_wwn: "0x5000c50078cf9c09"
        m0d_serial: "Z3021N3P"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf9c09"
      - m0d_wwn: "0x5000c50078d056ef"
        m0d_serial: "Z3021LCL"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d056ef"
      - m0d_wwn: "0x5000c50078c128a9"
        m0d_serial: "Z3020CN6"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078c128a9"
      - m0d_wwn: "0x5000c50078cf9fe6"
        m0d_serial: "Z3021MQ4"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf9fe6"
      - m0d_wwn: "0x5000c50078c15213"
        m0d_serial: "Z3020C2H"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078c15213"
      - m0d_wwn: "0x5000c50078cf7dd6"
        m0d_serial: "Z3021N7V"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf7dd6"
  - m0h_fqdn: "castor-dev1-1-ssu-1-5"
    host_mem_as: 536870912
    host_mem_rss: 65772544
    host_mem_stack: 65772544
    host_mem_memlock: 65772544
    host_cores: [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]
    lnid: "172.18.1.5@tcp"
    m0h_roles:
      - name: ha
      - name: storage

    m0h_devices:
      - m0d_wwn: "0x5000c5007922caa7"
        m0d_serial: "Z3028RTQ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007922caa7"
      - m0d_wwn: "0x5000c50079200c40"
        m0d_serial: "Z30286EE"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079200c40"
      - m0d_wwn: "0x5000c500791fd832"
        m0d_serial: "Z3028ZK6"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500791fd832"
      - m0d_wwn: "0x5000c5007927bcfa"
        m0d_serial: "Z30296GC"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007927bcfa"
      - m0d_wwn: "0x5000c5007926db5b"
        m0d_serial: "Z30298Z4"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926db5b"
      - m0d_wwn: "0x5000c50079264ee9"
        m0d_serial: "Z3029D3D"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079264ee9"
      - m0d_wwn: "0x5000c500792710a5"
        m0d_serial: "Z30298GK"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792710a5"
      - m0d_wwn: "0x5000c50079264240"
        m0d_serial: "Z3029CTH"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079264240"
      - m0d_wwn: "0x5000c50079265202"
        m0d_serial: "Z3029D6M"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079265202"
      - m0d_wwn: "0x5000c50079278761"
        m0d_serial: "Z30297AV"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079278761"
      - m0d_wwn: "0x5000c5007927b40b"
        m0d_serial: "Z30296FG"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007927b40b"
      - m0d_wwn: "0x5000c5007925ca4d"
        m0d_serial: "Z3029CCT"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007925ca4d"
      - m0d_wwn: "0x5000c5007925f783"
        m0d_serial: "Z3029CB3"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007925f783"
      - m0d_wwn: "0x5000c5007926fdfb"
        m0d_serial: "Z30298L3"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926fdfb"
      - m0d_wwn: "0x5000c50079264f06"
        m0d_serial: "Z3029D8L"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079264f06"
      - m0d_wwn: "0x5000c50079261d17"
        m0d_serial: "Z3029BTW"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079261d17"
      - m0d_wwn: "0x5000c50079278c6a"
        m0d_serial: "Z30297HX"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079278c6a"
      - m0d_wwn: "0x5000c500791fa4fc"
        m0d_serial: "Z3028Q20"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500791fa4fc"
      - m0d_wwn: "0x5000c5007926f7f3"
        m0d_serial: "Z30298VZ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926f7f3"
      - m0d_wwn: "0x5000c5007922ddc8"
        m0d_serial: "Z3028S3T"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007922ddc8"
      - m0d_wwn: "0x5000c50079254e4a"
        m0d_serial: "Z3029E89"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079254e4a"
      - m0d_wwn: "0x5000c50079275583"
        m0d_serial: "Z30297N8"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079275583"
      - m0d_wwn: "0x5000c500792649ae"
        m0d_serial: "Z3029D5R"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792649ae"
      - m0d_wwn: "0x5000c500792651e0"
        m0d_serial: "Z3029DKM"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792651e0"
      - m0d_wwn: "0x5000c500792663ce"
        m0d_serial: "Z3029D59"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792663ce"
      - m0d_wwn: "0x5000c500791fd721"
        m0d_serial: "Z3028ZKC"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500791fd721"
      - m0d_wwn: "0x5000c50079261fb1"
        m0d_serial: "Z3029BTA"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079261fb1"
      - m0d_wwn: "0x5000c5007920ece5"
        m0d_serial: "Z3028QCD"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007920ece5"
      - m0d_wwn: "0x5000c50079264db9"
        m0d_serial: "Z3029D2L"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079264db9"
      - m0d_wwn: "0x5000c50079277ca7"
        m0d_serial: "Z30296YL"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079277ca7"
      - m0d_wwn: "0x5000c500792727e1"
        m0d_serial: "Z302983Y"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792727e1"
      - m0d_wwn: "0x5000c5007925fecf"
        m0d_serial: "Z3029BW6"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007925fecf"
      - m0d_wwn: "0x5000c5007925dfec"
        m0d_serial: "Z3029CEH"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007925dfec"
      - m0d_wwn: "0x5000c50079260386"
        m0d_serial: "Z3029CGF"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079260386"
      - m0d_wwn: "0x5000c5007926dc71"
        m0d_serial: "Z30298YM"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926dc71"
      - m0d_wwn: "0x5000c5007923308b"
        m0d_serial: "Z30284RL"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007923308b"
      - m0d_wwn: "0x5000c5007926a391"
        m0d_serial: "Z3029D2C"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926a391"
      - m0d_wwn: "0x5000c5007926708f"
        m0d_serial: "Z3029D96"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926708f"
      - m0d_wwn: "0x5000c50079266eff"
        m0d_serial: "Z3029DH1"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079266eff"
      - m0d_wwn: "0x5000c50079205fee"
        m0d_serial: "Z3028RMZ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079205fee"
      - m0d_wwn: "0x5000c50079261edd"
        m0d_serial: "Z3029CMC"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079261edd"
      - m0d_wwn: "0x5000c50079256653"
        m0d_serial: "Z3029E5X"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079256653"
      - m0d_wwn: "0x5000c5007927af2a"
        m0d_serial: "Z30296G5"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007927af2a"
      - m0d_wwn: "0x5000c500791fd213"
        m0d_serial: "Z3028ZSV"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500791fd213"
      - m0d_wwn: "0x5000c50079274c40"
        m0d_serial: "Z30297PX"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079274c40"
      - m0d_wwn: "0x5000c500791fbedf"
        m0d_serial: "Z3028ZZL"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500791fbedf"
      - m0d_wwn: "0x5000c5007920dfdd"
        m0d_serial: "Z3028RME"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007920dfdd"
      - m0d_wwn: "0x5000c50079270b2e"
        m0d_serial: "Z30298LA"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079270b2e"
      - m0d_wwn: "0x5000c50079252cca"
        m0d_serial: "Z3029ENZ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079252cca"
      - m0d_wwn: "0x5000c50079273916"
        m0d_serial: "Z3029873"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079273916"
      - m0d_wwn: "0x5000c500792357e0"
        m0d_serial: "Z30285X9"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792357e0"
      - m0d_wwn: "0x5000c5007926eb53"
        m0d_serial: "Z30298XB"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926eb53"
      - m0d_wwn: "0x5000c5007926e985"
        m0d_serial: "Z30298WB"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926e985"
      - m0d_wwn: "0x5000c50079264f51"
        m0d_serial: "Z3029DJR"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079264f51"
      - m0d_wwn: "0x5000c50079264699"
        m0d_serial: "Z3029CSQ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079264699"
      - m0d_wwn: "0x5000c500792778d8"
        m0d_serial: "Z30297QV"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792778d8"
      - m0d_wwn: "0x5000c50079279609"
        m0d_serial: "Z30296M7"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079279609"
      - m0d_wwn: "0x5000c500792634f2"
        m0d_serial: "Z3029CN8"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792634f2"
      - m0d_wwn: "0x5000c5007926555a"
        m0d_serial: "Z3029D6V"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926555a"
      - m0d_wwn: "0x5000c50079263dd5"
        m0d_serial: "Z3029CR4"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079263dd5"
      - m0d_wwn: "0x5000c50079266a90"
        m0d_serial: "Z3029DH7"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079266a90"
      - m0d_wwn: "0x5000c500792786a0"
        m0d_serial: "Z302971A"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792786a0"
      - m0d_wwn: "0x5000c5007926538d"
        m0d_serial: "Z3029D3H"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926538d"
      - m0d_wwn: "0x5000c500792780ff"
        m0d_serial: "Z30296YK"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792780ff"
      - m0d_wwn: "0x5000c50079257432"
        m0d_serial: "Z3029DWG"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079257432"
      - m0d_wwn: "0x5000c5007922ec58"
        m0d_serial: "Z3028RD3"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007922ec58"
      - m0d_wwn: "0x5000c50079272754"
        m0d_serial: "Z302983F"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079272754"
      - m0d_wwn: "0x5000c5007926e45c"
        m0d_serial: "Z30298S8"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926e45c"
      - m0d_wwn: "0x5000c50079265af0"
        m0d_serial: "Z3029D9W"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079265af0"
      - m0d_wwn: "0x5000c500792625c7"
        m0d_serial: "Z3029BQD"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792625c7"
      - m0d_wwn: "0x5000c500792781d7"
        m0d_serial: "Z30296Z7"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792781d7"
      - m0d_wwn: "0x5000c5007925fe0e"
        m0d_serial: "Z3029BZX"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007925fe0e"
      - m0d_wwn: "0x5000c50079257621"
        m0d_serial: "Z3029DNG"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079257621"
      - m0d_wwn: "0x5000c5007926540d"
        m0d_serial: "Z3029DBX"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926540d"
      - m0d_wwn: "0x5000c50079263117"
        m0d_serial: "Z3029CN1"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079263117"
      - m0d_wwn: "0x5000c500791ffa86"
        m0d_serial: "Z302863J"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500791ffa86"
      - m0d_wwn: "0x5000c500791fa26c"
        m0d_serial: "Z3028Q6C"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500791fa26c"
      - m0d_wwn: "0x5000c500792334ae"
        m0d_serial: "Z30284RR"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792334ae"
      - m0d_wwn: "0x5000c500792670b6"
        m0d_serial: "Z3029DAQ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792670b6"
      - m0d_wwn: "0x5000c500791fd6bd"
        m0d_serial: "Z30286KG"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500791fd6bd"
      - m0d_wwn: "0x5000c5007926e7cd"
        m0d_serial: "Z30298WC"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926e7cd"
      - m0d_wwn: "0x5000c50079272494"
        m0d_serial: "Z302988S"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079272494"
  - m0h_fqdn: "castor-dev1-1-ssu-1-6"
    host_mem_as: 536870912
    host_mem_rss: 65772544
    host_mem_stack: 65772544
    host_mem_memlock: 65772544
    host_cores: [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]
    lnid: "172.18.1.6@tcp"
    m0h_roles:
      - name: ha
      - name: storage

    m0h_devices:
      - m0d_wwn: "0x5000c50078cf5987"
        m0d_serial: "Z3021NVL"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf5987"
      - m0d_wwn: "0x5000c50078d0599f"
        m0d_serial: "Z3021LDL"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d0599f"
      - m0d_wwn: "0x5000c50078cf8854"
        m0d_serial: "Z3021NML"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8854"
      - m0d_wwn: "0x5000c50078cfd9eb"
        m0d_serial: "Z3021MAY"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfd9eb"
      - m0d_wwn: "0x5000c50078d000ff"
        m0d_serial: "Z3021LSR"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d000ff"
      - m0d_wwn: "0x5000c50078cf8678"
        m0d_serial: "Z3021NYA"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8678"
      - m0d_wwn: "0x5000c50078cf7e5e"
        m0d_serial: "Z3021N8Y"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf7e5e"
      - m0d_wwn: "0x5000c50078d02128"
        m0d_serial: "Z3021LV2"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d02128"
      - m0d_wwn: "0x5000c50078cfce13"
        m0d_serial: "Z3021M95"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfce13"
      - m0d_wwn: "0x5000c50078cfd505"
        m0d_serial: "Z3021MA6"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfd505"
      - m0d_wwn: "0x5000c50078cf9ed2"
        m0d_serial: "Z3021MNJ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf9ed2"
      - m0d_wwn: "0x5000c50078cf8433"
        m0d_serial: "Z3021N4N"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8433"
      - m0d_wwn: "0x5000c50078cf810b"
        m0d_serial: "Z3021N9C"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf810b"
      - m0d_wwn: "0x5000c50078cf8b0a"
        m0d_serial: "Z3021NPN"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8b0a"
      - m0d_wwn: "0x5000c50078cf592d"
        m0d_serial: "Z3021NVY"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf592d"
      - m0d_wwn: "0x5000c50078d05a19"
        m0d_serial: "Z3021LCR"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d05a19"
      - m0d_wwn: "0x5000c50078cf8b78"
        m0d_serial: "Z3021NQK"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8b78"
      - m0d_wwn: "0x5000c50078cfe8dd"
        m0d_serial: "Z3021M67"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfe8dd"
      - m0d_wwn: "0x5000c50078cfa307"
        m0d_serial: "Z3021MRJ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfa307"
      - m0d_wwn: "0x5000c50078d01e24"
        m0d_serial: "Z3021M13"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d01e24"
      - m0d_wwn: "0x5000c50078cf83cb"
        m0d_serial: "Z3021MZJ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf83cb"
      - m0d_wwn: "0x5000c50078cfad11"
        m0d_serial: "Z3021MKK"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfad11"
      - m0d_wwn: "0x5000c50078cf8d7f"
        m0d_serial: "Z3021NQY"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8d7f"
      - m0d_wwn: "0x5000c50078cfb707"
        m0d_serial: "Z3021MMX"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfb707"
      - m0d_wwn: "0x5000c50078cfe039"
        m0d_serial: "Z3021M77"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfe039"
      - m0d_wwn: "0x5000c50078cf8abc"
        m0d_serial: "Z3021NPC"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8abc"
      - m0d_wwn: "0x5000c50078cff869"
        m0d_serial: "Z3021LX4"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cff869"
      - m0d_wwn: "0x5000c50078cfaa2d"
        m0d_serial: "Z3021MWQ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfaa2d"
      - m0d_wwn: "0x5000c50078cf8257"
        m0d_serial: "Z3021MYS"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8257"
      - m0d_wwn: "0x5000c50078d009ea"
        m0d_serial: "Z3021MHP"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d009ea"
      - m0d_wwn: "0x5000c50078cfb68c"
        m0d_serial: "Z3021ML2"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfb68c"
      - m0d_wwn: "0x5000c50078cfa022"
        m0d_serial: "Z3021MPT"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfa022"
      - m0d_wwn: "0x5000c50078c12499"
        m0d_serial: "Z3020CK3"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078c12499"
      - m0d_wwn: "0x5000c50078cfa466"
        m0d_serial: "Z3021MQ9"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfa466"
      - m0d_wwn: "0x5000c50078cff64c"
        m0d_serial: "Z3021LWV"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cff64c"
      - m0d_wwn: "0x5000c50078cf8a69"
        m0d_serial: "Z3021NPM"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8a69"
      - m0d_wwn: "0x5000c50078cf84bb"
        m0d_serial: "Z3021MYW"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf84bb"
      - m0d_wwn: "0x5000c50078cfb7b0"
        m0d_serial: "Z3021MLT"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfb7b0"
      - m0d_wwn: "0x5000c50078cf7f9b"
        m0d_serial: "Z3021N70"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf7f9b"
      - m0d_wwn: "0x5000c50078cfc770"
        m0d_serial: "Z3021NJA"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfc770"
      - m0d_wwn: "0x5000c50078cfc366"
        m0d_serial: "Z3021MEG"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfc366"
      - m0d_wwn: "0x5000c50078d0227a"
        m0d_serial: "Z3021LTV"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d0227a"
      - m0d_wwn: "0x5000c50078cfabb1"
        m0d_serial: "Z3021MKD"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfabb1"
      - m0d_wwn: "0x5000c50078cfdc0c"
        m0d_serial: "Z3021MB4"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfdc0c"
      - m0d_wwn: "0x5000c50078cff68b"
        m0d_serial: "Z3021M3G"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cff68b"
      - m0d_wwn: "0x5000c50078cf8c98"
        m0d_serial: "Z3021NAK"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8c98"
      - m0d_wwn: "0x5000c50078cfa194"
        m0d_serial: "Z3021MVK"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfa194"
      - m0d_wwn: "0x5000c50078cfc6e9"
        m0d_serial: "Z3021ME6"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfc6e9"
      - m0d_wwn: "0x5000c50078d02cdf"
        m0d_serial: "Z3021M5N"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d02cdf"
      - m0d_wwn: "0x5000c50078cf8714"
        m0d_serial: "Z3021NAL"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8714"
      - m0d_wwn: "0x5000c50078d01d4c"
        m0d_serial: "Z3021M14"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d01d4c"
      - m0d_wwn: "0x5000c50078cfc002"
        m0d_serial: "Z3021MCC"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfc002"
      - m0d_wwn: "0x5000c50078cfdbc9"
        m0d_serial: "Z3021M5S"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfdbc9"
      - m0d_wwn: "0x5000c50078cfbfa7"
        m0d_serial: "Z3021NEE"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfbfa7"
      - m0d_wwn: "0x5000c50078cf6974"
        m0d_serial: "Z3021NS0"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf6974"
      - m0d_wwn: "0x5000c50078cffb9b"
        m0d_serial: "Z3021M1W"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cffb9b"
      - m0d_wwn: "0x5000c50078cfa1d9"
        m0d_serial: "Z3021N30"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfa1d9"
      - m0d_wwn: "0x5000c50078cf84c8"
        m0d_serial: "Z3021NBN"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf84c8"
      - m0d_wwn: "0x5000c50078d060a2"
        m0d_serial: "Z3021LBW"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d060a2"
      - m0d_wwn: "0x5000c50078cf84a2"
        m0d_serial: "Z3021N7E"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf84a2"
      - m0d_wwn: "0x5000c50078cf8899"
        m0d_serial: "Z3021NM9"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8899"
      - m0d_wwn: "0x5000c50078cfbc8c"
        m0d_serial: "Z3021MMD"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfbc8c"
      - m0d_wwn: "0x5000c50078cf5eb0"
        m0d_serial: "Z3021NNT"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf5eb0"
      - m0d_wwn: "0x5000c50078d023e6"
        m0d_serial: "Z3021LLE"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d023e6"
      - m0d_wwn: "0x5000c50078cf8c0c"
        m0d_serial: "Z3021NQ2"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8c0c"
      - m0d_wwn: "0x5000c50078cf7ff2"
        m0d_serial: "Z3021N6R"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf7ff2"
      - m0d_wwn: "0x5000c50078cf66f3"
        m0d_serial: "Z3021NJB"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf66f3"
      - m0d_wwn: "0x5000c50078cf7b36"
        m0d_serial: "Z3021NA2"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf7b36"
      - m0d_wwn: "0x5000c50078cf9f29"
        m0d_serial: "Z3021MNR"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf9f29"
      - m0d_wwn: "0x5000c50078d01f74"
        m0d_serial: "Z3021LW5"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d01f74"
      - m0d_wwn: "0x5000c50078cfc979"
        m0d_serial: "Z3021MG0"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfc979"
      - m0d_wwn: "0x5000c50078cf7b6b"
        m0d_serial: "Z3021N8D"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf7b6b"
      - m0d_wwn: "0x5000c50078cf6516"
        m0d_serial: "Z3021NRK"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf6516"
      - m0d_wwn: "0x5000c50078cf9ab0"
        m0d_serial: "Z3021MVC"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf9ab0"
      - m0d_wwn: "0x5000c50078cf8446"
        m0d_serial: "Z3021MZZ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8446"
      - m0d_wwn: "0x5000c50078d12c78"
        m0d_serial: "Z3021MV4"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d12c78"
      - m0d_wwn: "0x5000c50078cf86f8"
        m0d_serial: "Z3021N3S"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf86f8"
      - m0d_wwn: "0x5000c50078cfaa5e"
        m0d_serial: "Z3021NRP"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfaa5e"
      - m0d_wwn: "0x5000c50078cfc5fd"
        m0d_serial: "Z3021MDM"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfc5fd"
      - m0d_wwn: "0x5000c50078cfdc55"
        m0d_serial: "Z3021M56"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfdc55"
      - m0d_wwn: "0x5000c50078d01a20"
        m0d_serial: "Z3021LPE"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d01a20"
      - m0d_wwn: "0x5000c50078d022b5"
        m0d_serial: "Z3021LJS"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d022b5"
  - m0h_fqdn: "castor-dev1-1-ssu-1-8"
    host_mem_as: 536870912
    host_mem_rss: 65772544
    host_mem_stack: 65772544
    host_mem_memlock: 65772544
    host_cores: [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]
    lnid: "172.18.1.8@tcp"
    m0h_roles:
      - name: ha
      - name: storage

    m0h_devices:
      - m0d_wwn: "0x5000c5007927abee"
        m0d_serial: "Z30296CS"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007927abee"
      - m0d_wwn: "0x5000c5007925711c"
        m0d_serial: "Z3029DT1"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007925711c"
      - m0d_wwn: "0x5000c500792600cb"
        m0d_serial: "Z3029CNL"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792600cb"
      - m0d_wwn: "0x5000c5007927af46"
        m0d_serial: "Z30296GY"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007927af46"
      - m0d_wwn: "0x5000c50079265ccd"
        m0d_serial: "Z3029D71"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079265ccd"
      - m0d_wwn: "0x5000c5007927af32"
        m0d_serial: "Z30296D5"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007927af32"
      - m0d_wwn: "0x5000c5007927aa76"
        m0d_serial: "Z30296BQ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007927aa76"
      - m0d_wwn: "0x5000c50079257952"
        m0d_serial: "Z3029DRH"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079257952"
      - m0d_wwn: "0x5000c5007925f2e5"
        m0d_serial: "Z3029C3X"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007925f2e5"
      - m0d_wwn: "0x5000c50079254909"
        m0d_serial: "Z3029E8L"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079254909"
      - m0d_wwn: "0x5000c50079267279"
        m0d_serial: "Z3029DGS"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079267279"
      - m0d_wwn: "0x5000c5007926494e"
        m0d_serial: "Z3029DCY"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926494e"
      - m0d_wwn: "0x5000c50079271ee6"
        m0d_serial: "Z3029959"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079271ee6"
      - m0d_wwn: "0x5000c5007925ec5f"
        m0d_serial: "Z3029C6T"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007925ec5f"
      - m0d_wwn: "0x5000c5007927328b"
        m0d_serial: "Z302985T"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007927328b"
      - m0d_wwn: "0x5000c5007925e278"
        m0d_serial: "Z3029C8K"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007925e278"
      - m0d_wwn: "0x5000c500792731ad"
        m0d_serial: "Z3029851"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792731ad"
      - m0d_wwn: "0x5000c50079273664"
        m0d_serial: "Z302984E"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079273664"
      - m0d_wwn: "0x5000c500792631c6"
        m0d_serial: "Z3029CLY"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792631c6"
      - m0d_wwn: "0x5000c5007925f4f8"
        m0d_serial: "Z3029BWW"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007925f4f8"
      - m0d_wwn: "0x5000c50079276649"
        m0d_serial: "Z30297FQ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079276649"
      - m0d_wwn: "0x5000c5007927c63e"
        m0d_serial: "Z3029636"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007927c63e"
      - m0d_wwn: "0x5000c5007927af0f"
        m0d_serial: "Z30296GX"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007927af0f"
      - m0d_wwn: "0x5000c50079256e8b"
        m0d_serial: "Z3029DSC"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079256e8b"
      - m0d_wwn: "0x5000c50079263d3b"
        m0d_serial: "Z3029D65"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079263d3b"
      - m0d_wwn: "0x5000c500792654f6"
        m0d_serial: "Z3029DEV"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792654f6"
      - m0d_wwn: "0x5000c5007925f29e"
        m0d_serial: "Z3029C43"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007925f29e"
      - m0d_wwn: "0x5000c50079266182"
        m0d_serial: "Z3029CYW"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079266182"
      - m0d_wwn: "0x5000c50079272aa7"
        m0d_serial: "Z3029837"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079272aa7"
      - m0d_wwn: "0x5000c5007925fe70"
        m0d_serial: "Z3029CEJ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007925fe70"
      - m0d_wwn: "0x5000c50079279054"
        m0d_serial: "Z30297JW"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079279054"
      - m0d_wwn: "0x5000c500792646d6"
        m0d_serial: "Z3029DBZ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792646d6"
      - m0d_wwn: "0x5000c50079255dff"
        m0d_serial: "Z3029E4E"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079255dff"
      - m0d_wwn: "0x5000c50079271f0c"
        m0d_serial: "Z302988Q"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079271f0c"
      - m0d_wwn: "0x5000c50079272d88"
        m0d_serial: "Z302982K"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079272d88"
      - m0d_wwn: "0x5000c50079263bc5"
        m0d_serial: "Z3029DJP"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079263bc5"
      - m0d_wwn: "0x5000c500792661f2"
        m0d_serial: "Z3029CYT"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792661f2"
      - m0d_wwn: "0x5000c500792743e8"
        m0d_serial: "Z30297TC"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792743e8"
      - m0d_wwn: "0x5000c500792652e6"
        m0d_serial: "Z3029CP1"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792652e6"
      - m0d_wwn: "0x5000c50079279566"
        m0d_serial: "Z30296N5"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079279566"
      - m0d_wwn: "0x5000c50079279185"
        m0d_serial: "Z30296L3"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079279185"
      - m0d_wwn: "0x5000c50079278d9f"
        m0d_serial: "Z30297J2"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079278d9f"
      - m0d_wwn: "0x5000c5007926e127"
        m0d_serial: "Z30298YC"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926e127"
      - m0d_wwn: "0x5000c5007926efec"
        m0d_serial: "Z30298SB"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926efec"
      - m0d_wwn: "0x5000c5007927892b"
        m0d_serial: "Z30296T7"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007927892b"
      - m0d_wwn: "0x5000c5007925c8fc"
        m0d_serial: "Z3029CCC"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007925c8fc"
      - m0d_wwn: "0x5000c50079270c2b"
        m0d_serial: "Z30298FG"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079270c2b"
      - m0d_wwn: "0x5000c50079273e18"
        m0d_serial: "Z302986T"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079273e18"
      - m0d_wwn: "0x5000c5007925f117"
        m0d_serial: "Z3029C31"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007925f117"
      - m0d_wwn: "0x5000c50079271f44"
        m0d_serial: "Z30298M7"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079271f44"
      - m0d_wwn: "0x5000c500792754c8"
        m0d_serial: "Z30297N5"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792754c8"
      - m0d_wwn: "0x5000c5007925ea4d"
        m0d_serial: "Z3029CBA"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007925ea4d"
      - m0d_wwn: "0x5000c50079264e70"
        m0d_serial: "Z3029CZV"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079264e70"
      - m0d_wwn: "0x5000c500792792df"
        m0d_serial: "Z30297JS"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792792df"
      - m0d_wwn: "0x5000c5007926442d"
        m0d_serial: "Z3029CWD"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926442d"
      - m0d_wwn: "0x5000c5007926fee6"
        m0d_serial: "Z30298ZX"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926fee6"
      - m0d_wwn: "0x5000c50079277e40"
        m0d_serial: "Z3029774"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079277e40"
      - m0d_wwn: "0x5000c50079256352"
        m0d_serial: "Z3029E04"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079256352"
      - m0d_wwn: "0x5000c500792723bf"
        m0d_serial: "Z30298CJ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792723bf"
      - m0d_wwn: "0x5000c50079264df3"
        m0d_serial: "Z3029D2G"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079264df3"
      - m0d_wwn: "0x5000c50079271f78"
        m0d_serial: "Z302995R"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079271f78"
      - m0d_wwn: "0x5000c5007927fc18"
        m0d_serial: "Z30295HK"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007927fc18"
      - m0d_wwn: "0x5000c500792667bf"
        m0d_serial: "Z3029D9C"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792667bf"
      - m0d_wwn: "0x5000c50079264728"
        m0d_serial: "Z3029DGC"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079264728"
      - m0d_wwn: "0x5000c50079266734"
        m0d_serial: "Z3029D56"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079266734"
      - m0d_wwn: "0x5000c500792630aa"
        m0d_serial: "Z3029CKW"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792630aa"
      - m0d_wwn: "0x5000c500792620af"
        m0d_serial: "Z3029CMP"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792620af"
      - m0d_wwn: "0x5000c50079271f30"
        m0d_serial: "Z302991D"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079271f30"
      - m0d_wwn: "0x5000c50079263332"
        m0d_serial: "Z3029CTZ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079263332"
      - m0d_wwn: "0x5000c50079252c3e"
        m0d_serial: "Z3029EJK"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079252c3e"
      - m0d_wwn: "0x5000c5007925212c"
        m0d_serial: "Z3029EQL"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007925212c"
      - m0d_wwn: "0x5000c50079273d79"
        m0d_serial: "Z302984C"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079273d79"
      - m0d_wwn: "0x5000c5007927ac0d"
        m0d_serial: "Z30296C3"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007927ac0d"
      - m0d_wwn: "0x5000c5007925fdf1"
        m0d_serial: "Z3029C54"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007925fdf1"
      - m0d_wwn: "0x5000c500792668f2"
        m0d_serial: "Z3029D94"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792668f2"
      - m0d_wwn: "0x5000c5007926e566"
        m0d_serial: "Z302990G"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926e566"
      - m0d_wwn: "0x5000c50079272472"
        m0d_serial: "Z30298B7"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079272472"
      - m0d_wwn: "0x5000c500792790f5"
        m0d_serial: "Z30296LE"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792790f5"
      - m0d_wwn: "0x5000c500792719e3"
        m0d_serial: "Z302993W"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792719e3"
      - m0d_wwn: "0x5000c50079278bd1"
        m0d_serial: "Z30297J6"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079278bd1"
      - m0d_wwn: "0x5000c5007927aaf5"
        m0d_serial: "Z30296C5"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007927aaf5"
      - m0d_wwn: "0x5000c5007925243d"
        m0d_serial: "Z3029ERA"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007925243d"
  - m0h_fqdn: "castor-dev1-1-ssu-1-9"
    host_mem_as: 536870912
    host_mem_rss: 65772544
    host_mem_stack: 65772544
    host_mem_memlock: 65772544
    host_cores: [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]
    lnid: "172.18.1.9@tcp"
    m0h_roles:
      - name: ha
      - name: storage

    m0h_devices:
      - m0d_wwn: "0x5000c50078d038b1"
        m0d_serial: "Z3021LFD"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d038b1"
      - m0d_wwn: "0x5000c50078cf838f"
        m0d_serial: "Z3021N2P"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf838f"
      - m0d_wwn: "0x5000c50078cf7ef0"
        m0d_serial: "Z3021N99"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf7ef0"
      - m0d_wwn: "0x5000c50078cf9f30"
        m0d_serial: "Z3021MS8"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf9f30"
      - m0d_wwn: "0x5000c50078cf7a37"
        m0d_serial: "Z3021N2M"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf7a37"
      - m0d_wwn: "0x5000c50078cf6304"
        m0d_serial: "Z3021NKM"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf6304"
      - m0d_wwn: "0x5000c50078cfa394"
        m0d_serial: "Z3021MR5"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfa394"
      - m0d_wwn: "0x5000c50078d058ce"
        m0d_serial: "Z3021LD1"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d058ce"
      - m0d_wwn: "0x5000c50078cf654a"
        m0d_serial: "Z3021NH4"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf654a"
      - m0d_wwn: "0x5000c50078cfacb9"
        m0d_serial: "Z3021MWN"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfacb9"
      - m0d_wwn: "0x5000c50078d028d2"
        m0d_serial: "Z3021LVW"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d028d2"
      - m0d_wwn: "0x5000c50078cfcee2"
        m0d_serial: "Z3021MBK"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfcee2"
      - m0d_wwn: "0x5000c50078cf9fa0"
        m0d_serial: "Z3021MNN"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf9fa0"
      - m0d_wwn: "0x5000c50078cf864a"
        m0d_serial: "Z3021NAG"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf864a"
      - m0d_wwn: "0x5000c50078cfdb60"
        m0d_serial: "Z3021M9Z"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfdb60"
      - m0d_wwn: "0x5000c50078cff3d5"
        m0d_serial: "Z3021M3R"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cff3d5"
      - m0d_wwn: "0x5000c50078cf9681"
        m0d_serial: "Z3021N2T"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf9681"
      - m0d_wwn: "0x5000c50078cfe3ab"
        m0d_serial: "Z3021M7G"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfe3ab"
      - m0d_wwn: "0x5000c50078cfa469"
        m0d_serial: "Z3021MNH"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfa469"
      - m0d_wwn: "0x5000c50078cfde2f"
        m0d_serial: "Z3021M62"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfde2f"
      - m0d_wwn: "0x5000c50078d055a1"
        m0d_serial: "Z3021LFZ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d055a1"
      - m0d_wwn: "0x5000c50078cf60ef"
        m0d_serial: "Z3021NJK"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf60ef"
      - m0d_wwn: "0x5000c50078d020ee"
        m0d_serial: "Z3021M0Z"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d020ee"
      - m0d_wwn: "0x5000c50078cf9e81"
        m0d_serial: "Z3021MNG"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf9e81"
      - m0d_wwn: "0x5000c50078cf7d5a"
        m0d_serial: "Z3021N5W"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf7d5a"
      - m0d_wwn: "0x5000c50078cff3dd"
        m0d_serial: "Z3021MHQ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cff3dd"
      - m0d_wwn: "0x5000c50078d01110"
        m0d_serial: "Z3021LYA"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d01110"
      - m0d_wwn: "0x5000c50078cf622d"
        m0d_serial: "Z3021NRT"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf622d"
      - m0d_wwn: "0x5000c50078cfd891"
        m0d_serial: "Z3021MHX"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfd891"
      - m0d_wwn: "0x5000c50078cffd6f"
        m0d_serial: "Z3021LR0"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cffd6f"
      - m0d_wwn: "0x5000c50078cf8d62"
        m0d_serial: "Z3021N3W"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8d62"
      - m0d_wwn: "0x5000c50078cfce48"
        m0d_serial: "Z3021M97"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfce48"
      - m0d_wwn: "0x5000c50078d02112"
        m0d_serial: "Z3021M6P"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d02112"
      - m0d_wwn: "0x5000c50078cff628"
        m0d_serial: "Z3021LXF"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cff628"
      - m0d_wwn: "0x5000c50078cfe89c"
        m0d_serial: "Z3021MA3"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfe89c"
      - m0d_wwn: "0x5000c50078cf777d"
        m0d_serial: "Z3021NEZ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf777d"
      - m0d_wwn: "0x5000c50078cfabd4"
        m0d_serial: "Z3021MX4"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfabd4"
      - m0d_wwn: "0x5000c50078cf2ca3"
        m0d_serial: "Z3021P1D"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf2ca3"
      - m0d_wwn: "0x5000c50078cf89b9"
        m0d_serial: "Z3021NNH"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf89b9"
      - m0d_wwn: "0x5000c50078cf8465"
        m0d_serial: "Z3021MZA"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8465"
      - m0d_wwn: "0x5000c50078cf88be"
        m0d_serial: "Z3021N0Y"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf88be"
      - m0d_wwn: "0x5000c50078d005bc"
        m0d_serial: "Z3021LSJ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d005bc"
      - m0d_wwn: "0x5000c50078d05ce7"
        m0d_serial: "Z3021LDY"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d05ce7"
      - m0d_wwn: "0x5000c50078cf9f91"
        m0d_serial: "Z3021MV2"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf9f91"
      - m0d_wwn: "0x5000c50078cfffcf"
        m0d_serial: "Z3021LSA"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfffcf"
      - m0d_wwn: "0x5000c50078d03626"
        m0d_serial: "Z3021LE6"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d03626"
      - m0d_wwn: "0x5000c50078cf8bf1"
        m0d_serial: "Z3021N4G"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8bf1"
      - m0d_wwn: "0x5000c50078d00729"
        m0d_serial: "Z3021M2P"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d00729"
      - m0d_wwn: "0x5000c50078cf58dd"
        m0d_serial: "Z3021NGK"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf58dd"
      - m0d_wwn: "0x5000c50078cf7f1f"
        m0d_serial: "Z3021N42"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf7f1f"
      - m0d_wwn: "0x5000c50078cffd69"
        m0d_serial: "Z3021LXQ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cffd69"
      - m0d_wwn: "0x5000c50078d0049e"
        m0d_serial: "Z3021LS3"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d0049e"
      - m0d_wwn: "0x5000c50078d00830"
        m0d_serial: "Z3021M32"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d00830"
      - m0d_wwn: "0x5000c50078cff45c"
        m0d_serial: "Z3021M43"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cff45c"
      - m0d_wwn: "0x5000c50078cfa46d"
        m0d_serial: "Z3021MQ5"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfa46d"
      - m0d_wwn: "0x5000c50078d0228e"
        m0d_serial: "Z3021LM6"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d0228e"
      - m0d_wwn: "0x5000c50078d0052d"
        m0d_serial: "Z3021LRM"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d0052d"
      - m0d_wwn: "0x5000c50078cf9e6e"
        m0d_serial: "Z3021MVS"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf9e6e"
      - m0d_wwn: "0x5000c50078d01d4f"
        m0d_serial: "Z3021M04"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d01d4f"
      - m0d_wwn: "0x5000c50078cf8c22"
        m0d_serial: "Z3021N6P"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8c22"
      - m0d_wwn: "0x5000c50078cfc395"
        m0d_serial: "Z3021MEQ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfc395"
      - m0d_wwn: "0x5000c50078cfc4ff"
        m0d_serial: "Z3021NEC"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfc4ff"
      - m0d_wwn: "0x5000c50078cffd42"
        m0d_serial: "Z3021M3Z"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cffd42"
      - m0d_wwn: "0x5000c50078d02236"
        m0d_serial: "Z3021LW9"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d02236"
      - m0d_wwn: "0x5000c50078cfd90a"
        m0d_serial: "Z3021M45"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfd90a"
      - m0d_wwn: "0x5000c50078cfc1f2"
        m0d_serial: "Z3021NDK"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfc1f2"
      - m0d_wwn: "0x5000c50078d05203"
        m0d_serial: "Z3021LAD"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d05203"
      - m0d_wwn: "0x5000c50078d01f9a"
        m0d_serial: "Z3021M17"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d01f9a"
      - m0d_wwn: "0x5000c50078cf8984"
        m0d_serial: "Z3021NMR"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8984"
      - m0d_wwn: "0x5000c50078cfa44c"
        m0d_serial: "Z3021MRY"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cfa44c"
      - m0d_wwn: "0x5000c50078cf85fd"
        m0d_serial: "Z3021MZG"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf85fd"
      - m0d_wwn: "0x5000c50078d058ae"
        m0d_serial: "Z3021LCW"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d058ae"
      - m0d_wwn: "0x5000c50078cf8a78"
        m0d_serial: "Z3021NM8"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf8a78"
      - m0d_wwn: "0x5000c50078cf5a55"
        m0d_serial: "Z3021NW2"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf5a55"
      - m0d_wwn: "0x5000c50078cf5374"
        m0d_serial: "Z3021P25"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf5374"
      - m0d_wwn: "0x5000c50078cf6a9b"
        m0d_serial: "Z3021NJ8"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf6a9b"
      - m0d_wwn: "0x5000c50078cf7951"
        m0d_serial: "Z3021NCP"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf7951"
      - m0d_wwn: "0x5000c50078cf7af5"
        m0d_serial: "Z3021N8N"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf7af5"
      - m0d_wwn: "0x5000c50078cf5b9c"
        m0d_serial: "Z3021NH2"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf5b9c"
      - m0d_wwn: "0x5000c50078cf7de9"
        m0d_serial: "Z3021NA1"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cf7de9"
      - m0d_wwn: "0x5000c50078cff15a"
        m0d_serial: "Z3021LWT"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078cff15a"
      - m0d_wwn: "0x5000c50078d04cb8"
        m0d_serial: "Z3021LGG"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50078d04cb8"
id_m0_globals:
  m0_data_units: 8
  m0_parity_units: 2
  m0_md_redundancy: 3
  m0_failure_set_gen:
    tag: Dynamic
    contents: []
EOF

	# this SSU is temporary unused
	cat << EOF > /dev/null
  - m0h_fqdn: "castor-dev1-1-ssu-1-11"
    host_mem_as: 536870912
    host_mem_rss: 65772544
    host_mem_stack: 65772544
    host_mem_memlock: 65772544
    host_cores: [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]
    lnid: "172.18.1.11@tcp"
    m0h_roles:
      - name: ha
      - name: storage

    m0h_devices:
      - m0d_wwn: "0x5000c500792643f8"
        m0d_serial: "Z3029DGA"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792643f8"
      - m0d_wwn: "0x5000c50079273986"
        m0d_serial: "Z302988K"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079273986"
      - m0d_wwn: "0x5000c50079263f9b"
        m0d_serial: "Z3029DJY"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079263f9b"
      - m0d_wwn: "0x5000c50079274686"
        m0d_serial: "Z30297VH"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079274686"
      - m0d_wwn: "0x5000c50079256e44"
        m0d_serial: "Z3029E6M"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079256e44"
      - m0d_wwn: "0x5000c500792731bd"
        m0d_serial: "Z302985M"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792731bd"
      - m0d_wwn: "0x5000c5007927b806"
        m0d_serial: "Z3029668"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007927b806"
      - m0d_wwn: "0x5000c5007925fa8b"
        m0d_serial: "Z3029BZW"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007925fa8b"
      - m0d_wwn: "0x5000c5007927b5e8"
        m0d_serial: "Z302968W"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007927b5e8"
      - m0d_wwn: "0x5000c500792707a9"
        m0d_serial: "Z30298PE"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792707a9"
      - m0d_wwn: "0x5000c50079264d1a"
        m0d_serial: "Z3029CZB"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079264d1a"
      - m0d_wwn: "0x5000c500792796fa"
        m0d_serial: "Z30296MZ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792796fa"
      - m0d_wwn: "0x5000c50079265d0f"
        m0d_serial: "Z3029D9V"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079265d0f"
      - m0d_wwn: "0x5000c5007926dc91"
        m0d_serial: "Z30298XM"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926dc91"
      - m0d_wwn: "0x5000c50079271b54"
        m0d_serial: "Z302989K"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079271b54"
      - m0d_wwn: "0x5000c50079273741"
        m0d_serial: "Z302993D"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079273741"
      - m0d_wwn: "0x5000c50079273ac9"
        m0d_serial: "Z302982H"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079273ac9"
      - m0d_wwn: "0x5000c50079272e30"
        m0d_serial: "Z302985P"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079272e30"
      - m0d_wwn: "0x5000c50079265192"
        m0d_serial: "Z3029DDY"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079265192"
      - m0d_wwn: "0x5000c5007926e78e"
        m0d_serial: "Z30298WE"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926e78e"
      - m0d_wwn: "0x5000c5007927865e"
        m0d_serial: "Z3029722"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007927865e"
      - m0d_wwn: "0x5000c50079271eb9"
        m0d_serial: "Z3029944"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079271eb9"
      - m0d_wwn: "0x5000c50079265acb"
        m0d_serial: "Z3029DHS"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079265acb"
      - m0d_wwn: "0x5000c50079264fd5"
        m0d_serial: "Z3029DK1"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079264fd5"
      - m0d_wwn: "0x5000c5007926db30"
        m0d_serial: "Z3029901"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926db30"
      - m0d_wwn: "0x5000c50079271043"
        m0d_serial: "Z30298EN"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079271043"
      - m0d_wwn: "0x5000c5007927511c"
        m0d_serial: "Z30297W2"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007927511c"
      - m0d_wwn: "0x5000c50079273874"
        m0d_serial: "Z302980S"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079273874"
      - m0d_wwn: "0x5000c50079263e54"
        m0d_serial: "Z3029CRK"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079263e54"
      - m0d_wwn: "0x5000c50079273ec8"
        m0d_serial: "Z30297Y5"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079273ec8"
      - m0d_wwn: "0x5000c50079271a42"
        m0d_serial: "Z30298AD"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079271a42"
      - m0d_wwn: "0x5000c50079278e83"
        m0d_serial: "Z30297HH"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079278e83"
      - m0d_wwn: "0x5000c50079263ab7"
        m0d_serial: "Z3029CQB"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079263ab7"
      - m0d_wwn: "0x5000c500792768c3"
        m0d_serial: "Z30297DW"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792768c3"
      - m0d_wwn: "0x5000c50079270fdf"
        m0d_serial: "Z30298DZ"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079270fdf"
      - m0d_wwn: "0x5000c50079264fb0"
        m0d_serial: "Z3029D39"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079264fb0"
      - m0d_wwn: "0x5000c5007926eae8"
        m0d_serial: "Z30298W6"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926eae8"
      - m0d_wwn: "0x5000c5007926302c"
        m0d_serial: "Z3029CPR"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926302c"
      - m0d_wwn: "0x5000c50079274d63"
        m0d_serial: "Z30297SS"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079274d63"
      - m0d_wwn: "0x5000c5007926716d"
        m0d_serial: "Z3029DGD"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926716d"
      - m0d_wwn: "0x5000c500791f8f1a"
        m0d_serial: "Z3028QDT"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500791f8f1a"
      - m0d_wwn: "0x5000c5007926dbdb"
        m0d_serial: "Z30298XC"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926dbdb"
      - m0d_wwn: "0x5000c50079275e08"
        m0d_serial: "Z30297LH"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079275e08"
      - m0d_wwn: "0x5000c50079276ac1"
        m0d_serial: "Z30297F3"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079276ac1"
      - m0d_wwn: "0x5000c50079275309"
        m0d_serial: "Z30297QN"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079275309"
      - m0d_wwn: "0x5000c5007927089c"
        m0d_serial: "Z30298KB"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007927089c"
      - m0d_wwn: "0x5000c500792659c8"
        m0d_serial: "Z3029CYH"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792659c8"
      - m0d_wwn: "0x5000c5007926fa15"
        m0d_serial: "Z30298XG"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926fa15"
      - m0d_wwn: "0x5000c50079263384"
        m0d_serial: "Z3029CNH"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079263384"
      - m0d_wwn: "0x5000c50079272cdb"
        m0d_serial: "Z3029878"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079272cdb"
      - m0d_wwn: "0x5000c5007926545c"
        m0d_serial: "Z3029DKS"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926545c"
      - m0d_wwn: "0x5000c500792746e1"
        m0d_serial: "Z30297SK"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792746e1"
      - m0d_wwn: "0x5000c5007926fe4c"
        m0d_serial: "Z30298MF"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926fe4c"
      - m0d_wwn: "0x5000c50079263f5e"
        m0d_serial: "Z3029CQH"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079263f5e"
      - m0d_wwn: "0x5000c500792722d1"
        m0d_serial: "Z30298CV"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792722d1"
      - m0d_wwn: "0x5000c50079270ed7"
        m0d_serial: "Z30298PM"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079270ed7"
      - m0d_wwn: "0x5000c50079274b75"
        m0d_serial: "Z302987C"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079274b75"
      - m0d_wwn: "0x5000c50079270de2"
        m0d_serial: "Z30298KR"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079270de2"
      - m0d_wwn: "0x5000c50079271ca9"
        m0d_serial: "Z302988P"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079271ca9"
      - m0d_wwn: "0x5000c500792783ad"
        m0d_serial: "Z30296XR"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792783ad"
      - m0d_wwn: "0x5000c50079277a21"
        m0d_serial: "Z30297K8"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079277a21"
      - m0d_wwn: "0x5000c50079271a12"
        m0d_serial: "Z302993V"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079271a12"
      - m0d_wwn: "0x5000c50079264e81"
        m0d_serial: "Z3029D0D"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079264e81"
      - m0d_wwn: "0x5000c50079270aba"
        m0d_serial: "Z30298P5"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079270aba"
      - m0d_wwn: "0x5000c50079270ddd"
        m0d_serial: "Z30298NL"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079270ddd"
      - m0d_wwn: "0x5000c5007927a4ec"
        m0d_serial: "Z30296JN"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007927a4ec"
      - m0d_wwn: "0x5000c5007926f906"
        m0d_serial: "Z30298W3"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926f906"
      - m0d_wwn: "0x5000c5007926099d"
        m0d_serial: "Z3029CKM"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926099d"
      - m0d_wwn: "0x5000c50079262ef6"
        m0d_serial: "Z3029CJT"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079262ef6"
      - m0d_wwn: "0x5000c5007925e978"
        m0d_serial: "Z3029C5Q"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007925e978"
      - m0d_wwn: "0x5000c50079274f0d"
        m0d_serial: "Z302981K"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079274f0d"
      - m0d_wwn: "0x5000c5007926f6c6"
        m0d_serial: "Z30298VW"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007926f6c6"
      - m0d_wwn: "0x5000c500791fd563"
        m0d_serial: "Z3028ZVT"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500791fd563"
      - m0d_wwn: "0x5000c50079270873"
        m0d_serial: "Z30298PW"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079270873"
      - m0d_wwn: "0x5000c5007927ab12"
        m0d_serial: "Z30296BX"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007927ab12"
      - m0d_wwn: "0x5000c50079261487"
        m0d_serial: "Z3029BN9"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079261487"
      - m0d_wwn: "0x5000c50079279292"
        m0d_serial: "Z30296V3"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079279292"
      - m0d_wwn: "0x5000c500792656c5"
        m0d_serial: "Z3029D1W"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792656c5"
      - m0d_wwn: "0x5000c5007925f264"
        m0d_serial: "Z3029C7J"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007925f264"
      - m0d_wwn: "0x5000c50079271242"
        m0d_serial: "Z30298PA"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c50079271242"
      - m0d_wwn: "0x5000c500792740d3"
        m0d_serial: "Z302980V"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c500792740d3"
      - m0d_wwn: "0x5000c5007925fb17"
        m0d_serial: "Z3029CF5"
        m0d_bsize: 4096
        m0d_size: 4000787030016
        m0d_path: "/dev/disk/by-id/wwn-0x5000c5007925fb17"
EOF
}

main "$@"
