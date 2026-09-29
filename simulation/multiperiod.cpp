#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../algorithms/kpse/kpse.h"
#include "../algorithms/unisketch/unisketch.h"

using Packet = std::pair<uint32_t, uint32_t>;
using FlowElements =
    std::unordered_map<uint32_t, std::unordered_set<uint32_t>>;

enum class Task { kKpse, kHscd };
enum class Direction { kIncrease, kDecrease };

struct Options {
    Task task = Task::kKpse;
    Direction direction = Direction::kIncrease;
    std::vector<std::string> period_inputs;
    int memory_kb = 2048;
    uint32_t seed = 1;
    uint32_t k = 0;
    uint32_t top_k = 0;
    uint32_t virtual_bitmap_bits = 5000;
    bool has_k = false;
    bool has_top_k = false;
    bool has_direction = false;
    bool has_seed = false;
    bool has_virtual_bitmap_bits = false;
    bool show_help = false;
};

struct PeriodData {
    std::vector<Packet> packets;
    FlowElements elements;
};

struct Change {
    uint32_t flow = 0;
    double earlier = 0.0;
    double later = 0.0;
    double delta = 0.0;
};

struct HscdMetrics {
    std::size_t actual = 0;
    std::size_t reported = 0;
    std::size_t true_positives = 0;
    double precision = 0.0;
    double recall = 0.0;
    double f1_score = 0.0;
};

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

std::string require_value(int argc, char** argv, int* index,
                          const std::string& option) {
    if (*index + 1 >= argc || std::string(argv[*index + 1]).rfind("--", 0) == 0) {
        throw std::runtime_error("Missing value for " + option);
    }
    ++(*index);
    return argv[*index];
}

void print_usage(const char* program) {
    std::cout
        << "Usage:\n"
        << "  " << program
        << " kpse --k K --period-input PATH [--period-input PATH ...]"
           " [--memory-kb KIB] [--virtual-bitmap-bits N]\n"
        << "  " << program
        << " hscd --top-k K --direction increase|decrease"
           " --period-input EARLIER --period-input LATER"
           " [--memory-kb KIB] [--seed N]\n\n"
        << "Each period input uses the same 'element_id flow_id' format as"
           " the single-period driver.\n";
}

Options parse_args(int argc, char** argv) {
    Options options;
    if (argc == 2 && std::string(argv[1]) == "--help") {
        options.show_help = true;
        return options;
    }
    if (argc < 2) {
        throw std::runtime_error("Missing task; expected kpse or hscd");
    }

    const std::string task = argv[1];
    if (task == "kpse") {
        options.task = Task::kKpse;
    } else if (task == "hscd") {
        options.task = Task::kHscd;
    } else {
        throw std::runtime_error("Unknown task: " + task);
    }

    for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help") {
            options.show_help = true;
        } else if (argument == "--period-input") {
            options.period_inputs.push_back(
                require_value(argc, argv, &index, argument));
        } else if (argument == "--memory-kb") {
            const uint64_t maximum_memory_kb =
                std::numeric_limits<uint32_t>::max() / (1024ULL * 8ULL);
            const uint64_t value = parse_decimal(
                require_value(argc, argv, &index, argument), "Memory",
                maximum_memory_kb);
            if (value == 0) {
                throw std::runtime_error("Memory must be a positive integer");
            }
            options.memory_kb = static_cast<int>(value);
        } else if (argument == "--seed") {
            options.seed = static_cast<uint32_t>(parse_decimal(
                require_value(argc, argv, &index, argument), "Seed",
                std::numeric_limits<uint32_t>::max()));
            options.has_seed = true;
        } else if (argument == "--k") {
            options.k = static_cast<uint32_t>(parse_decimal(
                require_value(argc, argv, &index, argument), "K", 255));
            options.has_k = true;
        } else if (argument == "--top-k") {
            options.top_k = static_cast<uint32_t>(parse_decimal(
                require_value(argc, argv, &index, argument), "Top-k",
                std::numeric_limits<uint32_t>::max()));
            options.has_top_k = true;
        } else if (argument == "--direction") {
            const std::string value = require_value(argc, argv, &index, argument);
            if (value == "increase") {
                options.direction = Direction::kIncrease;
            } else if (value == "decrease") {
                options.direction = Direction::kDecrease;
            } else {
                throw std::runtime_error(
                    "Direction must be increase or decrease");
            }
            options.has_direction = true;
        } else if (argument == "--virtual-bitmap-bits") {
            options.virtual_bitmap_bits = static_cast<uint32_t>(parse_decimal(
                require_value(argc, argv, &index, argument),
                "Virtual bitmap bits", std::numeric_limits<uint32_t>::max()));
            options.has_virtual_bitmap_bits = true;
        } else {
            throw std::runtime_error("Unknown argument: " + argument);
        }
    }

    if (options.show_help) {
        return options;
    }
    if (options.task == Task::kKpse) {
        if (!options.has_k || options.k == 0) {
            throw std::runtime_error("K must be a positive integer");
        }
        if (options.period_inputs.size() < 2) {
            throw std::runtime_error("KPSE requires at least two period inputs");
        }
        if (options.period_inputs.size() > 255) {
            throw std::runtime_error("KPSE supports at most 255 periods");
        }
        if (options.k > options.period_inputs.size()) {
            throw std::runtime_error(
                "K must not exceed the number of periods");
        }
        if (options.virtual_bitmap_bits < 2) {
            throw std::runtime_error(
                "Virtual bitmap bits must be at least 2");
        }
        if (options.has_top_k || options.has_direction || options.has_seed) {
            throw std::runtime_error("HSCD options are not valid for KPSE");
        }
    } else {
        if (options.period_inputs.size() != 2) {
            throw std::runtime_error(
                "HSCD requires exactly two period inputs");
        }
        if (!options.has_top_k || options.top_k == 0) {
            throw std::runtime_error("Top-k must be a positive integer");
        }
        if (!options.has_direction) {
            throw std::runtime_error(
                "HSCD requires --direction increase or decrease");
        }
        if (options.has_k || options.has_virtual_bitmap_bits) {
            throw std::runtime_error("KPSE options are not valid for HSCD");
        }
    }
    return options;
}

