#pragma once

#include <emp-tool/emp-tool.h>
#include <vector>
#include <cassert>
#include <cstdint>

// Build a circuit that computes the SUM of n_parties k-bit inputs.
// Uses a BALANCED BINARY TREE of ripple-carry adders:
//   level 0: pair up inputs → N/2 adders (k-bit each)
//   level 1: pair up results → N/4 adders ((k+1)-bit each)
//   ...until one result remains.
//
// Same gate count as sequential sum but different wire topology:
// boundary wires scale with tree width at each level, not just the
// running accumulator width.
//
// Total: N-1 adders with growing bit widths.
// Output: k + ceil(log2(N)) bits.
//
inline emp::BristolFormat* buildTreeSumCircuit(int n_parties, int k_bits, int noise_bits = 0) {
    assert(n_parties >= 2 && k_bits >= 1 && noise_bits >= 0);

    int total_inputs = n_parties * k_bits + noise_bits + (noise_bits > 0);
    int next_wire = total_inputs;
    std::vector<int> gates;

    auto halfAdder = [&](int a, int b) -> std::pair<int, int> {
        int sum_w = next_wire++;
        int cout_w = next_wire++;
        gates.insert(gates.end(), {a, b, sum_w, XOR_GATE});
        gates.insert(gates.end(), {a, b, cout_w, AND_GATE});
        return {sum_w, cout_w};
    };

    auto fullAdder = [&](int a, int b, int cin) -> std::pair<int, int> {
        int t1 = next_wire++, sum_w = next_wire++;
        int t2 = next_wire++, t3 = next_wire++, cout_w = next_wire++;
        gates.insert(gates.end(), {a, b, t1, XOR_GATE});
        gates.insert(gates.end(), {t1, cin, sum_w, XOR_GATE});
        gates.insert(gates.end(), {a, b, t2, AND_GATE});
        gates.insert(gates.end(), {t1, cin, t3, AND_GATE});
        gates.insert(gates.end(), {t2, t3, cout_w, XOR_GATE});
        return {sum_w, cout_w};
    };

    auto rippleAdd = [&](const std::vector<int>& a_wires, const std::vector<int>& b_wires)
            -> std::vector<int> {
        int wa = (int)a_wires.size(), wb = (int)b_wires.size();
        int width = std::max(wa, wb);
        std::vector<int> result;
        int carry = -1;
        for (int i = 0; i < width; i++) {
            int ai = (i < wa) ? a_wires[i] : -1;
            int bi = (i < wb) ? b_wires[i] : -1;
            if (ai == -1 && bi == -1) {
                if (carry != -1) { result.push_back(carry); carry = -1; }
            } else if (ai == -1) {
                if (carry == -1) { result.push_back(bi); }
                else { auto [s, c] = halfAdder(bi, carry); result.push_back(s); carry = c; }
            } else if (bi == -1) {
                if (carry == -1) { result.push_back(ai); }
                else { auto [s, c] = halfAdder(ai, carry); result.push_back(s); carry = c; }
            } else {
                if (carry == -1) { auto [s, c] = halfAdder(ai, bi); result.push_back(s); carry = c; }
                else { auto [s, c] = fullAdder(ai, bi, carry); result.push_back(s); carry = c; }
            }
        }
        if (carry != -1) result.push_back(carry);
        return result;
    };

    // Initial operands (each party's k-bit input, LSB first)
    std::vector<std::vector<int>> operands(n_parties);
    for (int p = 0; p < n_parties; p++) {
        operands[p].resize(k_bits);
        for (int j = 0; j < k_bits; j++)
            operands[p][j] = p * k_bits + j;
    }

    // Binary tree reduction: pair up adjacent operands at each level
    while ((int)operands.size() > 1) {
        std::vector<std::vector<int>> next_level;
        for (int i = 0; i + 1 < (int)operands.size(); i += 2)
            next_level.push_back(rippleAdd(operands[i], operands[i + 1]));
        if (operands.size() % 2 == 1)
            next_level.push_back(operands.back());
        operands = std::move(next_level);
    }

    if (noise_bits > 0) {
        assert(k_bits <= 30 && noise_bits <= 62);
        auto gate = [&](int a, int b, int type) { int out = next_wire++; gates.insert(gates.end(), {a, b, out, type}); return out; };
        auto mux = [&](int select, int a, int b) { return gate(a, gate(select, gate(a, b, XOR_GATE), AND_GATE), XOR_GATE); };
        uint64_t max_sum = (uint64_t)n_parties * ((1ULL << k_bits) - 1);
        int zero_wire = gate(0, 0, XOR_GATE), width = std::max((int)operands[0].size() + 1, noise_bits) + 1;
        operands[0].resize(width, zero_wire);
        std::vector<int> noise(width, n_parties * k_bits + noise_bits - 1);
        for (int b = 0; b < noise_bits; b++) noise[b] = n_parties * k_bits + b;
        operands[0] = rippleAdd(operands[0], noise);
        operands[0].resize(width);
        int one_wire = n_parties * k_bits + noise_bits, greater = zero_wire;
        for (int b = 0; b < width; b++) {
            int value = operands[0][b];
            greater = ((max_sum >> b) & 1) ? gate(value, greater, AND_GATE) : gate(gate(value, greater, XOR_GATE), gate(value, greater, AND_GATE), XOR_GATE);
        }
        int nonnegative = gate(operands[0].back(), one_wire, XOR_GATE);
        std::vector<int> clamped;
        for (int b = 0; (max_sum >> b) != 0; b++) {
            int limit = ((max_sum >> b) & 1) ? one_wire : zero_wire;
            clamped.push_back(gate(mux(greater, operands[0][b], limit), nonnegative, AND_GATE));
        }
        operands[0] = std::move(clamped);
    }
    // Copy result to output position
    std::vector<int>& result_wires = operands[0];
    int out_bits = (int)result_wires.size();
    int total_wires_before_output = next_wire;

    bool need_copy = false;
    for (int i = 0; i < out_bits; i++) {
        if (result_wires[i] != total_wires_before_output + i) { need_copy = true; break; }
    }
    if (need_copy) {
        int zero_wire = next_wire++;
        gates.insert(gates.end(), {0, 0, zero_wire, XOR_GATE});
        for (int i = 0; i < out_bits; i++) {
            int ow = next_wire++;
            gates.insert(gates.end(), {result_wires[i], zero_wire, ow, XOR_GATE});
        }
    }

    int n_gates = (int)gates.size() / 4;
    int n_wires = next_wire;
    return new emp::BristolFormat(n_gates, n_wires, total_inputs, 0, out_bits, gates.data());
}
