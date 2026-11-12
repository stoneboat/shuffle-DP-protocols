Run from `src/are-protocol-network` after building `bin/evaluator`:

```bash
mkdir -p benchmark/circuit_stats
./bin/evaluator --circuit-stats --circuit lcb --clients 8 --K 8 --D 2 --NB 8 --csv benchmark/circuit_stats/lcb_N8.csv --dump-gates benchmark/circuit_stats/lcb_N8_gates.csv
```

This builds the circuit and exits before protocol setup, sockets, clients or lookup-table loading. Use the same source version and circuit parameters as the benchmark. `--T` defaults to the number of clients; Gaussian sum may widen the requested `--NB`.

For a sweep, omit the large gate dumps:

```bash
for circuit in gausssum select distinct lcb; do
    for n in 8 16 32 64 128 256 512 1024; do
        ./bin/evaluator --circuit-stats --circuit "$circuit" --clients "$n" --K 8 --D 2 --NB 8 --csv "benchmark/circuit_stats/${circuit}_N${n}.csv"
    done
done
```

`max_pin_fanout` counts downstream input-pin connections from one gate output. A repeated input such as `XOR(w,w)` counts twice. `max_consumer_fanout` counts distinct consuming gates, so that example counts once. Neither includes external readout connections. Primary-input maxima are reported separately.

Gate and wire IDs start at zero. In gate dumps, `constant` is `0` or `1` for a constant recognized from the gate structure, and `-1` when unknown. The `max_nonconstant_*` fields exclude only those recognized constants. The summary includes the gate and wire attaining each maximum; dumps include every gate and its fan-out.

LCB reuses one zero-generating XOR gate for output padding and identity copies. The checker reports that shared constant and also reports maxima after excluding recognized constants. It does not alter the circuit or partition it.
