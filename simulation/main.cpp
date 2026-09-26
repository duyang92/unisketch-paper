#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../algorithms/m2d/m2d.h"
#include "../algorithms/rskt/rskt.h"
#include "../algorithms/spreadSketch/spreadsketch.h"
#include "../algorithms/unisketch/unisketch.h"
#include "../algorithms/vBitmap/vbitmap.h"

using Packet = std::pair<uint32_t, uint32_t>;
using Clock = std::chrono::steady_clock;

enum class Algorithm {
    kUniSketch,
    kVBitmapSs,
    kVBitmapSsRskt,
    kM2D,
    kAll,
};

struct Options {
    Algorithm algorithm = Algorithm::kUniSketch;
    std::string algorithm_name = "unisketch";
    std::string input_path = "data/00.txt";
    int memory_kb = 2048;
    uint32_t seed = 1;
    uint32_t ssd_threshold = 100;
    bool show_help = false;
};

using EstimateMap = std::unordered_map<uint32_t, double>;

struct SsdMetrics {
    std::size_t actual = 0;
    std::size_t reported = 0;
    std::size_t true_positives = 0;
    double precision = 0.0;
    double recall = 0.0;
    double f1_score = 0.0;
};

std::unordered_set<uint32_t> flow_set;
std::unordered_map<uint32_t, uint32_t> actual_spreads;
int data_size = 0;
int T_MEM = 2048;
uint32_t SSD_THRESHOLD = 100;
uint32_t* hash_seeds = nullptr;
Packet* data_arr = nullptr;

Options parse_args(int argc, char** argv);
void print_usage(const char* program);
void read_packet_data(const std::string& input_path);
void generate_hash_seeds(int len);
void run_algorithm(Algorithm algorithm);
void release_data();

namespace {

uint64_t parse_decimal(const std::string& value, const std::string& name,
                       uint64_t maximum) {
    if (value.empty() ||
        !std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return character >= '0' && character <= '9';
        })) {
        throw std::runtime_error(name + " must be a positive integer");
    }

    uint64_t parsed = 0;
    try {
        std::size_t consumed = 0;
        parsed = std::stoull(value, &consumed, 10);
        if (consumed != value.size()) {
            throw std::runtime_error(name + " must be a positive integer");
        }
    } catch (const std::invalid_argument&) {
        throw std::runtime_error(name + " must be a positive integer");
    } catch (const std::out_of_range&) {
        throw std::runtime_error(name + " value is out of range");
    }

    if (parsed > maximum) {
        throw std::runtime_error(name + " value is out of range");
    }
    return parsed;
}

Algorithm parse_algorithm(const std::string& value) {
    if (value == "unisketch") {
        return Algorithm::kUniSketch;
    }
    if (value == "vbitmap-ss") {
        return Algorithm::kVBitmapSs;
    }
    if (value == "vbitmap-ss-rskt") {
        return Algorithm::kVBitmapSsRskt;
    }
    if (value == "m2d") {
        return Algorithm::kM2D;
    }
    if (value == "all") {
        return Algorithm::kAll;
    }
    throw std::runtime_error("Unknown algorithm: " + value);
}

std::string require_value(int argc, char** argv, int* index,
                          const std::string& option) {
    if (*index + 1 >= argc || std::string(argv[*index + 1]).rfind("--", 0) == 0) {
        throw std::runtime_error("Missing value for " + option);
    }
    ++(*index);
    return argv[*index];
}

double elapsed_seconds(Clock::time_point start, Clock::time_point end) {
    const double seconds = std::chrono::duration<double>(end - start).count();
    return std::max(seconds, std::numeric_limits<double>::min());
}

double compute_mre(const EstimateMap& estimates) {
    double relative_error_sum = 0.0;
    for (const auto& [flow, actual_spread] : actual_spreads) {
        const auto estimate = estimates.find(flow);
        const double estimated_spread =
            estimate == estimates.end() ? 0.0 : estimate->second;
        relative_error_sum +=
            std::abs(static_cast<double>(actual_spread) - estimated_spread) /
            static_cast<double>(actual_spread);
    }
    return relative_error_sum / static_cast<double>(actual_spreads.size());
}

