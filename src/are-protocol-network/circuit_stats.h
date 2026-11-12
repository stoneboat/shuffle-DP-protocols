#pragma once
#include <emp-tool/emp-tool.h>
#include <algorithm>
#include <cstdint>
#include <ostream>
#include <vector>

struct CircuitFanoutStats {
    std::vector<int> pins, consumers;
    std::vector<int8_t> constant;
    int max_pin_gate = -1, max_consumer_gate = -1;
    int max_variable_pins = 0, max_variable_consumers = 0;
    int max_input_pins = 0, max_input_consumers = 0;
};

inline CircuitFanoutStats circuitFanoutStats(const emp::BristolFormat& circuit) {
    CircuitFanoutStats stats;
    stats.pins.resize(circuit.num_wire, 0);
    stats.consumers.resize(circuit.num_wire, 0);
    stats.constant.resize(circuit.num_wire, -1);
    for (int g = 0; g < circuit.num_gate; g++) {
        int a = circuit.gates[4*g], b = circuit.gates[4*g+1], w = circuit.gates[4*g+2], type = circuit.gates[4*g+3];
        stats.pins[a]++; stats.consumers[a]++;
        if (type != NOT_GATE) { stats.pins[b]++; if (a != b) stats.consumers[b]++; }
        if (type == XOR_GATE && a == b) stats.constant[w] = 0;
        else if (type == NOT_GATE && stats.constant[a] >= 0) stats.constant[w] = !stats.constant[a];
        else if (type == AND_GATE && (stats.constant[a] == 0 || stats.constant[b] == 0)) stats.constant[w] = 0;
        else if (type != NOT_GATE && stats.constant[a] >= 0 && stats.constant[b] >= 0) stats.constant[w] = type == AND_GATE ? stats.constant[a] & stats.constant[b] : stats.constant[a] ^ stats.constant[b];
    }
    for (int w = 0; w < circuit.n1 + circuit.n2; w++) {
        stats.max_input_pins = std::max(stats.max_input_pins, stats.pins[w]);
        stats.max_input_consumers = std::max(stats.max_input_consumers, stats.consumers[w]);
    }
    for (int g = 0; g < circuit.num_gate; g++) {
        int w = circuit.gates[4*g+2];
        if (stats.max_pin_gate < 0 || stats.pins[w] > stats.pins[circuit.gates[4*stats.max_pin_gate+2]]) stats.max_pin_gate = g;
        if (stats.max_consumer_gate < 0 || stats.consumers[w] > stats.consumers[circuit.gates[4*stats.max_consumer_gate+2]]) stats.max_consumer_gate = g;
        if (stats.constant[w] < 0) {
            stats.max_variable_pins = std::max(stats.max_variable_pins, stats.pins[w]);
            stats.max_variable_consumers = std::max(stats.max_variable_consumers, stats.consumers[w]);
        }
    }
    return stats;
}

inline const char* circuitGateType(int type) { return type == AND_GATE ? "AND" : type == XOR_GATE ? "XOR" : "NOT"; }

static const char* CIRCUIT_STATS_HEADER = "gates,wires,inputs,outputs,max_pin_fanout,max_consumer_fanout,max_pin_gate,max_pin_wire,max_pin_type,max_pin_constant,max_consumer_gate,max_consumer_wire,max_nonconstant_pin_fanout,max_nonconstant_consumer_fanout,max_input_pin_fanout,max_input_consumer_fanout";

inline void writeCircuitStatsRow(std::ostream& out, const emp::BristolFormat& circuit, const CircuitFanoutStats& stats) {
    int pg = stats.max_pin_gate, cg = stats.max_consumer_gate;
    int pw = pg < 0 ? -1 : circuit.gates[4*pg+2], cw = cg < 0 ? -1 : circuit.gates[4*cg+2];
    out << circuit.num_gate << ',' << circuit.num_wire << ',' << circuit.n1 + circuit.n2 << ',' << circuit.n3 << ','
        << (pw < 0 ? 0 : stats.pins[pw]) << ',' << (cw < 0 ? 0 : stats.consumers[cw]) << ',' << pg << ',' << pw << ','
        << (pg < 0 ? "none" : circuitGateType(circuit.gates[4*pg+3])) << ',' << (pw < 0 ? -1 : (int)stats.constant[pw]) << ',' << cg << ',' << cw << ','
        << stats.max_variable_pins << ',' << stats.max_variable_consumers << ',' << stats.max_input_pins << ',' << stats.max_input_consumers << '\n';
}

inline void writeCircuitGates(std::ostream& out, const emp::BristolFormat& circuit, const CircuitFanoutStats& stats) {
    out << "gate,type,input0,input1,output,output_pin_fanout,output_consumer_fanout,constant\n";
    for (int g = 0; g < circuit.num_gate; g++) {
        int a = circuit.gates[4*g], b = circuit.gates[4*g+1], w = circuit.gates[4*g+2], type = circuit.gates[4*g+3];
        out << g << ',' << circuitGateType(type) << ',' << a << ',' << (type == NOT_GATE ? -1 : b) << ',' << w << ',' << stats.pins[w] << ',' << stats.consumers[w] << ',' << (int)stats.constant[w] << '\n';
    }
}
