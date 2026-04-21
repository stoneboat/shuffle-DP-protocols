#pragma once

#ifndef MCLBN_FP_UNIT_SIZE
#define MCLBN_FP_UNIT_SIZE 4
#endif
#ifndef MCLBN_FR_UNIT_SIZE
#define MCLBN_FR_UNIT_SIZE 4
#endif

#include <mcl/bn.hpp>
#include <vector>
#include <unordered_map>
#include <string>
#include <iostream>
#include <fstream>

using namespace mcl::bn;

struct PublicParams {
    G1 g1;
    G2 g2;
    GT gT;
};

struct EncodedData {
    G1 part1;
    G2 part2;
    GT part3;
};

// Encode for party i.
//
// Party 0 holds (b0, s):
//   chi0 = b0 + 1
//   part1 = r0 * g1          ∈ G1
//   part2 = chi0*r0 * g2     ∈ G2
//   part3 = gT^(chi0*r0^2 + emb(s))  ∈ GT
//
// Party 1 holds b1:
//   chi1 = -(b1 + 1)
//   part1 = r1 * g1          ∈ G1
//   part2 = chi1*r1 * g2     ∈ G2
//   part3 = gT^(chi1*r1^2)   ∈ GT
//
// After summing, decode via:
//   Y3 / e(Y1, Y2) = gT^(emb(s) - (b0-b1)*r0*r1)


// which equals gT^emb(s) iff b0 == b1.

class RabinOTARE {
private:
    int ell_A;
    PublicParams pp;

    // Lookup table: maps serialized gT^ev → ev for all ev ∈ [1, 2^ell_A]
    // Built once during Setup(), makes Decode O(1) instead of O(2^ell_A).
    // Only contains values derived from public parameters — no security impact.
    std::unordered_map<std::string, int> decode_table;
    bool table_built = false;

    // emb(s) = bin(s) + 1, computed in Fr to handle arbitrary-length strings
    Fr emb(const std::vector<int>& s) {
        Fr v(0), two(2);
        for (int b : s) { Fr::mul(v, v, two); Fr::add(v, v, Fr(b)); }
        Fr::add(v, v, Fr(1));
        return v;
    }

public:
    RabinOTARE(int security_param = 128, int input_length = 4)
        : ell_A(input_length) {}

    void Setup() {
        initPairing(mcl::BN254);
        hashAndMapToG1(pp.g1, "generator g1", 12);
        hashAndMapToG2(pp.g2, "generator g2", 12);
        pairing(pp.gT, pp.g1, pp.g2);
    }

    // Build the decode lookup table: gT^ev for ev = 1..2^ell_A.
    // Call this once after Setup() before any Decode() calls.
    // Memory: ~400 bytes per entry (GT serialization + int + hash overhead).
    //   ell_A=12: ~1.6 MB (4096 entries)
    //   ell_A=16: ~26 MB  (65536 entries)
    //   ell_A=20: ~420 MB (1048576 entries)
    void BuildLookupTable() {
        if (table_built) return;

        int max_emb = 1 << ell_A;
        decode_table.reserve(max_emb);

        // Compute gT^1, gT^2, ..., gT^max_emb incrementally
        // using repeated multiplication: cur = cur * gT
        GT cur;
        cur.setOne();
        for (int ev = 1; ev <= max_emb; ev++) {
            GT::mul(cur, cur, pp.gT);  // cur = gT^ev
            decode_table[cur.getStr()] = ev;
        }

        table_built = true;
        std::cout << "[RabinOTARE] Lookup table built: " << max_emb
                  << " entries for ell_A=" << ell_A << std::endl;
    }

    const PublicParams& getParams() const { return pp; }
    int getEllA() const { return ell_A; }
    bool hasLookupTable() const { return table_built; }

    // Save lookup table to a binary file.
    // Format: [ell_A (4B)] [num_entries (4B)] [entry...]
    //   entry: [key_len (4B)] [key_str] [value (4B)]
    bool SaveLookupTable(const std::string& path) const {
        if (!table_built) return false;
        std::ofstream ofs(path, std::ios::binary);
        if (!ofs) return false;
        int n = (int)decode_table.size();
        ofs.write((const char*)&ell_A, 4);
        ofs.write((const char*)&n, 4);
        for (auto& [key, val] : decode_table) {
            int klen = (int)key.size();
            ofs.write((const char*)&klen, 4);
            ofs.write(key.data(), klen);
            ofs.write((const char*)&val, 4);
        }
        return ofs.good();
    }

