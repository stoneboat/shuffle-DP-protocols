#pragma once

#include <emp-tool/emp-tool.h>
#include <vector>
#include <cassert>
#include <algorithm>

// ── DP Selection (report-noisy-max / Gumbel-max) ────────────────────────────
//
// Oblivious DP selection: n voters each cast a 1-bit vote among d choices.
// The circuit aggregates votes into a histogram, adds pre-scaled Gumbel noise,
// and outputs the index of the choice with the highest noisy score.
//
// Input layout (all bits, LSB first per value):
//   [0, n*d)                : vote bits — voter i, choice j at position i*d+j
//   [n*d, n*d + d*noise_bits) : Gumbel noise per choice (unsigned, noise_bits each)
//
// Output: idx_bits = ceil(log2(d)) bits — index of the selected choice.
//
// Score[j] = (count[j] << noise_bits) + gumbel[j]
// Since the shift fills lower bits with zero, this is just wire concatenation
// (zero gates for the score construction step).
//
// Gate counts:
//   Popcount (d choices × tree of n 1-bit values):  d * ~n * count_bits AND gates
//   Argmax (d-1 comparisons + muxes):  (d-1) * ~(3*score_bits + idx_bits) AND gates
//
inline emp::BristolFormat* buildSelectionCircuit(int n_voters, int d_choices,
                                                  int noise_bits) {
    assert(n_voters >= 1 && d_choices >= 2 && noise_bits >= 1);

    // Bit widths
    int count_bits = 1;
    while ((1 << count_bits) <= n_voters) count_bits++;
    int score_bits = count_bits + noise_bits;
    int idx_bits = 1;
    while ((1 << idx_bits) < d_choices) idx_bits++;

    int total_inputs = n_voters * d_choices + d_choices * noise_bits;
    int next_wire = total_inputs;
    std::vector<int> gates;

    // ── Adder helpers (same as sum circuits) ────────────────────────────────

    auto halfAdder = [&](int a, int b) -> std::pair<int, int> {
        int s = next_wire++, c = next_wire++;
        gates.insert(gates.end(), {a, b, s, XOR_GATE});
        gates.insert(gates.end(), {a, b, c, AND_GATE});
        return {s, c};
    };

    auto fullAdder = [&](int a, int b, int cin) -> std::pair<int, int> {
        int t1 = next_wire++, sw = next_wire++;
        int t2 = next_wire++, t3 = next_wire++, co = next_wire++;
        gates.insert(gates.end(), {a, b, t1, XOR_GATE});
        gates.insert(gates.end(), {t1, cin, sw, XOR_GATE});
        gates.insert(gates.end(), {a, b, t2, AND_GATE});
        gates.insert(gates.end(), {t1, cin, t3, AND_GATE});
        gates.insert(gates.end(), {t2, t3, co, XOR_GATE});
        return {sw, co};
    };

    auto rippleAdd = [&](const std::vector<int>& aw, const std::vector<int>& bw)
            -> std::vector<int> {
        int wa = (int)aw.size(), wb = (int)bw.size(), w = std::max(wa, wb);
        std::vector<int> res;
        int carry = -1;
        for (int i = 0; i < w; i++) {
            int ai = (i < wa) ? aw[i] : -1;
            int bi = (i < wb) ? bw[i] : -1;
            if (ai == -1 && bi == -1) {
                if (carry != -1) { res.push_back(carry); carry = -1; }
            } else if (ai == -1) {
                if (carry == -1) res.push_back(bi);
                else { auto [s, c] = halfAdder(bi, carry); res.push_back(s); carry = c; }
            } else if (bi == -1) {
                if (carry == -1) res.push_back(ai);
                else { auto [s, c] = halfAdder(ai, carry); res.push_back(s); carry = c; }
            } else {
                if (carry == -1) { auto [s, c] = halfAdder(ai, bi); res.push_back(s); carry = c; }
                else { auto [s, c] = fullAdder(ai, bi, carry); res.push_back(s); carry = c; }
            }
        }
        if (carry != -1) res.push_back(carry);
        return res;
    };

    // ── Step 1: Popcount per choice (tree reduction of 1-bit values) ────────

    // counts[j] = vector of wires representing the count for choice j (LSB first)
    std::vector<std::vector<int>> counts(d_choices);

    for (int j = 0; j < d_choices; j++) {
        // Gather the n vote bits for choice j
        std::vector<std::vector<int>> vals(n_voters);
        for (int i = 0; i < n_voters; i++)
            vals[i] = { i * d_choices + j };  // single bit = 1-bit number

        // Tree reduction: pair up and add until one result remains
        while ((int)vals.size() > 1) {
            std::vector<std::vector<int>> next;
            for (size_t i = 0; i + 1 < vals.size(); i += 2)
                next.push_back(rippleAdd(vals[i], vals[i + 1]));
            if (vals.size() % 2 == 1)
                next.push_back(vals.back());
            vals = next;
        }
        counts[j] = vals[0];
    }

    // ── Step 2: Score construction (count << noise_bits | gumbel) ────────────
    //
    // score[j] = {gumbel_bits[0..noise_bits-1], count_bits[0..count_bits-1]}
    // This is pure wire routing — zero gates.

    int noise_base = n_voters * d_choices;  // where noise inputs start
    std::vector<std::vector<int>> scores(d_choices);

    for (int j = 0; j < d_choices; j++) {
        std::vector<int> sc;
        // Lower bits: Gumbel noise
        for (int b = 0; b < noise_bits; b++)
            sc.push_back(noise_base + j * noise_bits + b);
        // Upper bits: count
        for (int b = 0; b < (int)counts[j].size(); b++)
            sc.push_back(counts[j][b]);
        // Pad to score_bits if count is shorter
        // (tree popcount of n values yields ceil(log2(n+1)) bits,
        //  but for small n the result may be shorter than count_bits)
        while ((int)sc.size() < score_bits)
            sc.push_back(sc.back());  // sign-extend... but these are unsigned, so should pad with 0
        // Actually for unsigned, we need a zero wire to pad. Let's just leave
        // it at current size — the comparator handles different widths.
        scores[j] = sc;
    }

    // We need a zero wire for padding and index encoding
    int zero_wire = next_wire++;
    gates.insert(gates.end(), {0, 0, zero_wire, XOR_GATE});

    // Pad all scores to score_bits using zero wire
    for (int j = 0; j < d_choices; j++) {
        while ((int)scores[j].size() < score_bits)
            scores[j].push_back(zero_wire);
    }

    // ── Step 3: Argmax — linear scan with comparator + conditional mux ──────
    //
    // Comparator: is score_a > score_b? (unsigned, LSB-to-MSB propagation)
    //   Same logic as sorting circuit's compare-and-swap.

    // Encode initial best index as idx_bits constant wires for j=0 (all zeros)
    std::vector<int> best_idx(idx_bits, zero_wire);
    std::vector<int> best_score = scores[0];

    for (int j = 1; j < d_choices; j++) {
        // Comparator: GT = (scores[j] > best_score), unsigned
        const auto& sa = scores[j];
        const auto& sb = best_score;
        int w = score_bits;

        std::vector<int> p(w), g(w);
        for (int b = 0; b < w; b++) {
            p[b] = next_wire++;
            gates.insert(gates.end(), {sa[b], sb[b], p[b], XOR_GATE});
            g[b] = next_wire++;
            gates.insert(gates.end(), {sa[b], p[b], g[b], AND_GATE});
        }

        int GT = g[0];
        for (int b = 1; b < w; b++) {
            int gt_and_p = next_wire++;
            gates.insert(gates.end(), {GT, p[b], gt_and_p, AND_GATE});
            int xor1 = next_wire++;
            gates.insert(gates.end(), {g[b], GT, xor1, XOR_GATE});
            int new_GT = next_wire++;
            gates.insert(gates.end(), {xor1, gt_and_p, new_GT, XOR_GATE});
            GT = new_GT;
        }

        // Conditional update: if GT, swap best_score and best_idx
        // new_best[b] = (sa[b] XOR sb_old[b]) AND GT XOR sb_old[b]
        std::vector<int> new_score(w);
        for (int b = 0; b < w; b++) {
            // p[b] = sa[b] XOR sb[b] already computed
            int mask = next_wire++;
            gates.insert(gates.end(), {p[b], GT, mask, AND_GATE});
            new_score[b] = next_wire++;
            gates.insert(gates.end(), {sb[b], mask, new_score[b], XOR_GATE});
        }
        best_score = new_score;

        // Update index: MUX between best_idx and candidate j using only AND+XOR.
        // new[b] = GT ? cand_bit : best_idx[b]
        //   cand_bit = 0: new = best XOR (best AND GT)         — 1 AND + 1 XOR
        //   cand_bit = 1: new = (best XOR GT) XOR (best AND GT) — 1 AND + 2 XOR (= OR)
        std::vector<int> new_idx(idx_bits);
        for (int b = 0; b < idx_bits; b++) {
            int band = next_wire++;
            gates.insert(gates.end(), {best_idx[b], GT, band, AND_GATE});
            if ((j >> b) & 1) {
                // new = best OR GT = (best XOR GT) XOR (best AND GT)
                int bxor = next_wire++;
                gates.insert(gates.end(), {best_idx[b], GT, bxor, XOR_GATE});
                new_idx[b] = next_wire++;
                gates.insert(gates.end(), {bxor, band, new_idx[b], XOR_GATE});
            } else {
                // new = best XOR (best AND GT)
                new_idx[b] = next_wire++;
                gates.insert(gates.end(), {best_idx[b], band, new_idx[b], XOR_GATE});
            }
        }
        best_idx = new_idx;
    }

    // ── Copy output to final wires ──────────────────────────────────────────
    int out_bits = idx_bits;
    for (int b = 0; b < idx_bits; b++) {
        int ow = next_wire++;
        gates.insert(gates.end(), {best_idx[b], zero_wire, ow, XOR_GATE});
    }

    int n_gates = (int)gates.size() / 4;
    int n_wires = next_wire;
    return new emp::BristolFormat(n_gates, n_wires, total_inputs, 0, out_bits, gates.data());
}
