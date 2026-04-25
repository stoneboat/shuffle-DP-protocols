#pragma once

#include "rabin_ot_are.h"
#include <vector>
#include <random>

// ── Algorithm 8: 1-of-2 String OT ARE ────────────────────────────────────────
//
//  EncodeSender(m_0, m_1):
//    1. Pick random bit c ← {0,1}
//    2. Pad: s_c = m_c || 0^λ,  s_{1-c} = m_{1-c} || 0^λ
//    3. Instance 1: Rabin-OT sender with (b0=c,   s=s_c)
//       Instance 2: Rabin-OT sender with (b0=1-c, s=s_{1-c})
//    4. Output (enc1, enc2, c)
//
//  EncodeReceiver(b):
//    1. Rabin-OT receiver with b1=b for both instances
//
//  Decode:
//    1. Aggregate both pairs
//    2. Decode each — exactly one succeeds with λ trailing zeros
//    3. Strip the λ zeros to recover m_b
// ─────────────────────────────────────────────────────────────────────────────

struct SenderEncoding {
    EncodedData enc1;   // Rabin-OT sender enc: (b0=c,   s=m_c   || 0^λ)
    EncodedData enc2;   // Rabin-OT sender enc: (b0=1-c, s=m_{1-c} || 0^λ)
    int c;              // random bit (public)
};

struct ReceiverEncoding {
    EncodedData enc1;   // Rabin-OT receiver enc for b (first  instance)
    EncodedData enc2;   // Rabin-OT receiver enc for b (second instance)
};

class StringOTARE {
private:
    RabinOTARE rabin_ot;
    int msg_len;    // length of each message in bits
    int lambda;     // zero-padding length (security parameter)
    std::mt19937 rng;

public:
    // msg_len: length of m_0, m_1 in bits
    // lambda:  length of zero-padding appended to each message
    StringOTARE(int msg_len = 4, int lambda = 8)
        : rabin_ot(128, msg_len + lambda),
          msg_len(msg_len),
          lambda(lambda),
          rng(std::random_device{}()) {}

    void Setup(bool build_table = true) {
        rabin_ot.Setup();
        if (build_table) rabin_ot.BuildLookupTable();
    }

    // Load a precomputed lookup table from disk (call after Setup(false))
    bool LoadTable(const std::string& path) { return rabin_ot.LoadLookupTable(path); }
    bool SaveTable(const std::string& path) const { return rabin_ot.SaveLookupTable(path); }
    // Mmap-shared variants — kernel page cache shares one copy across all
    // client processes on the host instead of duplicating per-process.
    bool LoadTableMmap(const std::string& path) { return rabin_ot.LoadLookupTableMmap(path); }
    bool SaveTableMmap(const std::string& path) const { return rabin_ot.SaveLookupTableMmap(path); }
    void LoadOrBuild(const std::string& mmap_path, const std::string& legacy_path) {
        rabin_ot.LoadOrBuild(mmap_path, legacy_path);
    }

    int getMsgLen() const { return msg_len; }
    int getEllA() const { return rabin_ot.getEllA(); }

    // Sender encodes two messages m_0, m_1 ∈ {0,1}^msg_len.
    SenderEncoding EncodeSender(const std::vector<int>& m0,
                                const std::vector<int>& m1) {
        std::uniform_int_distribution<int> bit_dist(0, 1);
        int c = bit_dist(rng);

        const std::vector<int>& mc   = (c == 0) ? m0 : m1;
        const std::vector<int>& m1mc = (c == 0) ? m1 : m0;

        // s_c = m_c || 0^λ,  trailing lambda bits default to 0
        std::vector<int> s_c  (msg_len + lambda, 0);
        std::vector<int> s_1mc(msg_len + lambda, 0);
        for (int i = 0; i < msg_len; i++) {
            s_c[i]   = mc[i];
            s_1mc[i] = m1mc[i];
        }

        std::vector<int> in1 = {c};
        in1.insert(in1.end(), s_c.begin(), s_c.end());

        std::vector<int> in2 = {1 - c};
        in2.insert(in2.end(), s_1mc.begin(), s_1mc.end());

        return { rabin_ot.Encode(0, in1), rabin_ot.Encode(0, in2), c };
    }

    // Receiver encodes selection bit b ∈ {0,1}.
    ReceiverEncoding EncodeReceiver(int b) {
        return { rabin_ot.Encode(1, {b}), rabin_ot.Encode(1, {b}) };
    }

    // Aggregate and decode; returns m_b or {} on failure.
    std::vector<int> Decode(const SenderEncoding& se, const ReceiverEncoding& re) {
        EncodedData sum1, sum2;
        G1::add(sum1.part1, se.enc1.part1, re.enc1.part1);
        G2::add(sum1.part2, se.enc1.part2, re.enc1.part2);
        GT::mul(sum1.part3, se.enc1.part3, re.enc1.part3);

        G1::add(sum2.part1, se.enc2.part1, re.enc2.part1);
        G2::add(sum2.part2, se.enc2.part2, re.enc2.part2);
        GT::mul(sum2.part3, se.enc2.part3, re.enc2.part3);

        std::vector<int> y1 = rabin_ot.Decode(sum1);
        std::vector<int> y2 = rabin_ot.Decode(sum2);

        // Identify valid result by λ trailing zeros
        auto hasTrailingZeros = [&](const std::vector<int>& y) -> bool {
            if ((int)y.size() != msg_len + lambda) return false;
            for (int i = msg_len; i < msg_len + lambda; i++)
                if (y[i] != 0) return false;
            return true;
        };

        const std::vector<int>* valid = nullptr;
        if (!y1.empty() && hasTrailingZeros(y1)) valid = &y1;
        else if (!y2.empty() && hasTrailingZeros(y2)) valid = &y2;

        if (!valid) return {};
        return std::vector<int>(valid->begin(), valid->begin() + msg_len);
    }
};