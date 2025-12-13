# About

This branch focuses on compiling bounded C programs into Boolean circuits. These circuits can later be used to generate garbled circuits and, ultimately, to build a balanced ARE encoding.


## Installation

To compile bounded C programs into Boolean circuits, you first need a bounded C implementation. As a reference example, see the oblivious sorting code at:

- `src/boolean_circuit/oblivious_sort/bitonic_sort_u32.c`

This branch uses the **CBMC** + **CBMC-GC-2** toolchain to translate bounded C into a **Bristol** circuit file (a text format that explicitly lists gates and wires in the Boolean circuit).

### 1) Install the toolchain (Bell cluster)

Run:

```bash
./install_cluster_bell_c_bounded.sh
```
This script downloads/builds CBMC and CBMC-GC-2 (cached in scratch) and installs the runtime binaries under /tmp (ephemeral, node-local). You should re-run it when you start a new job/session on a different node.

### 2) Activate the environment

Before generating circuits, source:
```bash
source scripts/local/env_bell_circuit
```
This loads the required modules and updates PATH so cbmc and the circuit compiler are available in your current shell.

---