SsdMetrics compute_ssd_metrics(const EstimateMap& candidates) {
    std::unordered_set<uint32_t> actual_super_spreaders;
    for (const auto& [flow, actual_spread] : actual_spreads) {
        if (actual_spread >= SSD_THRESHOLD) {
            actual_super_spreaders.insert(flow);
        }
    }

    std::unordered_set<uint32_t> reported_super_spreaders;
    for (const auto& [flow, estimated_spread] : candidates) {
        if (flow != 0 && estimated_spread >= SSD_THRESHOLD) {
            reported_super_spreaders.insert(flow);
        }
    }

    SsdMetrics metrics;
    metrics.actual = actual_super_spreaders.size();
    metrics.reported = reported_super_spreaders.size();
    for (uint32_t flow : reported_super_spreaders) {
        if (actual_super_spreaders.find(flow) != actual_super_spreaders.end()) {
            ++metrics.true_positives;
        }
    }

    if (metrics.reported != 0) {
        metrics.precision = static_cast<double>(metrics.true_positives) /
                            static_cast<double>(metrics.reported);
    }
    if (metrics.actual != 0) {
        metrics.recall = static_cast<double>(metrics.true_positives) /
                         static_cast<double>(metrics.actual);
    }
    if (metrics.precision + metrics.recall != 0.0) {
        metrics.f1_score = 2.0 * metrics.precision * metrics.recall /
                           (metrics.precision + metrics.recall);
    }
    return metrics;
}

void print_run_header(const std::string& algorithm_name) {
    std::cout << "Algorithm: " << algorithm_name << '\n';
    std::cout << "Input records: " << data_size << '\n';
    std::cout << "Distinct flows: " << flow_set.size() << '\n';
    std::cout << "Memory: " << T_MEM << " KiB\n";
}

void print_results(double insert_seconds, double query_seconds,
                   double estimate_checksum, const EstimateMap& estimates,
                   const EstimateMap& ssd_candidates) {
    const double throughput = data_size / 1000000.0 / insert_seconds;
    const double query_nanoseconds =
        query_seconds * 1e9 / static_cast<double>(flow_set.size());

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Insert throughput: " << throughput << " Mpps\n";
    std::cout << "Per-flow query time: " << query_nanoseconds << " ns\n";
    std::cout << "Estimate checksum: " << estimate_checksum << '\n';

    const SsdMetrics ssd_metrics = compute_ssd_metrics(ssd_candidates);
    const std::size_t false_positives =
        ssd_metrics.reported - ssd_metrics.true_positives;
    const std::size_t false_negatives =
        ssd_metrics.actual - ssd_metrics.true_positives;
    std::cout << "PFSE MRE: " << compute_mre(estimates) << '\n';
    std::cout << "SSD threshold: " << SSD_THRESHOLD << '\n';
    std::cout << "Actual super-spreaders: " << ssd_metrics.actual << '\n';
    std::cout << "Reported super-spreaders: " << ssd_metrics.reported << '\n';
    std::cout << "SSD true positives: " << ssd_metrics.true_positives << '\n';
    std::cout << "SSD false positives: " << false_positives << '\n';
    std::cout << "SSD false negatives: " << false_negatives << '\n';
    std::cout << "SSD precision: " << ssd_metrics.precision << '\n';
    std::cout << "SSD recall: " << ssd_metrics.recall << '\n';
    std::cout << "SSD F1-score: " << ssd_metrics.f1_score << '\n';
}

