#pragma once

#include <emp-tool/emp-tool.h>
#include <vector>
#include <cassert>
#include <algorithm>

// ── Distinct Elements via oblivious sort + adjacent-unique count + noise ───
// Output is (#{distinct values} - 1) + noise; the "+1" is added in the clear
// Input layout (LSB first per value):
//   [0, n*k)                 : user values — user i's value at wires i*k..i*k+k-1
//   [n*k, n*k + noise_bits)  : single noise sample (unsigned, LSB first)
//
// Output: noisy (count-1), max(count_bits, noise_bits)+1 bits wide.

inline emp::BristolFormat* buildDistinctElementsCircuit(int n_users, int k_bits, int noise_bits) {
    assert(n_users >= 2 && k_bits >= 1 && noise_bits >= 1);
    assert((n_users & (n_users - 1)) == 0 && "n_users must be a power of 2");

    int N = n_users, K = k_bits;
    int total_inputs = N * K + noise_bits;
    int next_wire = total_inputs;
    std::vector<int> gates;

    // ── Constant wires ──────────────────────────────────────────────────────
    int zero_wire = next_wire++;
    gates.insert(gates.end(), {0, 0, zero_wire, XOR_GATE});

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

    std::vector<std::vector<int>> elem(N, std::vector<int>(K));
    for (int i = 0; i < N; i++)
        for (int b = 0; b < K; b++)
            elem[i][b] = i * K + b;

    auto compareAndSwap = [&](int idx_a, int idx_b, bool ascending) {
        int ia = ascending ? idx_a : idx_b;
        int ib = ascending ? idx_b : idx_a;

        std::vector<int> p(K), g(K);
        for (int j = 0; j < K; j++) {
            p[j] = next_wire++;
            gates.insert(gates.end(), {elem[ia][j], elem[ib][j], p[j], XOR_GATE});
            g[j] = next_wire++;
            gates.insert(gates.end(), {elem[ia][j], p[j], g[j], AND_GATE});
        }
        int GT = g[0];
        for (int j = 1; j < K; j++) {
            int gt_and_p = next_wire++;
            gates.insert(gates.end(), {GT, p[j], gt_and_p, AND_GATE});
            int xor1 = next_wire++;
            gates.insert(gates.end(), {g[j], GT, xor1, XOR_GATE});
            int new_GT = next_wire++;
            gates.insert(gates.end(), {xor1, gt_and_p, new_GT, XOR_GATE});
            GT = new_GT;
        }

        std::vector<int> new_a(K), new_b(K);
        for (int j = 0; j < K; j++) {
            int xor_ab = next_wire++;
            gates.insert(gates.end(), {elem[idx_a][j], elem[idx_b][j], xor_ab, XOR_GATE});
            int mask = next_wire++;
            gates.insert(gates.end(), {xor_ab, GT, mask, AND_GATE});
            new_a[j] = next_wire++;
            gates.insert(gates.end(), {elem[idx_a][j], mask, new_a[j], XOR_GATE});
            new_b[j] = next_wire++;
            gates.insert(gates.end(), {elem[idx_b][j], mask, new_b[j], XOR_GATE});
        }
        elem[idx_a] = new_a;
        elem[idx_b] = new_b;
    };

    for (int step = 2; step <= N; step <<= 1)
        for (int substep = step >> 1; substep >= 1; substep >>= 1)
            for (int i = 0; i < N; i++) {
                int j = i ^ substep;
                if (j > i)
                    compareAndSwap(i, j, ((i & step) == 0));
            }

    // Adjacent-unique indicators
     auto orGate = [&](int a, int b) -> int {
        int x = next_wire++;
        gates.insert(gates.end(), {a, b, x, XOR_GATE});
        int y = next_wire++;
        gates.insert(gates.end(), {a, b, y, AND_GATE});
        int out = next_wire++;
        gates.insert(gates.end(), {x, y, out, XOR_GATE});
        return out;
    };

    std::vector<int> contribs;
    for (int i = 1; i < N; i++) {
        std::vector<int> diff_bits(K);
        for (int b = 0; b < K; b++) {
            diff_bits[b] = next_wire++;
            gates.insert(gates.end(), {elem[i][b], elem[i - 1][b], diff_bits[b], XOR_GATE});
        }
        while (diff_bits.size() > 1) {
            std::vector<int> next_layer;
            for (size_t j = 0; j + 1 < diff_bits.size(); j += 2)
                next_layer.push_back(orGate(diff_bits[j], diff_bits[j + 1]));
            if (diff_bits.size() % 2 == 1)
                next_layer.push_back(diff_bits.back());
            diff_bits = next_layer;
        }
        contribs.push_back(diff_bits[0]);
    }

    // Popcount of contribs (tree reduction of 1-bit numbers)
    std::vector<std::vector<int>> vals(contribs.size());
    for (size_t i = 0; i < contribs.size(); i++) vals[i] = { contribs[i] };
    while ((int)vals.size() > 1) {
        std::vector<std::vector<int>> next_layer;
        for (size_t i = 0; i + 1 < vals.size(); i += 2)
            next_layer.push_back(rippleAdd(vals[i], vals[i + 1]));
        if (vals.size() % 2 == 1)
            next_layer.push_back(vals.back());
        vals = next_layer;
    }
    std::vector<int> count = vals[0];

    // Add Laplace noise
    std::vector<int> noise(noise_bits);
    int noise_base = N * K;
    for (int b = 0; b < noise_bits; b++) noise[b] = noise_base + b;

    std::vector<int> noisy = rippleAdd(count, noise);

    // Output
    int out_bits = (int)noisy.size();
    for (int b = 0; b < out_bits; b++) {
        int ow = next_wire++;
        gates.insert(gates.end(), {noisy[b], zero_wire, ow, XOR_GATE});
    }

    int n_gates = (int)gates.size() / 4;
    int n_wires = next_wire;
    return new emp::BristolFormat(n_gates, n_wires, total_inputs, 0, out_bits, gates.data());
}
