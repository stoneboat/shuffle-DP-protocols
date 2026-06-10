#include "permxor_are.h"
#include <iostream>

static void printVec(const std::string& name, const std::vector<int>& v) {
    std::cout << name << "=[";
    for (size_t i = 0; i < v.size(); i++) std::cout << v[i] << (i+1 < v.size() ? "," : "");
    std::cout << "]";
}

int main() {
    std::cout << "=== Permute-XOR ARE (Algorithm 1) ===" << std::endl;

    PermXOTARE pxt(4);   // 4-bit wire labels, default lambda=8
    pxt.Setup();

    // f_pxt((s0,s1),(b,s0',s1')) = (s0' XOR s_b) || (s1' XOR s_{1-b})
    std::vector<int> s0  = {1, 0, 1, 1};   // garbler's wire label for "0"
    std::vector<int> s1  = {0, 1, 0, 0};   // garbler's wire label for "1"
    std::vector<int> s0p = {1, 1, 0, 0};   // receiver's mask for first half
    std::vector<int> s1p = {0, 0, 1, 1};   // receiver's mask for second half

    bool all_pass = true;
    for (int b : {0, 1}) {
        std::cout << "\n--- b=" << b << " ---" << std::endl;
        auto se = pxt.EncodeSender(s0, s1);
        auto re = pxt.EncodeReceiver(b, s0p, s1p);
        auto [fst, snd] = pxt.Decode(se, re);

        std::vector<int> exp_fst(4), exp_snd(4);
        const auto& sb  = (b == 0 ? s0 : s1);
        const auto& s1b = (b == 0 ? s1 : s0);
        for (int i = 0; i < 4; i++) { exp_fst[i] = s0p[i]^sb[i]; exp_snd[i] = s1p[i]^s1b[i]; }

        printVec("  got_fst ", fst); std::cout << std::endl;
        printVec("  exp_fst ", exp_fst); std::cout << std::endl;
        printVec("  got_snd ", snd); std::cout << std::endl;
        printVec("  exp_snd ", exp_snd); std::cout << std::endl;
        bool pass = (fst == exp_fst && snd == exp_snd);
        std::cout << "  PASS: " << (pass ? "YES" : "NO") << std::endl;
        if (!pass) all_pass = false;
    }

    std::cout << "\n=== " << (all_pass ? "ALL PASS" : "FAILURES DETECTED") << " ===" << std::endl;
    return all_pass ? 0 : 1;
}