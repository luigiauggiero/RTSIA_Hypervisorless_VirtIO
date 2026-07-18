#!/bin/sh
set -x  #Sets execution trace (xtrace) for debugging purposes

# Master creates virtual network (tap) interface with provided IP address
ip tuntap del mode tap tap0;ip tuntap add mode tap user $USER tap0;ifconfig tap0 192.168.200.254 up 
haveged & #Linux deamon that generates entropy (has to be waited in demo4) 
insmod /hvl/user-mbox.ko #loads a kernel module for the mailbox (shared memory) that handles Inter-core pyhisical interrupts
cp /hvl/zephyr.elf /lib/firmware/ #Zephyr firmware image preparation
echo zephyr.elf >/sys/class/remoteproc/remoteproc0/firmware # remoteproc is used tu specify which firmware to load in memory

# Traditionally VirtIO was born for VMs with heavy VMM; here lkvm (light) is used as VirtIO backend directly into AMP architecture
# The following parameters are used:
# --shmem-addr 0x37000000 -> specifies physical address of the shared memory
# --rng -> retrieves data from the entropy deamon and creates an associated VirtIO device
# --network mode=tap,tapif=tap0,trans=mmio -> creates VirtIO network card linking it to tap0 interface previously created
# --transport mmio -> specifies that data exchange between Linux and Zephyr will take place by Memory/Mapped I/O, using the shared memory abstracted by VirtIO
/hvl/lkvm run --debug --vxworks --rsld --pmm --debug-nohostfs --transport mmio --shmem-addr 0x37000000 --shmem-size 0x1000000 --cpus 1 --mem 128 --no-dtb --debug --rng --network mode=tap,tapif=tap0,trans=mmio --vproxy

set +x #Disables debug
