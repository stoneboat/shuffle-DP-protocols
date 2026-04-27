#include "net_serialize.h"
#include "gc_protocol.h"
#include <iostream>
#include <cstring>
#include <memory>
#include <random>
#ifdef _OPENMP
#include <omp.h>
#endif

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

    // Snapshot of the NetIO send counter at each phase boundary so we can
    // measure the actual bytes pushed onto the socket per phase. emp::NetIO
    // increments its `counter` field on every send_data; recv_data is not
    // counted, so this measures sender-side bandwidth only.
    uint64_t wire_snap_start = io->counter;

    // ── Phase 1: Boundary handling (as consumer) ────────────────────────────
    if (c > 0) {
        int n_boundary = recvInt(io);
        if (n_boundary > 0) {
            ScopedTimer bt(metrics.boundary_time_us);

            // Receive all hash infos serially first, then parallelize the
            // PXT-ARE encode/decode work across them.
            std::vector<BoundaryHashInfo> bhs(n_boundary);
            for (int i = 0; i < n_boundary; i++)
                bhs[i] = recvBoundaryHashInfo(io);

            std::vector<BoundaryResultMsg> results(n_boundary);
            std::vector<emp::block> W0_dsts(n_boundary);

            // Pre-allocate one PermXOTARE per OpenMP thread, serially in the
            // main thread. Setup() calls initPairing(BN254) which mutates mcl
            // global state — concurrent calls (or calls concurrent with mcl
            // ops on other threads) corrupt pairing parameters and produce
            // garbage encodings. Doing every Setup before entering the
            // parallel region is the only safe arrangement.
            int nthreads = 1;
        #ifdef _OPENMP
            nthreads = omp_get_max_threads();
        #endif
            std::vector<std::unique_ptr<PermXOTARE>> pxt_pool(nthreads);
            for (int t = 0; t < nthreads; t++) {
                pxt_pool[t] = std::unique_ptr<PermXOTARE>(new PermXOTARE(8, 4));
                pxt_pool[t]->Setup(/*build_table=*/false);
                // quiet=true: the global pxt_boundary already announced this
                // file at startup; the per-thread pool would otherwise emit
                // O(nthreads x N) duplicate banners and drown the slurm log.
                if (!pxt_pool[t]->LoadTableMmap("bin/lookup_20.mmap.bin", /*quiet=*/true))
                    pxt_pool[t]->LoadTable("bin/lookup_20.bin");
            }

            #pragma omp parallel num_threads(nthreads)
            {
                int tid = 0;
            #ifdef _OPENMP
                tid = omp_get_thread_num();
            #endif
                PermXOTARE& local_pxt = *pxt_pool[tid];

                #pragma omp for schedule(dynamic, 4)
                for (int i = 0; i < n_boundary; i++) {
                    const BoundaryHashInfo& bh = bhs[i];
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

                    for (int j = 0; j < 16; j++) {
                        auto s0  = byteToBits(raw_dst0[j]);
                        auto s1  = byteToBits(raw_dst1[j]);
                        auto s0p = byteToBits(raw_HKp0[j]);
                        auto s1p = byteToBits(raw_HKp1[j]);

                        auto se = local_pxt.EncodeSender(s0, s1);
                        auto re = local_pxt.EncodeReceiver(bh.perm_bit ? 1 : 0, s0p, s1p);
                        auto [first, second] = local_pxt.Decode(se, re);
                        assert(!first.empty() && !second.empty());
                        diff0[j] = bitsToUint8(first);
                        diff1[j] = bitsToUint8(second);
                    }
                    W0_dsts[i] = W0_dst;
                    BoundaryResultMsg br;
                    br.wire_id = bh.wire_id;
                    memcpy(&br.xor_diff_0, diff0, 16);
                    memcpy(&br.xor_diff_1, diff1, 16);
                    br.perm_bit = bh.perm_bit;
                    results[i] = br;
                }
            }

            metrics.boundary_are_bytes += (size_t)n_boundary * 16 * (2 * 480 + 2 * 480);
            for (int i = 0; i < n_boundary; i++)
                W0_wire[bhs[i].wire_id] = W0_dsts[i];

            // Send all boundary results back to evaluator
            sendInt(io, (int)results.size());
            for (auto& br : results)
                sendBoundaryResult(io, br);
            io->flush();
        }

        // Wait for DONE signal
        recvInt(io);
    }
    // Bytes shipped during the consumer-side boundary phase (BoundaryResultMsg
    // only — the PXT-ARE encodings themselves are decoded locally, never sent).
    uint64_t wire_snap_after_boundary_consumer = io->counter;
    metrics.wire_boundary_bytes = wire_snap_after_boundary_consumer - wire_snap_start;

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
    uint64_t wire_snap_before_garble_send = io->counter;
    sendInt(io, (int)garbled_tables.size());
    if (!garbled_tables.empty())
        io->send_block(garbled_tables.data(), (int)garbled_tables.size());
    metrics.wire_garble_bytes = io->counter - wire_snap_before_garble_send;

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
    uint64_t wire_snap_before_producer = io->counter;
    sendInt(io, (int)prod_data.size());
    for (auto& bh : prod_data)
        sendBoundaryHashInfo(io, bh);
    // Producer-side boundary bytes are still part of the boundary-handshake
    // budget on the wire, so fold them into wire_boundary_bytes.
    metrics.wire_boundary_bytes += io->counter - wire_snap_before_producer;

    // ── Send output W0 labels ───────────────────────────────────────────────
    sendInt(io, (int)pi.output_wires.size());
    for (int w : pi.output_wires) {
        sendInt(io, w);
        sendBlock(io, W0_wire[w]);
    }

    // ── Compute and send OT-ARE encodings for input wires ───────────────────
    {
        ScopedTimer ot_t(metrics.ot_are_time_us);

        // Compute all encodings in parallel into a buffer, then send sequentially.
        struct InputWireEnc {
            int w;
            SenderEncoding se[16];
            ReceiverEncoding re[16];
        };
        std::vector<InputWireEnc> inp_encs(n_inputs);

        // Pre-allocate one StringOTARE per OpenMP thread serially. See note
        // in the boundary loop above: Setup() calls initPairing(BN254) which
        // mutates mcl globals and is unsafe to overlap with mcl ops on other
        // threads. Encoding doesn't need a decode lookup table, so we skip it.
        int nthreads = 1;
    #ifdef _OPENMP
        nthreads = omp_get_max_threads();
    #endif
        std::vector<std::unique_ptr<StringOTARE>> ot_pool(nthreads);
        for (int t = 0; t < nthreads; t++) {
            ot_pool[t] = std::unique_ptr<StringOTARE>(new StringOTARE(8, 4));
            ot_pool[t]->Setup(/*build_table=*/false);
        }

        #pragma omp parallel num_threads(nthreads)
        {
            int tid = 0;
        #ifdef _OPENMP
            tid = omp_get_thread_num();
        #endif
            StringOTARE& local_ot = *ot_pool[tid];

            #pragma omp for schedule(dynamic, 4)
            for (int i = 0; i < n_inputs; i++) {
                int w = pi.inp_start + i;
                int bit = my_input_bits[i];
                emp::block W0 = W0_inputs[i];
                emp::block W1 = W0 ^ delta;

                uint8_t raw0[16], raw1[16];
                memcpy(raw0, &W0, 16);
                memcpy(raw1, &W1, 16);

                inp_encs[i].w = w;
                for (int j = 0; j < 16; j++) {
                    inp_encs[i].se[j] = local_ot.EncodeSender(byteToBits(raw0[j]), byteToBits(raw1[j]));
                    inp_encs[i].re[j] = local_ot.EncodeReceiver(bit);
                }
            }
        }

        uint64_t wire_snap_before_ot_send = io->counter;
        sendInt(io, n_inputs);
        for (int i = 0; i < n_inputs; i++) {
            sendInt(io, inp_encs[i].w);
            for (int j = 0; j < 16; j++) {
                sendSenderEncoding(io, inp_encs[i].se[j]);
                sendReceiverEncoding(io, inp_encs[i].re[j]);
            }
        }
        metrics.ot_are_bytes      += (size_t)n_inputs * 16 * (2 * 480 + 2 * 480);
        metrics.wire_ot_are_bytes  = io->counter - wire_snap_before_ot_send;
    }

    // Total wire bytes captured *before* shipping the metrics struct. The
    // sendClientMetrics call itself adds ~120 bytes of overhead that won't be
    // reflected in the recorded value — negligible against MB-scale totals.
    metrics.wire_total_bytes = io->counter - wire_snap_start;

    // ── Send metrics ────────────────────────────────────────────────────────
    sendClientMetrics(io, metrics);
    io->flush();

    if (args.verbose) {
        std::cout << "[Client " << c << "] Done. garble="
                  << metrics.garble_time_us << "us  ot="
                  << metrics.ot_are_time_us << "us  boundary="
                  << metrics.boundary_time_us << "us  total="
                  << metrics.total_time_us() << "us  are_bytes="
                  << metrics.total_bytes() << "  wire_bytes="
                  << metrics.wire_total_bytes
                  << " (garble=" << metrics.wire_garble_bytes
                  << " ot=" << metrics.wire_ot_are_bytes
                  << " bnd=" << metrics.wire_boundary_bytes << ")"
                  << std::endl;
    }

    delete io;
    return 0;
}
