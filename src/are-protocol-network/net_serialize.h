#pragma once

#include <emp-tool/emp-tool.h>
#include <vector>
#include <set>
#include <map>
#include <string>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cassert>
#include "ot/rabin_ot_are.h"
#include "ot/string_ot_are.h"
#include "ot/permxor_are.h"
#include "metrics.h"

inline void deriveDeltaAndSeed(emp::block& delta, emp::block& mitccrh_seed) {
    emp::block fixed = emp::makeBlock(0x50524F544F434F4CULL, 0x4152455F44454C54ULL);
    emp::PRG prg(&fixed);
    emp::block blocks[2];
    prg.random_block(blocks, 2);
    delta = emp::set_bit(blocks[0], 0);
    mitccrh_seed = blocks[1];
}

// ── Message types ────────────────────────────────────────────────────────────
enum MsgType : uint32_t {
    MSG_PARTITION_INFO    = 1,
    MSG_SETUP             = 2,
    MSG_GARBLED_TABLES    = 3,
    MSG_BOUNDARY_PROD     = 4,
    MSG_BOUNDARY_HASH_INFO= 5,
    MSG_BOUNDARY_RESULT   = 6,
    MSG_BOUNDARY_DONE     = 7,
    MSG_OT_ARE_ENCS       = 8,
    MSG_OUTPUT_W0         = 9,
    MSG_CLIENT_METRICS    = 10,
};

// ── mcl serialization sizes (BN254) ─────────────────────────────────────────
static constexpr size_t G1_SERIAL_SIZE = 32;
static constexpr size_t G2_SERIAL_SIZE = 64;
static constexpr size_t GT_SERIAL_SIZE = 384;
static constexpr size_t ENCODED_DATA_SIZE = G1_SERIAL_SIZE + G2_SERIAL_SIZE + GT_SERIAL_SIZE; // 480

// ── Partition information sent to each client ───────────────────────────────
struct PartitionInfo {
    int num_clients;
    int client_id;
    int total_inputs;
    int num_wires;
    int num_gates;
    int out_n;
    int out_base;
    int inp_start;
    int inp_end;
    int gate_start;
    int gate_end;

    // Boundary wires this client needs (as consumer) — wire_id and producing client
    std::vector<std::pair<int,int>> boundary_in;  // (wire_id, producer_client_id)
    // Boundary wires this client produces (as producer) for later clients
    std::vector<std::pair<int,int>> boundary_out; // (wire_id, consumer_client_id)
    // Output wires owned by this client
    std::vector<int> output_wires;

    // Full gate data for this client's gate range: 4 ints per gate
    std::vector<int> gate_data;
    // Wire partition array (full, needed for boundary detection during garbling)
    std::vector<int> wire_partition;
};

// ── Compute partition (extracted from gc_protocol.h runProtocol) ─────────────
struct GlobalPartition {
    int num_clients;
    int total_inputs;
    int out_n;
    int out_base;
    std::vector<int> inp_start, inp_end;
    std::vector<int> gate_start, gate_end;
    std::vector<int> wire_partition;
    // per-client boundary info
    std::vector<std::vector<std::pair<int,int>>> boundary_in;  // [client] -> [(wire, producer)]
    std::vector<std::vector<std::pair<int,int>>> boundary_out; // [client] -> [(wire, consumer)]
    std::vector<std::vector<int>> output_wires; // [client] -> [wire_ids]
};

