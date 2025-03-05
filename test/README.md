# Testing socket_vmnet

## Performance testing

You can run performance tests using the perf.sh script.

```console
% test/perf.sh
Usage test/perf.sh command [options]

Commands:
  create          create test vms
  delete          delete test vms
  run             run benchmarks

Options:
  -c COUNT        number of vms
  -t SECONDS      time in seconds to transmit for (default 30)
  -v VM_TYPE      lima vmType [vz, qemu] (default vz)
  -b IFACE        if set, use lima bridged network with IFACE
  -o OUT_DIR      output directory (default perf.out)
```

### Example run

Create test vms:

```console
test/perf.sh create -c 3 2>perf.log
```

Run all tests with shared and bridged modes, vz and qemu vm type,
using 1, 2, and 3 vms:

```bash
for v in vz qemu; do
    for c in $(seq 3); do
        test/perf.sh run -c $c -v $v 2>>perf.log
        test/perf.sh run -c $c -v $v -b en10 2>>perf.log
    done
done
```

Delete the test vms:

```console
test/perf.sh delete -c 3 2>>perf.log
```

This creates the following results:

```console
% tree test/perf.out
test/perf.out
├── bridged-qemu-1
│   ├── host-to-vm.json
│   └── vm-to-host.json
├── bridged-qemu-2
│   ├── host-to-vm.json
│   ├── vm-to-host.json
│   └── vm-to-vm.json
├── bridged-qemu-3
│   ├── host-to-vm.json
│   ├── vm-to-host.json
│   └── vm-to-vm.json
...
```

You can create reports like this:

```console
% for c in 1 2 3; do
    result="test/perf.out/shared-vz-$c/host-to-vm.json"
    gbits_per_second=$(jq -r '.end.sum_received.bits_per_second / 1000000000' < "$result")
    printf "%d vms: %.2f Gbits/s\n" $c $gbits_per_second
done
1 vms: 2.35 Gbits/s
2 vms: 1.83 Gbits/s
3 vms: 1.15 Gbits/s
```
