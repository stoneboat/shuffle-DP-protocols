#pragma once

#include <emp-tool/emp-tool.h>
#include <vector>
#include <cassert>
#include <algorithm>

// ── Tree Mechanism (Fenwick/BIT tree for noisy prefix sums) ────────────────
//
// Oblivious Fenwick tree mechanism for continual release of noisy prefix sums.
// Matches the protocol in src/boolean_circuit/tree_mechanism/tree_mech.c
//
// Input layout (all bits, LSB first per value):
//   x_flat:     T * D values, each k bits.  x[t][d] at wire (t*D + d)*k + bit
//   noise_flat: T * D values, each k bits.  noise[node-1][d] at wire T*D*k + ((node-1)*D + d)*k + bit
//               (Fenwick nodes 1..T, stored 0-indexed)
//
// Output: T * D prefix-sum values, each sum_bits wide (sum_bits >= k).
//   ps[t][d] = sum_{tau=0..t} x[tau][d] + sum_{i in FenwickCover(t+1)} noise[i][d]
//
// All loop bounds and indices are deterministic (no secret-dependent branching),
// so the circuit is a fixed DAG of ripple-carry adders.
//

inline emp::BristolFormat* buildTreeMechanismCircuit(int num_steps, int vec_dim,
                                                      int k_bits) {
    int T = num_steps, D = vec_dim, K = k_bits;
    assert(T >= 1 && D >= 1 && K >= 1);

    int total_x_bits = T * D * K;
    int total_noise_bits = T * D * K;
    int total_inputs = total_x_bits + total_noise_bits;
    int next_wire = total_inputs;
    std::vector<int> gates;

    // ── Adder helpers ──────────────────────────────────────────────────────

    auto halfAdder = [&](int a, int b) -> std::pair<int, int> {
        int s = next_wire++, c = next_wire++;
        gates.insert(gates.end(), {a, b, s, XOR_GATE});
        gates.insert(gates.end(), {a, b, c, AND_GATE});
        return {s, c};
    };

    auto fullAdder = [&](int a, int b, int cin) -> std::pair<int, int> {
        int t1 = next_wire++, sw = next_wire++;
        int t2 = next_wire++, t3 = next_wire++, co = next_wire++;
        gates.insert(gates.end(), {a, b, t1, XOR_GATE});
        gates.insert(gates.end(), {t1, cin, sw, XOR_GATE});
        gates.insert(gates.end(), {a, b, t2, AND_GATE});
        gates.insert(gates.end(), {t1, cin, t3, AND_GATE});
        gates.insert(gates.end(), {t2, t3, co, XOR_GATE});
        return {sw, co};
    };

    auto rippleAdd = [&](const std::vector<int>& aw, const std::vector<int>& bw)
            -> std::vector<int> {
        int wa = (int)aw.size(), wb = (int)bw.size(), w = std::max(wa, wb);
        std::vector<int> res;
        int carry = -1;
        for (int i = 0; i < w; i++) {
            int ai = (i < wa) ? aw[i] : -1;
            int bi = (i < wb) ? bw[i] : -1;
            if (ai == -1 && bi == -1) {
                if (carry != -1) { res.push_back(carry); carry = -1; }
            } else if (ai == -1) {
                if (carry == -1) res.push_back(bi);
                else { auto [s, c] = halfAdder(bi, carry); res.push_back(s); carry = c; }
            } else if (bi == -1) {
                if (carry == -1) res.push_back(ai);
                else { auto [s, c] = halfAdder(ai, carry); res.push_back(s); carry = c; }
            } else {
                if (carry == -1) { auto [s, c] = halfAdder(ai, bi); res.push_back(s); carry = c; }
                else { auto [s, c] = fullAdder(ai, bi, carry); res.push_back(s); carry = c; }
            }
        }
        if (carry != -1) res.push_back(carry);
        return res;
    };

    // ── Input wire accessors ───────────────────────────────────────────────

    // x[t][d]: k-bit value, LSB first
    auto getXWires = [&](int t, int d) -> std::vector<int> {
        std::vector<int> w(K);
        int base = (t * D + d) * K;
        for (int b = 0; b < K; b++) w[b] = base + b;
        return w;
    };

    // noise[node1-1][d]: k-bit value for Fenwick node node1 (1-indexed)
    auto getNoiseWires = [&](int node1, int d) -> std::vector<int> {
        std::vector<int> w(K);
        int base = total_x_bits + ((node1 - 1) * D + d) * K;
        for (int b = 0; b < K; b++) w[b] = base + b;
        return w;
    };

    // ── Phase 1: Build Fenwick sums ────────────────────────────────────────
    // fenwick[node1][d] = wire vector representing sum accumulated at node node1
    // node1 in [1..T]

    std::vector<std::vector<std::vector<int>>> fenwick(T + 1,
        std::vector<std::vector<int>>(D));

    for (int t = 0; t < T; t++) {
        int i = t + 1;
        while (i <= T) {
            for (int d = 0; d < D; d++) {
                auto xw = getXWires(t, d);
                if (fenwick[i][d].empty())
                    fenwick[i][d] = xw;  // first value — wire aliasing, no gates
                else
                    fenwick[i][d] = rippleAdd(fenwick[i][d], xw);
            }
            i += (i & (-i));  // i += lsb(i)
        }
    }

    // ── Phase 2: Query noisy prefix sums ───────────────────────────────────
    // ps[t][d] = sum_{Fenwick cover of t+1} (fenwick[node] + noise[node])

    std::vector<std::vector<std::vector<int>>> ps_wires(T,
        std::vector<std::vector<int>>(D));

    for (int t = 0; t < T; t++) {
        std::vector<std::vector<int>> acc(D);
        int i = t + 1;
        while (i > 0) {
            for (int d = 0; d < D; d++) {
                // term = fenwick[i][d] + noise[i][d]
                auto nw = getNoiseWires(i, d);
                std::vector<int> term;
                if (fenwick[i][d].empty())
                    term = nw;
                else
                    term = rippleAdd(fenwick[i][d], nw);

                if (acc[d].empty())
                    acc[d] = term;
                else
                    acc[d] = rippleAdd(acc[d], term);
            }
            i -= (i & (-i));  // i -= lsb(i)
        }
        ps_wires[t] = acc;
    }

    // ── Copy outputs to final contiguous wire range ────────────────────────

    // Find max output bit width across all T*D outputs
    int max_out_bits = 0;
    for (int t = 0; t < T; t++)
        for (int d = 0; d < D; d++)
            max_out_bits = std::max(max_out_bits, (int)ps_wires[t][d].size());

    int out_bits = T * D * max_out_bits;

    // Zero wire for padding
    int zero_wire = next_wire++;
    gates.insert(gates.end(), {0, 0, zero_wire, XOR_GATE});

    // Emit output wires in order: ps[0][0], ps[0][1], ..., ps[T-1][D-1]
    for (int t = 0; t < T; t++) {
        for (int d = 0; d < D; d++) {
            auto& ow = ps_wires[t][d];
            for (int b = 0; b < max_out_bits; b++) {
                int src = (b < (int)ow.size()) ? ow[b] : zero_wire;
                int dst = next_wire++;
                gates.insert(gates.end(), {src, zero_wire, dst, XOR_GATE});
            }
        }
    }

    int n_gates = (int)gates.size() / 4;
    int n_wires = next_wire;
    return new emp::BristolFormat(n_gates, n_wires, total_inputs, 0, out_bits,
                                  gates.data());
}