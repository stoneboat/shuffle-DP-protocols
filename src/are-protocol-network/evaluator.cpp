#include "net_serialize.h"
#include "gc_protocol.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <map>
#include <random>
#include <cmath>
#include <algorithm>
#include <unistd.h>
#include <sys/wait.h>
#include <libgen.h>

struct Args {
    int num_clients = 2;
    int base_port = 12345;
    std::string circuit = "treemech";
    int T = -1, D = 2, K = 8;  // T=-1 means auto (T=N for treemech)
    int N_sort = -1; // -1 means auto (N_sort=N)
    int NB = 8;      // noise bits for selection
    bool balanced = true;        // legacy flag; mirrors partition_mode for back-compat
    PartitionMode partition_mode = PART_TOPOLOGICAL_BALANCED;
    bool partition_explicit = false; // true if --partition was passed
    double gamma = 0.2;          // slack for non-XOR balance constraint (paper §7)
    std::string csv_file;
    std::string experiment; // custom experiment name for CSV
    bool verbose = true;
    bool benchmark = false;   // run all circuit/N/partition combos
    bool spawn_clients = false; // fork+exec client processes
    bool circuit_explicit = false; // true if --circuit was passed
    int n_min = 2;            // benchmark sweep: smallest N (default 2)
    int n_max = 64;           // benchmark sweep: largest N  (default 64)
    std::string self_path;    // argv[0] for finding client binary
};

static Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "--clients" || arg == "-n") && i+1 < argc) a.num_clients = atoi(argv[++i]);
        else if ((arg == "--port" || arg == "-p") && i+1 < argc) a.base_port = atoi(argv[++i]);
        else if ((arg == "--circuit" || arg == "-c") && i+1 < argc) { a.circuit = argv[++i]; a.circuit_explicit = true; }
        else if (arg == "--T" && i+1 < argc) a.T = atoi(argv[++i]);
        else if (arg == "--D" && i+1 < argc) a.D = atoi(argv[++i]);
        else if (arg == "--K" && i+1 < argc) a.K = atoi(argv[++i]);
        else if (arg == "--NB" && i+1 < argc) a.NB = atoi(argv[++i]);
        else if (arg == "--unbalanced") {
            a.balanced = false;
            a.partition_mode = PART_UNBALANCED;
            a.partition_explicit = true;
        }
        else if ((arg == "--partition" || arg == "--part") && i+1 < argc) {
            a.partition_mode = parsePartitionMode(argv[++i]);
            a.balanced = (a.partition_mode != PART_UNBALANCED);
            a.partition_explicit = true;
        }
        else if (arg == "--gamma" && i+1 < argc) a.gamma = atof(argv[++i]);
        else if ((arg == "--csv" || arg == "-o") && i+1 < argc) a.csv_file = argv[++i];
        else if ((arg == "--exp" || arg == "-e") && i+1 < argc) a.experiment = argv[++i];
        else if (arg == "-q") a.verbose = false;
        else if (arg == "--benchmark") a.benchmark = true;
        else if (arg == "--spawn-clients") a.spawn_clients = true;
        else if (arg == "--n-min" && i+1 < argc) a.n_min = atoi(argv[++i]);
        else if (arg == "--n-max" && i+1 < argc) a.n_max = atoi(argv[++i]);
    }
    return a;
}

static emp::BristolFormat* buildCircuit(const Args& a) {
    int N = a.num_clients;
    int n_sort = (a.N_sort > 0) ? a.N_sort : N;
    int T = (a.T > 0) ? a.T : N;
    if (a.circuit == "treemech")
        return buildTreeMechanismCircuit(T, a.D, a.K);
    if (a.circuit == "seqsum")
        return buildSequentialSumCircuit(n_sort, a.K);
    if (a.circuit == "gausssum")
        return buildTreeSumCircuit(n_sort, a.K);
    if (a.circuit == "treesum")
        return buildTreeSumCircuit(n_sort, a.K);
    if (a.circuit == "bitonic")
        return buildSortingCircuit(n_sort, a.K);
    if (a.circuit == "select")
        return buildSelectionCircuit(n_sort, a.D, a.NB);
    if (a.circuit == "lcb")
        return buildLinearContextualCircuit(T, a.D, a.K);
    if (a.circuit == "distinct")
        return buildDistinctElementsCircuit(n_sort, a.K, a.NB);
    if (a.circuit == "distincthist")
        return buildDistinctElementsHistogramCircuit(n_sort, a.K, a.NB);
    std::cerr << "Unknown circuit: " << a.circuit << std::endl;
    exit(1);
}

