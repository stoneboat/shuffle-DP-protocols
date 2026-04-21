#pragma once

#include <emp-tool/emp-tool.h>
#include <vector>
#include <cassert>
#include <algorithm>

// ── Linear Contextual Bandits via Tree Mechanism ────────────────────────────
//
// Implements the shuffle DP protocol for linear contextual bandits as described
// in Section 7.2 of the paper.  The learner maintains two running statistics
// that need private release at every round t in [T]:
//
//   u_t = sum_{tau<=t} phi(c_tau, a_tau) * y_tau          (d-vector)
//   V_t = lambda*I + sum_{tau<=t} phi * phi^T              (d x d matrix)
//
// Both are prefix sums, so we instantiate a Fenwick tree mechanism over
// D_total = d + d*d dimensions (d for u, d^2 for the upper-triangle or full
// flattened V).  Laplacian noise is added per Fenwick node for (eps,0)-DP.
//
// Input layout (all values k bits, LSB first):
//   ── per-round sufficient statistics (T rounds) ──
//     u_flat:  T * d  values   — u contribution at each round
//     V_flat:  T * d*d values  — flattened V contribution at each round
//   ── noise (one per Fenwick node, 1..T) ──
//     noise_u: T * d  values   — Laplacian noise for u
//     noise_V: T * d*d values  — Laplacian noise for V
//
//   Total input bits = 2 * T * (d + d*d) * k
//
// Output: T * (d + d*d) noisy prefix-sum values, each max_out_bits wide.
//   For round t:
//     out_u[t][j]    = sum_{tau<=t} u[tau][j]    + tree_noise_u(t, j)
//     out_V[t][i][j] = sum_{tau<=t} V[tau][i*d+j] + tree_noise_V(t, i*d+j)
//
// The circuit is data-oblivious: all loop bounds depend only on T, d, k.
// It is also leveled with width O(d^2) — boundary wires scale with d^2,
// giving per-client bandwidth O(T * d^2) in the balanced ARE protocol.
//

inline emp::BristolFormat* buildLinearContextualCircuit(int num_steps, int feat_dim,
                                                        int k_bits) {
    int T = num_steps, d = feat_dim, K = k_bits;
    assert(T >= 1 && d >= 1 && K >= 1);

    // D_total = d (for u) + d*d (for V)
    int D = d + d * d;

    int total_data_bits  = T * D * K;   // u + V contributions
    int total_noise_bits = T * D * K;   // noise for each Fenwick node
    int total_inputs = total_data_bits + total_noise_bits;
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

    // data[t][dim]: k-bit value at round t, dimension dim in [0, D)
    // dim 0..d-1 = u components, dim d..d+d*d-1 = V components
    auto getDataWires = [&](int t, int dim) -> std::vector<int> {
        std::vector<int> w(K);
        int base = (t * D + dim) * K;
        for (int b = 0; b < K; b++) w[b] = base + b;
        return w;
    };

    // noise[node1-1][dim]: k-bit noise for Fenwick node node1 (1-indexed)
    auto getNoiseWires = [&](int node1, int dim) -> std::vector<int> {
        std::vector<int> w(K);
        int base = total_data_bits + ((node1 - 1) * D + dim) * K;
        for (int b = 0; b < K; b++) w[b] = base + b;
        return w;
    };

    // ── Phase 1: Build Fenwick sums ────────────────────────────────────────
    // fenwick[node1][dim] = wire vector for accumulated data at Fenwick node

    std::vector<std::vector<std::vector<int>>> fenwick(T + 1,
        std::vector<std::vector<int>>(D));

    for (int t = 0; t < T; t++) {
        int i = t + 1;
        while (i <= T) {
            for (int dim = 0; dim < D; dim++) {
                auto xw = getDataWires(t, dim);
                if (fenwick[i][dim].empty())
                    fenwick[i][dim] = xw;
                else
                    fenwick[i][dim] = rippleAdd(fenwick[i][dim], xw);
            }
            i += (i & (-i));
        }
    }

    // ── Phase 2: Query noisy prefix sums ───────────────────────────────────
    // ps[t][dim] = sum of (fenwick[node] + noise[node]) over Fenwick cover

    std::vector<std::vector<std::vector<int>>> ps_wires(T,
        std::vector<std::vector<int>>(D));

    for (int t = 0; t < T; t++) {
        std::vector<std::vector<int>> acc(D);
        int i = t + 1;
        while (i > 0) {
            for (int dim = 0; dim < D; dim++) {
                auto nw = getNoiseWires(i, dim);
                std::vector<int> term;
                if (fenwick[i][dim].empty())
                    term = nw;
                else
                    term = rippleAdd(fenwick[i][dim], nw);

                if (acc[dim].empty())
                    acc[dim] = term;
                else
                    acc[dim] = rippleAdd(acc[dim], term);
            }
            i -= (i & (-i));
        }
        ps_wires[t] = acc;
    }

    // ── Copy outputs to final contiguous wire range ────────────────────────

    int max_out_bits = 0;
    for (int t = 0; t < T; t++)
        for (int dim = 0; dim < D; dim++)
            max_out_bits = std::max(max_out_bits, (int)ps_wires[t][dim].size());

    int out_bits = T * D * max_out_bits;

    // Zero wire for padding
    int zero_wire = next_wire++;
    gates.insert(gates.end(), {0, 0, zero_wire, XOR_GATE});

    // Output layout: ps[0][0..D-1], ps[1][0..D-1], ..., ps[T-1][0..D-1]
    // Within each round: first d entries are u, next d*d are V (row-major)
    for (int t = 0; t < T; t++) {
        for (int dim = 0; dim < D; dim++) {
            auto& ow = ps_wires[t][dim];
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