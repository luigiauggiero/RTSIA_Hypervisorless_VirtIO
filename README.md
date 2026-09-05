# Investigating Real-Time Communication Between Asymmetric Processors with OpenAMP (Hypervisorless VirtIO) 

This repository contains the custom firmware, linker scripts, and testing procedures used to analyze an experimental **Hypervisorless VirtIO** architecture. The project demonstrates how to establish lateral Inter-Processor Communication (IPC) between a Linux Master (Cortex-A53) and a Zephyr RTOS Remote (Cortex-R5F) on a Zynq UltraScale+ MPSoC, entirely bypassing the need for a heavyweight hypervisor.

Instead of traditional VM-Exits and hypervisor-managed dynamic memory, this implementation relies on a user-space Physical Machine Monitor (PMM), direct hardware Inter-Processor Interrupts (IPIs), and explicitly shared static memory bounce buffers.

Author: Emanuele Barbato, Luigi Auggiero

---

## 🛠️ Core Code Modifications

The bulk of the work involves adapting the Zephyr RTOS firmware (`zephyr.elf`) to bypass standard virtualized VirtIO assumptions and force physical memory interactions.

### 1. Static Memory & VirtQueue Allocation
In a standard environment, the hypervisor allocates VirtQueues dynamically. Here, the firmware is hardcoded to map the VirtIO-MMIO transport directly onto a pre-shared physical memory pool.
* **`prj.conf`**: We explicitly enable the custom linker script (`CONFIG_HAVE_CUSTOM_LINKER_SCRIPT=y`) and inject a specific compiler flag (`-DHVL_VIRTIO`) to trigger the experimental hypervisorless routines in the OpenAMP libraries.
* **`linker_r5_hvl.ld`**: The memory layout is statically defined. The shared memory pool is mapped at `0x37000000`. Crucially, the `KEEP(*(.shared.vring.*))` directive forces the linker to allocate the VirtQueue control rings exactly within this shared physical space, making them symmetrically accessible to the Linux PMM.

### 2. Hardware IPIs over VM-Exits
Without a hypervisor, standard VirtIO "doorbell" registers (VM-exits) cannot trap memory accesses. We replaced them with direct hardware signaling.
* **`src/xlnx_ipi.c`**: This custom driver manages the Xilinx ZynqMP IPI hardware block. 
  * The "kick" notification is implemented in `xlnx_ipi_notify()`, which uses the `WRITE32(dev, IPI_TRIG, (1 << 24))` macro to physically raise an interrupt line towards the Linux master.
  * The reverse notification is caught by the `xlnx_ipi_isr()` hardware interrupt service routine, which triggers the `virtio_mmio_hvl_cb_run()` callback to instruct the driver to scan the Used Ring.

### 3. Custom Data Plane Instrumentation (`virt-rng`)
To validate end-to-end communication and control over the data plane, the silent boot behavior of the `virt-rng` (Random Number Generator) driver was altered.
* **`src/main.c`**: The `get_entropy()` function was instrumented to request a larger 32-byte payload from the Linux PMM. Once the Linux `haveged` daemon fills the bounce buffer and sends the reverse IPI, the Zephyr firmware explicitly dumps the raw hexadecimal payload to the UART console.

### 4. Custom Cross-Compilation
* **`CMakeLists.txt`**: To successfully build this firmware, the standard Zephyr package manager (`west`) was bypassed. The build rules were modified to force the compiler to exclusively link the experimental `virtio-exp` frontend modules, resolving dependency conflicts with the stable OpenAMP release.

---

## 🔬 How to Reproduce the Experiments

The project relies on a QEMU-emulated ZCU102 environment. The following steps reproduce the dynamic memory inspection and payload interception.

### A. Payload Injection (Ping)
From the Linux master console, flood the network with a custom payload (hex `52`, ASCII `'R'`) using a larger packet size to stress the bounce buffers:
```bash
ping -p 52 -s 128 -c 500 192.168.200.2 > /dev/null &
```
B. Static Memory Inspection (QEMU Monitor)
Suspend the emulator and access the QEMU monitor to bypass Linux CONFIG_STRICT_DEVMEM restrictions. Examine the base address of the shared memory:

xp /32xw 0x37000000

Expected Output: You will spot 0x74726976 (the v i r t magic string) and 0x4d564b4c (L K V M Vendor ID), proving the VirtIO device was created by the user-space PMM, not a hypervisor.
C. Dynamic Payload Interception
Traverse the Descriptor Ring (around 0x37001000) to find the physical pointer of the active Bounce Buffer currently allocated by libmetal. Once the address is found (e.g., 0x370078e8), decode it as ASCII characters:

xp /128c 0x370078e8

Expected Output: Right after the non-printable MAC/IP/ICMP headers, you will see a continuous block of 'R' characters, visually proving the transit of the explicitly copied payload through the shared memory bounce buffers.

## ⚠️ Physical Hardware Status (Xilinx ZCU102)

While the real-time firmware compiles into a valid ELF binary loadable via remoteproc, execution on physical silicon is currently blocked by an architectural memory-attribute conflict:

- The generic Linux UIO driver (uio_pdrv_genirq) maps physical shared memory as Device Memory (Device-nGnRE).

- Compiler-optimized paired stores (STP) emitted by the PMM trigger fatal hardware Alignment Faults (BUS_ADRALN) on ARMv8-A cores when targeting Device Memory.

- Addressing this bottleneck requires reconfiguring the devicetree (DTS) and rebuilding the platform BSP to back the shared aperture with a CMA-managed coherent memory pool (uio_dmem_genirq).

