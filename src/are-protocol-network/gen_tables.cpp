// gen_tables.cpp — Precompute and save ARE lookup tables to disk.
//
// Usage:
//   ./gen_tables [output_dir]
//
// Generates:
//   lookup_12.bin       — legacy heap-loaded format for StringOTARE(8,4)
//   lookup_12.mmap.bin  — flat sorted format, mmap-shared across processes
//   lookup_20.bin       — legacy heap-loaded format for PermXOTARE(8,4) (~420 MB heap × N!)
//   lookup_20.mmap.bin  — flat sorted format, mmap-shared (one copy regardless of N)
//
// Run this once. All evaluator/client processes prefer the .mmap.bin form and fall back to the legacy .bin if missing.

#include "ot/rabin_ot_are.h"
#include "ot/string_ot_are.h"
#include "ot/permxor_are.h"
#include <iostream>
#include <string>
#include <chrono>

static void generateTable(int ell_A, const std::string& dir) {
    std::string base      = dir + "/bin/lookup_" + std::to_string(ell_A);
    std::string legacy_path = base + ".bin";
    std::string mmap_path   = base + ".mmap.bin";
    std::cout << "Generating lookup table for ell_A=" << ell_A
              << " (" << (1 << ell_A) << " entries)..." << std::endl;

    auto t0 = std::chrono::high_resolution_clock::now();

    RabinOTARE ot(128, ell_A);
    ot.Setup();
    ot.BuildLookupTable();

    auto t1 = std::chrono::high_resolution_clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();

    if (ot.SaveLookupTable(legacy_path))
        std::cout << "  Saved (legacy) to " << legacy_path << " (" << secs << "s)" << std::endl;
    else
        std::cerr << "  FAILED to save " << legacy_path << std::endl;

    if (ot.SaveLookupTableMmap(mmap_path))
        std::cout << "  Saved (mmap)   to " << mmap_path   << std::endl;
    else
        std::cerr << "  FAILED to save " << mmap_path << std::endl;
}

int main(int argc, char** argv) {
    std::string dir = ".";
    if (argc > 1) dir = argv[1];

    generateTable(12, dir);  // StringOTARE(8,4): RabinOTARE(128, 8+4=12)
    generateTable(20, dir);  // PermXOTARE(8,4):  StringOTARE(16,4) → RabinOTARE(128, 16+4=20)

    std::cout << "Done. Tables saved to " << dir << "/" << std::endl;
    return 0;
}