void test_vbitmap_ss() {
    print_run_header("vbitmap-ss");

    const int bitmap_length = static_cast<int>(8.0 * 1024 * T_MEM / 2);
    const int virtual_bitmap_length = 5000;
    vBitmap bitmap(bitmap_length, virtual_bitmap_length, hash_seeds);

    const int counter_bits = 32;
    const int depth = 4;
    const int bitmap_bits = 79;
    const int components = 3;
    const int component_memory = 438;
    const int width = static_cast<int>(
        1.0 * T_MEM * 1024 * 8 / 2 / depth /
        (component_memory + counter_bits + 8));
    DetectorSS spread_sketch(depth, width, counter_bits, bitmap_bits,
                             components, component_memory);

    const auto insert_start = Clock::now();
    for (int index = 0; index < data_size; ++index) {
        bitmap.insert(data_arr[index].first, data_arr[index].second);
        spread_sketch.Update(data_arr[index].first, data_arr[index].second, 1);
    }
    const auto insert_end = Clock::now();

    double estimate_checksum = 0.0;
    EstimateMap estimates;
    estimates.reserve(flow_set.size());
    const auto query_start = Clock::now();
    const double sum_bits = bitmap.sum_bits();
    for (uint32_t flow : flow_set) {
        const double estimate = bitmap.query_per_flow(flow, sum_bits);
        estimates.emplace(flow, estimate);
        estimate_checksum += estimate;
    }
    const auto query_end = Clock::now();

    std::vector<std::pair<key_tp, val_tp>> detected_super_spreaders;
    spread_sketch.Query(SSD_THRESHOLD, detected_super_spreaders);
    EstimateMap ssd_candidates;
    for (const auto& [flow, estimate] : detected_super_spreaders) {
        ssd_candidates[flow] = estimate;
    }

    print_results(elapsed_seconds(insert_start, insert_end),
                  elapsed_seconds(query_start, query_end), estimate_checksum,
                  estimates, ssd_candidates);
}

void test_vbitmap_ss_rskt() {
    print_run_header("vbitmap-ss-rskt");

    int bitmap_length = static_cast<int>(8.0 * 1024 * T_MEM / 3);
    const int virtual_bitmap_length = 5000;
    vBitmap bitmap(bitmap_length, virtual_bitmap_length, hash_seeds);

    const int counter_bits = 32;
    const int depth = 4;
    const int bitmap_bits = 79;
    const int components = 3;
    const int component_memory = 438;
    const int width = static_cast<int>(
        1.0 * 1024 * 8 * T_MEM / 3 / depth /
        (component_memory + counter_bits + 8));
    DetectorSS spread_sketch(depth, width, counter_bits, bitmap_bits,
                             components, component_memory);

    bitmap_length = 128;
    const int hll_bits = 5;
    const int rskt_width = static_cast<int>(
        1.0 * 1024 * 8 * T_MEM / 3 / hll_bits / bitmap_length / 2);
    RSKT rskt(rskt_width, bitmap_length, hash_seeds);

    const auto insert_start = Clock::now();
    for (int index = 0; index < data_size; ++index) {
        bitmap.insert(data_arr[index].first, data_arr[index].second);
        spread_sketch.Update(data_arr[index].first, data_arr[index].second, 1);
        rskt.insert(data_arr[index].first, data_arr[index].second);
    }
    const auto insert_end = Clock::now();

    double estimate_checksum = 0.0;
    EstimateMap estimates;
    estimates.reserve(flow_set.size());
    const auto query_start = Clock::now();
    const double sum_bits = bitmap.sum_bits();
    for (uint32_t flow : flow_set) {
        const double estimate = bitmap.query_per_flow(flow, sum_bits);
        estimates.emplace(flow, estimate);
        estimate_checksum += estimate;
    }
    const auto query_end = Clock::now();

    std::vector<std::pair<key_tp, val_tp>> detected_super_spreaders;
    spread_sketch.Query(SSD_THRESHOLD, detected_super_spreaders);
    EstimateMap ssd_candidates;
    for (const auto& [flow, estimate] : detected_super_spreaders) {
        ssd_candidates[flow] = estimate;
    }

    print_results(elapsed_seconds(insert_start, insert_end),
                  elapsed_seconds(query_start, query_end), estimate_checksum,
                  estimates, ssd_candidates);
}