PeriodData read_period(const std::string& input_path) {
    std::ifstream input(input_path);
    if (!input.is_open()) {
        throw std::runtime_error("Cannot open input file: " + input_path);
    }

    PeriodData period;
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
                                     std::to_string(line_number) + " in " +
                                     input_path);
        }

        try {
            const uint32_t element = static_cast<uint32_t>(parse_decimal(
                element_token, "Input value",
                std::numeric_limits<uint32_t>::max()));
            const uint32_t flow = static_cast<uint32_t>(parse_decimal(
                flow_token, "Input value",
                std::numeric_limits<uint32_t>::max()));
            period.packets.push_back({flow, element});
            period.elements[flow].insert(element);
        } catch (const std::runtime_error&) {
            throw std::runtime_error("Invalid input at line " +
                                     std::to_string(line_number) + " in " +
                                     input_path);
        }
    }

    if (period.packets.empty()) {
        throw std::runtime_error("Input file is empty: " + input_path);
    }
    return period;
}

std::vector<uint32_t> generate_hash_seeds(uint32_t seed) {
    std::srand(seed);
    std::vector<uint32_t> seeds;
    std::unordered_set<uint32_t> unique;
    while (seeds.size() < 5) {
        const uint32_t candidate = static_cast<uint32_t>(std::rand());
        if (unique.insert(candidate).second) {
            seeds.push_back(candidate);
        }
    }
    return seeds;
}

std::unique_ptr<uniSketch> make_unisketch(int memory_kb,
                                         uint32_t* hash_seeds) {
    const double register_ratio = 7.0;
    const double bucket_ratio = 3.0;
    const int register_bits = 16;
    const int bucket_size = 4;
    const int levels = 4;
    const int virtual_registers = 128;
    const int registers = static_cast<int>(
        memory_kb * 1024.0 * 8 *
        (register_ratio / (register_ratio + bucket_ratio)) / register_bits);
    const int buckets = static_cast<int>(
        memory_kb * 1024.0 * 8 *
        (bucket_ratio / (register_ratio + bucket_ratio)) / bucket_size /
        (32 + 32));
    return std::make_unique<uniSketch>(registers, register_bits,
                                       virtual_registers, buckets, levels,
                                       bucket_size, hash_seeds);
}