inline GlobalPartition computePartition(emp::BristolFormat* circ, int num_clients, bool balanced) {
    GlobalPartition gp;
    gp.num_clients = num_clients;
    gp.total_inputs = circ->n1 + circ->n2;
    gp.out_n = circ->n3;
    gp.out_base = circ->num_wire - circ->n3;

    gp.inp_start.resize(num_clients);
    gp.inp_end.resize(num_clients);
    gp.gate_start.resize(num_clients);
    gp.gate_end.resize(num_clients);

    // Partition input wires
    if (balanced) {
        int base = gp.total_inputs / num_clients, rem = gp.total_inputs % num_clients;
        gp.inp_start[0] = 0;
        for (int i = 0; i < num_clients; i++) {
            gp.inp_end[i] = gp.inp_start[i] + base + (i < rem ? 1 : 0);
            if (i + 1 < num_clients) gp.inp_start[i + 1] = gp.inp_end[i];
        }
    } else {
        gp.inp_start[0] = 0;
        gp.inp_end[0] = gp.total_inputs;
        for (int i = 1; i < num_clients; i++) {
            gp.inp_start[i] = gp.total_inputs;
            gp.inp_end[i] = gp.total_inputs;
        }
    }

    // Partition gates
    if (balanced) {
        int base = circ->num_gate / num_clients, rem = circ->num_gate % num_clients;
        gp.gate_start[0] = 0;
        for (int i = 0; i < num_clients; i++) {
            gp.gate_end[i] = gp.gate_start[i] + base + (i < rem ? 1 : 0);
            if (i + 1 < num_clients) gp.gate_start[i + 1] = gp.gate_end[i];
        }
    } else {
        gp.gate_start[0] = 0;
        gp.gate_end[0] = circ->num_gate;
        for (int i = 1; i < num_clients; i++) {
            gp.gate_start[i] = circ->num_gate;
            gp.gate_end[i] = circ->num_gate;
        }
    }

    // Wire partition
    gp.wire_partition.assign(circ->num_wire, -1);
    for (int c = 0; c < num_clients; c++)
        for (int w = gp.inp_start[c]; w < gp.inp_end[c]; w++)
            gp.wire_partition[w] = c;
    for (int c = 0; c < num_clients; c++)
        for (int g = gp.gate_start[c]; g < gp.gate_end[c]; g++)
            gp.wire_partition[circ->gates[4*g+2]] = c;

    // Boundary wires
    gp.boundary_in.resize(num_clients);
    gp.boundary_out.resize(num_clients);
    for (int c = 1; c < num_clients; c++) {
        std::set<int> need;
        for (int g = gp.gate_start[c]; g < gp.gate_end[c]; g++) {
            int in0 = circ->gates[4*g+0], in1 = circ->gates[4*g+1];
            if (gp.wire_partition[in0] >= 0 && gp.wire_partition[in0] < c) need.insert(in0);
            if (gp.wire_partition[in1] >= 0 && gp.wire_partition[in1] < c) need.insert(in1);
        }
        for (int w : need) {
            int producer = gp.wire_partition[w];
            gp.boundary_in[c].push_back({w, producer});
            gp.boundary_out[producer].push_back({w, c});
        }
    }

    // Output wires
    gp.output_wires.resize(num_clients);
    for (int i = 0; i < gp.out_n; i++) {
        int w = gp.out_base + i;
        int owner = gp.wire_partition[w];
        if (owner >= 0) gp.output_wires[owner].push_back(w);
    }

    return gp;
}

// ─────────────────────────────────────────────────────────────────────────────
// Extended partition modes (paper §7: Definitions 7.1, 7.2, Problem 1)
//
// The original computePartition(circ, N, balanced) above is preserved and
// remains the canonical "topologically balanced" / "all to client 0" splitter.
// The functions below add three additional partition strategies that all keep
// the contiguous gate-range structure (so the existing protocol still works):
//
//   PART_TOPOLOGICAL_BALANCED  — same as computePartition(circ, N, true)
//   PART_UNBALANCED            — same as computePartition(circ, N, false)
//   PART_NONXOR_BALANCED       — paper Def 7.2: equal Σ w(v) per client where
//                                w(v) = 1[v is non-XOR/non-NOT gate] (i.e.,
//                                AND-gate count balanced — the only gates
//                                that produce garbled tables / dominate cost).
//   PART_MIN_CUT               — paper Problem 1: among contiguous splits that
//                                satisfy the (1+γ) non-XOR balance constraint,
//                                pick cut points that minimize the pin-level
//                                cut count (gate→gate edges crossing the cut).
//
// All modes use the same balanced (equal-size) input partitioning as the
// original code so they inherit the same correctness profile.
// ─────────────────────────────────────────────────────────────────────────────

enum PartitionMode {
    PART_TOPOLOGICAL_BALANCED = 0,
    PART_UNBALANCED           = 1,
    PART_NONXOR_BALANCED      = 2,
    PART_MIN_CUT              = 3,
    PART_MIN_MAX_IN           = 4,
};

inline std::string partitionModeName(PartitionMode m) {
    switch (m) {
        case PART_TOPOLOGICAL_BALANCED: return "topo_bal";
        case PART_UNBALANCED:           return "unbal";
        case PART_NONXOR_BALANCED:      return "nonxor_bal";
        case PART_MIN_CUT:              return "min_cut";
        case PART_MIN_MAX_IN:           return "min_max_in";
    }
    return "unknown";
}

inline PartitionMode parsePartitionMode(const std::string& s) {
    if (s == "topo_bal" || s == "balanced" || s == "bal" || s == "topo")
        return PART_TOPOLOGICAL_BALANCED;
    if (s == "unbal" || s == "unbalanced")
        return PART_UNBALANCED;
    if (s == "nonxor_bal" || s == "nonxor" || s == "and_bal")
        return PART_NONXOR_BALANCED;
    if (s == "min_cut" || s == "mincut" || s == "cut")
        return PART_MIN_CUT;
    if (s == "min_max_in" || s == "minmax_in" || s == "minmax" || s == "min_max" || s == "mmi")
        return PART_MIN_MAX_IN;
    return PART_TOPOLOGICAL_BALANCED;
}