// ── Client process spawning ─────────────────────────────────────────────────

static std::string getClientPath(const std::string& evaluator_path) {
    std::string ep = evaluator_path;
    auto pos = ep.rfind('/');
    if (pos != std::string::npos)
        return ep.substr(0, pos + 1) + "client";
    return "./client";
}

// ── Input sampling for DP circuits ──────────────────────────────────────────
// Produces the full plaintext bit-vector (size == total_inputs) that matches
// the circuit's input layout. Noise distributions chosen per circuit:
//   - gausssum: each client's value = clamp(x_i + z_i) with z_i ~ N(0, σ_local)
//   - distinct: user values uniform over [0, 2^K); noise ~ Lap(1/ε) appended as NB-bit two's complement
// For other circuits, falls back to uniform random bits (benchmark-only).
static std::vector<int> sampleInputBits(const Args& a, emp::BristolFormat* circ, std::mt19937& rng) {
    int total_inputs = circ->n1 + circ->n2;
    std::vector<int> bits(total_inputs);
    int N = a.num_clients;
    int K = a.K, NB = a.NB;

    if (a.circuit == "gausssum") {
        const double epsilon = 1.0;
        const int max_val = (1 << K) - 1;
        const double delta_dp = 1.0 / ((double)N * N);
        const double sigma_central = max_val * std::sqrt(2.0 * std::log(1.25 / delta_dp)) / epsilon;
        const double sigma_local   = sigma_central / std::sqrt((double)N);
        std::uniform_int_distribution<int> val_dist(0, max_val);
        std::normal_distribution<double> noise_dist(0.0, sigma_local);
        for (int c = 0; c < N; c++) {
            int x_i = val_dist(rng);
            double z_i = noise_dist(rng);
            int noisy = std::clamp((int)std::round(x_i + z_i), 0, max_val);
            for (int b = 0; b < K; b++)
                bits[c * K + b] = (noisy >> b) & 1;
        }
        return bits;
    }

    if (a.circuit == "distinct" || a.circuit == "distincthist") {
        const double epsilon = 1.0;
        const int max_val = (1 << K) - 1;
        const int noise_max = (1 << (NB - 1)) - 1;
        const int noise_min = -(1 << (NB - 1));
        std::uniform_int_distribution<int> val_dist(0, max_val);
        std::exponential_distribution<double> exp_dist(epsilon);
        std::uniform_int_distribution<int> sign_dist(0, 1);
        for (int c = 0; c < N; c++) {
            int x_i = val_dist(rng);
            for (int b = 0; b < K; b++)
                bits[c * K + b] = (x_i >> b) & 1;
        }
        double z_d = (sign_dist(rng) ? 1.0 : -1.0) * exp_dist(rng);
        int z = std::clamp((int)std::round(z_d), noise_min, noise_max);
        uint32_t z_u = (uint32_t)(z & ((1u << NB) - 1));
        for (int b = 0; b < NB; b++)
            bits[N * K + b] = (z_u >> b) & 1;
        if (a.circuit == "distincthist")
            bits[N * K + NB] = 1; // constant-1 wire
        return bits;
    }

    if (a.circuit == "select") {
        // One-hot votes + Gumbel-per-choice sampled in plaintext, biased into
        // unsigned NB bits, plus trailing constant-1 wire (required by circuit).
        const double epsilon = 1.0;
        const int D = a.D;
        const int noise_mid = 1 << (NB - 1);
        const int noise_max = (1 << NB) - 1;
        std::uniform_int_distribution<int> choice_dist(0, D - 1);
        std::uniform_real_distribution<double> u01(1e-12, 1.0);
        for (int i = 0; i < N; i++) {
            int pick = choice_dist(rng);
            bits[i * D + pick] = 1;
        }
        for (int j = 0; j < D; j++) {
            double g = -std::log(-std::log(u01(rng))) / epsilon;
            int g_u = std::clamp((int)std::round(g + noise_mid), 0, noise_max);
            for (int b = 0; b < NB; b++)
                bits[N * D + j * NB + b] = (g_u >> b) & 1;
        }
        bits[N * D + D * NB] = 1;  // constant-1
        return bits;
    }

    if (a.circuit == "lcb") {
        // Tree (Fenwick) mechanism with Laplace noise.
        // Layout: [0, T*D*K) data || [T*D*K, 2*T*D*K) noise.
        //   T = num rounds (defaults to N), d = feat_dim, D = d + d*d.
        // One Lap(0, log(T)/eps) sample per (Fenwick node i in [1,T], dimension
        // dim in [0,D)), bit-decomposed as K-bit two's complement. Sampled as
        // sign * Exp(eps/log T) — same trick as the distinct branch.
        const double epsilon = 1.0;
        const int T = (a.T > 0) ? a.T : N;
        const int d = a.D;
        const int D = d + d * d;
        const int max_val = (1 << K) - 1;
        const int noise_max = (1 << (K - 1)) - 1;
        const int noise_min = -(1 << (K - 1));
        const double log_T = std::log((double)std::max(T, 2));
        const double lap_rate = epsilon / log_T;  // E[|z|] = log T / eps
        std::uniform_int_distribution<int> val_dist(0, max_val);
        std::exponential_distribution<double> exp_dist(lap_rate);
        std::uniform_int_distribution<int> sign_dist(0, 1);

        const int total_data_bits = T * D * K;
        // Data: per-round contributions (uniform values; the protocol cost is
        // data-independent so any reasonable values benchmark identically).
        for (int t = 0; t < T; t++) {
            for (int dim = 0; dim < D; dim++) {
                int v = val_dist(rng);
                int base = (t * D + dim) * K;
                for (int b = 0; b < K; b++)
                    bits[base + b] = (v >> b) & 1;
            }
        }
        // Noise: one Lap(0, log T / eps) per (Fenwick node, dimension).
        for (int i = 1; i <= T; i++) {
            for (int dim = 0; dim < D; dim++) {
                double z_d = (sign_dist(rng) ? 1.0 : -1.0) * exp_dist(rng);
                int z = std::clamp((int)std::round(z_d), noise_min, noise_max);
                uint32_t z_u = (uint32_t)(z & ((1u << K) - 1));
                int base = total_data_bits + ((i - 1) * D + dim) * K;
                for (int b = 0; b < K; b++)
                    bits[base + b] = (z_u >> b) & 1;
            }
        }
        return bits;
    }

    std::uniform_int_distribution<int> bit_dist(0, 1);
    for (auto& b : bits) b = bit_dist(rng);
    return bits;
}

