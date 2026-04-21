// benchmark.cpp — Experiment runner for distributed GC+ARE
//
// Compares balanced vs unbalanced using AND-chain, SUM, bitonic sort,
// and brick sort circuits. Uses lookup table optimization for fast Decode.
//
// Usage:
//   ./benchmark                   # run all, CSV to stdout
//   ./benchmark results.csv       # run all, CSV to file
//   ./benchmark results.csv 2     # run only experiment set 2

#include "gc_protocol.h"
#include <fstream>
#include <cmath>
#include <random>

static void runOne(emp::BristolFormat* circ, int n, bool bal,
                   const std::string& exp_name, const std::string& circ_name,
                   std::vector<RunResult>& out, int bit_width = 0,
                   const std::vector<int>* custom_inputs = nullptr) {
    std::cout << "  N=" << n
              << "  gates=" << circ->num_gate
              << "  inputs=" << (circ->n1 + circ->n2)
              << "  " << (bal ? "balanced  " : "unbalanced")
              << " ..." << std::flush;

    auto r = runProtocol(circ, n, bal, exp_name, circ_name, bit_width,
                         /*verbose=*/false, custom_inputs);

    int bnd = 0, n_and = 0;
    for (auto& c : r.clients) { bnd += c.num_boundary_in; n_and += c.num_and_gates; }
    std::cout << "  correct=" << r.correct
              << "  avg_time=" << (r.avg_case_time() / 1e3) << "ms"
              << "  avg_bytes=" << (size_t)r.avg_case_bytes()
              << "  AND=" << n_and
              << "  boundary=" << bnd << std::endl;
    out.push_back(r);
}

