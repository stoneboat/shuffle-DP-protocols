#pragma once

#include <emp-tool/emp-tool.h>
#include <vector>
#include <cassert>
#include <algorithm>

// ── Distinct Elements via histogram (one-hot decode + bucket sums + noise) ──
// Avoids the bitonic sort entirely. For domain size D = 2^K:
//   - Each user one-hot decodes their K-bit value into D bits.
//   - For each bucket b ∈ [0, D), tree-sum the N 1-bit indicators → count[b].
//   - is_present[b] = OR over bits of count[b].
//   - distinct = tree-sum over D indicators, then add noise.
//
// Input layout (LSB first per value):
//   [0, n*k)                       : user values (user i at i*k..i*k+k-1)
//   [n*k, n*k + noise_bits)        : signed Laplace/Gaussian noise sample
//   [n*k + noise_bits]             : CONSTANT-1 wire (protocol must set to 1)
//
// Output: distinct + noise, max(K+1, noise_bits) + 1 bits wide.
//
// NOT is implemented as XOR with the constant-1 input wire — avoids
// NOT_GATE entirely (which has known interaction issues with half-gates AND).

inline emp::BristolFormat* buildDistinctElementsHistogramCircuit(int n_users, int k_bits, int noise_bits) {
    assert(n_users >= 2 && k_bits >= 1 && noise_bits >= 1);

    const int N = n_users, K = k_bits;
    const int D = 1 << K;
    const int total_inputs = N * K + noise_bits + 1;
    const int const_1 = N * K + noise_bits;

    int next_wire = total_inputs;
    std::vector<int> gates;

    // ── Adder helpers ───────────────────────────────────────────────────────
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
    auto rippleAdd = [&](const std::vector<int>& aw, const std::vector<int>& bw)-> std::vector<int> {
        int wa = (int)aw.size(), wb = (int)bw.size(), w = std::max(wa, wb);
        std::vector<int> res; int carry = -1;
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

    auto orGate = [&](int a, int b) -> int {
        int x = next_wire++; gates.insert(gates.end(), {a, b, x, XOR_GATE});
        int y = next_wire++; gates.insert(gates.end(), {a, b, y, AND_GATE});
        int out = next_wire++; gates.insert(gates.end(), {x, y, out, XOR_GATE});
        return out;
    };

    auto NOT = [&](int a) -> int {
        int out = next_wire++;
        gates.insert(gates.end(), {a, const_1, out, XOR_GATE});
        return out;
    };

    // ── Decode each user's value into D one-hot bits ─────────────────────────
    std::vector<std::vector<int>> e(N, std::vector<int>(D));
    for (int i = 0; i < N; i++) {
        std::vector<int> bit(K), nbit(K);
        for (int j = 0; j < K; j++) {
            bit[j]  = i * K + j;
            nbit[j] = NOT(bit[j]);
        }
        for (int b = 0; b < D; b++) {
            std::vector<int> terms(K);
            for (int j = 0; j < K; j++)
                terms[j] = ((b >> j) & 1) ? bit[j] : nbit[j];
            // Tree-AND across K terms
            while (terms.size() > 1) {
                std::vector<int> nxt;
                for (size_t k = 0; k + 1 < terms.size(); k += 2) {
                    int out = next_wire++;
                    gates.insert(gates.end(), {terms[k], terms[k+1], out, AND_GATE});
                    nxt.push_back(out);
                }
                if (terms.size() % 2 == 1) nxt.push_back(terms.back());
                terms = std::move(nxt);
            }
            e[i][b] = terms[0];
        }
    }

    // ── Per-bucket tree sum across users ────────────────────────────────────
    std::vector<std::vector<int>> count(D);
    for (int b = 0; b < D; b++) {
        std::vector<std::vector<int>> vals(N);
        for (int i = 0; i < N; i++) vals[i] = { e[i][b] };
        while ((int)vals.size() > 1) {
            std::vector<std::vector<int>> nxt;
            for (size_t i = 0; i + 1 < vals.size(); i += 2)
                nxt.push_back(rippleAdd(vals[i], vals[i+1]));
            if (vals.size() % 2 == 1) nxt.push_back(vals.back());
            vals = std::move(nxt);
        }
        count[b] = vals[0];
    }

    // ── is_present[b] = OR over bits of count[b] ────────────────────────────
    std::vector<int> ind(D);
    for (int b = 0; b < D; b++) {
        std::vector<int> bits = count[b];
        while (bits.size() > 1) {
            std::vector<int> nxt;
            for (size_t i = 0; i + 1 < bits.size(); i += 2)
                nxt.push_back(orGate(bits[i], bits[i+1]));
            if (bits.size() % 2 == 1) nxt.push_back(bits.back());
            bits = std::move(nxt);
        }
        ind[b] = bits[0];
    }

    // ── Tree-sum indicators → distinct count ────────────────────────────────
    std::vector<std::vector<int>> svals(D);
    for (int b = 0; b < D; b++) svals[b] = { ind[b] };
    while ((int)svals.size() > 1) {
        std::vector<std::vector<int>> nxt;
        for (size_t i = 0; i + 1 < svals.size(); i += 2)
            nxt.push_back(rippleAdd(svals[i], svals[i+1]));
        if (svals.size() % 2 == 1) nxt.push_back(svals.back());
        svals = std::move(nxt);
    }
    std::vector<int> distinct = svals[0];

    // ── Add noise ───────────────────────────────────────────────────────────
    std::vector<int> noise(noise_bits);
    int noise_base = N * K;
    for (int b = 0; b < noise_bits; b++) noise[b] = noise_base + b;
    std::vector<int> noisy = rippleAdd(distinct, noise);

    // ── Output (copy through XOR with zero) ─────────────────────────────────
    int zero_wire = next_wire++;
    gates.insert(gates.end(), {0, 0, zero_wire, XOR_GATE});
    int out_bits = (int)noisy.size();
    for (int b = 0; b < out_bits; b++) {
        int ow = next_wire++;
        gates.insert(gates.end(), {noisy[b], zero_wire, ow, XOR_GATE});
    }

    int n_gates = (int)gates.size() / 4;
    int n_wires = next_wire;
    return new emp::BristolFormat(n_gates, n_wires, total_inputs, 0, out_bits, gates.data());
}
