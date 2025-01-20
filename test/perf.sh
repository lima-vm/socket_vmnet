#!/bin/bash
set -e
cd "$(dirname "$0")"

TIME=${2:-30}

server_address() {
    limactl shell server ip -j -4 addr show dev lima0 | jq -r '.[0].addr_info[0].local'
}

create() {
    limactl create --name server --tty=false lima.yaml &
    limactl create --name client --tty=false lima.yaml &
    wait
}

host-to-vm() {
    limactl start server
    limactl shell server sudo systemctl start iperf3.service
    iperf3-darwin --client $(server_address) --time $TIME
    limactl stop server
}

host-to-vm-2() {
    limactl start server &
    limactl start client &
    wait
    limactl shell server sudo systemctl start iperf3.service
    iperf3-darwin --client $(server_address) --time $TIME
    limactl stop server
    limactl stop client
}

vm-to-vm() {
    limactl start server &
    limactl start client &
    wait
    limactl shell server sudo systemctl start iperf3.service
    addr=$(server_address)
    limactl shell client iperf3 --client $addr --time $TIME
    limactl stop server
    limactl stop client
}

delete() {
    limactl delete -f server
    limactl delete -f client
}

case $1 in
create)
    create
    ;;
host-to-vm)
    host-to-vm
    ;;
host-to-vm-2)
    host-to-vm-2
    ;;
vm-to-vm)
    vm-to-vm
    ;;
delete)
    delete
    ;;
*)
    echo "Usage $0 command"
    echo
    echo "Available commands:"
    echo "  create                  create test vms"
    echo "  delete                  delete test vms"
    echo "  host-to-vm [TIME]       test host to vm performance"
    echo "  host-to-vm-2 [TIME]     test host to vm performance with 1 extra vm"
    echo "  vm-to-vm [TIME]         test vm to vm performance"
    ;;
esac