// Fill GlobalPartition fields from explicit input/gate cut arrays. Mirrors the
// boundary-detection logic of the original computePartition(): for each
// consumer client c, it adds wires whose producer client is strictly less than
// c. (The protocol's sequential structure relies on producer < consumer.)
inline void fillGlobalPartitionFromCuts(GlobalPartition& gp,
                                        emp::BristolFormat* circ,
                                        const std::vector<int>& inp_start,
                                        const std::vector<int>& inp_end,
                                        const std::vector<int>& gate_start,
                                        const std::vector<int>& gate_end) {
    int num_clients = (int)inp_start.size();
    gp.num_clients  = num_clients;
    gp.total_inputs = circ->n1 + circ->n2;
    gp.out_n        = circ->n3;
    gp.out_base     = circ->num_wire - circ->n3;
    gp.inp_start    = inp_start;
    gp.inp_end      = inp_end;
    gp.gate_start   = gate_start;
    gp.gate_end     = gate_end;

    gp.wire_partition.assign(circ->num_wire, -1);
    for (int c = 0; c < num_clients; c++)
        for (int w = inp_start[c]; w < inp_end[c]; w++)
            gp.wire_partition[w] = c;
    for (int c = 0; c < num_clients; c++)
        for (int g = gate_start[c]; g < gate_end[c]; g++)
            gp.wire_partition[circ->gates[4*g+2]] = c;

    gp.boundary_in.assign(num_clients, {});
    gp.boundary_out.assign(num_clients, {});
    for (int c = 1; c < num_clients; c++) {
        std::set<int> need;
        for (int g = gate_start[c]; g < gate_end[c]; g++) {
            int in0 = circ->gates[4*g+0], in1 = circ->gates[4*g+1];
            if (gp.wire_partition[in0] >= 0 && gp.wire_partition[in0] < c) need.insert(in0);
            if (gp.wire_partition[in1] >= 0 && gp.wire_partition[in1] < c) need.insert(in1);
        }
        for (int w : need) {
            int producer = gp.wire_partition[w];
            gp.boundary_in[c].push_back({w, producer});
            gp.boundary_out[producer].push_back({w, c});
        }
    }

    gp.output_wires.assign(num_clients, {});
    for (int i = 0; i < gp.out_n; i++) {
        int w = gp.out_base + i;
        int owner = gp.wire_partition[w];
        if (owner >= 0) gp.output_wires[owner].push_back(w);
    }
}

// Equal-size input partition shared by all extended modes.
inline void balancedInputCuts(int total_inputs, int num_clients,
                              std::vector<int>& inp_start,
                              std::vector<int>& inp_end) {
    inp_start.assign(num_clients, 0);
    inp_end.assign(num_clients, 0);
    int base = total_inputs / num_clients, rem = total_inputs % num_clients;
    inp_start[0] = 0;
    for (int c = 0; c < num_clients; c++) {
        inp_end[c] = inp_start[c] + base + (c < rem ? 1 : 0);
        if (c + 1 < num_clients) inp_start[c+1] = inp_end[c];
    }
}

// Paper Def 7.2: Balanced non-XOR weighted n-way partition.
// Splits the topological gate sequence into N contiguous chunks with roughly
// equal AND-gate (= non-XOR/non-NOT) counts.
inline GlobalPartition computePartitionNonXOR(emp::BristolFormat* circ, int num_clients) {
    GlobalPartition gp;
    int total_inputs = circ->n1 + circ->n2;
    int num_gate     = circ->num_gate;

    std::vector<int> inp_start, inp_end;
    balancedInputCuts(total_inputs, num_clients, inp_start, inp_end);

    std::vector<int> gate_start(num_clients, 0), gate_end(num_clients, 0);

    if (num_gate == 0 || num_clients == 1) {
        gate_end[num_clients - 1] = num_gate;
        fillGlobalPartitionFromCuts(gp, circ, inp_start, inp_end, gate_start, gate_end);
        return gp;
    }

    // prefix_nonxor[g] = # of non-XOR/non-NOT gates in [0, g)
    std::vector<int> prefix_nonxor(num_gate + 1, 0);
    for (int g = 0; g < num_gate; g++) {
        int type = circ->gates[4*g+3];
        bool is_nonxor = (type != XOR_GATE && type != NOT_GATE);
        prefix_nonxor[g+1] = prefix_nonxor[g] + (is_nonxor ? 1 : 0);
    }
    int total_nonxor = prefix_nonxor[num_gate];

    // If there are no non-XOR gates at all, fall back to gate-count balance.
    if (total_nonxor == 0) {
        int base = num_gate / num_clients, rem = num_gate % num_clients;
        gate_start[0] = 0;
        for (int c = 0; c < num_clients; c++) {
            gate_end[c] = gate_start[c] + base + (c < rem ? 1 : 0);
            if (c + 1 < num_clients) gate_start[c+1] = gate_end[c];
        }
        fillGlobalPartitionFromCuts(gp, circ, inp_start, inp_end, gate_start, gate_end);
        return gp;
    }

    gate_start[0] = 0;
    for (int c = 0; c < num_clients; c++) {
        if (c == num_clients - 1) {
            gate_end[c] = num_gate;
        } else {
            int target = (int)std::round((double)(c+1) * total_nonxor / num_clients);
            // smallest p with prefix_nonxor[p] >= target
            auto it = std::lower_bound(prefix_nonxor.begin() + gate_start[c],
                                       prefix_nonxor.end(), target);
            int p = (int)(it - prefix_nonxor.begin());
            // Ensure each remaining client gets at least one gate.
            int min_p = gate_start[c] + 1;
            int max_p = num_gate - (num_clients - 1 - c);
            if (p < min_p) p = min_p;
            if (p > max_p) p = max_p;
            gate_end[c] = p;
            gate_start[c+1] = p;
        }
    }

    fillGlobalPartitionFromCuts(gp, circ, inp_start, inp_end, gate_start, gate_end);
    return gp;
}