void run_kpse(const Options& options,
              const std::vector<PeriodData>& periods) {
    const uint64_t physical_bits_64 =
        static_cast<uint64_t>(options.memory_kb) * 1024 * 8;
    if (physical_bits_64 > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("Memory value is too large for KPSE");
    }

    KPSE sketch(static_cast<uint8_t>(periods.size()),
                static_cast<uint8_t>(options.k),
                options.virtual_bitmap_bits,
                static_cast<uint32_t>(physical_bits_64));

    std::unordered_map<uint32_t,
                       std::unordered_map<uint32_t, uint32_t>>
        element_period_counts;
    for (std::size_t period_index = 0; period_index < periods.size();
         ++period_index) {
        const PeriodData& period = periods[period_index];
        for (const Packet& packet : period.packets) {
            sketch.insert(static_cast<uint32_t>(period_index), packet.first,
                          packet.second);
        }
        for (const auto& [flow, elements] : period.elements) {
            for (uint32_t element : elements) {
                ++element_period_counts[flow][element];
            }
        }
    }

    const std::size_t bytes =
        (static_cast<std::size_t>(options.virtual_bitmap_bits) + 7) / 8;
    double relative_error_sum = 0.0;
    double estimate_checksum = 0.0;
    std::size_t evaluated_flows = 0;
    for (const auto& [flow, element_counts] : element_period_counts) {
        uint32_t actual = 0;
        for (const auto& [element, count] : element_counts) {
            (void)element;
            if (count >= options.k) {
                ++actual;
            }
        }
        if (actual == 0) {
            continue;
        }

        std::vector<std::vector<uint8_t>> storage(
            periods.size(), std::vector<uint8_t>(bytes, 0));
        std::vector<uint8_t*> bitmaps;
        bitmaps.reserve(storage.size());
        for (std::vector<uint8_t>& bitmap : storage) {
            bitmaps.push_back(bitmap.data());
        }
        sketch.get_bitmaps(flow, bitmaps.data());
        CALC_KPSE calculator(static_cast<uint8_t>(periods.size()),
                             static_cast<uint8_t>(options.k),
                             options.virtual_bitmap_bits, bitmaps.data());
        calculator.sum_bitmaps();
        const double estimate = std::max(0.0, calculator.total_kps());
        relative_error_sum +=
            std::abs(static_cast<double>(actual) - estimate) / actual;
        estimate_checksum += estimate;
        ++evaluated_flows;
    }

    if (evaluated_flows == 0) {
        throw std::runtime_error(
            "No flow has a nonzero ground-truth K-persistent spread");
    }

    std::cout << "Task: kpse\n";
    std::cout << "Periods: " << periods.size() << '\n';
    std::cout << "K: " << options.k << '\n';
    std::cout << "Distinct flows: " << element_period_counts.size() << '\n';
    std::cout << "Evaluated flows: " << evaluated_flows << '\n';
    std::cout << "Memory per period: " << options.memory_kb << " KiB\n";
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Estimate checksum: " << estimate_checksum << '\n';
    std::cout << "KPSE MRE: "
              << relative_error_sum / static_cast<double>(evaluated_flows)
              << '\n';
}

std::vector<Change> rank_changes(
    const std::unordered_map<uint32_t, double>& earlier,
    const std::unordered_map<uint32_t, double>& later, Direction direction,
    uint32_t top_k) {
    std::unordered_set<uint32_t> flows;
    for (const auto& [flow, value] : earlier) {
        (void)value;
        flows.insert(flow);
    }
    for (const auto& [flow, value] : later) {
        (void)value;
        flows.insert(flow);
    }

    std::vector<Change> changes;
    for (uint32_t flow : flows) {
        const auto earlier_it = earlier.find(flow);
        const auto later_it = later.find(flow);
        const double earlier_value =
            earlier_it == earlier.end() ? 0.0 : earlier_it->second;
        const double later_value =
            later_it == later.end() ? 0.0 : later_it->second;
        const double delta = later_value - earlier_value;
        if ((direction == Direction::kIncrease && delta > 0.0) ||
            (direction == Direction::kDecrease && delta < 0.0)) {
            changes.push_back({flow, earlier_value, later_value, delta});
        }
    }

    std::sort(changes.begin(), changes.end(),
              [direction](const Change& lhs, const Change& rhs) {
                  if (lhs.delta != rhs.delta) {
                      return direction == Direction::kIncrease
                                 ? lhs.delta > rhs.delta
                                 : lhs.delta < rhs.delta;
                  }
                  return lhs.flow < rhs.flow;
              });
    if (changes.size() > top_k) {
        changes.resize(top_k);
    }
    return changes;
}

