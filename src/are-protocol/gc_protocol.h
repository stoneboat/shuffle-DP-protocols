#pragma once

#include <emp-tool/emp-tool.h>
#include <vector>
#include <set>
#include <map>
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
