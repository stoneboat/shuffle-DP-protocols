#include "net_serialize.h"
#include "gc_protocol.h"
#include <iostream>
#include <cstring>
#include <random>

struct ClientArgs {
    std::string host = "127.0.0.1";
    int base_port = 12345;
    int client_id = 0;
    std::string input_bits;
    bool verbose = true;
};

static ClientArgs parseArgs(int argc, char** argv) {
    ClientArgs a;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "--host" || arg == "-h") && i+1 < argc) a.host = argv[++i];
        else if ((arg == "--port" || arg == "-p") && i+1 < argc) a.base_port = atoi(argv[++i]);
        else if ((arg == "--id" || arg == "-i") && i+1 < argc) a.client_id = atoi(argv[++i]);
        else if ((arg == "--input") && i+1 < argc) a.input_bits = argv[++i];
        else if (arg == "-q") a.verbose = false;
    }
    return a;
}

int main(int argc, char** argv) {
    ClientArgs args = parseArgs(argc, argv);
    int c = args.client_id;

    // Connect to evaluator
    if (args.verbose)
        std::cout << "[Client " << c << "] Connecting to " << args.host
                  << ":" << (args.base_port + c) << std::endl;
    emp::NetIO* io = new emp::NetIO(args.host.c_str(), args.base_port + c, /*quiet=*/true);
    if (args.verbose) std::cout << "[Client " << c << "] Connected." << std::endl;

    // ── Phase 0: Receive partition info ──────────────────────────────────────
    PartitionInfo pi = recvPartitionInfo(io);
    assert(pi.client_id == c);
    if (args.verbose) {
        std::cout << "[Client " << c << "] Partition: inputs ["
                  << pi.inp_start << "," << pi.inp_end << ")  gates ["
                  << pi.gate_start << "," << pi.gate_end << ")"
                  << "  boundary_in=" << pi.boundary_in.size()
                  << "  boundary_out=" << pi.boundary_out.size()
                  << "  output_wires=" << pi.output_wires.size() << std::endl;
    }

    emp::block delta, mitccrh_seed;
    deriveDeltaAndSeed(delta, mitccrh_seed);
    if (args.verbose)
        std::cout << "[Client " << c << "] Delta+seed derived locally." << std::endl;

    emp::MITCCRH<8> garble_mitccrh;
    garble_mitccrh.setS(mitccrh_seed);

    StringOTARE ot_input(8, 4);
    ot_input.Setup(/*build_table=*/false);

    PermXOTARE pxt_boundary(8, 4);
    pxt_boundary.Setup(/*build_table=*/false);
    if (c > 0 && !pi.boundary_in.empty()) {
        // Prefer mmap-shared file: at large N, every client process duplicating
        // the ell_A=20 table (~420 MB heap) is what OOMs the node. With the
        // .mmap.bin file, the kernel page cache keeps one copy host-wide.
        if (!pxt_boundary.LoadTableMmap("bin/lookup_20.mmap.bin")) {
            if (!pxt_boundary.LoadTable("bin/lookup_20.bin")) {
                std::cerr << "[Client " << c << "] bin/lookup_20 table not found, building..." << std::endl;
                pxt_boundary.Setup(/*build_table=*/true);
            }
        }
    }

    int n_inputs = pi.inp_end - pi.inp_start;
    std::vector<emp::block> W0_inputs(n_inputs);
    if (n_inputs > 0) {
        emp::PRG prg_inp;
        prg_inp.random_block(W0_inputs.data(), n_inputs);
    }

    std::map<int, emp::block> W0_wire;
    for (int i = 0; i < n_inputs; i++)
        W0_wire[pi.inp_start + i] = W0_inputs[i];

    std::vector<int> my_input_bits(n_inputs);
    if (!args.input_bits.empty() && (int)args.input_bits.size() >= n_inputs) {
        for (int i = 0; i < n_inputs; i++)
            my_input_bits[i] = args.input_bits[i] - '0';
    } else {
        std::mt19937 rng(42 + c);
        std::uniform_int_distribution<int> bit_dist(0, 1);
        for (int i = 0; i < n_inputs; i++)
            my_input_bits[i] = bit_dist(rng);
    }

    // Metrics
    ClientMetrics metrics;
    metrics.client_id = c;
    metrics.num_gates = pi.gate_end - pi.gate_start;
    metrics.num_input_wires = n_inputs;
    metrics.num_boundary_in = (int)pi.boundary_in.size();
    metrics.num_boundary_out = (int)pi.boundary_out.size();

    // ── Phase 1: Boundary handling (as consumer) ────────────────────────────
    if (c > 0) {
        int n_boundary = recvInt(io);
        if (n_boundary > 0) {
            ScopedTimer bt(metrics.boundary_time_us);

            // Receive hash info for each boundary wire and compute PXT-ARE transfer
            std::vector<BoundaryResultMsg> results;
            for (int i = 0; i < n_boundary; i++) {
                BoundaryHashInfo bh = recvBoundaryHashInfo(io);

                emp::PRG prg_bnd;
                emp::block W0_dst;
                prg_bnd.random_block(&W0_dst, 1);
                emp::block W1_dst = W0_dst ^ delta;

                uint8_t raw_dst0[16], raw_dst1[16], raw_HKp0[16], raw_HKp1[16];
                uint8_t diff0[16], diff1[16];
                memcpy(raw_dst0, &W0_dst, 16);
                memcpy(raw_dst1, &W1_dst, 16);
                memcpy(raw_HKp0, &bh.H_Kp0, 16);
                memcpy(raw_HKp1, &bh.H_Kp1, 16);

                size_t enc_bytes = 0;
                for (int j = 0; j < 16; j++) {
                    auto s0 = byteToBits(raw_dst0[j]);
                    auto s1 = byteToBits(raw_dst1[j]);
                    auto s0p = byteToBits(raw_HKp0[j]);
                    auto s1p = byteToBits(raw_HKp1[j]);

                    auto se = pxt_boundary.EncodeSender(s0, s1);
                    auto re = pxt_boundary.EncodeReceiver(bh.perm_bit ? 1 : 0, s0p, s1p);
                    auto [first, second] = pxt_boundary.Decode(se, re);
                    assert(!first.empty() && !second.empty());
                    diff0[j] = bitsToUint8(first);
                    diff1[j] = bitsToUint8(second);
                    enc_bytes += 2 * 480 + 2 * 480;
                }
                metrics.boundary_are_bytes += enc_bytes;
                W0_wire[bh.wire_id] = W0_dst;

                BoundaryResultMsg br;
                br.wire_id = bh.wire_id;
                memcpy(&br.xor_diff_0, diff0, 16);
                memcpy(&br.xor_diff_1, diff1, 16);
                br.perm_bit = bh.perm_bit;
                results.push_back(br);
            }

            // Send all boundary results back to evaluator
            sendInt(io, (int)results.size());
            for (auto& br : results)
                sendBoundaryResult(io, br);
            io->flush();
        }

        // Wait for DONE signal
        recvInt(io);
    }

    // ── Phase 1b: Garble gates ──────────────────────────────────────────────
    std::vector<emp::block> garbled_tables;
    {
        ScopedTimer gt(metrics.garble_time_us);
        int ng = pi.gate_end - pi.gate_start;
        for (int gi = 0; gi < ng; gi++) {
            int in0   = pi.gate_data[gi*4 + 0];
            int in1   = pi.gate_data[gi*4 + 1];
            int out_w = pi.gate_data[gi*4 + 2];
            int type  = pi.gate_data[gi*4 + 3];

            if (type == XOR_GATE) {
                W0_wire[out_w] = W0_wire[in0] ^ W0_wire[in1];
            } else if (type == AND_GATE) {
                emp::block table[2];
                W0_wire[out_w] = emp::halfgates_garble(
                    W0_wire[in0], W0_wire[in0] ^ delta,
                    W0_wire[in1], W0_wire[in1] ^ delta,
                    delta, table, &garble_mitccrh);
                garbled_tables.push_back(table[0]);
                garbled_tables.push_back(table[1]);
                metrics.num_and_gates++;
            } else if (type == NOT_GATE) {
                W0_wire[out_w] = W0_wire[in0] ^ delta;
            }
        }
    }
    metrics.garbled_table_bytes = garbled_tables.size() * sizeof(emp::block);

    if (args.verbose)
        std::cout << "[Client " << c << "] Garbled " << metrics.num_and_gates << " AND gates, " << metrics.garbled_table_bytes << " bytes." << std::endl;

    // ── Send garbled tables ─────────────────────────────────────────────────
    sendInt(io, (int)garbled_tables.size());
    if (!garbled_tables.empty())
        io->send_block(garbled_tables.data(), (int)garbled_tables.size());

    // ── Send boundary producer data ─────────────────────────────────────────
    // For each wire this client produces that is needed by a later client
    std::vector<BoundaryHashInfo> prod_data;
    {
        // Collect unique boundary wires this client produces
        std::set<int> prod_wires;
        for (auto& [wire_id, consumer] : pi.boundary_out)
            prod_wires.insert(wire_id);

        for (int w : prod_wires) {
            BoundaryHashInfo bh;
            bh.wire_id = w;
            emp::block W0_w = W0_wire[w];
            bh.perm_bit = emp::getLSB(W0_w);
            emp::block Kp0 = bh.perm_bit ? (W0_w ^ delta) : W0_w;
            emp::block Kp1 = bh.perm_bit ? W0_w : (W0_w ^ delta);
            bh.H_Kp0 = hashBlock(Kp0);
            bh.H_Kp1 = hashBlock(Kp1);
            prod_data.push_back(bh);
        }
    }
    sendInt(io, (int)prod_data.size());
    for (auto& bh : prod_data)
        sendBoundaryHashInfo(io, bh);

    // ── Send output W0 labels ───────────────────────────────────────────────
    sendInt(io, (int)pi.output_wires.size());
    for (int w : pi.output_wires) {
        sendInt(io, w);
        sendBlock(io, W0_wire[w]);
    }

    // ── Compute and send OT-ARE encodings for input wires ───────────────────
    {
        ScopedTimer ot_t(metrics.ot_are_time_us);
        sendInt(io, n_inputs);
        for (int i = 0; i < n_inputs; i++) {
            int w = pi.inp_start + i;
            int bit = my_input_bits[i];
            emp::block W0 = W0_inputs[i];
            emp::block W1 = W0 ^ delta;

            sendInt(io, w);

            uint8_t raw0[16], raw1[16];
            memcpy(raw0, &W0, 16);
            memcpy(raw1, &W1, 16);
            size_t enc_bytes = 0;

            for (int j = 0; j < 16; j++) {
                auto se = ot_input.EncodeSender(byteToBits(raw0[j]), byteToBits(raw1[j]));
                sendSenderEncoding(io, se);
                auto re = ot_input.EncodeReceiver(bit);
                sendReceiverEncoding(io, re);
                enc_bytes += 2 * 480 + 2 * 480;
            }
            metrics.ot_are_bytes += enc_bytes;
        }
    }

    // ── Send metrics ────────────────────────────────────────────────────────
    sendClientMetrics(io, metrics);
    io->flush();

    if (args.verbose) {
        std::cout << "[Client " << c << "] Done. garble="
                  << metrics.garble_time_us << "us  ot="
                  << metrics.ot_are_time_us << "us  boundary="
                  << metrics.boundary_time_us << "us  total="
                  << metrics.total_time_us() << "us  bytes="
                  << metrics.total_bytes() << std::endl;
    }

    delete io;
    return 0;
}
