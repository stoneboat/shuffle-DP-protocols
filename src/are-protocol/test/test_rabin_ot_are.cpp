#include "rabin_ot_are.h"
#include <iostream>

int main() {
    std::cout << "=== Rabin-OT ARE Scheme ===" << std::endl;

    RabinOTARE scheme(128, 4);
    scheme.Setup();

    // Party 0: b0=1, s=[0,1,0,1]  →  emb(s) = bin([0,1,0,1]) + 1 = 6
    std::vector<int> party0_input = {1, 0, 1, 0, 1};
    // Party 1: b1=1  (matches b0, so decode should succeed)
    std::vector<int> party1_input = {1};

    std::cout << "=== Encoding ===" << std::endl;
    EncodedData enc0 = scheme.Encode(0, party0_input);
    EncodedData enc1 = scheme.Encode(1, party1_input);

    std::cout << "\n=== Aggregation ===" << std::endl;
    EncodedData sum;
    G1::add(sum.part1, enc0.part1, enc1.part1);
    G2::add(sum.part2, enc0.part2, enc1.part2);
    GT::mul(sum.part3, enc0.part3, enc1.part3);

    std::cout << "\n=== Decoding ===" << std::endl;
    auto result = scheme.Decode(sum);
    std::cout << "Decoded s: [";
    for (int i = 0; i < (int)result.size(); i++)
        std::cout << result[i] << (i+1 < (int)result.size() ? "," : "");
    std::cout << "]  (expected [0,1,0,1])" << std::endl;
    std::cout << "PASS: " << (result == std::vector<int>{0,1,0,1} ? "YES" : "NO") << std::endl;

    std::cout << "\n=== Mismatch test (b0=0, b1=1) ===" << std::endl;
    std::vector<int> p0_mismatch = {0, 0, 1, 0, 1};
    std::vector<int> p1_mismatch = {1};
    EncodedData e0m = scheme.Encode(0, p0_mismatch);
    EncodedData e1m = scheme.Encode(1, p1_mismatch);
    EncodedData sum_mismatch;
    G1::add(sum_mismatch.part1, e0m.part1, e1m.part1);
    G2::add(sum_mismatch.part2, e0m.part2, e1m.part2);
    GT::mul(sum_mismatch.part3, e0m.part3, e1m.part3);
    auto mismatch_result = scheme.Decode(sum_mismatch);
    std::cout << "Mismatch decode returned empty: "
              << (mismatch_result.empty() ? "YES (correct)" : "NO (unexpected)") << std::endl;

    return 0;
}