// Paper Problem 1: Balanced non-XOR min-cut partitioning.
// Among contiguous gate splits that satisfy non-XOR (1+γ)/n balance, pick the
// cut points that minimize cut_pin = #{gate→gate edges crossing a cut}.
//
// Heuristic: for each cut c, find the gate-index window [p_lo, p_hi] whose
// prefix non-XOR count lies within tol = γ·W_tot/(2n) of the target, then pick
// the position in that window with the fewest crossings.
inline GlobalPartition computePartitionMinCut(emp::BristolFormat* circ, int num_clients,
                                              double gamma = 0.2) {
    GlobalPartition gp;
    int total_inputs = circ->n1 + circ->n2;
    int num_gate     = circ->num_gate;

    std::vector<int> inp_start, inp_end;
    balancedInputCuts(total_inputs, num_clients, inp_start, inp_end);

    std::vector<int> gate_start(num_clients, 0), gate_end(num_clients, 0);

    if (num_gate == 0 || num_clients == 1) {
        gate_end[num_clients - 1] = num_gate;
        fillGlobalPartitionFromCuts(gp, circ, inp_start, inp_end, gate_start, gate_end);
        return gp;
    }

    // wire_producer[w] = gate index that outputs w (or -1 for input wires).
    std::vector<int> wire_producer(circ->num_wire, -1);
    for (int g = 0; g < num_gate; g++)
        wire_producer[circ->gates[4*g+2]] = g;

    // crossings[p] = # of gate→gate edges (u → v) with u < p ≤ v.
    // Each such edge contributes +1 to crossings[p] for p ∈ {u+1, ..., v}.
    // Use a difference array, then prefix sum.
    std::vector<int> diff(num_gate + 2, 0);
    for (int v = 0; v < num_gate; v++) {
        int in0 = circ->gates[4*v+0], in1 = circ->gates[4*v+1];
        int t   = circ->gates[4*v+3];
        int u0 = wire_producer[in0];
        if (u0 >= 0 && u0 < v) { diff[u0+1] += 1; diff[v+1] -= 1; }
        if (t != NOT_GATE) {
            int u1 = wire_producer[in1];
            if (u1 >= 0 && u1 < v) { diff[u1+1] += 1; diff[v+1] -= 1; }
        }
    }
    std::vector<int> crossings(num_gate + 2, 0);
    int run = 0;
    for (int p = 0; p <= num_gate + 1; p++) { run += diff[p]; crossings[p] = run; }

    // Non-XOR prefix.
    std::vector<int> prefix_nonxor(num_gate + 1, 0);
    for (int g = 0; g < num_gate; g++) {
        int type = circ->gates[4*g+3];
        bool is_nonxor = (type != XOR_GATE && type != NOT_GATE);
        prefix_nonxor[g+1] = prefix_nonxor[g] + (is_nonxor ? 1 : 0);
    }
    int total_nonxor = prefix_nonxor[num_gate];

    // If there are no non-XOR gates, balance by gate count instead.
    int weight_unit = (total_nonxor > 0) ? 1 : 0;
    std::vector<int>& prefix_w = prefix_nonxor;
    int total_w = total_nonxor;
    std::vector<int> prefix_count;
    if (weight_unit == 0) {
        prefix_count.resize(num_gate + 1);
        for (int g = 0; g <= num_gate; g++) prefix_count[g] = g;
        prefix_w = prefix_count;
        total_w = num_gate;
    }

    gate_start[0] = 0;
    for (int c = 0; c < num_clients - 1; c++) {
        double target = (double)(c+1) * total_w / num_clients;
        double tol    = gamma * total_w / (2.0 * num_clients);
        int lo_target = std::max(0, (int)std::ceil(target - tol));
        int hi_target = (int)std::floor(target + tol);
        // Window must cover at least one feasible position.
        if (hi_target < lo_target) hi_target = lo_target;

        int seg_lo = gate_start[c] + 1;
        int seg_hi = num_gate - (num_clients - 1 - c);
        if (seg_hi < seg_lo) seg_hi = seg_lo;

        // Find p range where prefix_w[p] ∈ [lo_target, hi_target].
        auto it_lo = std::lower_bound(prefix_w.begin() + seg_lo, prefix_w.begin() + seg_hi + 1, lo_target);
        auto it_hi = std::upper_bound(prefix_w.begin() + seg_lo, prefix_w.begin() + seg_hi + 1, hi_target);
        int p_lo = (int)(it_lo - prefix_w.begin());
        int p_hi = (int)(it_hi - prefix_w.begin()) - 1;
        if (p_lo < seg_lo) p_lo = seg_lo;
        if (p_hi > seg_hi) p_hi = seg_hi;
        if (p_hi < p_lo) p_hi = p_lo;

        // Pick p ∈ [p_lo, p_hi] minimising crossings[p].
        int best_p = p_lo, best_cr = crossings[p_lo];
        for (int p = p_lo + 1; p <= p_hi; p++) {
            if (crossings[p] < best_cr) { best_cr = crossings[p]; best_p = p; }
        }
        gate_end[c] = best_p;
        gate_start[c+1] = best_p;
    }
    gate_end[num_clients - 1] = num_gate;

    fillGlobalPartitionFromCuts(gp, circ, inp_start, inp_end, gate_start, gate_end);
    return gp;
}

