#pragma once

#include "string_ot_are.h"
#include <cassert>

// ── Algorithm 1: Permute-XOR ARE Scheme (PXT-ARE) ────────────────────────────
//
// Computes f_pxt((s_0, s_1), (b, s_0', s_1')) = (s_0' XOR s_b) || (s_1' XOR s_{1-b})
// This is the "permute-then-XOR" operation used for boundary wire label transfer between garbling parties in a partitioned garbled circuit.
//
// Encoding:
//   Party 0 (sender, has wire label pair s_0, s_1):
//     1. r ← random {0,1}^{2*ell_A}
//     2. OT messages: m_0 = (s_0||s_1) XOR r,  m_1 = (s_1||s_0) XOR r
//     3. OT-ARE-Encode as sender with (m_0, m_1)
//     4. Output (r, ot_sender_enc)
//
//   Party 1 (receiver, has selection bit b and masks s_0', s_1'):
//     1. OT-ARE-Encode as receiver with b
//     2. Output (s_0'||s_1', ot_receiver_enc)
//
// Aggregation:
//   xor_part = r XOR (s_0'||s_1')
//   are_part = OT-ARE aggregate of both encodings
//
// Decode:
//   y_2  = OT-ARE-Decode(are_part) = (s_b||s_{1-b}) XOR r
//   y    = xor_part XOR y_2 = (r XOR s_0'||s_1') XOR ((s_b||s_{1-b}) XOR r) = (s_0' XOR s_b) || (s_1' XOR s_{1-b})
// ─────────────────────────────────────────────────────────────────────────────

struct PXTSenderEnc {
    std::vector<int> r;         // random mask, length 2*ell_A
    SenderEncoding ot_enc;      // String OT-ARE sender encoding (message length 2*ell_A)
};

struct PXTReceiverEnc {
    std::vector<int> s_concat;  // s_0'||s_1', length 2*ell_A
    ReceiverEncoding ot_enc;    // String OT-ARE receiver encoding
};

class PermXOTARE {
    StringOTARE ot;     // OT with message length 2*ell_A
    int ell_A;          // wire label length (outer)
    std::mt19937 rng;

    std::vector<int> xorVec(const std::vector<int>& a, const std::vector<int>& b) {
        std::vector<int> r(a.size());
        for (size_t i = 0; i < a.size(); i++) r[i] = a[i]^b[i];
        return r;
    }
    std::vector<int> concat(const std::vector<int>& a, const std::vector<int>& b) {
        std::vector<int> r = a; r.insert(r.end(), b.begin(), b.end()); return r;
    }
    std::vector<int> randBits(int n) {
        std::vector<int> v(n);
        std::uniform_int_distribution<int> d(0, 1);
        for (auto& x : v) x = d(rng);
        return v;
    }

public:
    // ell_A = wire label length; OT internally uses 2*ell_A bit messages
    // lambda: zero-padding length for String OT-ARE security
    PermXOTARE(int ell = 4, int lambda = 8)
        : ot(2*ell, lambda), ell_A(ell), rng(std::random_device{}()) {}

    void Setup(bool build_table = true) { ot.Setup(build_table); }
    bool LoadTable(const std::string& path) { return ot.LoadTable(path); }
    bool SaveTable(const std::string& path) const { return ot.SaveTable(path); }
    bool LoadTableMmap(const std::string& path) { return ot.LoadTableMmap(path); }
    bool SaveTableMmap(const std::string& path) const { return ot.SaveTableMmap(path); }
    void LoadOrBuild(const std::string& mmap_path, const std::string& legacy_path) {
        ot.LoadOrBuild(mmap_path, legacy_path);
    }
    int getEllA() const { return ot.getEllA(); }

    // Party 0 (garbler / boundary wire owner):
    //   inputs: s_0, s_1 ∈ {0,1}^ell_A  (the two wire labels)
    PXTSenderEnc EncodeSender(const std::vector<int>& s0, const std::vector<int>& s1) {
        assert((int)s0.size() == ell_A && (int)s1.size() == ell_A);
        auto r  = randBits(2*ell_A);
        auto m0 = xorVec(concat(s0, s1), r);   // (s_0||s_1) XOR r
        auto m1 = xorVec(concat(s1, s0), r);   // (s_1||s_0) XOR r  ← permuted
        return { r, ot.EncodeSender(m0, m1) };
    }

    // Party 1 (receiving garbler / evaluator at boundary):
    //   inputs: b ∈ {0,1}, s_0' ∈ {0,1}^ell_A, s_1' ∈ {0,1}^ell_A
    PXTReceiverEnc EncodeReceiver(int b, const std::vector<int>& s0p, const std::vector<int>& s1p) {
        assert((int)s0p.size() == ell_A && (int)s1p.size() == ell_A);
        return { concat(s0p, s1p), ot.EncodeReceiver(b) };
    }

    // Decode → (s_0' XOR s_b, s_1' XOR s_{1-b})
    std::pair<std::vector<int>, std::vector<int>>
    Decode(const PXTSenderEnc& se, const PXTReceiverEnc& re) {
        auto y1 = xorVec(se.r, re.s_concat);           // r XOR (s_0'||s_1')
        auto y2 = ot.Decode(se.ot_enc, re.ot_enc);     // (s_b||s_{1-b}) XOR r
        if (y2.empty()) {
            std::cerr << "[PXT] Decode failed: OT-ARE decode returned empty." << std::endl;
            return {{}, {}};
        }
        auto y = xorVec(y1, y2);    // (s_0' XOR s_b) || (s_1' XOR s_{1-b})
        return {
            std::vector<int>(y.begin(), y.begin() + ell_A), std::vector<int>(y.begin() + ell_A, y.end())
        };
    }
};