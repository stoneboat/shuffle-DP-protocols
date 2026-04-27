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
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

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

    // Mmap-shared sorted-array form of the table. When LoadLookupTableMmap()
    // succeeds, all client processes on the same host share one set of pages
    // for the lookup data via the kernel page cache (MAP_SHARED|PROT_READ),
    // instead of each duplicating the table on the heap. Critical for runs
    // with hundreds of clients on one node, where ell_A=20 (~420 MB) × N
    // would exhaust memory.
    void*         mmap_ptr_     = nullptr;
    size_t        mmap_len_     = 0;
    int           mmap_fd_      = -1;
    const uint8_t* mmap_entries_ = nullptr;  // points into mmap region
    int           mmap_n_       = 0;
    int           mmap_key_sz_  = 0;
    static constexpr uint32_t MMAP_MAGIC = 0x4C4D4150u; // "PMAL" little-endian

    // Fixed-size binary key derived from mcl's canonical GT serialization
    // (used only by the mmap path).
    static std::string serializeKey(const GT& g) {
        // GT in BN254 serializes to 384 bytes; use it as a fixed-size key.
        std::string s(512, '\0');
        size_t n = g.serialize(s.data(), s.size());
        s.resize(n);
        return s;
    }

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

    ~RabinOTARE() {
        if (mmap_ptr_) ::munmap(mmap_ptr_, mmap_len_);
        if (mmap_fd_ >= 0) ::close(mmap_fd_);
    }
    RabinOTARE(const RabinOTARE&) = delete;
    RabinOTARE& operator=(const RabinOTARE&) = delete;

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

    // Save the lookup table in a flat, sorted, mmap-shareable format.
    //
    //   Header (16 bytes):
    //     uint32 magic = MMAP_MAGIC ("PMAL")
    //     uint32 ell_A
    //     uint32 n_entries
    //     uint32 key_size           (binary GT serialization size — 384 in BN254)
    //   Body:
    //     n_entries × (key_size bytes : binary key | int32 value)
    //     entries are sorted lexicographically by key bytes (for binary search).
    //
    // Walks gT^ev incrementally rather than reusing decode_table, so the keys
    // are independent of how the in-heap table was keyed (legacy getStr).
    bool SaveLookupTableMmap(const std::string& path) const {
        int max_emb = 1 << ell_A;
        std::vector<std::pair<std::string, int>> entries;
        entries.reserve(max_emb);

        GT cur; cur.setOne();
        size_t key_sz = 0;
        for (int ev = 1; ev <= max_emb; ev++) {
            GT::mul(cur, cur, pp.gT);
            std::string k = serializeKey(cur);
            if (key_sz == 0) key_sz = k.size();
            else if (k.size() != key_sz) {
                std::cerr << "[RabinOTARE] non-uniform key size in mmap save ("
                          << k.size() << " vs " << key_sz << "), aborting" << std::endl;
                return false;
            }
            entries.emplace_back(std::move(k), ev);
        }
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        std::ofstream ofs(path, std::ios::binary);
        if (!ofs) return false;
        uint32_t hdr[4] = {
            MMAP_MAGIC, (uint32_t)ell_A,
            (uint32_t)entries.size(), (uint32_t)key_sz
        };
        ofs.write((const char*)hdr, sizeof(hdr));
        for (auto& [k, v] : entries) {
            ofs.write(k.data(), k.size());
            int32_t v32 = v;
            ofs.write((const char*)&v32, 4);
        }
        if (!ofs.good()) return false;
        std::cout << "[RabinOTARE] Lookup table saved (mmap format): "
                  << entries.size() << " entries, key_size=" << key_sz
                  << ", " << path << std::endl;
        return true;
    }

    // Memory-map the flat lookup file shared across all processes on the host.
    // The kernel keeps a single copy of the file's pages and serves them to
    // every process that mmaps with MAP_SHARED|PROT_READ — so adding more
    // clients no longer multiplies the lookup-table memory cost.
    bool LoadLookupTableMmap(const std::string& path, bool quiet = false) {
        int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) return false;
        struct stat st;
        if (::fstat(fd, &st) < 0 || st.st_size < 16) {
            ::close(fd); return false;
        }
        void* p = ::mmap(nullptr, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) { ::close(fd); return false; }

        const uint32_t* hdr = reinterpret_cast<const uint32_t*>(p);
        if (hdr[0] != MMAP_MAGIC || (int)hdr[1] != ell_A) {
            ::munmap(p, st.st_size); ::close(fd);
            std::cerr << "[RabinOTARE] mmap header mismatch (magic/ell_A) in "
                      << path << std::endl;
            return false;
        }
        size_t expected = 16 + (size_t)hdr[2] * (hdr[3] + 4);
        if ((size_t)st.st_size < expected) {
            ::munmap(p, st.st_size); ::close(fd);
            return false;
        }

        if (mmap_ptr_) ::munmap(mmap_ptr_, mmap_len_);
        if (mmap_fd_ >= 0) ::close(mmap_fd_);
        mmap_ptr_     = p;
        mmap_len_     = st.st_size;
        mmap_fd_      = fd;
        mmap_n_       = (int)hdr[2];
        mmap_key_sz_  = (int)hdr[3];
        mmap_entries_ = (const uint8_t*)p + 16;
        table_built   = true;

        // Advise the kernel: we want random access (binary search) and the
        // pages should stay resident across processes.
        ::madvise(p, st.st_size, MADV_RANDOM);

        if (!quiet) std::cout << "[RabinOTARE] Lookup table mmap'd shared: " << mmap_n_
                  << " entries for ell_A=" << ell_A
                  << " from " << path
                  << " (" << (st.st_size / (1024*1024)) << " MiB shared via page cache)" << std::endl;
        return true;
    }

    // Convenience: try mmap first (cheap, shared), then legacy heap load,
    // then build from scratch. Use this from client startup paths.
    void LoadOrBuild(const std::string& mmap_path,
                     const std::string& legacy_path) {
        if (LoadLookupTableMmap(mmap_path)) return;
        if (LoadLookupTable(legacy_path))   return;
        BuildLookupTable();
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

        if (mmap_entries_) {
            // Mmap-shared decode: binary search the sorted flat array.
            // O(log n) memcmp's; pages are shared across all client processes.
            std::string key = serializeKey(P);
            const int es = mmap_key_sz_ + 4;
            int lo = 0, hi = mmap_n_;
            while (lo < hi) {
                int mid = (lo + hi) >> 1;
                int cmp_len = std::min((int)key.size(), mmap_key_sz_);
                int c = std::memcmp(key.data(), mmap_entries_ + (size_t)mid * es, cmp_len);
                if (c == 0 && (int)key.size() != mmap_key_sz_)
                    c = (int)key.size() < mmap_key_sz_ ? -1 : 1;
                if (c == 0) {
                    int32_t ev;
                    std::memcpy(&ev, mmap_entries_ + (size_t)mid * es + mmap_key_sz_, 4);
                    int bin_val = ev - 1;
                    std::vector<int> s(ell_A);
                    for (int j = ell_A - 1; j >= 0; j--) {
                        s[j] = bin_val & 1; bin_val >>= 1;
                    }
                    return s;
                } else if (c < 0) hi = mid;
                else              lo = mid + 1;
            }
            return {};  // bits didn't match
        }

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
