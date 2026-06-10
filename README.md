# Balanced ARE Protocol

Networked implementation of the Balanced ARE garbled-circuit protocol: one evaluator orchestrates `N` client processes (one per data holder) over TCP. The evaluator partitions the Bristol-format circuit, hands each client its slice, and runs the garble/OT/PXT phases end-to-end.

A `--benchmark` mode sweeps `N` × `circuit` × `partition_mode` and writes per-run timing + communication metrics to CSV.

### Layout

```
src/are-protocol-network/
  evaluator.cpp        # orchestrator: builds circuit, partitions, runs protocol
  client.cpp           # per-party process; connects to evaluator on base_port + id
  gen_tables.cpp       # one-time generator for lookup_12 / lookup_20 ARE tables
  net_serialize.h      # wire formats, partition modes, GlobalPartition
  Makefile
  launch.sh            # local: spawns evaluator + N clients on 127.0.0.1
  run_benchmarks.sh    # local/cluster: one (circuit, partition) sweep over N
  exp.slurm            # SLURM array job: 4 circuits × 4 partition modes
  benchmark/csv/       # CSVs produced by --benchmark runs
```

### Building Blocks
```
src/are-protocol/
    ot/permxor_are.h    #Permute-XOR ARE - Wire splicing technique
    ot/string_ot_are.h  #String OT ARE - Input transfer via ARE
    ot/rabin_ot_are.h   #Building block for String OT ARE
```

## Instructions to run the code
### 1) Build the dependencies

The Makefile expects `mcl` and `emp-tool` to be already built and installed under `src/utils/`:

```
src/utils/mcl/install/{include,lib}
src/utils/emp-tool/install/{include,lib}
```

Build them once (from the project root) using each subproject's standard CMake flow before continuing.

### 2) Build the binaries

From `src/are-protocol-network/`:

```bash
make            # builds bin/evaluator, bin/client, bin/gen_tables
make clean      # rm -rf bin/
```

The Makefile pins `-std=c++17 -O2 -fopenmp -march=native -maes -mpclmul -mssse3` and bakes an rpath to `utils/emp-tool/install/lib`.

### 3) Generate lookup tables (once)

Both binaries memory-map two precomputed ARE tables at runtime. Generate them once and leave them in `bin/`:

```bash
./bin/gen_tables .      # writes bin/lookup_{12,20}.bin and bin/lookup_{12,20}.mmap.bin
```

### 4) Run locally

The simplest path — one evaluator + `N` clients on `127.0.0.1`:

```bash
bash launch.sh N [PORT] [CIRCUIT] [extra evaluator args...]

# examples
bash launch.sh 4                                # 4 clients, default treemech
```

`launch.sh` starts the evaluator in the background, spawns `N` clients, then waits.

Equivalently, you can let the evaluator fork the clients itself:

```bash
./bin/evaluator --clients 4 --port 12345 --circuit gausssum --spawn-clients
```

### 5) Run a benchmark sweep

`run_benchmarks.sh` runs one `(circuit, partition_mode)` pair over a range of `N`, spawning clients itself. Each invocation writes its own CSV so parallel jobs don't clobber:

```bash
bash run_benchmarks.sh CIRCUIT PARTITION_MODE [N_MIN] [N_MAX] [BASE_PORT]

# examples
bash run_benchmarks.sh lcb      topo_bal    8 512 20500
bash run_benchmarks.sh distinct min_max_in  8 512 21000
```
### 6) Run on Slurm

`exp.slurm` is an array job that fans out 4 circuits × 4 partition modes = 16 tasks:

```bash
sbatch exp.slurm
```

### Evaluator flags

Key flags accepted by `bin/evaluator`:

| Flag                       | Default       | Meaning |
| -------------------------- | ------------- | ------- |
| `--clients, -n`            | `2`           | Number of clients `N` |
| `--port, -p`               | `12345`       | Base TCP port (client `i` uses `port + i`) |
| `--circuit, -c`            | `treemech`    | One of: `treemech`, `seqsum`, `gausssum`, `treesum`, `bitonic`, `select`, `lcb`, `distinct`, `distincthist` |
| `--partition, --part`      | `topo_bal`    | One of: `topo_bal`, `unbal`, `nonxor_bal`, `min_cut`, `min_max_in` |
| `--unbalanced`             | —             | Shortcut for `--partition unbal` |
| `--gamma`                  | `0.2`         | Slack for non-XOR balance constraint (paper §7) |
| `--K`                      | `8`           | Bit-width per data value |
| `--D`                      | `2`           | Dimensions (e.g. `lcb`, `select`) |
| `--T`                      | `N`           | Time horizon (`treemech`, `lcb`) |
| `--NB`                     | `8`           | Noise bit-width |
| `--benchmark`              | off           | Sweep `N` × `circuit` × `partition` (implies `--spawn-clients`) |
| `--spawn-clients`          | off           | Fork+exec the `N` client processes from the evaluator |
| `--n-min`, `--n-max`       | `2`, `64`     | Benchmark sweep range; `N` doubles each step |
| `--csv, -o`                | —             | Write per-run metrics to CSV |
| `--exp, -e`                | auto          | Experiment name tag in the CSV |
| `-q`                       | —             | Quiet mode |