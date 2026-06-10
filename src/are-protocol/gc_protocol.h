#pragma once

#include <emp-tool/emp-tool.h>
#include "permxor_are.h"
#include "metrics.h"
#include "string_ot_are.h"
#include <iostream>
#include <vector>
#include <random>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <set>
#include <map>
#include <chrono>

// ── Hash helper for boundary wire transfer (random oracle H in Algorithm 2) ───
inline emp::block hashBlock(emp::block x) {
    emp::Hash hash;
    hash.put(&x, sizeof(emp::block));
    char digest[emp::Hash::DIGEST_SIZE];
    hash.digest(digest);
    emp::block out;
    memcpy(&out, digest, sizeof(emp::block));
    return out;
}

// ── Block -> bits helpers ──────────────────────────────────────────────────────
inline std::vector<int> byteToBits(uint8_t v) {
    std::vector<int> bits(8);
    for (int i = 7; i >= 0; i--) { bits[i] = v & 1; v >>= 1; }
    return bits;
}
inline uint8_t bitsToUint8(const std::vector<int>& bits) {
    uint8_t v = 0; for (int b : bits) v = uint8_t((v << 1) | b); return v;
}

// ── OT-ARE: deliver a single wire label (128 bits) via byte-by-byte String OT ──
inline emp::block deliverLabelViaOTARE(emp::block W0, emp::block W1, int b, StringOTARE& ot, size_t* encoding_bytes = nullptr) {
    uint8_t raw0[16], raw1[16], result[16];
    memcpy(raw0, &W0, 16); memcpy(raw1, &W1, 16);
    size_t enc_bytes = 0;
    for (int i = 0; i < 16; i++) {
        auto se = ot.EncodeSender(byteToBits(raw0[i]), byteToBits(raw1[i]));
        auto re = ot.EncodeReceiver(b);
        auto decoded = ot.Decode(se, re);
        assert(!decoded.empty() && "OT-ARE decode failed");
        result[i] = bitsToUint8(decoded);
        enc_bytes += 2 * 480 + 2 * 480;  // sender + receiver Rabin-OT encodings
    }
    if (encoding_bytes) *encoding_bytes += enc_bytes;
    emp::block out; memcpy(&out, result, 16); return out;
}

// ── PermXOR-ARE boundary wire transfer (byte-by-byte, single round) ──────────
struct BoundaryTransferResult {
    emp::block W0_dst;
    emp::block W1_dst;
    emp::block xor_diff_0;
    emp::block xor_diff_1;
    bool perm_bit;
};

inline BoundaryTransferResult transferBoundaryPXT(
        emp::block W0_src, emp::block W1_src,
        emp::block W0_dst, emp::block W1_dst,
        PermXOTARE& pxt, size_t* encoding_bytes = nullptr) {

    bool b = emp::getLSB(W0_src);
    emp::block Kp0 = b ? W1_src : W0_src;
    emp::block Kp1 = b ? W0_src : W1_src;

    emp::block HKp0 = hashBlock(Kp0);
    emp::block HKp1 = hashBlock(Kp1);

    uint8_t raw_dst0[16], raw_dst1[16], raw_HKp0[16], raw_HKp1[16];
    uint8_t diff0[16], diff1[16];
    memcpy(raw_dst0, &W0_dst, 16);
    memcpy(raw_dst1, &W1_dst, 16);
    memcpy(raw_HKp0, &HKp0, 16);
    memcpy(raw_HKp1, &HKp1, 16);

    size_t enc_bytes = 0;
    for (int i = 0; i < 16; i++) {
        auto s0 = byteToBits(raw_dst0[i]);
        auto s1 = byteToBits(raw_dst1[i]);
        auto s0p = byteToBits(raw_HKp0[i]);
        auto s1p = byteToBits(raw_HKp1[i]);

        auto se = pxt.EncodeSender(s0, s1);
        auto re = pxt.EncodeReceiver(b ? 1 : 0, s0p, s1p);
        auto [first, second] = pxt.Decode(se, re);
        assert(!first.empty() && !second.empty());
        diff0[i] = bitsToUint8(first);
        diff1[i] = bitsToUint8(second);
        enc_bytes += 2 * 480 + 2 * 480;
    }
    if (encoding_bytes) *encoding_bytes += enc_bytes;

    BoundaryTransferResult res;
    res.W0_dst = W0_dst;
    res.W1_dst = W1_dst;
    res.perm_bit = b;
    memcpy(&res.xor_diff_0, diff0, 16);
    memcpy(&res.xor_diff_1, diff1, 16);
    return res;
}

