#!/bin/bash

set -e
set -o pipefail

cd "$(dirname "$0")"

server_address() {
    limactl shell perf1 ip -j -4 addr show dev lima0 | jq -r '.[0].addr_info[0].local'
}

create() {
    echo "[perf] Creating $count vms"
    for i in $(seq $count); do
        limactl create --name perf$i --tty=false lima.yaml &
    done
    wait
}

delete() {
    echo "[perf] Delete $count vms"
    for i in $(seq $count); do
        limactl delete -f perf$i
    done
}

run() {
    echo "[perf] Updating vms"
    update_vms

    echo "[perf] Starting vms"
    start_vms

    echo "[perf] Starting iperf3 service"
    limactl shell perf1 sudo systemctl start iperf3.service >>perf.log

    config="$out_dir/$mode-$vm_type-$count"
    mkdir -p "$config"
    addr=$(server_address)

    echo "[perf] Running $config/host-to-vm"
    iperf3-darwin --client $addr --time $time --json > "$config/host-to-vm.json"

    echo "[perf] Running $config/vm-to-host"
    iperf3-darwin --client $addr --time $time --reverse --json > "$config/vm-to-host.json"

    if [ $count -gt 1 ]; then
        echo "[perf] Running $config/vm-to-vm"
        limactl shell perf2 iperf3 --client $addr --time $time --json > "$config/vm-to-vm.json"
    fi

    echo "[perf] Stopping vms"
    stop_vms
}

update_vms() {
    socket="/var/run/socket_vmnet"
    if [ "$mode" = "bridged" ]; then
        socket+=".bridged.$iface"
    fi
    for i in $(seq $count); do
        limactl edit perf$i --tty=false \
            --set ".vmType = \"$vm_type\" | .networks[0].socket = \"$socket\""
    done
}

start_vms() {
    for i in $(seq $count); do
        limactl start perf$i &
    done
    wait
}

stop_vms() {
    for i in $(seq $count); do
        limactl stop perf$i
    done
}

usage() {
    echo "Usage $0 command [options]"
    echo
    echo "Commands:"
    echo "  create          create test vms"
    echo "  delete          delete test vms"
    echo "  run             run benchmarks"
    echo
    echo "Options:"
    echo "  -c COUNT        number of vms"
    echo "  -t SECONDS      time in seconds to transmit for (default 30)"
    echo "  -v VM_TYPE      lima vmType [vz, qemu] (default vz)"
    echo "  -b IFACE        if set, use lima bridged network with IFACE"
    echo "  -o OUT_DIR      output directory (default perf.out)"
}

# Defaults
count=2
time=30
vm_type=vz
mode=shared
iface=
out_dir=perf.out

case $1 in
create|delete|run)
    command=$1
    shift
    ;;
*)
    usage
    exit 1
    ;;
esac

args=$(getopt c:t:v:b:o: $*)
if [ $? -ne 0 ]; then
    usage
    exit 1
fi

set -- $args

while :; do
    case $1 in
    -c)
        count=$2
        shift; shift
        ;;
    -t)
        time=$2
        shift; shift
        ;;
    -v)
        vm_type="$2"
        shift; shift
        ;;
    -b)
        mode=bridged
        iface="$2"
        shift; shift
        ;;
    -o)
        out_dir="$2"
        shift; shift
        ;;
    --)
        shift; break;
        ;;
    esac
done

$command
