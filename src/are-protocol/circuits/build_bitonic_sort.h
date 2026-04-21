#pragma once

#include <emp-tool/emp-tool.h>
#include <vector>
#include <cassert>

// ── Bitonic sorting network ──────────────────────────────────────────────────
//
// Sorts n_elements k-bit unsigned integers using a bitonic sorting network.
// Requires n_elements to be a power of 2.
// Uses only AND and XOR gates (no NOT gates needed).
//
// Compare-and-swap for two k-bit numbers:
//   Comparator (A > B): process LSB to MSB using the identity
//     GT_0 = a_0 AND (a_0 XOR b_0)
//     GT_i = g_i XOR GT_{i-1} XOR (GT_{i-1} AND p_i)
//     where p_i = a_i XOR b_i (free), g_i = a_i AND p_i (1 AND)
//     Per bit: 2 AND gates + free XOR.  Total: 2k - 1 AND gates.
//   Conditional swap: k AND gates (mask each bit pair with swap signal).
//   Total per compare-and-swap: 3k - 1 AND gates + O(k) free XOR gates.
//
// Bitonic sort of N elements: N/2 * log2(N) * (log2(N)+1) / 2 compare-and-swaps.
// Total AND gates: O(N * k * log^2(N)).
//
// Boundary wires scale with BOTH k (bit width) AND N:
//   - Each compare-and-swap has ~5k internal wires
//   - The network has O(N log^2 N) compare-and-swaps
//   - Many independent comparators per stage → balanced partitioning can
//     split stages with fewer cross-partition wires than sequential circuits
//
inline emp::BristolFormat* buildSortingCircuit(int n_elements, int k_bits) {
    assert(n_elements >= 2 && k_bits >= 1);
    assert((n_elements & (n_elements - 1)) == 0 && "n_elements must be a power of 2");

    int N = n_elements;
    int next_wire = N * k_bits;
    std::vector<int> gates;

    // Element wire arrays: elem[i][j] = wire for element i, bit j (LSB = index 0)
    std::vector<std::vector<int>> elem(N, std::vector<int>(k_bits));
    for (int i = 0; i < N; i++)
        for (int j = 0; j < k_bits; j++)
            elem[i][j] = i * k_bits + j;

    // Compare-and-swap: compares elements at idx_a and idx_b,
    // swaps to put min at idx_a / max at idx_b (ascending) or vice versa.
    auto compareAndSwap = [&](int idx_a, int idx_b, bool ascending) {
        // For ascending: compute GT = (elem[idx_a] > elem[idx_b]), swap if GT
        // For descending: compute GT = (elem[idx_b] > elem[idx_a]), swap if GT
        int ia = ascending ? idx_a : idx_b;
        int ib = ascending ? idx_b : idx_a;

        // Comparator: LSB-to-MSB propagation (no NOT gates needed)
        //   p_j = elem[ia][j] XOR elem[ib][j]          (free)
        //   g_j = elem[ia][j] AND p_j                   (1 AND)
        //   GT_0 = g_0
        //   GT_j = g_j XOR GT_{j-1} XOR (GT_{j-1} AND p_j)  (2 AND + free XOR)
        std::vector<int> p(k_bits), g(k_bits);
        for (int j = 0; j < k_bits; j++) {
            p[j] = next_wire++;
            gates.insert(gates.end(), {elem[ia][j], elem[ib][j], p[j], XOR_GATE});
            g[j] = next_wire++;
            gates.insert(gates.end(), {elem[ia][j], p[j], g[j], AND_GATE});
        }

        int GT = g[0];
        for (int j = 1; j < k_bits; j++) {
            int gt_and_p = next_wire++;
            gates.insert(gates.end(), {GT, p[j], gt_and_p, AND_GATE});
            int xor1 = next_wire++;
            gates.insert(gates.end(), {g[j], GT, xor1, XOR_GATE});
            int new_GT = next_wire++;
            gates.insert(gates.end(), {xor1, gt_and_p, new_GT, XOR_GATE});
            GT = new_GT;
        }

        // Conditional swap: for each bit, XOR both elements with (diff AND GT)
        std::vector<int> new_a(k_bits), new_b(k_bits);
        for (int j = 0; j < k_bits; j++) {
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

    // Bitonic sorting network (iterative)
    for (int step = 2; step <= N; step <<= 1)
        for (int substep = step >> 1; substep >= 1; substep >>= 1)
            for (int i = 0; i < N; i++) {
                int j = i ^ substep;
                if (j > i)
                    compareAndSwap(i, j, ((i & step) == 0));
            }

    // Copy sorted wires to output position (BristolFormat expects last n3 wires)
    int out_bits = N * k_bits;
    int zero_wire = next_wire++;
    gates.insert(gates.end(), {0, 0, zero_wire, XOR_GATE});

    for (int i = 0; i < N; i++)
        for (int j = 0; j < k_bits; j++) {
            int ow = next_wire++;
            gates.insert(gates.end(), {elem[i][j], zero_wire, ow, XOR_GATE});
        }

    int total_inputs = N * k_bits;
    int n_gates = (int)gates.size() / 4;
    int n_wires = next_wire;

    return new emp::BristolFormat(n_gates, n_wires, total_inputs, 0, out_bits, gates.data());
}