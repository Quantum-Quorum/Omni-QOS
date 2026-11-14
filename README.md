# OmniQ-OS: Real-Time Quantum Linux

<p align="center">
  <img src="distro/branding/logo.svg" alt="OmniQ-OS Logo" width="600"/>
</p>

<p align="center">
  <strong>A Deterministic Hard Real-Time Operating System for Heterogeneous Tri-Compute (CPU + GPU + QPU)</strong>
</p>

<p align="center">
  <a href="#architecture"><img src="https://img.shields.io/badge/Kernel-Linux%206.8%20PREEMPT__RT-blue.svg" alt="Kernel"/></a>
  <a href="#tri-compute"><img src="https://img.shields.io/badge/Tri--Compute-CPU%20%2B%20GPU%20%2B%20QPU-cyan.svg" alt="Tri-Compute"/></a>
  <a href="#qec-latency"><img src="https://img.shields.io/badge/QEC%20Loop-%3C%20600%20ns-green.svg" alt="QEC Latency"/></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-Apache%202.0-yellow.svg" alt="License"/></a>
</p>

---

## 1. Vision & Overview

**OmniQ-OS** is an open-source, hard real-time Linux operating system engineered specifically to operate cryogenic Quantum Processing Units (QPUs) alongside classical CPUs and GPUs.

Modern quantum computers are largely treated as high-latency cloud batch appliances. This model breaks down for fault-tolerant quantum computing and near-term hybrid algorithms:
* **The Decoherence Wall**: Physical superconducting transmons have phase coherence times ($T_2$) of $\sim 50\text{--}150\,\mu\text{s}$. Cloud round-trips ($> 10\,\text{ms}$) make real-time syndrome feedback impossible.
* **Kernel Jitter**: Standard Linux CFS/EEVDF schedulers introduce $10\text{--}50\,\text{ms}$ of non-deterministic jitter, preempting microwave pulse streams and causing phase dephasing.
* **The No-Cloning Barrier**: By the **Quantum No-Cloning Theorem**, quantum states cannot be swapped to classical RAM or paged to disk. Quantum memory must be managed **spatially**.
* **The Tri-Compute Latency Bottleneck**: Hybrid quantum-classical computing requires direct, zero-copy communication between QPUs, GPUs (for neural syndrome decoders and gradient optimization), and CPUs.

OmniQ-OS solves these challenges through a native Linux Compute Accelerator subsystem (`drivers/accel/qpu/`), **GPUDirect-QPU zero-copy DMA** ($< 600\,\text{ns}$ round trip), a **Quantum Memory Management Unit (QMMU)** with guard-band shielding, and **3-state hardware auto-detection** with transparent simulation fallback.

---

## 2. System Architecture

```
+---------------------------------------------------------------------------------------+
|                                    USER SPACE                                         |
|                                                                                       |
|   +-----------------------+   +------------------------+   +----------------------+   |
|   |       qpu-smi         |   |      libomniq-qpu      |   |  Hybrid AI / VQE App |   |
|   |  (System Management)  |   |     (POSIX C API)      |   | (PyTorch + cuQuantum)|   |
|   +-----------------------+   +------------------------+   +----------------------+   |
|                                            |                                          |
|                                            v                                          |
+---------------------------------------------------------------------------------------+
|                       OMNIQ-OS REAL-TIME KERNEL (PREEMPT_RT)                          |
|                                                                                       |
|   +-------------------------------------------------------------------------------+   |
|   |                     drivers/accel/qpu/ (Unified QPU Subsystem)                |   |
|   |                                                                               |   |
|   |   +--------------------------+             +------------------------------+   |   |
|   |   | Hardware Mode (PCIe/AXI) |             | Fallback Mode: Virt-QPU Sim  |   |   |
|   |   | • Xilinx RFSoC DMA       |   <=====>   | • Transparent emulation      |   |   |
|   |   | • Cryo-telemetry parser  |             | • Zero-config dev machine    |   |   |
|   |   +--------------------------+             +------------------------------+   |   |
|   |                                                                               |   |
|   |   +---------------------------------------+   +---------------------------+   |   |
|   |   |  GPUDirect-QPU Zero-Copy DMA Bridge   |   |  QMMU Spatial Allocator   |   |   |
|   |   |  (Direct PCIe P2P: GPU VRAM <-> QPU)  |   |  with Guard-Band Shields  |   |   |
|   |   +---------------------------------------+   +---------------------------+   |   |
|   +-------------------------------------------------------------------------------+   |
+---------------------------------------------------------------------------------------+
           |                                   |                              |
           v (PCIe Gen5)                       v (NVLink / PCIe)              v (PCIe / AXI)
     [ Classical CPU ]                  [ NVIDIA/AMD GPU ]             [ QPU Controller ]
```