// Paper Problem 2 (with μ_in instead of μ_out): Balanced non-XOR min-max
// boundary partitioning. Among contiguous gate splits satisfying (1+γ)/n
// non-XOR balance, pick cut points that minimise max_c |boundary_in[c]| —
// the worst-client incoming pin count, which is the per-step bottleneck
// for the sequential ARE protocol.
//
// The paper formulates the problem with outgoing boundary μ_out; we use μ_in
// because in this protocol each client waits on incoming OT-ARE work, so
// max_c |boundary_in[c]| is what dictates the critical path. Both share the
// same overall structure (each cross-edge has one producer and one consumer).
//
// Algorithm: warm-start from computePartitionMinCut, then sequential
// per-cut hill-climb. For each cut c (boundary between client c-1 and c),
// enumerate a small set of candidate positions inside its (1+γ)/n feasibility
// window — the current position, both endpoints, and a uniformly-spaced
// interior — recompute per-client boundary_in counts, and accept the position
// minimising the global max. Sweep cuts repeatedly until no cut improves.
inline GlobalPartition computePartitionMinMaxBoundary(emp::BristolFormat* circ, int num_clients,
                                                      double gamma = 0.2) {
    GlobalPartition gp = computePartitionMinCut(circ, num_clients, gamma);
    if (num_clients <= 1 || circ->num_gate == 0) return gp;

    int num_gate = circ->num_gate;

    // prefix non-XOR for feasibility windows (mirrors min-cut / non-XOR modes)
    std::vector<int> prefix_nonxor(num_gate + 1, 0);
    for (int g = 0; g < num_gate; g++) {
        int type = circ->gates[4*g+3];
        bool is_nonxor = (type != XOR_GATE && type != NOT_GATE);
        prefix_nonxor[g+1] = prefix_nonxor[g] + (is_nonxor ? 1 : 0);
    }
    int total_nonxor = prefix_nonxor[num_gate];
    std::vector<int> prefix_count;
    std::vector<int>* prefix_w_ptr = &prefix_nonxor;
    int total_w = total_nonxor;
    if (total_nonxor == 0) {
        prefix_count.resize(num_gate + 1);
        for (int g = 0; g <= num_gate; g++) prefix_count[g] = g;
        prefix_w_ptr = &prefix_count;
        total_w = num_gate;
    }
    std::vector<int>& prefix_w = *prefix_w_ptr;

    std::vector<int> gate_start = gp.gate_start;
    std::vector<int> gate_end   = gp.gate_end;
    const std::vector<int>& inp_start = gp.inp_start;
    const std::vector<int>& inp_end   = gp.inp_end;

    auto recomputeBoundaryIn = [&](const std::vector<int>& gs,
                                   const std::vector<int>& ge) -> std::vector<int> {
        std::vector<int> wp(circ->num_wire, -1);
        for (int c = 0; c < num_clients; c++)
            for (int w = inp_start[c]; w < inp_end[c]; w++) wp[w] = c;
        for (int c = 0; c < num_clients; c++)
            for (int g = gs[c]; g < ge[c]; g++) wp[circ->gates[4*g+2]] = c;

        std::vector<int> counts(num_clients, 0);
        std::vector<int> last_seen(circ->num_wire, -1);
        for (int c = 1; c < num_clients; c++) {
            for (int g = gs[c]; g < ge[c]; g++) {
                int in0 = circ->gates[4*g+0];
                int in1 = circ->gates[4*g+1];
                int t   = circ->gates[4*g+3];
                if (wp[in0] >= 0 && wp[in0] < c && last_seen[in0] != c) {
                    last_seen[in0] = c; counts[c]++;
                }
                if (t != NOT_GATE && wp[in1] >= 0 && wp[in1] < c && last_seen[in1] != c) {
                    last_seen[in1] = c; counts[c]++;
                }
            }
        }
        return counts;
    };

    auto windowForCut = [&](int c, const std::vector<int>& gs) -> std::pair<int,int> {
        // Cut c is the boundary between client c-1 and c (gate_end[c-1]==gate_start[c]).
        double target = (double)c * total_w / num_clients;
        double tol    = gamma * total_w / (2.0 * num_clients);
        int lo_target = std::max(0, (int)std::ceil(target - tol));
        int hi_target = (int)std::floor(target + tol);
        if (hi_target < lo_target) hi_target = lo_target;

        int seg_lo = gs[c-1] + 1;                   // ≥1 gate for client c-1
        int seg_hi = num_gate - (num_clients - c);  // ≥1 gate per remaining client
        if (seg_hi < seg_lo) seg_hi = seg_lo;

        auto it_lo = std::lower_bound(prefix_w.begin() + seg_lo,
                                      prefix_w.begin() + seg_hi + 1, lo_target);
        auto it_hi = std::upper_bound(prefix_w.begin() + seg_lo,
                                      prefix_w.begin() + seg_hi + 1, hi_target);
        int p_lo = (int)(it_lo - prefix_w.begin());
        int p_hi = (int)(it_hi - prefix_w.begin()) - 1;
        if (p_lo < seg_lo) p_lo = seg_lo;
        if (p_hi > seg_hi) p_hi = seg_hi;
        if (p_hi < p_lo) p_hi = p_lo;
        return {p_lo, p_hi};
    };

    auto vecMax = [](const std::vector<int>& v) {
        int m = 0; for (int x : v) if (x > m) m = x; return m;
    };

    std::vector<int> cur_counts = recomputeBoundaryIn(gate_start, gate_end);
    int cur_max = vecMax(cur_counts);

    const int max_passes           = 6;
    const int interior_candidates  = 10;

    for (int pass = 0; pass < max_passes; pass++) {
        bool improved = false;
        for (int c = 1; c < num_clients; c++) {
            auto [p_lo, p_hi] = windowForCut(c, gate_start);
            int cur_p = gate_start[c];

            std::vector<int> cands = {cur_p, p_lo, p_hi};
            int span = p_hi - p_lo;
            if (span > 0) {
                int step = std::max(1, span / std::max(1, interior_candidates));
                for (int p = p_lo; p <= p_hi; p += step) cands.push_back(p);
            }
            std::sort(cands.begin(), cands.end());
            cands.erase(std::unique(cands.begin(), cands.end()), cands.end());

            int best_p   = cur_p;
            int best_max = cur_max;
            std::vector<int> best_counts = cur_counts;
            for (int p : cands) {
                if (p == cur_p) continue;
                std::vector<int> gs = gate_start, ge = gate_end;
                gs[c]   = p;
                ge[c-1] = p;
                std::vector<int> cnts = recomputeBoundaryIn(gs, ge);
                int m = vecMax(cnts);
                if (m < best_max) {
                    best_max    = m;
                    best_p      = p;
                    best_counts = std::move(cnts);
                }
            }
            if (best_p != cur_p) {
                gate_end[c-1] = best_p;
                gate_start[c] = best_p;
                cur_counts    = std::move(best_counts);
                cur_max       = best_max;
                improved = true;
            }
        }
        if (!improved) break;
    }

    GlobalPartition out;
    fillGlobalPartitionFromCuts(out, circ, inp_start, inp_end, gate_start, gate_end);
    return out;
}

