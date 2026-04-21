#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <numeric>

struct ClientMetrics {
    int client_id          = 0;
    int num_gates          = 0;
    int num_and_gates      = 0;
    int num_input_wires    = 0;
    int num_boundary_in    = 0;
    int num_boundary_out   = 0;
    double garble_time_us  = 0;
    double ot_are_time_us  = 0;
    double boundary_time_us = 0;
    size_t garbled_table_bytes  = 0;
    size_t ot_are_bytes         = 0;
    size_t boundary_are_bytes   = 0;

    double total_time_us() const {
        return garble_time_us + ot_are_time_us + boundary_time_us;
    }
    size_t total_bytes() const {
        return garbled_table_bytes + ot_are_bytes + boundary_are_bytes;
    }
};

struct RunResult {
    std::string experiment;
    int n_clients;
    std::string circuit_name;
    int circuit_gates;
    int bit_width = 0;
    bool balanced;
    std::vector<ClientMetrics> clients;
    double eval_time_us   = 0;
    double total_time_us  = 0;
    bool correct          = false;

    double worst_case_time() const {
        double mx = 0;
        for (auto& c : clients) mx = std::max(mx, c.total_time_us());
        return mx;
    }
    size_t worst_case_bytes() const {
        size_t mx = 0;
        for (auto& c : clients) mx = std::max(mx, c.total_bytes());
        return mx;
    }
    double avg_case_time() const {
        if (clients.empty()) return 0;
        double sum = 0;
        for (auto& c : clients) sum += c.total_time_us();
        return sum / clients.size();
    }
    double avg_case_bytes() const {
        if (clients.empty()) return 0;
        double sum = 0;
        for (auto& c : clients) sum += (double)c.total_bytes();
        return sum / clients.size();
    }
    size_t total_communication() const {
        size_t s = 0;
        for (auto& c : clients) s += c.total_bytes();
        return s;
    }
};

// CSV header
static const char* CSV_HEADER =
    "experiment,n_clients,circuit,circuit_gates,bit_width,balanced,client_id,"
    "num_gates,num_and,num_inputs,num_boundary_in,num_boundary_out,"
    "garble_time_us,ot_are_time_us,boundary_time_us,total_time_us,"
    "garbled_table_bytes,ot_are_bytes,boundary_bytes,total_bytes";

static void writeCSVRow(std::ostream& out, const RunResult& r, const ClientMetrics& c) {
    out << r.experiment << ","
        << r.n_clients << ","
        << r.circuit_name << ","
        << r.circuit_gates << ","
        << r.bit_width << ","
        << (r.balanced ? 1 : 0) << ","
        << c.client_id << ","
        << c.num_gates << ","
        << c.num_and_gates << ","
        << c.num_input_wires << ","
        << c.num_boundary_in << ","
        << c.num_boundary_out << ","
        << c.garble_time_us << ","
        << c.ot_are_time_us << ","
        << c.boundary_time_us << ","
        << c.total_time_us() << ","
        << c.garbled_table_bytes << ","
        << c.ot_are_bytes << ","
        << c.boundary_are_bytes << ","
        << c.total_bytes()
        << "\n";
}

static void writeCSV(std::ostream& out, const std::vector<RunResult>& results) {
    out << CSV_HEADER << "\n";
    for (auto& r : results)
        for (auto& c : r.clients)
            writeCSVRow(out, r, c);
}

static void printSummary(const RunResult& r) {
    std::cout << "  " << r.experiment
              << "  N=" << r.n_clients
              << "  circuit=" << r.circuit_name
              << "  gates=" << r.circuit_gates
              << "  balanced=" << r.balanced
              << "  correct=" << r.correct << "\n";
    std::cout << "  avg_time=" << r.avg_case_time() << " us"
              << "  avg_bytes=" << (size_t)r.avg_case_bytes()
              << "  worst_time=" << r.worst_case_time() << " us"
              << "  worst_bytes=" << r.worst_case_bytes()
              << "  total_comm=" << r.total_communication()
              << "  eval_time=" << r.eval_time_us << " us\n";
    for (auto& c : r.clients) {
        std::cout << "    client[" << c.client_id << "]:"
                  << " gates=" << c.num_gates
                  << " AND=" << c.num_and_gates
                  << " inp=" << c.num_input_wires
                  << " bnd_in=" << c.num_boundary_in
                  << " bnd_out=" << c.num_boundary_out
                  << " garble=" << c.garble_time_us << "us"
                  << " ot=" << c.ot_are_time_us << "us"
                  << " bnd=" << c.boundary_time_us << "us"
                  << " total=" << c.total_time_us() << "us"
                  << " bytes=" << c.total_bytes() << "\n";
    }
}

// Simple RAII timer
class ScopedTimer {
    std::chrono::high_resolution_clock::time_point start_;
    double& target_us_;
public:
    ScopedTimer(double& target_us)
        : start_(std::chrono::high_resolution_clock::now()), target_us_(target_us) {}
    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        target_us_ += std::chrono::duration<double, std::micro>(end - start_).count();
    }
};