int main(int argc, char** argv) {
    const char* csv_file = nullptr;
    int only_exp = 0;

    for (int i = 1; i < argc; i++) {
        char* end;
        long v = strtol(argv[i], &end, 10);
        if (*end == '\0' && v >= 1 && v <= 21)
            only_exp = (int)v;
        else
            csv_file = argv[i];
    }

    std::vector<RunResult> all_results;

    // ── Experiment 1: AND-chain, vary N ──────────────────────────────────────
    if (only_exp == 0 || only_exp == 1) {
        const int G = 20;
        auto* circ = buildLargeANDChain(G);
        std::string circ_name = "and-chain-" + std::to_string(G);

        std::cout << "=== Exp 1: AND-chain (" << G << " gates), vary N ===" << std::endl;
        for (int n : {2, 4, 8, 10}) {
            if (n > circ->n1 + circ->n2) break;
            for (bool bal : {true, false}) {
                std::string exp = "exp1_and_N" + std::to_string(n) +
                                  (bal ? "_bal" : "_unbal");
                runOne(circ, n, bal, exp, circ_name, all_results, 1);
            }
        }
        delete circ;
    }

    // ── Experiment 2: Sequential SUM circuit, vary N (8-bit inputs) ───────────
    if (only_exp == 0 || only_exp == 2) {
        const int K = 8;  // 8-bit inputs
        std::cout << "\n=== Exp 2: Sequential SUM (" << K << "-bit inputs), vary N ===" << std::endl;

        for (int n : {2, 4, 8, 10}) {
            auto* circ = buildSequentialSumCircuit(n, K);
            std::string circ_name = "seqsum-" + std::to_string(n) + "x" + std::to_string(K);
            std::cout << "  [" << circ_name << ": " << circ->num_gate << " gates, "
                      << (circ->n1 + circ->n2) << " inputs, "
                      << circ->n3 << " output bits]" << std::endl;

            for (bool bal : {true, false}) {
                std::string exp = "exp2_sum_N" + std::to_string(n) +
                                  (bal ? "_bal" : "_unbal");
                runOne(circ, n, bal, exp, circ_name, all_results, K);
            }
            delete circ;
        }
    }

    // ── Experiment 3: Sequential SUM, vary bit width (fixed N=4) ──────────────
    if (only_exp == 0 || only_exp == 3) {
        std::cout << "\n=== Exp 3: Sequential SUM (N=4), vary bit width ===" << std::endl;

        for (int k : {4, 8, 16, 32}) {
            auto* circ = buildSequentialSumCircuit(4, k);
            std::string circ_name = "seqsum-4x" + std::to_string(k);
            std::cout << "  [" << circ_name << ": " << circ->num_gate << " gates, "
                      << (circ->n1 + circ->n2) << " inputs, "
                      << circ->n3 << " output bits]" << std::endl;

            for (bool bal : {true, false}) {
                std::string exp = "exp3_sum_K" + std::to_string(k) +
                                  (bal ? "_bal" : "_unbal");
                runOne(circ, 4, bal, exp, circ_name, all_results, k);
            }
            delete circ;
        }
    }

    // ── Experiment 4: Large Sequential SUM (N=10, 16-bit), detailed breakdown ─
    if (only_exp == 0 || only_exp == 4) {
        const int N = 10, K = 16;
        auto* circ = buildSequentialSumCircuit(N, K);
        std::string circ_name = "seqsum-" + std::to_string(N) + "x" + std::to_string(K);

        std::cout << "\n=== Exp 4: Large Sequential SUM (" << circ_name << ": "
                  << circ->num_gate << " gates) ===" << std::endl;

        for (bool bal : {true, false}) {
            std::string exp = std::string("exp4_large_") + (bal ? "bal" : "unbal");
            auto r = runProtocol(circ, N, bal, exp, circ_name, K, /*verbose=*/false);
            std::cout << "\n  " << (bal ? "BALANCED" : "UNBALANCED") << ":" << std::endl;
            printSummary(r);
            all_results.push_back(r);
        }
        delete circ;
    }

    // ── Experiment 5: Scaling N with 16-bit Sequential SUM ─────────────────────
    if (only_exp == 0 || only_exp == 5) {
        const int K = 16;
        std::cout << "\n=== Exp 5: Sequential SUM-16bit, scaling N ===" << std::endl;

        for (int n : {2, 4, 6, 8, 10, 16, 20}) {
            auto* circ = buildSequentialSumCircuit(n, K);
            std::string circ_name = "seqsum-" + std::to_string(n) + "x" + std::to_string(K);
            std::cout << "  [" << circ_name << ": " << circ->num_gate << " gates]" << std::endl;

            for (bool bal : {true, false}) {
                std::string exp = "exp5_sum16_N" + std::to_string(n) +
                                  (bal ? "_bal" : "_unbal");
                runOne(circ, n, bal, exp, circ_name, all_results, K);
            }
            delete circ;
        }
    }

    // ── Experiment 6: Sorting, vary N (4-bit elements) ─────────────────────
    if (only_exp == 0 || only_exp == 6) {
        const int K = 4;
        std::cout << "\n=== Exp 6: Sorting (" << K << "-bit elements), vary N ===" << std::endl;

        for (int n : {2, 4, 8}) {
            auto* circ = buildSortingCircuit(n, K);
            std::string circ_name = "sort-" + std::to_string(n) + "x" + std::to_string(K);
            std::cout << "  [" << circ_name << ": " << circ->num_gate << " gates, "
                      << (circ->n1 + circ->n2) << " inputs, "
                      << circ->n3 << " output bits]" << std::endl;

            for (bool bal : {true, false}) {
                std::string exp = "exp6_sort_N" + std::to_string(n) +
                                  (bal ? "_bal" : "_unbal");
                runOne(circ, n, bal, exp, circ_name, all_results, K);
            }
            delete circ;
        }
    }

    // ── Experiment 7: Sorting, vary bit width (fixed N=4) ───────────────────
    if (only_exp == 0 || only_exp == 7) {
        const int N = 4;
        std::cout << "\n=== Exp 7: Sorting (N=" << N << "), vary bit width ===" << std::endl;

        for (int k : {2, 4, 8}) {
            auto* circ = buildSortingCircuit(N, k);
            std::string circ_name = "sort-" + std::to_string(N) + "x" + std::to_string(k);
            std::cout << "  [" << circ_name << ": " << circ->num_gate << " gates, "
                      << (circ->n1 + circ->n2) << " inputs, "
                      << circ->n3 << " output bits]" << std::endl;

            for (bool bal : {true, false}) {
                std::string exp = "exp7_sort_K" + std::to_string(k) +
                                  (bal ? "_bal" : "_unbal");
                runOne(circ, N, bal, exp, circ_name, all_results, k);
            }
            delete circ;
        }
    }

    // ── Experiment 8: Sorting, detailed breakdown (N=8, 4-bit) ──────────────
    if (only_exp == 0 || only_exp == 8) {
        const int N = 8, K = 4;
        auto* circ = buildSortingCircuit(N, K);
        std::string circ_name = "sort-" + std::to_string(N) + "x" + std::to_string(K);

        std::cout << "\n=== Exp 8: Sorting (" << circ_name << ": "
                  << circ->num_gate << " gates) ===" << std::endl;

        for (bool bal : {true, false}) {
            std::string exp = std::string("exp8_sort_") + (bal ? "bal" : "unbal");
            auto r = runProtocol(circ, N, bal, exp, circ_name, K, /*verbose=*/false);
            std::cout << "\n  " << (bal ? "BALANCED" : "UNBALANCED") << ":" << std::endl;
            printSummary(r);
            all_results.push_back(r);
        }
        delete circ;
    }

    // ── Experiment 9: Sequential sum, k=8, vary N ─────────────────────────
    // Sequential chain of adders: boundary wires ≈ constant per boundary.
    // Per-client cost comparison: balanced should win for larger N because
    // baseline client does garble(all) + OT-ARE(all inputs), while each
    // balanced client does garble(gates/N) + OT-ARE(inputs/N) + PXT-ARE(~k boundary).
    if (only_exp == 0 || only_exp == 9) {
        const int K = 8;
        std::cout << "\n=== Exp 9: Sequential SUM (" << K << "-bit), vary N ===" << std::endl;

        for (int n : {2, 4, 8, 16, 32}) {
            auto* circ = buildSequentialSumCircuit(n, K);
            std::string circ_name = "seqsum-" + std::to_string(n) + "x" + std::to_string(K);
            std::cout << "  [" << circ_name << ": " << circ->num_gate << " gates, "
                      << (circ->n1 + circ->n2) << " inputs, "
                      << circ->n3 << " output bits]" << std::endl;

            for (bool bal : {true, false}) {
                std::string exp = "exp9_seqsum_N" + std::to_string(n) +
                                  (bal ? "_bal" : "_unbal");
                runOne(circ, n, bal, exp, circ_name, all_results, K);
            }
            delete circ;
        }
    }

    // ── Experiment 10: Bitonic sorting, k=4, extended N range ─────────────────
    // Run with many more clients to find where balanced starts winning.
    if (only_exp == 0 || only_exp == 10) {
        const int K = 4;
        std::cout << "\n=== Exp 10: Bitonic Sort (" << K << "-bit), extended N ===" << std::endl;

        for (int n : {2, 4, 8, 16, 32}) {
            auto* circ = buildSortingCircuit(n, K);
            std::string circ_name = "bitonic-" + std::to_string(n) + "x" + std::to_string(K);
            std::cout << "  [" << circ_name << ": " << circ->num_gate << " gates, "
                      << (circ->n1 + circ->n2) << " inputs, "
                      << circ->n3 << " output bits]" << std::endl;

            for (bool bal : {true, false}) {
                std::string exp = "exp10_bitonic_N" + std::to_string(n) +
                                  (bal ? "_bal" : "_unbal");
                runOne(circ, n, bal, exp, circ_name, all_results, K);
            }
            delete circ;
        }
    }

    // ── Experiment 11: Tree SUM, k=8, vary N ──────────────────────────────
    // Balanced binary tree of ripple-carry adders: boundary wires scale with
    // tree width at each level, giving a different partitioning profile than
    // the sequential (left-fold) sum.
    if (only_exp == 0 || only_exp == 11) {
        const int K = 8;
        std::cout << "\n=== Exp 11: Tree SUM (" << K << "-bit), vary N ===" << std::endl;

        for (int n : {2, 4, 8, 16, 32}) {
            auto* circ = buildTreeSumCircuit(n, K);
            std::string circ_name = "treesum-" + std::to_string(n) + "x" + std::to_string(K);
            std::cout << "  [" << circ_name << ": " << circ->num_gate << " gates, "
                      << (circ->n1 + circ->n2) << " inputs, "
                      << circ->n3 << " output bits]" << std::endl;

            for (bool bal : {true, false}) {
                std::string exp = "exp11_treesum_N" + std::to_string(n) +
                                  (bal ? "_bal" : "_unbal");
                runOne(circ, n, bal, exp, circ_name, all_results, K);
            }
            delete circ;
        }
    }

    // ── Experiment 12: Gaussian Sum ─────────────────────────────────────────
    // Discrete Gaussian mechanism in the shuffle model:
    //   - Each client holds x_i in [0, Delta], Delta = 2^k - 1
    //   - Each client adds noise z_i ~ N(0, sigma_local^2) to their input
    //   - The noisy value (x_i + z_i) is clamped to [0, 2^k - 1] and encoded
    //     as k-bit unsigned, then fed into a sequential sum circuit.
    //   - Output sum has total noise ~ N(0, sigma_central^2)
    //   - sigma_central = Delta * sqrt(2 * ln(1.25/delta)) / epsilon
    //   - sigma_local   = sigma_central / sqrt(n)
    if (only_exp == 0 || only_exp == 12) {
        const int K = 8;
        const double epsilon = 1.0;
        const int max_val = (1 << K) - 1;  // 255 for k=8

        std::cout << "\n=== Exp 12: Gaussian Sum (" << K << "-bit), "
                  << "epsilon=" << epsilon << ", Delta=" << max_val
                  << ", vary N ===" << std::endl;

        std::mt19937 noise_rng(123);

        for (int n : {2, 4, 8, 16, 32}) {
            const double delta = 1.0 / ((double)n * n);
            const double sigma_central = max_val * std::sqrt(2.0 * std::log(1.25 / delta)) / epsilon;
            const double sigma_local   = sigma_central / std::sqrt((double)n);

            // Generate noisy inputs: each client's k-bit value = clamp(x_i + z_i)
            std::uniform_int_distribution<int> val_dist(0, max_val);
            std::normal_distribution<double> noise_dist(0.0, sigma_local);

            std::vector<int> input_bits(n * K);
            std::cout << "  inputs: ";
            for (int c = 0; c < n; c++) {
                int x_i = val_dist(noise_rng);
                double z_i = noise_dist(noise_rng);
                int noisy = std::clamp((int)std::round(x_i + z_i), 0, max_val);
                // Encode as k-bit unsigned (LSB first, matching circuit convention)
                for (int b = 0; b < K; b++)
                    input_bits[c * K + b] = (noisy >> b) & 1;
                if (c < 4 || c == n - 1)
                    std::cout << (c > 0 ? ", " : "") << x_i << "+"
                              << (int)std::round(z_i) << "=" << noisy;
                else if (c == 4)
                    std::cout << ", ...";
            }
            std::cout << std::endl;

            auto* circ = buildSequentialSumCircuit(n, K);
            std::string circ_name = "gausssum-" + std::to_string(n) + "x" + std::to_string(K);
            std::cout << "  [" << circ_name << ": " << circ->num_gate << " gates, "
                      << (circ->n1 + circ->n2) << " inputs, "
                      << circ->n3 << " output bits]"
                      << "  delta=1/" << (n*n)
                      << "  sigma_central=" << sigma_central
                      << "  sigma_local=" << sigma_local << std::endl;

            for (bool bal : {true, false}) {
                std::string exp = "exp12_gausssum_N" + std::to_string(n) +
                                  (bal ? "_bal" : "_unbal");
                runOne(circ, n, bal, exp, circ_name, all_results, K, &input_bits);
            }
            delete circ;
        }
    }

    // ── Experiment 13: Selection (Gumbel-max), d=8, noise_bits=8, vary N ───
    // DP selection via report-noisy-max: n voters each pick one of d choices
    // (one-hot). Noise per choice = standard Gumbel sampled in plaintext,
    // scaled by 1/ε, biased + clamped into unsigned NB-bit for in-circuit
    // unsigned comparison. Last input wire is constant 1 (required by circuit).
    if (only_exp == 0 || only_exp == 13) {
        const int D = 8, NB = 8;
        const double epsilon = 1.0;
        std::cout << "\n=== Exp 13: Selection (d=" << D << ", noise_bits=" << NB
                  << ", eps=" << epsilon << "), vary N ===" << std::endl;

        std::mt19937 rng(4242);
        std::uniform_real_distribution<double> u01(1e-12, 1.0);
        const int noise_mid = 1 << (NB - 1);
        const int noise_max = (1 << NB) - 1;
        auto gumbel = [&]() { return -std::log(-std::log(u01(rng))) / epsilon; };

        for (int n : {2, 4, 8, 16, 32}) {
            auto* circ = buildSelectionCircuit(n, D, NB);
            std::string circ_name = "select-" + std::to_string(n) + "x" + std::to_string(D)
                                    + "_nb" + std::to_string(NB);
            std::cout << "  [" << circ_name << ": " << circ->num_gate << " gates, "
                      << (circ->n1 + circ->n2) << " inputs, "
                      << circ->n3 << " output bits]" << std::endl;

            std::uniform_int_distribution<int> choice_dist(0, D - 1);
            std::vector<int> input_bits(n * D + D * NB + 1, 0);
            for (int i = 0; i < n; i++) {
                int pick = choice_dist(rng);
                input_bits[i * D + pick] = 1;
            }
            for (int j = 0; j < D; j++) {
                int g_u = std::clamp((int)std::round(gumbel() + noise_mid), 0, noise_max);
                for (int b = 0; b < NB; b++)
                    input_bits[n * D + j * NB + b] = (g_u >> b) & 1;
            }
            input_bits[n * D + D * NB] = 1;  // constant-1 wire

            for (bool bal : {true, false}) {
                std::string exp = "exp13_select_N" + std::to_string(n) +
                                  (bal ? "_bal" : "_unbal");
                runOne(circ, n, bal, exp, circ_name, all_results, NB, &input_bits);
            }
            delete circ;
        }
    }

    // ── Experiment 14: Selection, N=8, noise_bits=8, vary d ─────────────────
    if (only_exp == 0 || only_exp == 14) {
        const int N = 8, NB = 8;
        const double epsilon = 1.0;
        std::cout << "\n=== Exp 14: Selection (N=" << N << ", noise_bits=" << NB
                  << ", eps=" << epsilon << "), vary d ===" << std::endl;

        std::mt19937 rng(4343);
        std::uniform_real_distribution<double> u01(1e-12, 1.0);
        const int noise_mid = 1 << (NB - 1);
        const int noise_max = (1 << NB) - 1;
        auto gumbel = [&]() { return -std::log(-std::log(u01(rng))) / epsilon; };

        for (int d : {2, 4, 8, 16, 32}) {
            auto* circ = buildSelectionCircuit(N, d, NB);
            std::string circ_name = "select-" + std::to_string(N) + "x" + std::to_string(d)
                                    + "_nb" + std::to_string(NB);
            std::cout << "  [" << circ_name << ": " << circ->num_gate << " gates, "
                      << (circ->n1 + circ->n2) << " inputs, "
                      << circ->n3 << " output bits]" << std::endl;

            std::uniform_int_distribution<int> choice_dist(0, d - 1);
            std::vector<int> input_bits(N * d + d * NB + 1, 0);
            for (int i = 0; i < N; i++) {
                int pick = choice_dist(rng);
                input_bits[i * d + pick] = 1;
            }
            for (int j = 0; j < d; j++) {
                int g_u = std::clamp((int)std::round(gumbel() + noise_mid), 0, noise_max);
                for (int b = 0; b < NB; b++)
                    input_bits[N * d + j * NB + b] = (g_u >> b) & 1;
            }
            input_bits[N * d + d * NB] = 1;  // constant-1 wire

            for (bool bal : {true, false}) {
                std::string exp = "exp14_select_D" + std::to_string(d) +
                                  (bal ? "_bal" : "_unbal");
                runOne(circ, N, bal, exp, circ_name, all_results, NB, &input_bits);
            }
            delete circ;
        }
    }

    // ── Experiment 15: Tree Mechanism, T=N, D=2, k=8, vary N ──────────────
    // Fenwick tree for noisy prefix sums (continual observation model).
    // Inputs: T*D x-values + T*D noise values, each k bits.
    // T scales with N: each client contributes one time step.
    if (only_exp == 0 || only_exp == 15) {
        const int D = 2, K = 8;
        std::cout << "\n=== Exp 15: Tree Mechanism (T=N, D=" << D
                  << ", k=" << K << "), vary N ===" << std::endl;

        for (int n : {2, 4, 8, 16, 32}) {
            auto* circ = buildTreeMechanismCircuit(n, D, K);
            std::string circ_name = "treemech-T" + std::to_string(n) + "D"
                                    + std::to_string(D) + "k" + std::to_string(K);
            std::cout << "  [" << circ_name << ": " << circ->num_gate << " gates, "
                      << (circ->n1 + circ->n2) << " inputs, "
                      << circ->n3 << " output bits]" << std::endl;

            for (bool bal : {true, false}) {
                std::string exp = "exp15_treemech_N" + std::to_string(n) +
                                  (bal ? "_bal" : "_unbal");
                runOne(circ, n, bal, exp, circ_name, all_results, K);
            }
            delete circ;
        }
    }

    // ── Experiment 16: Tree Mechanism, N=8, D=2, k=8, vary T ────────────
    if (only_exp == 0 || only_exp == 16) {
        const int D = 2, K = 8, N = 8;
        std::cout << "\n=== Exp 16: Tree Mechanism (N=" << N << ", D=" << D
                  << ", k=" << K << "), vary T ===" << std::endl;

        for (int t : {2, 4, 8, 16}) {
            auto* circ = buildTreeMechanismCircuit(t, D, K);
            std::string circ_name = "treemech-T" + std::to_string(t) + "D"
                                    + std::to_string(D) + "k" + std::to_string(K);
            std::cout << "  [" << circ_name << ": " << circ->num_gate << " gates, "
                      << (circ->n1 + circ->n2) << " inputs, "
                      << circ->n3 << " output bits]" << std::endl;

            for (bool bal : {true, false}) {
                std::string exp = "exp16_treemech_T" + std::to_string(t) +
                                  (bal ? "_bal" : "_unbal");
                runOne(circ, N, bal, exp, circ_name, all_results, K);
            }
            delete circ;
        }
    }

    // ── Experiment 17: Linear Contextual Bandits, d=2, k=8, vary N (T=N) ──
    // Fenwick tree mechanism over D = d + d^2 = 6 dimensions.
    // Each client contributes one round of sufficient statistics.
    if (only_exp == 0 || only_exp == 17) {
        const int d = 2, K = 8;
        int D = d + d * d;  // = 6
        std::cout << "\n=== Exp 17: Linear Contextual Bandits (T=N, d=" << d
                  << ", D=" << D << ", k=" << K << "), vary N ===" << std::endl;

        for (int n : {2, 4, 8, 16}) {
            auto* circ = buildLinearContextualCircuit(n, d, K);
            std::string circ_name = "lcb-T" + std::to_string(n) + "d"
                                    + std::to_string(d) + "k" + std::to_string(K);
            std::cout << "  [" << circ_name << ": " << circ->num_gate << " gates, "
                      << (circ->n1 + circ->n2) << " inputs, "
                      << circ->n3 << " output bits]" << std::endl;

            for (bool bal : {true, false}) {
                std::string exp = "exp17_lcb_N" + std::to_string(n) +
                                  (bal ? "_bal" : "_unbal");
                runOne(circ, n, bal, exp, circ_name, all_results, K);
            }
            delete circ;
        }
    }

    // ── Experiment 18: Linear Contextual Bandits, N=8, k=8, vary d ─────────
    // Shows how cost scales with feature dimension d (D = d + d^2).
    if (only_exp == 0 || only_exp == 18) {
        const int N = 8, K = 8;
        std::cout << "\n=== Exp 18: Linear Contextual Bandits (N=" << N
                  << ", T=" << N << ", k=" << K << "), vary d ===" << std::endl;

        for (int d : {1, 2, 3, 4}) {
            int D = d + d * d;
            auto* circ = buildLinearContextualCircuit(N, d, K);
            std::string circ_name = "lcb-T" + std::to_string(N) + "d"
                                    + std::to_string(d) + "k" + std::to_string(K);
            std::cout << "  [" << circ_name << " (D=" << D << "): "
                      << circ->num_gate << " gates, "
                      << (circ->n1 + circ->n2) << " inputs, "
                      << circ->n3 << " output bits]" << std::endl;

            for (bool bal : {true, false}) {
                std::string exp = "exp18_lcb_d" + std::to_string(d) +
                                  (bal ? "_bal" : "_unbal");
                runOne(circ, N, bal, exp, circ_name, all_results, K);
            }
            delete circ;
        }
    }

    // ── Experiment 19: Linear Contextual Bandits, N=4, d=2, k=8, vary T ────
    // Decouples T from N: fixed number of clients, varying time horizon.
    if (only_exp == 0 || only_exp == 19) {
        const int N = 4, d = 2, K = 8;
        int D = d + d * d;
        std::cout << "\n=== Exp 19: Linear Contextual Bandits (N=" << N
                  << ", d=" << d << ", D=" << D << ", k=" << K
                  << "), vary T ===" << std::endl;

        for (int t : {2, 4, 8, 16}) {
            auto* circ = buildLinearContextualCircuit(t, d, K);
            std::string circ_name = "lcb-T" + std::to_string(t) + "d"
                                    + std::to_string(d) + "k" + std::to_string(K);
            std::cout << "  [" << circ_name << ": " << circ->num_gate << " gates, "
                      << (circ->n1 + circ->n2) << " inputs, "
                      << circ->n3 << " output bits]" << std::endl;

            for (bool bal : {true, false}) {
                std::string exp = "exp19_lcb_T" + std::to_string(t) +
                                  (bal ? "_bal" : "_unbal");
                runOne(circ, N, bal, exp, circ_name, all_results, K);
            }
            delete circ;
        }
    }

    // ── Experiment 20: Distinct Elements, k=4, noise_bits=8, vary N ─────────
    // Shuffle DP distinct-element counting: bitonic sort + adjacent-unique + popcount + Laplace noise.
    // User values uniform in [0, 2^K). Noise ~ Lap(1/ε), clamped to signed NB-bit
    // two's complement and written LSB-first into wires [N*K, N*K+NB).
    if (only_exp == 0 || only_exp == 20) {
        const int K = 4, NB = 8;
        const double epsilon = 1.0;
        std::cout << "\n=== Exp 20: Distinct Elements (k=" << K << ", noise_bits=" << NB
                  << ", eps=" << epsilon << "), vary N ===" << std::endl;

        std::mt19937 noise_rng(2026);
        const int max_val = (1 << K) - 1;
        const int noise_max = (1 << (NB - 1)) - 1;
        const int noise_min = -(1 << (NB - 1));

        for (int n : {2, 4, 8, 16, 32}) {
            auto* circ = buildDistinctElementsCircuit(n, K, NB);
            std::string circ_name = "distinct-N" + std::to_string(n) + "k" + std::to_string(K) + "_nb" + std::to_string(NB);
            std::cout << "  [" << circ_name << ": " << circ->num_gate << " gates, " << (circ->n1 + circ->n2) << " inputs, " << circ->n3 << " output bits]" << std::endl;

            std::uniform_int_distribution<int> val_dist(0, max_val);
            std::exponential_distribution<double> exp_dist(epsilon);
            std::uniform_int_distribution<int> sign_dist(0, 1);

            std::vector<int> input_bits(n * K + NB);
            std::cout << "  user values: ";
            for (int c = 0; c < n; c++) {
                int x_i = val_dist(noise_rng);
                for (int b = 0; b < K; b++)
                    input_bits[c * K + b] = (x_i >> b) & 1;
                if (c < 6 || c == n - 1) std::cout << (c > 0 ? "," : "") << x_i;
                else if (c == 6) std::cout << ",...";
            }
            double z_d = (sign_dist(noise_rng) ? 1.0 : -1.0) * exp_dist(noise_rng);
            int z = std::clamp((int)std::round(z_d), noise_min, noise_max);
            uint32_t z_u = (uint32_t)(z & ((1u << NB) - 1));
            for (int b = 0; b < NB; b++)
                input_bits[n * K + b] = (z_u >> b) & 1;
            std::cout << "   Laplace noise z=" << z << std::endl;

            for (bool bal : {true, false}) {
                std::string exp = "exp20_distinct_N" + std::to_string(n) + (bal ? "_bal" : "_unbal");
                runOne(circ, n, bal, exp, circ_name, all_results, K, &input_bits);
            }
            delete circ;
        }
    }

    // ── Experiment 21: Distinct Elements HISTOGRAM, k=4, noise_bits=8, vary N ─
    // Same task and noise as exp 20 but using the histogram circuit (no sort).
    // One extra input wire at [N*K + NB] is the constant-1 that in-circuit NOTs use.
    if (only_exp == 0 || only_exp == 21) {
        const int K = 4, NB = 8;
        const double epsilon = 1.0;
        std::cout << "\n=== Exp 21: Distinct Elements Histogram (k=" << K << ", noise_bits=" << NB
                  << ", eps=" << epsilon << "), vary N ===" << std::endl;

        std::mt19937 noise_rng(2026);
        const int max_val = (1 << K) - 1;
        const int noise_max = (1 << (NB - 1)) - 1;
        const int noise_min = -(1 << (NB - 1));

        for (int n : {2, 4, 8, 16, 32}) {
            auto* circ = buildDistinctElementsHistogramCircuit(n, K, NB);
            std::string circ_name = "distinct_hist-N" + std::to_string(n) + "k" + std::to_string(K) + "_nb" + std::to_string(NB);
            std::cout << "  [" << circ_name << ": " << circ->num_gate << " gates, "
                      << (circ->n1 + circ->n2) << " inputs, " << circ->n3 << " output bits]" << std::endl;

            std::uniform_int_distribution<int> val_dist(0, max_val);
            std::exponential_distribution<double> exp_dist(epsilon);
            std::uniform_int_distribution<int> sign_dist(0, 1);

            std::vector<int> input_bits(n * K + NB + 1);
            std::cout << "  user values: ";
            for (int c = 0; c < n; c++) {
                int x_i = val_dist(noise_rng);
                for (int b = 0; b < K; b++)
                    input_bits[c * K + b] = (x_i >> b) & 1;
                if (c < 6 || c == n - 1) std::cout << (c > 0 ? "," : "") << x_i;
                else if (c == 6) std::cout << ",...";
            }
            double z_d = (sign_dist(noise_rng) ? 1.0 : -1.0) * exp_dist(noise_rng);
            int z = std::clamp((int)std::round(z_d), noise_min, noise_max);
            uint32_t z_u = (uint32_t)(z & ((1u << NB) - 1));
            for (int b = 0; b < NB; b++)
                input_bits[n * K + b] = (z_u >> b) & 1;
            input_bits[n * K + NB] = 1; // constant-1 wire
            std::cout << "   Laplace noise z=" << z << std::endl;

            for (bool bal : {true, false}) {
                std::string exp = "exp21_distincthist_N" + std::to_string(n) + (bal ? "_bal" : "_unbal");
                runOne(circ, n, bal, exp, circ_name, all_results, K, &input_bits);
            }
            delete circ;
        }
    }

    // ── Output CSV ───────────────────────────────────────────────────────────
    if (csv_file) {
        std::ofstream ofs(csv_file);
        if (!ofs) {
            std::cerr << "Cannot open: " << csv_file << std::endl;
            return 1;
        }
        writeCSV(ofs, all_results);
        std::cout << "\nCSV written to: " << csv_file << std::endl;
    } else {
        std::cout << "\n=== CSV Output ===" << std::endl;
        writeCSV(std::cout, all_results);
    }

    int n_pass = 0, n_fail = 0;
    for (auto& r : all_results) {
        if (r.correct) n_pass++; else n_fail++;
    }

    std::cout << "\n=== " << (n_fail == 0 ? "ALL PASSED" : "FAILURES DETECTED")
              << " (" << n_pass << " pass, " << n_fail << " fail, "
              << all_results.size() << " total) ===" << std::endl;

    return n_fail == 0 ? 0 : 1;
}