// Slice full plaintext bit-vector into per-client "0"/"1" strings matching the
// evaluator's input partition. Each client only sees bits for its wire range.
static std::vector<std::string> sliceInputsPerClient(const std::vector<int>& bits, const GlobalPartition& gp) {
    int N = (int)gp.inp_start.size();
    std::vector<std::string> per_client(N);
    for (int c = 0; c < N; c++) {
        std::string s;
        s.reserve(gp.inp_end[c] - gp.inp_start[c]);
        for (int w = gp.inp_start[c]; w < gp.inp_end[c]; w++)
            s.push_back(bits[w] ? '1' : '0');
        per_client[c] = std::move(s);
    }
    return per_client;
}

static std::vector<pid_t> spawnClients(int n, int base_port, const std::string& client_bin, const std::vector<std::string>& per_client_inputs = {}) {
    std::vector<pid_t> pids;
    for (int i = 0; i < n; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            std::string port_str = std::to_string(base_port);
            std::string id_str = std::to_string(i);
            if (!per_client_inputs.empty() && !per_client_inputs[i].empty()) {
                execl(client_bin.c_str(), "client","--host", "127.0.0.1", "--port", port_str.c_str(), "--id", id_str.c_str(), "--input", per_client_inputs[i].c_str(), "-q", nullptr);
            } else {
                execl(client_bin.c_str(), "client","--host", "127.0.0.1", "--port", port_str.c_str(), "--id", id_str.c_str(), "-q", nullptr);
            }
            perror("execl client");
            _exit(1);
        }
        if (pid < 0) { perror("fork"); exit(1); }
        pids.push_back(pid);
    }
    return pids;
}

