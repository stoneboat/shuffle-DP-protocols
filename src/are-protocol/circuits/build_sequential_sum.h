#pragma once

#include <emp-tool/emp-tool.h>
#include <vector>
#include <cassert>

// Build a circuit that computes the SUM of n_parties k-bit inputs.
// Uses a SEQUENTIAL chain: acc = x[0]; acc += x[1]; acc += x[2]; ...
//
// Boundary wires are ~constant per partition boundary: only the running
// sum bits (k + O(log n)) cross each boundary, regardless of N.
//
// Each full adder (1 bit): 5 gates (2 AND, 3 XOR)
// k-bit ripple-carry adder: 5k - 3 gates
// Total: n-1 adders with growing bit widths.
//
inline emp::BristolFormat* buildSequentialSumCircuit(int n_parties, int k_bits) {
    assert(n_parties >= 2 && k_bits >= 1);

    int next_wire = n_parties * k_bits;
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

    // Sequential left-fold: acc = x[0]; acc += x[1]; acc += x[2]; ...
    std::vector<int> running = operands[0];
    for (int p = 1; p < n_parties; p++)
        running = rippleAdd(running, operands[p]);

    // Copy result to output position
    std::vector<int>& result_wires = running;
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

    int total_inputs = n_parties * k_bits;
    int n_gates = (int)gates.size() / 4;
    int n_wires = next_wire;
    return new emp::BristolFormat(n_gates, n_wires, total_inputs, 0, out_bits, gates.data());
}