// Unified dispatch.
inline GlobalPartition computePartitionByMode(emp::BristolFormat* circ, int num_clients,
                                              PartitionMode mode, double gamma = 0.2) {
    switch (mode) {
        case PART_TOPOLOGICAL_BALANCED: return computePartition(circ, num_clients, true);
        case PART_UNBALANCED:           return computePartition(circ, num_clients, false);
        case PART_NONXOR_BALANCED:      return computePartitionNonXOR(circ, num_clients);
        case PART_MIN_CUT:              return computePartitionMinCut(circ, num_clients, gamma);
        case PART_MIN_MAX_IN:           return computePartitionMinMaxBoundary(circ, num_clients, gamma);
    }
    return computePartition(circ, num_clients, true);
}

// Sum of |boundary_in[c]| across clients — pin-level cut count (paper notation
// cut_pin(π)). Useful as a partition-quality metric to record in CSV.
inline int totalBoundaryPins(const GlobalPartition& gp) {
    int s = 0;
    for (auto& v : gp.boundary_in) s += (int)v.size();
    return s;
}

inline PartitionInfo makePartitionInfo(const GlobalPartition& gp, emp::BristolFormat* circ, int c) {
    PartitionInfo pi;
    pi.num_clients = gp.num_clients;
    pi.client_id = c;
    pi.total_inputs = gp.total_inputs;
    pi.num_wires = circ->num_wire;
    pi.num_gates = circ->num_gate;
    pi.out_n = gp.out_n;
    pi.out_base = gp.out_base;
    pi.inp_start = gp.inp_start[c];
    pi.inp_end = gp.inp_end[c];
    pi.gate_start = gp.gate_start[c];
    pi.gate_end = gp.gate_end[c];
    pi.boundary_in = gp.boundary_in[c];
    pi.boundary_out = gp.boundary_out[c];
    pi.output_wires = gp.output_wires[c];
    int ng = pi.gate_end - pi.gate_start;
    pi.gate_data.resize(ng * 4);
    for (int g = 0; g < ng; g++)
        for (int j = 0; j < 4; j++)
            pi.gate_data[g*4+j] = circ->gates[4*(pi.gate_start + g) + j];
    pi.wire_partition = gp.wire_partition;
    return pi;
}