static void waitForAll(const std::vector<pid_t>& pids) {
    for (pid_t p : pids) waitpid(p, nullptr, 0);
}

// ── Single experiment (the core protocol) ───────────────────────────────────

static RunResult runProtocol(const Args& args) {
    int N = args.num_clients;

    // Build circuit
    emp::BristolFormat* circ = buildCircuit(args);
    if (args.verbose) {
        std::cout << "[Evaluator] Circuit: " << args.circuit<< "  gates=" << circ->num_gate<< "  inputs=" << (circ->n1 + circ->n2)<< "  outputs=" << circ->n3 << std::endl;
    }

    // Compute partition (dispatched by --partition; legacy --unbalanced still
    // works because parseArgs maps it to PART_UNBALANCED).
    GlobalPartition gp = computePartitionByMode(circ, N, args.partition_mode, args.gamma);

    // Open N server connections
    if (args.verbose)
        std::cout << "[Evaluator] Waiting for " << N << " clients on ports "<< args.base_port << ".." << (args.base_port + N - 1) << std::endl;

    std::vector<emp::NetIO*> ios(N);
    for (int c = 0; c < N; c++) {
        ios[c] = new emp::NetIO(nullptr, args.base_port + c, /*quiet=*/true);
        if (args.verbose)
            std::cout << "[Evaluator] Client " << c << " connected." << std::endl;
    }

    // ── Phase 0: Send partition info to each client ─────────────────────────
    for (int c = 0; c < N; c++) {
        PartitionInfo pi = makePartitionInfo(gp, circ, c);
        sendPartitionInfo(ios[c], pi);
        ios[c]->flush();
    }
    if (args.verbose) std::cout << "[Evaluator] Partition info sent." << std::endl;

    // ── Phase 0b: Derive delta+seed deterministically (no network) ──────────
    emp::block delta, mitccrh_seed;
    deriveDeltaAndSeed(delta, mitccrh_seed);
    if (args.verbose) std::cout << "[Evaluator] Delta+seed derived locally." << std::endl;

    // ── Phase 1: Sequential garbling with boundary coordination ─────────────
    std::map<int, BoundaryHashInfo> boundary_hash_store;
    std::map<std::pair<int,int>, BoundaryTransferResult> boundary_transfers;
    std::vector<emp::block> all_garbled_tables;
    std::map<int, emp::block> output_W0;

    struct OTWireEncs {
        SenderEncoding se[16];
        ReceiverEncoding re[16];
    };
    std::map<int, OTWireEncs> ot_encs;
    std::vector<ClientMetrics> all_metrics(N);

    // Initialize ARE for decoding — prefer mmap-shared file (one copy across
    // all client processes via the kernel page cache), fall back to legacy
    // heap-loaded file, then to building from scratch.
    StringOTARE ot_decode(8, 4);
    ot_decode.Setup(/*build_table=*/false);
    if (!ot_decode.LoadTableMmap("bin/lookup_12.mmap.bin")) {
        if (!ot_decode.LoadTable("bin/lookup_12.bin")) {
            std::cerr << "[Evaluator] lookup_12 table not found, building..." << std::endl;
            ot_decode.Setup(/*build_table=*/true);
        }
    }

    for (int c = 0; c < N; c++) {
        if (args.verbose)
            std::cout << "[Evaluator] Processing client " << c << "..." << std::endl;

        // ── Boundary: relay hash info to consumer, receive results ───────
        if (c > 0 && !gp.boundary_in[c].empty()) {
            sendInt(ios[c], (int)gp.boundary_in[c].size());
            for (auto& [wire_id, producer] : gp.boundary_in[c]) {
                auto it = boundary_hash_store.find(wire_id);
                assert(it != boundary_hash_store.end());
                sendBoundaryHashInfo(ios[c], it->second);
            }
            ios[c]->flush();

            int n_results = recvInt(ios[c]);
            for (int i = 0; i < n_results; i++) {
                BoundaryResultMsg br = recvBoundaryResult(ios[c]);
                BoundaryTransferResult bt;
                bt.xor_diff_0 = br.xor_diff_0;
                bt.xor_diff_1 = br.xor_diff_1;
                bt.perm_bit = br.perm_bit;
                bt.W0_dst = emp::zero_block;
                bt.W1_dst = emp::zero_block;
                boundary_transfers[{c, br.wire_id}] = bt;
            }

            sendInt(ios[c], 1);
            ios[c]->flush();
        } else if (c > 0) {
            sendInt(ios[c], 0);
            ios[c]->flush();
            sendInt(ios[c], 1);
            ios[c]->flush();
        }

        // ── Receive garbled tables ───────────────────────────────────────
        int n_tables = recvInt(ios[c]);
        if (n_tables > 0) {
            std::vector<emp::block> tables(n_tables);
            ios[c]->recv_block(tables.data(), n_tables);
            all_garbled_tables.insert(all_garbled_tables.end(), tables.begin(), tables.end());
        }

        // ── Receive boundary producer data ───────────────────────────────
        int n_prod = recvInt(ios[c]);
        for (int i = 0; i < n_prod; i++) {
            BoundaryHashInfo bh = recvBoundaryHashInfo(ios[c]);
            boundary_hash_store[bh.wire_id] = bh;
        }

        // ── Receive output W0 labels ─────────────────────────────────────
        int n_out = recvInt(ios[c]);
        for (int i = 0; i < n_out; i++) {
            int wire_id = recvInt(ios[c]);
            emp::block w0 = recvBlock(ios[c]);
            output_W0[wire_id] = w0;
        }

        // ── Receive OT-ARE encodings ─────────────────────────────────────
        int n_ot = recvInt(ios[c]);
        for (int i = 0; i < n_ot; i++) {
            int wire_id = recvInt(ios[c]);
            OTWireEncs owe;
            for (int b = 0; b < 16; b++) {
                owe.se[b] = recvSenderEncoding(ios[c]);
                owe.re[b] = recvReceiverEncoding(ios[c]);
            }
            ot_encs[wire_id] = owe;
        }

        // ── Receive client metrics ───────────────────────────────────────
        all_metrics[c] = recvClientMetrics(ios[c]);
    }

    if (args.verbose)
        std::cout << "[Evaluator] All client data received. Decoding + evaluating..." << std::endl;

    // ── Phase 2: Decode OT-ARE to get input labels ──────────────────────────
    int total_inputs = gp.total_inputs;
    std::vector<emp::block> eva_inputs(total_inputs);

    int decode_ok = 0, decode_fail = 0;
    for (auto& [wire_id, owe] : ot_encs) {
        uint8_t result[16];
        bool wire_ok = true;
        for (int i = 0; i < 16; i++) {
            auto decoded = ot_decode.Decode(owe.se[i], owe.re[i]);
            if (decoded.empty()) {
                if (wire_ok) {
                    std::cerr << "[Evaluator] OT-ARE decode failed: wire=" << wire_id<< " byte=" << i << std::endl;
                    wire_ok = false;
                }
                decode_fail++;
                result[i] = 0;
            } else {
                decode_ok++;
                result[i] = bitsToUint8(decoded);
            }
        }
        memcpy(&eva_inputs[wire_id], result, 16);
    }
    if (decode_fail > 0) {
        std::cerr << "[Evaluator] OT-ARE: " << decode_ok << " ok, " << decode_fail<< " failed out of " << (ot_encs.size() * 16) << " total" << std::endl;
    }

    // ── Phase 3: Evaluate ───────────────────────────────────────────────────
    double eval_us = 0;
    std::vector<emp::block> eva_wires(circ->num_wire);
    {
        ScopedTimer et(eval_us);
        emp::MemIO mem_io;
        emp::PRG prg_const;
        emp::block constants[2];
        prg_const.random_block(constants, 2);
        constants[1] = constants[1] ^ delta;
        mem_io.send_block(constants, 2);
        mem_io.send_block(&mitccrh_seed, 1);
        if (!all_garbled_tables.empty())
            mem_io.send_block(all_garbled_tables.data(), (int)all_garbled_tables.size());
        mem_io.read_pos = 0;

        auto* eva = new emp::HalfGateEva<emp::MemIO>(&mem_io);
        emp::CircuitExecution::circ_exec = eva;

        for (int w = 0; w < total_inputs; w++)
            eva_wires[w] = eva_inputs[w];

        for (int c = 0; c < N; c++) {
            if (c > 0) {
                for (auto& [wire_id, producer] : gp.boundary_in[c]) {
                    auto it = boundary_transfers.find({c, wire_id});
                    if (it != boundary_transfers.end())
                        eva_wires[wire_id] = applyBoundaryARE(eva_wires[wire_id], it->second);
                }
            }
            for (int g = gp.gate_start[c]; g < gp.gate_end[c]; g++) {
                int in0 = circ->gates[4*g+0];
                int in1 = circ->gates[4*g+1];
                int out_w = circ->gates[4*g+2];
                int type = circ->gates[4*g+3];
                if (type == XOR_GATE)
                    eva_wires[out_w] = emp::CircuitExecution::circ_exec->xor_gate(eva_wires[in0], eva_wires[in1]);
                else if (type == AND_GATE)
                    eva_wires[out_w] = emp::CircuitExecution::circ_exec->and_gate(eva_wires[in0], eva_wires[in1]);
                else if (type == NOT_GATE)
                    eva_wires[out_w] = emp::CircuitExecution::circ_exec->not_gate(eva_wires[in0]);
            }
        }
        delete eva;
    }

    // ── Phase 4: Decode outputs ─────────────────────────────────────────────
    std::vector<int> output_bits(gp.out_n);
    for (int i = 0; i < gp.out_n; i++) {
        int w = gp.out_base + i;
        auto it = output_W0.find(w);
        assert(it != output_W0.end() && "Missing output W0 label");
        output_bits[i] = (int)emp::getLSB(eva_wires[w]) ^ (int)emp::getLSB(it->second);
    }

    if (args.verbose) {
        std::cout << "[Evaluator] Output bits: ";
        for (int b : output_bits) std::cout << b;
        std::cout << std::endl;
    }

    // ── Build RunResult ─────────────────────────────────────────────────────
    RunResult result;
    std::string mode_tag = "_" + partitionModeName(args.partition_mode);
    result.experiment = args.experiment.empty()? ("net_" + args.circuit + "_N" + std::to_string(N) + mode_tag): args.experiment;
    result.n_clients = N;
    result.circuit_name = args.circuit;
    result.circuit_gates = circ->num_gate;
    result.bit_width = args.K;
    result.balanced = args.balanced;
    result.partition_mode = partitionModeName(args.partition_mode);
    result.total_boundary_pins = totalBoundaryPins(gp);
    result.clients = all_metrics;
    result.eval_time_us = eval_us;
    result.correct = true;
    double total = eval_us;
    for (auto& cm : result.clients) total += cm.total_time_us();
    result.total_time_us = total;

    // Cleanup
    for (int c = 0; c < N; c++) delete ios[c];
    delete circ;

    return result;
}