inline emp::block applyBoundaryARE(emp::block active_label,
                                    const BoundaryTransferResult& bt) {
    // LSB of active label selects which PX-ARE component to use
    bool lsb = emp::getLSB(active_label);
    emp::block diff = lsb ? bt.xor_diff_1 : bt.xor_diff_0;
    return hashBlock(active_label) ^ diff;
}

// ── Bristol Circuit builders from circuits/ ──────────────────────────────────────────
#include "circuits/build_bitonic_sort.h"
#include "circuits/build_dp_selection.h"
#include "circuits/build_sequential_sum.h"
#include "circuits/build_tree_sum.h"
#include "circuits/build_and_chain.h"
#include "circuits/build_tree_mechanism.h"
#include "circuits/build_linear_contextual.h"
#include "circuits/build_distinct_elements.h"
#include "circuits/build_distinct_elements_histogram.h"

// ── Boolean evaluator (for verification) ─────────────────────────────────────
inline std::vector<int> boolEvalAllWires(const emp::BristolFormat* circ, const std::vector<int>& inputs) {
    std::vector<int> w(circ->num_wire, 0);
    for (int i = 0; i < (int)inputs.size(); i++) w[i] = inputs[i];
    for (int g = 0; g < circ->num_gate; g++) {
        int a = circ->gates[4*g+0], b = circ->gates[4*g+1];
        int out = circ->gates[4*g+2], type = circ->gates[4*g+3];
        if      (type == AND_GATE) w[out] = w[a] & w[b];
        else if (type == XOR_GATE) w[out] = w[a] ^ w[b];
        else if (type == NOT_GATE) w[out] = 1 - w[a];
    }
    return w;
}