// ── Low-level send/recv helpers ─────────────────────────────────────────────

inline void sendInt(emp::NetIO* io, int v) {
    io->send_data(&v, sizeof(int));
}
inline int recvInt(emp::NetIO* io) {
    int v; io->recv_data(&v, sizeof(int)); return v;
}
inline void sendUint32(emp::NetIO* io, uint32_t v) {
    io->send_data(&v, sizeof(uint32_t));
}
inline uint32_t recvUint32(emp::NetIO* io) {
    uint32_t v; io->recv_data(&v, sizeof(uint32_t)); return v;
}
inline void sendBool(emp::NetIO* io, bool v) {
    uint8_t b = v ? 1 : 0;
    io->send_data(&b, 1);
}
inline bool recvBool(emp::NetIO* io) {
    uint8_t b; io->recv_data(&b, 1); return b != 0;
}
inline void sendBlock(emp::NetIO* io, emp::block b) {
    io->send_block(&b, 1);
}
inline emp::block recvBlock(emp::NetIO* io) {
    emp::block b; io->recv_block(&b, 1); return b;
}
inline void sendIntVec(emp::NetIO* io, const std::vector<int>& v) {
    int n = (int)v.size();
    sendInt(io, n);
    if (n > 0) io->send_data(v.data(), n * sizeof(int));
}
inline std::vector<int> recvIntVec(emp::NetIO* io) {
    int n = recvInt(io);
    std::vector<int> v(n);
    if (n > 0) io->recv_data(v.data(), n * sizeof(int));
    return v;
}

// ── EncodedData serialization (G1=32, G2=64, GT=384 bytes) ─────────────────

inline void sendEncodedData(emp::NetIO* io, const EncodedData& ed) {
    char buf[ENCODED_DATA_SIZE];
    size_t off = 0;
    off += ed.part1.serialize(buf + off, G1_SERIAL_SIZE);
    off += ed.part2.serialize(buf + off, G2_SERIAL_SIZE);
    off += ed.part3.serialize(buf + off, GT_SERIAL_SIZE);
    assert(off == ENCODED_DATA_SIZE);
    io->send_data(buf, ENCODED_DATA_SIZE);
}

inline EncodedData recvEncodedData(emp::NetIO* io) {
    char buf[ENCODED_DATA_SIZE];
    io->recv_data(buf, ENCODED_DATA_SIZE);
    EncodedData ed;
    size_t off = 0;
    off += ed.part1.deserialize(buf + off, G1_SERIAL_SIZE);
    off += ed.part2.deserialize(buf + off, G2_SERIAL_SIZE);
    off += ed.part3.deserialize(buf + off, GT_SERIAL_SIZE);
    return ed;
}

// ── SenderEncoding: enc1 + enc2 + c ─────────────────────────────────────────

inline void sendSenderEncoding(emp::NetIO* io, const SenderEncoding& se) {
    sendEncodedData(io, se.enc1);
    sendEncodedData(io, se.enc2);
    sendInt(io, se.c);
}

inline SenderEncoding recvSenderEncoding(emp::NetIO* io) {
    SenderEncoding se;
    se.enc1 = recvEncodedData(io);
    se.enc2 = recvEncodedData(io);
    se.c = recvInt(io);
    return se;
}

// ── ReceiverEncoding: enc1 + enc2 ───────────────────────────────────────────

inline void sendReceiverEncoding(emp::NetIO* io, const ReceiverEncoding& re) {
    sendEncodedData(io, re.enc1);
    sendEncodedData(io, re.enc2);
}

inline ReceiverEncoding recvReceiverEncoding(emp::NetIO* io) {
    ReceiverEncoding re;
    re.enc1 = recvEncodedData(io);
    re.enc2 = recvEncodedData(io);
    return re;
}

// ── PartitionInfo send/recv ─────────────────────────────────────────────────

inline void sendPartitionInfo(emp::NetIO* io, const PartitionInfo& pi) {
    sendInt(io, pi.num_clients);
    sendInt(io, pi.client_id);
    sendInt(io, pi.total_inputs);
    sendInt(io, pi.num_wires);
    sendInt(io, pi.num_gates);
    sendInt(io, pi.out_n);
    sendInt(io, pi.out_base);
    sendInt(io, pi.inp_start);
    sendInt(io, pi.inp_end);
    sendInt(io, pi.gate_start);
    sendInt(io, pi.gate_end);

    // boundary_in
    sendInt(io, (int)pi.boundary_in.size());
    for (auto& [w, prod] : pi.boundary_in) {
        sendInt(io, w); sendInt(io, prod);
    }
    // boundary_out
    sendInt(io, (int)pi.boundary_out.size());
    for (auto& [w, cons] : pi.boundary_out) {
        sendInt(io, w); sendInt(io, cons);
    }
    // output_wires
    sendIntVec(io, pi.output_wires);
    // gate_data
    sendIntVec(io, pi.gate_data);
    // wire_partition
    sendIntVec(io, pi.wire_partition);
}