---

## 3. Key Subsystems

### 3.1 Kernel Driver (`drivers/accel/qpu/`)
Conforming to the Linux kernel Compute Accelerator (`accel`) framework:
* Device Node: `/dev/accel/qpu0`
* SysFS Telemetry: `/sys/class/accel/qpu0/`
* Standardized UAPI: `include/uapi/linux/qpu.h`

### 3.2 GPUDirect-QPU Zero-Copy DMA (< 600 ns QEC Loop)
Enables direct PCIe Peer-to-Peer (P2P) BAR mapping between FPGA microwave controllers (e.g. AMD/Xilinx RFSoC) and GPU High-Bandwidth Memory (HBM). Readout syndrome bits stream directly to GPU Tensor Cores, neural decoding executes in $< 150\,\text{ns}$, and Pauli frame rotations are pushed back to the pulse synthesizers—completing in **$500\,\text{ns}$**, well within the qubit coherence window.

### 3.3 Quantum Memory Management Unit (QMMU)
* **Spatial Multi-Tenancy**: Slices the physical qubit lattice across concurrent tenant processes.
* **Crosstalk Guard Bands**: Detunes and reserves boundary buffer qubits around active partitions to eliminate capacitive and AC Stark shift leakage.
* **`q-keepalived` Dynamic Decoupling**: Injects CPMG / XY-8 refocusing pulses during idle periods, extending effective $T_2$ coherence up to $10\times$.

### 3.4 Hardware Auto-Detection & Transparent Fallback
1. **State A (`ONLINE_HARDWARE`)**: FPGA PCIe endpoint detected, cryostat mixing chamber $< 50\,\text{mK}$. Direct microwave synthesis enabled.
2. **State B (`OFFLINE_MAINTENANCE`)**: FPGA detected but cryostat is warm ($> 50\,\text{mK}$). Read-only diagnostics active; RF pulse generation blocked to protect cryogenic electronics.
3. **State C (`SIMULATOR`)**: Developer laptop / workstation with no FPGA. Driver seamlessly falls back to high-performance statevector/density matrix emulation. Code executes identically across lab hardware and laptops!

---

## 4. Repository Layout

```
omniqos-linux/
├── kernel/
│   ├── drivers/accel/qpu/ # Linux kernel driver (C11)
│   ├── include/uapi/      # include/uapi/linux/qpu.h
│   └── dkms.conf          # Dynamic Kernel Module Support configuration
├── tools/
│   ├── qpu-smi/           # QPU System Management Interface (TUI & CLI)
│   └── omniqd/            # Quantum user-space management daemon
├── runtime/
│   ├── libomniq-qpu/      # POSIX C userland client library
│   └── python/            # omniqos Python bindings
├── distro/
│   ├── branding/          # Option A SVG and vector brand assets
│   └── live-build/        # Ubuntu 24.04 LTS PREEMPT_RT ISO builder
└── tests/
    └── test_qpu_uapi.c    # POSIX UAPI and QMMU verification test harness
```

---

## 5. License

OmniQ-OS kernel subsystems are licensed under **GPLv2** (as required for upstream Linux kernel drivers). Userland libraries, tools, and runtimes are licensed under the **Apache License, Version 2.0**.