    // Load lookup table from a binary file (must call Setup() first).
    bool LoadLookupTable(const std::string& path) {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) return false;
        int file_ell_A, n;
        ifs.read((char*)&file_ell_A, 4);
        ifs.read((char*)&n, 4);
        if (file_ell_A != ell_A) {
            std::cerr << "[RabinOTARE] Table file ell_A=" << file_ell_A
                      << " doesn't match expected " << ell_A << std::endl;
            return false;
        }
        decode_table.clear();
        decode_table.reserve(n);
        for (int i = 0; i < n; i++) {
            int klen;
            ifs.read((char*)&klen, 4);
            std::string key(klen, '\0');
            ifs.read(key.data(), klen);
            int val;
            ifs.read((char*)&val, 4);
            decode_table[key] = val;
        }
        table_built = true;
        std::cout << "[RabinOTARE] Lookup table loaded: " << n
                  << " entries for ell_A=" << ell_A
                  << " from " << path << std::endl;
        return ifs.good();
    }

    EncodedData Encode(int party_index, const std::vector<int>& x_input) {
        EncodedData enc;
        Fr r;
        r.setByCSPRNG();

        if (party_index == 0) {
            int b0 = x_input[0];
            std::vector<int> s_vec(x_input.begin() + 1, x_input.end());
            Fr s_emb = emb(s_vec);

            Fr chi(b0 + 1);
            Fr chi_r, r_sq, chi_r_sq, exponent;
            Fr::mul(chi_r,    chi, r);
            Fr::mul(r_sq,     r,   r);
            Fr::mul(chi_r_sq, chi, r_sq);
            Fr::add(exponent, chi_r_sq, s_emb);

            G1::mul(enc.part1, pp.g1, r);
            G2::mul(enc.part2, pp.g2, chi_r);
            GT::pow(enc.part3, pp.gT, exponent);

        } else {
            int b1 = x_input[0];

            Fr chi;
            Fr::neg(chi, Fr(b1 + 1));
            Fr chi_r, r_sq, chi_r_sq;
            Fr::mul(chi_r,    chi, r);
            Fr::mul(r_sq,     r,   r);
            Fr::mul(chi_r_sq, chi, r_sq);

            G1::mul(enc.part1, pp.g1, r);
            G2::mul(enc.part2, pp.g2, chi_r);
            GT::pow(enc.part3, pp.gT, chi_r_sq);
        }

        return enc;
    }

    // Decode: recover s from aggregated encoding.
    // If lookup table is built, uses O(1) hash lookup.
    // Otherwise falls back to brute-force O(2^ell_A) search.
    std::vector<int> Decode(const EncodedData& sum) {
        GT e_Y1_Y2, e_inv, P;
        pairing(e_Y1_Y2, sum.part1, sum.part2);
        GT::inv(e_inv, e_Y1_Y2);
        GT::mul(P, sum.part3, e_inv);
        // P = gT^emb(s) if bits matched, random GT element otherwise

        if (table_built) {
            // Lookup table decode: O(1)
            auto it = decode_table.find(P.getStr());
            if (it == decode_table.end()) return {};  // bits didn't match
            int ev = it->second;
            int bin_val = ev - 1;
            std::vector<int> s(ell_A);
            for (int j = ell_A - 1; j >= 0; j--) {
                s[j] = bin_val & 1;
                bin_val >>= 1;
            }
            return s;
        }

        // Brute-force fallback: O(2^ell_A) pairings
        int max_emb = 1 << ell_A;
        for (int ev = 1; ev <= max_emb; ev++) {
            G1 tmp; GT candidate;
            G1::mul(tmp, pp.g1, Fr(ev));
            pairing(candidate, tmp, pp.g2);
            if (candidate == P) {
                int bin_val = ev - 1;
                std::vector<int> s(ell_A);
                for (int j = ell_A - 1; j >= 0; j--) {
                    s[j] = bin_val & 1;
                    bin_val >>= 1;
                }
                return s;
            }
        }
        return {};
    }
};
