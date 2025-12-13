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

### 3) Generate the boolean circuit representation

Generate the boolean circuit representation from the bounded C implementation using the provided script:

```bash
./src/boolean_circuit/oblivious_sort/gen_circuit_file.sh -n 8 --unwind 16 -o build/boolean_circuits/bitonic_sort_u32_N8
```

This will generate the circuit files in the specified output directory.

**Testing the generated circuit:**

You can test the generated circuit by first creating a reference file for sorting (e.g., `reference.c`), then run:

```bash
cbmc-gc-2/bin/circuit-utils --create-tester tester.cpp --reference reference.c
```

Then compile and run the tester to verify the circuit correctness.

---