// ── Core protocol ────────────────────────────────────────────────────────────
inline RunResult runProtocol(emp::BristolFormat* circ, int num_clients, bool balanced,
                              const std::string& experiment_name,
                              const std::string& circuit_name,
                              int bit_width = 0,
                              bool verbose = true,
                              const std::vector<int>* custom_inputs = nullptr) {
    RunResult result;
    result.experiment    = experiment_name;
    result.n_clients     = num_clients;
    result.circuit_name  = circuit_name;
    result.circuit_gates = circ->num_gate;
    result.bit_width     = bit_width;
    result.balanced      = balanced;
    result.clients.resize(num_clients);
    for (int i = 0; i < num_clients; i++) result.clients[i].client_id = i;

    StringOTARE ot_input(8, 4);
    PermXOTARE pxt_boundary(8, 4);
    // Prefer mmap-shared lookup files: at large N, every client process
    // duplicating the ell_A=20 table (~420 MB) on its own heap is what OOMs
    // the node. With the .mmap.bin file the kernel keeps one shared copy.
    ot_input.Setup(/*build_table=*/false);
    if (!ot_input.LoadTableMmap("bin/lookup_12.mmap.bin")) {
        if (!ot_input.LoadTable("bin/lookup_12.bin")) {
            std::cerr << "bin/lookup_12 table not found, building..." << std::endl;
            ot_input.Setup(/*build_table=*/true);
        }
    }
    pxt_boundary.Setup(/*build_table=*/false);
    if (!pxt_boundary.LoadTableMmap("bin/lookup_20.mmap.bin")) {
        if (!pxt_boundary.LoadTable("bin/lookup_20.bin")) {
            std::cerr << "bin/lookup_20 table not found, building..." << std::endl;
            pxt_boundary.Setup(/*build_table=*/true);
        }
    }

    const int total_inputs = circ->n1 + circ->n2;
    const int out_n        = circ->n3;
    const int out_base     = circ->num_wire - out_n;

    // Partition input wires
    std::vector<int> inp_start(num_clients), inp_end(num_clients);
    if (balanced) {
        int base = total_inputs / num_clients, rem = total_inputs % num_clients;
        inp_start[0] = 0;
        for (int i = 0; i < num_clients; i++) {
            inp_end[i] = inp_start[i] + base + (i < rem ? 1 : 0);
            if (i + 1 < num_clients) inp_start[i + 1] = inp_end[i];
        }
    } else {
        inp_start[0] = 0;
        inp_end[0]   = total_inputs;
        for (int i = 1; i < num_clients; i++) {
            inp_start[i] = total_inputs; //CHECK: Client 0 gets all input wires. Every other client gets zero input wires (their start equals their end, so an empty range).
            inp_end[i]   = total_inputs;
        }
    }

    // Partition gates
    std::vector<int> gate_start(num_clients), gate_end(num_clients);
    if (balanced) {
        int base = circ->num_gate / num_clients, rem = circ->num_gate % num_clients;
        gate_start[0] = 0;
        for (int i = 0; i < num_clients; i++) {
            gate_end[i] = gate_start[i] + base + (i < rem ? 1 : 0);
            if (i + 1 < num_clients) gate_start[i + 1] = gate_end[i];
        }
    } else {
        gate_start[0] = 0;
        gate_end[0]   = circ->num_gate;
        for (int i = 1; i < num_clients; i++) {
            gate_start[i] = circ->num_gate;
            gate_end[i]   = circ->num_gate;
        }
    }

    // wire_partition
    std::vector<int> wire_partition(circ->num_wire, -1);
    for (int c = 0; c < num_clients; c++)
        for (int w = inp_start[c]; w < inp_end[c]; w++)
            wire_partition[w] = c; //input wires owned by input owners
    for (int c = 0; c < num_clients; c++)
        for (int g = gate_start[c]; g < gate_end[c]; g++)
            wire_partition[circ->gates[4*g+2]] = c; //internal wires owned by gate owners of respective wires

    for (int c = 0; c < num_clients; c++) {
        result.clients[c].num_gates       = gate_end[c] - gate_start[c];
        result.clients[c].num_input_wires = inp_end[c] - inp_start[c];
    }

    if (verbose) {
        std::cout << "Partition summary:" << std::endl;
        for (int i = 0; i < num_clients; i++)
            std::cout << "  client[" << i << "]: inputs ["
                      << inp_start[i] << "," << inp_end[i] << ")  gates ["
                      << gate_start[i] << "," << gate_end[i] << ")" << std::endl;
    }

    // Client inputs: use custom if provided, otherwise random
    std::vector<int> all_input_bits(total_inputs);
    if (custom_inputs && (int)custom_inputs->size() == total_inputs) {
        all_input_bits = *custom_inputs;
    } else {
        std::mt19937 demo_rng(42);
        std::uniform_int_distribution<int> bit_dist(0, 1);
        for (auto& b : all_input_bits) b = bit_dist(demo_rng);
    }
    std::vector<int> wire_bits = boolEvalAllWires(circ, all_input_bits);

    // ── Phase 1: Distributed garbling ────────────────────────────────────────
    // Garbler uses only W0 labels per wire (W1 = W0 ^ delta).
    if (verbose) std::cout << "\n[Phase 1] Distributed garbling..." << std::endl;

    // Generate delta (LSB must be 1) and MITCCRH seed
    emp::PRG prg_setup;
    emp::block setup_blocks[2];
    prg_setup.random_block(setup_blocks, 2);
    emp::block delta = emp::set_bit(setup_blocks[0], 0);
    emp::block mitccrh_seed = setup_blocks[1];

    emp::MITCCRH<8> garble_mitccrh;
    garble_mitccrh.setS(mitccrh_seed);

    // Collect garbled tables for the evaluator
    std::vector<emp::block> garbled_tables;

    // Generate random W0 labels for input wires
    emp::PRG prg_inp;
    std::vector<emp::block> W0_inputs(total_inputs);
    prg_inp.random_block(W0_inputs.data(), total_inputs);

    // Only W0 labels needed — W1 = W0 ^ delta for any wire.
    std::vector<emp::block> W0_wire(circ->num_wire);
    for (int w = 0; w < total_inputs; w++)
        W0_wire[w] = W0_inputs[w];

    // Key: (client_id, wire) — each client gets its own boundary transform.
    // A wire may cross multiple partition boundaries (e.g., wire produced by client 0
    // needed by both client 5 and client 6). Each needs its own PXT-ARE transfer.
    std::map<std::pair<int,int>, BoundaryTransferResult> boundary_transfers;

    // Count boundary wires
    for (int c = 1; c < num_clients; c++) {
        std::set<int> need;
        for (int g = gate_start[c]; g < gate_end[c]; g++) {
            int in0 = circ->gates[4*g+0], in1 = circ->gates[4*g+1];
            if (wire_partition[in0] >= 0 && wire_partition[in0] < c) need.insert(in0);
            if (wire_partition[in1] >= 0 && wire_partition[in1] < c) need.insert(in1);
        }
        result.clients[c].num_boundary_in = (int)need.size();
        for (int w : need) {
            int src = wire_partition[w];
            if (src >= 0) result.clients[src].num_boundary_out++;
        }
    }

    // Garble partition by partition
    for (int c = 0; c < num_clients; c++) {
        if (verbose)
            std::cout << "  client[" << c << "]: garbling gates ["
                      << gate_start[c] << ", " << gate_end[c] << ")..." << std::endl;

        size_t tables_before = garbled_tables.size();

        // Boundary transfer: consumer (client c) independently generates labels
        // for its boundary input wires, then PX-ARE computes the translation.
        if (c > 0) {
            std::set<int> need;
            for (int g = gate_start[c]; g < gate_end[c]; g++) {
                int in0 = circ->gates[4*g+0], in1 = circ->gates[4*g+1];
                if (wire_partition[in0] >= 0 && wire_partition[in0] < c) need.insert(in0);
                if (wire_partition[in1] >= 0 && wire_partition[in1] < c) need.insert(in1);
            }
            for (int w : need) {
                ScopedTimer bt(result.clients[c].boundary_time_us);
                size_t bnd_bytes = 0;

                // Consumer independently generates its own W0 label for this wire
                emp::PRG prg_bnd;
                emp::block W0_dst;
                prg_bnd.random_block(&W0_dst, 1);
                emp::block W1_dst = W0_dst ^ delta;

                // PX-ARE: producer (W0_wire[w]) → consumer (W0_dst)
                auto bt_result = transferBoundaryPXT(
                    W0_wire[w], W0_wire[w] ^ delta,  // producer's labels
                    W0_dst, W1_dst,                    // consumer's labels
                    pxt_boundary, &bnd_bytes);
                result.clients[c].boundary_are_bytes += bnd_bytes;

                // Consumer uses its own labels for subsequent garbling
                W0_wire[w] = bt_result.W0_dst;
                boundary_transfers[{c, w}] = bt_result;
                if (verbose)
                    std::cout << "    boundary wire " << w << ": PXT-ARE done." << std::endl;
            }
        }

        // Garble gates using W0 labels only
        {
            ScopedTimer gt(result.clients[c].garble_time_us);
            for (int g = gate_start[c]; g < gate_end[c]; g++) {
                int in0   = circ->gates[4*g+0];
                int in1   = circ->gates[4*g+1];
                int out_w = circ->gates[4*g+2];
                int type  = circ->gates[4*g+3];

                if (type == XOR_GATE) {
                    // Free XOR: W0_out = W0_a ^ W0_b
                    W0_wire[out_w] = W0_wire[in0] ^ W0_wire[in1];
                } else if (type == AND_GATE) {
                    // Half-gate garble with both labels per wire
                    emp::block table[2];
                    W0_wire[out_w] = emp::halfgates_garble(W0_wire[in0], W0_wire[in0] ^ delta,W0_wire[in1], W0_wire[in1] ^ delta,delta, table, &garble_mitccrh);
                    garbled_tables.push_back(table[0]);
                    garbled_tables.push_back(table[1]);
                    result.clients[c].num_and_gates++;
                } else if (type == NOT_GATE) {
                    // NOT: swap labels → W0_out = W0_in ^ delta
                    W0_wire[out_w] = W0_wire[in0] ^ delta;
                }
            }
        }

        result.clients[c].garbled_table_bytes = (garbled_tables.size() - tables_before) * sizeof(emp::block);
        if (verbose)
            std::cout << "    done  (" << result.clients[c].num_and_gates << " AND, "<< result.clients[c].garbled_table_bytes << " bytes)" << std::endl;
    }
    // ── Phase 2: OT-ARE input label delivery ─────────────────────────────────
    if (verbose) std::cout << "\n[Phase 2] OT-ARE input label delivery..." << std::endl;

    std::vector<emp::block> eva_inputs(total_inputs);
    for (int c = 0; c < num_clients; c++) {
        ScopedTimer ot_t(result.clients[c].ot_are_time_us);
        for (int w = inp_start[c]; w < inp_end[c]; w++) {
            int bit = all_input_bits[w];
            size_t ot_bytes = 0;
            eva_inputs[w] = deliverLabelViaOTARE(W0_inputs[w], W0_inputs[w] ^ delta, bit, ot_input, &ot_bytes);
            result.clients[c].ot_are_bytes += ot_bytes;
            emp::block expected = bit ? (W0_inputs[w] ^ delta) : W0_inputs[w];
            assert(memcmp(&eva_inputs[w], &expected, 16) == 0 && "OT-ARE label mismatch");
        }
    }

    // ── Phase 3: Evaluation ──────────────────────────────────────────────────
    if (verbose) std::cout << "\n[Phase 3] Evaluating..." << std::endl;
    double eval_us = 0;
    {
        ScopedTimer et(eval_us);

        // Build MemIO with the data HalfGateEva expects:
        //   constructor reads: 2 constant blocks + 1 MITCCRH seed block
        //   and_gate reads: 2 table blocks per AND gate (in topological order)
        emp::MemIO mem_io;
        emp::PRG prg_const;
        emp::block constants[2];
        prg_const.random_block(constants, 2);
        constants[1] = constants[1] ^ delta;
        mem_io.send_block(constants, 2);
        mem_io.send_block(&mitccrh_seed, 1);
        if (!garbled_tables.empty())
            mem_io.send_block(garbled_tables.data(), (int)garbled_tables.size());
        mem_io.read_pos = 0;

        auto* eva = new emp::HalfGateEva<emp::MemIO>(&mem_io);
        emp::CircuitExecution::circ_exec = eva;

        std::vector<emp::block> eva_wires(circ->num_wire);
        for (int w = 0; w < total_inputs; w++) eva_wires[w] = eva_inputs[w];

        for (int c = 0; c < num_clients; c++) {
            if (c > 0) {
                std::set<int> need;
                for (int g = gate_start[c]; g < gate_end[c]; g++) {
                    int in0 = circ->gates[4*g+0], in1 = circ->gates[4*g+1];
                    if (wire_partition[in0] >= 0 && wire_partition[in0] < c &&
                        boundary_transfers.count({c, in0})) need.insert(in0);
                    if (wire_partition[in1] >= 0 && wire_partition[in1] < c &&
                        boundary_transfers.count({c, in1})) need.insert(in1);
                }
                for (int w : need)
                    eva_wires[w] = applyBoundaryARE(eva_wires[w], boundary_transfers[{c, w}]);
            }

            for (int g = gate_start[c]; g < gate_end[c]; g++) {
                int in0 = circ->gates[4*g+0], in1 = circ->gates[4*g+1];
                int out_w = circ->gates[4*g+2], type = circ->gates[4*g+3];
                if (type == XOR_GATE)
                    eva_wires[out_w] = emp::CircuitExecution::circ_exec->xor_gate(eva_wires[in0], eva_wires[in1]);
                else if (type == AND_GATE)
                    eva_wires[out_w] = emp::CircuitExecution::circ_exec->and_gate(eva_wires[in0], eva_wires[in1]);
                else if (type == NOT_GATE)
                    eva_wires[out_w] = emp::CircuitExecution::circ_exec->not_gate(eva_wires[in0]);

            }
        }
        delete eva;

        // Phase 4: Output decoding — W0 labels are directly available
        bool all_pass = true;
        for (int i = 0; i < out_n; i++) {
            emp::block W0_out = W0_wire[out_base + i];
            emp::block eva_out = eva_wires[out_base + i];
            // Decode: output bit = LSB(eva_label) XOR LSB(W0_label)
            int output_bit = (int)emp::getLSB(eva_out) ^ (int)emp::getLSB(W0_out);

            // Verification against plaintext (test only)
            int actual = wire_bits[out_base + i];
            if (output_bit != actual) all_pass = false;
            if (verbose)
                std::cout << "  output[" << i << "]: got=" << output_bit
                          << "  expected=" << actual
                          << "  " << (output_bit == actual ? "PASS" : "FAIL") << std::endl;
        }
        result.correct = all_pass;
    }
    result.eval_time_us = eval_us;

    double total = eval_us;
    for (auto& c : result.clients) total += c.total_time_us();
    result.total_time_us = total;

    if (verbose) {
        std::cout << "\n=== " << (result.correct ? "ALL PASS" : "FAILURES DETECTED") << " ===" << std::endl;
        printSummary(result);
    }

    return result;
}
