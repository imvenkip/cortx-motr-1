#!/bin/bash
set -eux

IP=172.16.1.212
HALOND=/home/mask/.local/bin/halond
HALONCTL=/home/mask/.local/bin/halonctl
HALON_SOURCES=/work/halon
HALON_FACTS_YAML=halon_facts.yaml

HOSTNAME="`hostname`"

main() {
	sudo killall halond || true
	sleep 1
	sudo rm -rf halon-persistence
	sudo systemctl stop mero-kernel &
	sudo killall -9 lt-m0d m0d lt-m0mkfs m0mkfs || true
	wait

	sudo scripts/install-mero-service -u
	sudo scripts/install-mero-service -l
	sudo utils/m0setup -v -P 6 -N 2 -K 1 -i 1 -d /var/mero/img -s 128 -c
	sudo utils/m0setup -v -P 6 -N 2 -K 1 -i 1 -d /var/mero/img -s 128

	sudo rm -vf /etc/mero/genders
	sudo rm -vf /etc/mero/conf.xc
	sudo rm -vf /etc/mero/disks*.conf

	halon_facts_yaml > $HALON_FACTS_YAML

	sudo $HALOND -l $IP:9000 >& /tmp/halond.log &
	true
	sleep 2
	sudo $HALONCTL -l $IP:9010 -a $IP:9000 bootstrap station
	sudo $HALONCTL -l $IP:9010 -a $IP:9000 bootstrap satellite -t $IP:9000
	sudo $HALONCTL -l $IP:9010 -a $IP:9000 cluster load -f $HALON_FACTS_YAML -r $HALON_SOURCES/mero-halon/scripts/mero_provisioner_role_mappings.ede
}

function halon_facts_yaml() {
	cat << EOF
id_racks:
- rack_idx: 1
  rack_enclosures:
  - enc_idx: 1
    enc_bmc:
    - bmc_user: admin
      bmc_addr: bmc.enclosure1
      bmc_pass: admin
    enc_hosts:
    - h_cpucount: 8
      h_fqdn: $HOSTNAME
      h_memsize: 4096
      h_interfaces:
      - if_network: Data
        if_macAddress: '10-00-00-00-00'
        if_ipAddrs:
        - $IP
    enc_id: enclosure1
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
  - m0d_serial: serial-2
    m0d_bsize: 4096
    m0d_wwn: wwn-2
    m0d_path: /dev/loop2
    m0d_size: 596000000000
  - m0d_serial: serial-3
    m0d_bsize: 4096
    m0d_wwn: wwn-3
    m0d_path: /dev/loop3
    m0d_size: 596000000000
  - m0d_serial: serial-4
    m0d_bsize: 4096
    m0d_wwn: wwn-4
    m0d_path: /dev/loop4
    m0d_size: 596000000000
  - m0d_serial: serial-5
    m0d_bsize: 4096
    m0d_wwn: wwn-5
    m0d_path: /dev/loop5
    m0d_size: 596000000000
  - m0d_serial: serial-6
    m0d_bsize: 4096
    m0d_wwn: wwn-6
    m0d_path: /dev/loop6
    m0d_size: 596000000000
  host_mem_rss: 1
  m0h_fqdn: $HOSTNAME
  m0h_roles:
  - name: confd
  - name: ha
  - name: mds
  - name: storage
  - name: m0t1fs
  host_mem_stack: 1
id_m0_globals:
  m0_parity_units: 1
  m0_md_redundancy: 1
  m0_data_units: 2
  m0_failure_set_gen:
    tag: Dynamic
    contents: []
EOF
}

main "$@"