inline PartitionInfo recvPartitionInfo(emp::NetIO* io) {
    PartitionInfo pi;
    pi.num_clients = recvInt(io);
    pi.client_id = recvInt(io);
    pi.total_inputs = recvInt(io);
    pi.num_wires = recvInt(io);
    pi.num_gates = recvInt(io);
    pi.out_n = recvInt(io);
    pi.out_base = recvInt(io);
    pi.inp_start = recvInt(io);
    pi.inp_end = recvInt(io);
    pi.gate_start = recvInt(io);
    pi.gate_end = recvInt(io);

    int nb_in = recvInt(io);
    pi.boundary_in.resize(nb_in);
    for (int i = 0; i < nb_in; i++) {
        pi.boundary_in[i].first = recvInt(io);
        pi.boundary_in[i].second = recvInt(io);
    }
    int nb_out = recvInt(io);
    pi.boundary_out.resize(nb_out);
    for (int i = 0; i < nb_out; i++) {
        pi.boundary_out[i].first = recvInt(io);
        pi.boundary_out[i].second = recvInt(io);
    }
    pi.output_wires = recvIntVec(io);
    pi.gate_data = recvIntVec(io);
    pi.wire_partition = recvIntVec(io);
    return pi;
}

// ── Boundary hash info (producer -> evaluator -> consumer) ──────────────────
struct BoundaryHashInfo {
    int wire_id;
    bool perm_bit;      // LSB(W0_src)
    emp::block H_Kp0;   // hash of permuted label 0
    emp::block H_Kp1;   // hash of permuted label 1
};

inline void sendBoundaryHashInfo(emp::NetIO* io, const BoundaryHashInfo& bh) {
    sendInt(io, bh.wire_id);
    sendBool(io, bh.perm_bit);
    sendBlock(io, bh.H_Kp0);
    sendBlock(io, bh.H_Kp1);
}

inline BoundaryHashInfo recvBoundaryHashInfo(emp::NetIO* io) {
    BoundaryHashInfo bh;
    bh.wire_id = recvInt(io);
    bh.perm_bit = recvBool(io);
    bh.H_Kp0 = recvBlock(io);
    bh.H_Kp1 = recvBlock(io);
    return bh;
}

// ── Boundary result (consumer -> evaluator) ─────────────────────────────────
struct BoundaryResultMsg {
    int wire_id;
    emp::block xor_diff_0;
    emp::block xor_diff_1;
    bool perm_bit;
};

inline void sendBoundaryResult(emp::NetIO* io, const BoundaryResultMsg& br) {
    sendInt(io, br.wire_id);
    sendBlock(io, br.xor_diff_0);
    sendBlock(io, br.xor_diff_1);
    sendBool(io, br.perm_bit);
}

inline BoundaryResultMsg recvBoundaryResult(emp::NetIO* io) {
    BoundaryResultMsg br;
    br.wire_id = recvInt(io);
    br.xor_diff_0 = recvBlock(io);
    br.xor_diff_1 = recvBlock(io);
    br.perm_bit = recvBool(io);
    return br;
}

// ── ClientMetrics send/recv ─────────────────────────────────────────────────

inline void sendClientMetrics(emp::NetIO* io, const ClientMetrics& cm) {
    sendInt(io, cm.client_id);
    sendInt(io, cm.num_gates);
    sendInt(io, cm.num_and_gates);
    sendInt(io, cm.num_input_wires);
    sendInt(io, cm.num_boundary_in);
    sendInt(io, cm.num_boundary_out);
    double times[3] = {cm.garble_time_us, cm.ot_are_time_us, cm.boundary_time_us};
    io->send_data(times, sizeof(times));
    size_t bytes[3] = {cm.garbled_table_bytes, cm.ot_are_bytes, cm.boundary_are_bytes};
    io->send_data(bytes, sizeof(bytes));
}

inline ClientMetrics recvClientMetrics(emp::NetIO* io) {
    ClientMetrics cm;
    cm.client_id = recvInt(io);
    cm.num_gates = recvInt(io);
    cm.num_and_gates = recvInt(io);
    cm.num_input_wires = recvInt(io);
    cm.num_boundary_in = recvInt(io);
    cm.num_boundary_out = recvInt(io);
    double times[3];
    io->recv_data(times, sizeof(times));
    cm.garble_time_us = times[0];
    cm.ot_are_time_us = times[1];
    cm.boundary_time_us = times[2];
    size_t bytes[3];
    io->recv_data(bytes, sizeof(bytes));
    cm.garbled_table_bytes = bytes[0];
    cm.ot_are_bytes = bytes[1];
    cm.boundary_are_bytes = bytes[2];
    return cm;
}
