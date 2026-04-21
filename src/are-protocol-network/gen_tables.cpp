// gen_tables.cpp — Precompute and save ARE lookup tables to disk.
//
// Usage:
//   ./gen_tables [output_dir]
//
// Generates:
//   lookup_12.bin — for StringOTARE(8,4) decoding (ell_A=12, ~1.6 MB, fast)
//   lookup_20.bin — for PermXOTARE(8,4)  decoding (ell_A=20, ~420 MB, slow)
//
// Run this once. All evaluator/client processes load the tables from disk.

#include "ot/rabin_ot_are.h"
#include "ot/string_ot_are.h"
#include "ot/permxor_are.h"
#include <iostream>
#include <string>
#include <chrono>

static void generateTable(int ell_A, const std::string& dir) {
    std::string path = dir + "/bin/lookup_" + std::to_string(ell_A) + ".bin";
    std::cout << "Generating lookup table for ell_A=" << ell_A
              << " (" << (1 << ell_A) << " entries)..." << std::endl;

    auto t0 = std::chrono::high_resolution_clock::now();

    RabinOTARE ot(128, ell_A);
    ot.Setup();
    ot.BuildLookupTable();

    auto t1 = std::chrono::high_resolution_clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();

    if (ot.SaveLookupTable(path))
        std::cout << "  Saved to " << path << " (" << secs << "s)" << std::endl;
    else
        std::cerr << "  FAILED to save " << path << std::endl;
}

int main(int argc, char** argv) {
    std::string dir = ".";
    if (argc > 1) dir = argv[1];

    generateTable(12, dir);  // StringOTARE(8,4): RabinOTARE(128, 8+4=12)
    generateTable(20, dir);  // PermXOTARE(8,4):  StringOTARE(16,4) → RabinOTARE(128, 16+4=20)

    std::cout << "Done. Tables saved to " << dir << "/" << std::endl;
    return 0;
}