// ── main ────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    Args args = parseArgs(argc, argv);
    args.self_path = argv[0];
    std::string client_bin = getClientPath(args.self_path);

    if (args.benchmark) {
        args.spawn_clients = true;
        std::vector<RunResult> all_results;
        const std::vector<std::string> circuits = args.circuit_explicit
            ? std::vector<std::string>{args.circuit}
            : std::vector<std::string>{"gausssum", "select", "lcb", "bitonic"};
        std::vector<int> n_values;
        for (int n = 2; n <= 1 << 20; n <<= 1) {
            if (n >= args.n_min && n <= args.n_max) n_values.push_back(n);
        }
        if (n_values.empty()) {
            std::cerr << "[Evaluator] No N values in range [" << args.n_min<< ", " << args.n_max << "]" << std::endl;
            return 1;
        }
        // If --partition was explicit, only run that mode; otherwise sweep all.
        const std::vector<PartitionMode> modes = args.partition_explicit
            ? std::vector<PartitionMode>{args.partition_mode}
            : std::vector<PartitionMode>{
                  PART_TOPOLOGICAL_BALANCED,
                  PART_UNBALANCED,
                  PART_NONXOR_BALANCED,
                  PART_MIN_CUT,
                  PART_MIN_MAX_IN,
              };
        int port = args.base_port;

        for (auto& circ : circuits) {
            for (int n : n_values) {
                for (PartitionMode mode : modes) {
                    Args ea = args;
                    ea.circuit = circ;
                    ea.num_clients = n;
                    ea.partition_mode = mode;
                    ea.balanced = (mode != PART_UNBALANCED);
                    ea.base_port = port;
                    ea.verbose = false;

                    std::string mode_str = partitionModeName(mode);
                    ea.experiment = "net_" + circ + "_N" + std::to_string(n) + "_" + mode_str;
                    std::cout << "=== Running: " << ea.experiment<< " port=" << port << " ===" << std::endl;

                    // Sample plaintext inputs (Gaussian for gausssum, Laplace
                    // for distinct, uniform for others) and slice per client.
                    emp::BristolFormat* preview_circ = buildCircuit(ea);
                    GlobalPartition preview_gp = computePartitionByMode(preview_circ, n, mode, ea.gamma);
                    std::mt19937 sample_rng(2026 + 17 * n + (int)mode);
                    std::vector<int> all_bits = sampleInputBits(ea, preview_circ, sample_rng);
                    auto per_client_inputs = sliceInputsPerClient(all_bits, preview_gp);
                    delete preview_circ;

                    auto pids = spawnClients(n, port, client_bin, per_client_inputs);
                    RunResult r = runProtocol(ea);
                    waitForAll(pids);

                    printSummary(r);
                    all_results.push_back(std::move(r));

                    port += n + 10;
                }
            }
        }

        if (!args.csv_file.empty()) {
            std::ofstream ofs(args.csv_file);
            writeCSV(ofs, all_results);
            std::cout << "All results written to: " << args.csv_file << std::endl;
        }
        return 0;
    }

    // Single experiment mode
    if (args.spawn_clients) {
        auto pids = spawnClients(args.num_clients, args.base_port, client_bin);
        RunResult r = runProtocol(args);
        waitForAll(pids);

        std::cout << "\n=== Network Protocol Results ===" << std::endl;
        printSummary(r);

        if (!args.csv_file.empty()) {
            std::ofstream ofs(args.csv_file);
            writeCSV(ofs, {r});
            std::cout << "CSV written to: " << args.csv_file << std::endl;
        }
        return 0;
    }

    // Original mode: evaluator only, clients launched externally
    RunResult r = runProtocol(args);
    std::cout << "\n=== Network Protocol Results ===" << std::endl;
    printSummary(r);

    if (!args.csv_file.empty()) {
        std::ofstream ofs(args.csv_file);
        writeCSV(ofs, {r});
        std::cout << "CSV written to: " << args.csv_file << std::endl;
    }
    return 0;
}