void test_m2d() {
    print_run_header("m2d");

    const int hash_table_memory = 132;
    const int levels = 3;
    const int heap_size = 100;
    const int registers = 128;
    const double remaining_bits =
        T_MEM * 8.0 * 1024 - hash_table_memory * 8.0 * 1024 -
        heap_size * 64.0 * (levels + 1);
    const double sampling_probability = 0.25;
    const int width = static_cast<int>(
        remaining_bits / (registers * 5 + 32 * 32) /
        (1 + sampling_probability + sampling_probability * sampling_probability));
    Packet dimensions[3] = {
        Packet(registers, std::max(2, width)),
        Packet(registers,
               std::max(2, static_cast<int>(width * sampling_probability))),
        Packet(registers,
               std::max(2, static_cast<int>(
                               width * sampling_probability * sampling_probability)))};
    M2D m2d(levels, dimensions, 3, heap_size, sampling_probability, hash_seeds);

    const auto insert_start = Clock::now();
    for (int index = 0; index < data_size; ++index) {
        m2d.insert(data_arr[index].first, data_arr[index].second);
    }
    const auto insert_end = Clock::now();

    double estimate_checksum = 0.0;
    EstimateMap estimates;
    estimates.reserve(flow_set.size());
    const auto query_start = Clock::now();
    for (uint32_t flow : flow_set) {
        const double estimate = m2d.query_per_flow(flow);
        estimates.emplace(flow, estimate);
        estimate_checksum += estimate;
    }
    const auto query_end = Clock::now();
    const EstimateMap ssd_candidates = m2d.query_top_k();

    print_results(elapsed_seconds(insert_start, insert_end),
                  elapsed_seconds(query_start, query_end), estimate_checksum,
                  estimates, ssd_candidates);
}

void test_unisketch() {
    print_run_header("unisketch");

    const double register_ratio = 7.0;
    const double bucket_ratio = 3.0;
    const int register_bits = 16;
    const int bucket_size = 4;
    const int levels = 4;
    const int virtual_registers = 128;
    const int registers = static_cast<int>(
        T_MEM * 1024.0 * 8 * (register_ratio / (register_ratio + bucket_ratio)) /
        register_bits);
    const int buckets = static_cast<int>(
        T_MEM * 1024.0 * 8 * (bucket_ratio / (register_ratio + bucket_ratio)) /
        bucket_size / (32 + 32));
    uniSketch sketch(registers, register_bits, virtual_registers, buckets,
                     levels, bucket_size, hash_seeds);

    const auto insert_start = Clock::now();
    for (int index = 0; index < data_size; ++index) {
        sketch.insert(data_arr[index].first, data_arr[index].second);
    }
    const auto insert_end = Clock::now();

    double estimate_checksum = 0.0;
    EstimateMap estimates;
    estimates.reserve(flow_set.size());
    const auto query_start = Clock::now();
    for (uint32_t flow : flow_set) {
        const double estimate = sketch.query_per_flow(flow);
        estimates.emplace(flow, estimate);
        estimate_checksum += estimate;
    }
    const auto query_end = Clock::now();
    const EstimateMap ssd_candidates = sketch.query_top_k();

    print_results(elapsed_seconds(insert_start, insert_end),
                  elapsed_seconds(query_start, query_end), estimate_checksum,
                  estimates, ssd_candidates);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_args(argc, argv);
        if (options.show_help) {
            print_usage(argv[0]);
            return 0;
        }

        T_MEM = options.memory_kb;
        SSD_THRESHOLD = options.ssd_threshold;
        std::srand(options.seed);
        generate_hash_seeds(5);
        read_packet_data(options.input_path);
        run_algorithm(options.algorithm);
        release_data();
        return 0;
    } catch (const std::exception& error) {
        release_data();
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}

Options parse_args(int argc, char** argv) {
    Options options;

    if (argc == 2 && argv[1][0] != '-') {
        const uint64_t memory = parse_decimal(
            argv[1], "Memory", static_cast<uint64_t>(std::numeric_limits<int>::max()));
        if (memory == 0) {
            throw std::runtime_error("Memory must be a positive integer");
        }
        options.memory_kb = static_cast<int>(memory);
        return options;
    }

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help") {
            options.show_help = true;
        } else if (argument == "--algorithm") {
            options.algorithm_name = require_value(argc, argv, &index, argument);
            options.algorithm = parse_algorithm(options.algorithm_name);
        } else if (argument == "--memory-kb") {
            const std::string value = require_value(argc, argv, &index, argument);
            const uint64_t memory = parse_decimal(
                value, "Memory",
                static_cast<uint64_t>(std::numeric_limits<int>::max()));
            if (memory == 0) {
                throw std::runtime_error("Memory must be a positive integer");
            }
            options.memory_kb = static_cast<int>(memory);
        } else if (argument == "--input") {
            options.input_path = require_value(argc, argv, &index, argument);
        } else if (argument == "--seed") {
            const std::string value = require_value(argc, argv, &index, argument);
            options.seed = static_cast<uint32_t>(parse_decimal(
                value, "Seed", std::numeric_limits<uint32_t>::max()));
        } else if (argument == "--ssd-threshold") {
            const std::string value = require_value(argc, argv, &index, argument);
            const uint64_t threshold = parse_decimal(
                value, "SSD threshold", std::numeric_limits<uint32_t>::max());
            if (threshold == 0) {
                throw std::runtime_error(
                    "SSD threshold must be a positive integer");
            }
            options.ssd_threshold = static_cast<uint32_t>(threshold);
        } else {
            throw std::runtime_error("Unknown argument: " + argument);
        }
    }

    return options;
}

