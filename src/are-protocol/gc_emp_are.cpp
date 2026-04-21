// gc_emp_are.cpp — Interactive mode for distributed GC+ARE
//
// Usage:
//   ./gc_emp_are                          # 2 clients, AND-chain demo
//   ./gc_emp_are <N>                      # N clients, AND-chain demo
//   ./gc_emp_are <circuit.bristol> <N>    # N clients, custom Bristol circuit
//   ./gc_emp_are <N> <circuit.bristol>    # same (order doesn't matter)

#include "gc_protocol.h"

int main(int argc, char** argv) {
    int num_clients = 2;
    const char* circuit_file = nullptr;

    for (int i = 1; i < argc; i++) {
        char* end;
        long n = strtol(argv[i], &end, 10);
        if (*end == '\0' && n >= 2) num_clients = (int)n;
        else circuit_file = argv[i];
    }

    std::cout << "=== Distributed GC + ARE (N=" << num_clients << " clients) ===" << std::endl;

    emp::BristolFormat* circ = nullptr;
    if (circuit_file) {
        FILE* f = fopen(circuit_file, "r");
        if (!f) { std::cerr << "Cannot open: " << circuit_file << std::endl; return 1; }
        circ = new emp::BristolFormat(f); fclose(f);
        std::cout << "Circuit: " << circuit_file
                  << "  gates=" << circ->num_gate << "  wires=" << circ->num_wire
                  << "  inputs=" << (circ->n1 + circ->n2)
                  << "  outputs=" << circ->n3 << std::endl;
    } else {
        circ = buildANDChain(num_clients);
        std::cout << "Circuit: AND-chain(" << num_clients << " inputs)"
                  << "  gates=" << circ->num_gate << "  wires=" << circ->num_wire << std::endl;
    }

    auto result = runProtocol(circ, num_clients, /*balanced=*/true,
                               "default", circuit_file ? circuit_file : "and-chain",
                               /*verbose=*/true);

    delete circ;
    return result.correct ? 0 : 1;
}
