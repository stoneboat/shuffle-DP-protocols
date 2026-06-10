#pragma once

#include <emp-tool/emp-tool.h>
#include <vector>
#include <cassert>

inline emp::BristolFormat* buildANDChain(int n_inputs) {
    assert(n_inputs >= 2);
    int n_gates = n_inputs - 1;
    int n_wires = 2 * n_inputs - 1;
    std::vector<int> gates;
    gates.reserve(n_gates * 4);
    gates.push_back(0); gates.push_back(1);
    gates.push_back(n_inputs); gates.push_back(AND_GATE);
    for (int i = 1; i < n_inputs - 1; i++) {
        gates.push_back(n_inputs + i - 1);
        gates.push_back(i + 1);
        gates.push_back(n_inputs + i);
        gates.push_back(AND_GATE);
    }
    return new emp::BristolFormat(n_gates, n_wires, n_inputs, 0, 1, gates.data());
}

inline emp::BristolFormat* buildLargeANDChain(int n_gates) {
    return buildANDChain(n_gates + 1);
}