void print_usage(const char* program) {
    std::cout
        << "Usage:\n"
        << "  " << program
        << " [--algorithm NAME] [--memory-kb KIB] [--input PATH] [--seed N]"
           " [--ssd-threshold N]\n"
        << "  " << program << " MEMORY_KIB\n\n"
        << "Algorithms:\n"
        << "  unisketch\n"
        << "  vbitmap-ss\n"
        << "  vbitmap-ss-rskt\n"
        << "  m2d\n"
        << "  all\n";
}

void read_packet_data(const std::string& input_path) {
    std::ifstream input(input_path);
    if (!input.is_open()) {
        throw std::runtime_error("Cannot open input file: " + input_path);
    }

    std::vector<Packet> data;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }

        std::istringstream row(line);
        std::string element_token;
        std::string flow_token;
        std::string extra_token;
        if (!(row >> element_token >> flow_token) || (row >> extra_token)) {
            throw std::runtime_error("Invalid input at line " +
                                     std::to_string(line_number));
        }

        try {
            const uint64_t element_id = parse_decimal(
                element_token, "Input value", std::numeric_limits<uint32_t>::max());
            const uint64_t flow_id = parse_decimal(
                flow_token, "Input value", std::numeric_limits<uint32_t>::max());
            data.push_back({static_cast<uint32_t>(flow_id),
                            static_cast<uint32_t>(element_id)});
            flow_set.insert(static_cast<uint32_t>(flow_id));
        } catch (const std::runtime_error&) {
            throw std::runtime_error("Invalid input at line " +
                                     std::to_string(line_number));
        }
    }

    if (data.empty()) {
        throw std::runtime_error("Input file is empty: " + input_path);
    }
    if (data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Input contains too many records");
    }

    std::vector<Packet> unique_pairs = data;
    std::sort(unique_pairs.begin(), unique_pairs.end());
    unique_pairs.erase(std::unique(unique_pairs.begin(), unique_pairs.end()),
                       unique_pairs.end());
    actual_spreads.reserve(flow_set.size());
    for (const Packet& packet : unique_pairs) {
        ++actual_spreads[packet.first];
    }

    data_size = static_cast<int>(data.size());
    data_arr = new Packet[data.size()];
    std::copy(data.begin(), data.end(), data_arr);
}

void generate_hash_seeds(int len) {
    hash_seeds = new uint32_t[len];
    std::unordered_set<uint32_t> unique_seeds;
    int count = 0;
    while (count < len) {
        const uint32_t candidate = static_cast<uint32_t>(std::rand());
        if (unique_seeds.insert(candidate).second) {
            hash_seeds[count++] = candidate;
        }
    }
}

void run_algorithm(Algorithm algorithm) {
    switch (algorithm) {
        case Algorithm::kUniSketch:
            test_unisketch();
            break;
        case Algorithm::kVBitmapSs:
            test_vbitmap_ss();
            break;
        case Algorithm::kVBitmapSsRskt:
            test_vbitmap_ss_rskt();
            break;
        case Algorithm::kM2D:
            test_m2d();
            break;
        case Algorithm::kAll:
            test_unisketch();
            test_vbitmap_ss();
            test_vbitmap_ss_rskt();
            test_m2d();
            break;
    }
}

void release_data() {
    delete[] data_arr;
    data_arr = nullptr;
    delete[] hash_seeds;
    hash_seeds = nullptr;
    flow_set.clear();
    actual_spreads.clear();
    data_size = 0;
}