HscdMetrics compute_hscd_metrics(const std::vector<Change>& actual,
                                 const std::vector<Change>& reported) {
    std::unordered_set<uint32_t> actual_flows;
    for (const Change& change : actual) {
        actual_flows.insert(change.flow);
    }

    HscdMetrics metrics;
    metrics.actual = actual.size();
    metrics.reported = reported.size();
    for (const Change& change : reported) {
        if (actual_flows.find(change.flow) != actual_flows.end()) {
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

void run_hscd(const Options& options,
              const std::vector<PeriodData>& periods) {
    std::vector<uint32_t> hash_seeds = generate_hash_seeds(options.seed);
    std::unique_ptr<uniSketch> earlier_sketch =
        make_unisketch(options.memory_kb, hash_seeds.data());
    std::unique_ptr<uniSketch> later_sketch =
        make_unisketch(options.memory_kb, hash_seeds.data());

    for (const Packet& packet : periods[0].packets) {
        uint32_t flow = packet.first;
        uint32_t element = packet.second;
        earlier_sketch->insert(flow, element);
    }
    for (const Packet& packet : periods[1].packets) {
        uint32_t flow = packet.first;
        uint32_t element = packet.second;
        later_sketch->insert(flow, element);
    }

    std::unordered_map<uint32_t, double> actual_earlier;
    std::unordered_map<uint32_t, double> actual_later;
    std::unordered_map<uint32_t, double> estimated_earlier;
    std::unordered_map<uint32_t, double> estimated_later;
    for (const auto& [flow, elements] : periods[0].elements) {
        actual_earlier[flow] = static_cast<double>(elements.size());
        estimated_earlier[flow] = earlier_sketch->query_per_flow(flow);
    }
    for (const auto& [flow, elements] : periods[1].elements) {
        actual_later[flow] = static_cast<double>(elements.size());
        estimated_later[flow] = later_sketch->query_per_flow(flow);
    }

    const std::vector<Change> actual = rank_changes(
        actual_earlier, actual_later, options.direction, options.top_k);
    const std::vector<Change> reported = rank_changes(
        estimated_earlier, estimated_later, options.direction, options.top_k);
    const HscdMetrics metrics = compute_hscd_metrics(actual, reported);

    std::cout << "Task: hscd\n";
    std::cout << "Periods: 2\n";
    std::cout << "Direction: "
              << (options.direction == Direction::kIncrease ? "increase"
                                                            : "decrease")
              << '\n';
    std::cout << "Top-k: " << options.top_k << '\n';
    std::cout << "Memory per period: " << options.memory_kb << " KiB\n";
    std::cout << std::fixed << std::setprecision(6);
    for (std::size_t index = 0; index < reported.size(); ++index) {
        const Change& change = reported[index];
        std::cout << "Reported rank " << index + 1 << ": flow=" << change.flow
                  << " earlier=" << change.earlier
                  << " later=" << change.later
                  << " change=" << change.delta << '\n';
    }
    std::cout << "Actual heavy spread changers: " << metrics.actual << '\n';
    std::cout << "Reported heavy spread changers: " << metrics.reported << '\n';
    std::cout << "HSCD true positives: " << metrics.true_positives << '\n';
    std::cout << "HSCD false positives: "
              << metrics.reported - metrics.true_positives << '\n';
    std::cout << "HSCD false negatives: "
              << metrics.actual - metrics.true_positives << '\n';
    std::cout << "HSCD precision: " << metrics.precision << '\n';
    std::cout << "HSCD recall: " << metrics.recall << '\n';
    std::cout << "HSCD F1-score: " << metrics.f1_score << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_args(argc, argv);
        if (options.show_help) {
            print_usage(argv[0]);
            return 0;
        }

        std::vector<PeriodData> periods;
        periods.reserve(options.period_inputs.size());
        for (const std::string& input : options.period_inputs) {
            periods.push_back(read_period(input));
        }

        if (options.task == Task::kKpse) {
            run_kpse(options, periods);
        } else {
            run_hscd(options, periods);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
