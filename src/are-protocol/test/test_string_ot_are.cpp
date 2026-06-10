#include "string_ot_are.h"
#include <iostream>

int main() {
    std::cout << "=== 1-of-2 String OT ARE ===" << std::endl;

    StringOTARE scheme(4, 8);   // 4-bit messages, 8-bit zero padding
    scheme.Setup();

    std::vector<int> m0 = {1, 0, 1, 1};
    std::vector<int> m1 = {0, 1, 1, 0};

    // ── Test b=0 ──────────────────────────────────────────────────────────────
    std::cout << "\n--- b=0 (should recover m_0=[1,0,1,1]) ---" << std::endl;
    SenderEncoding   se0 = scheme.EncodeSender(m0, m1);
    ReceiverEncoding re0 = scheme.EncodeReceiver(0);
    std::vector<int> out0 = scheme.Decode(se0, re0);

    // ── Test b=1 ──────────────────────────────────────────────────────────────
    std::cout << "\n--- b=1 (should recover m_1=[0,1,1,0]) ---" << std::endl;
    SenderEncoding   se1 = scheme.EncodeSender(m0, m1);
    ReceiverEncoding re1 = scheme.EncodeReceiver(1);
    std::vector<int> out1 = scheme.Decode(se1, re1);

    // ── Verify ────────────────────────────────────────────────────────────────
    std::cout << "\n=== Results ===" << std::endl;
    std::cout << "b=0 output matches m_0: " << (out0 == m0 ? "YES" : "NO") << std::endl;
    std::cout << "b=1 output matches m_1: " << (out1 == m1 ? "YES" : "NO") << std::endl;

    return 0;
}