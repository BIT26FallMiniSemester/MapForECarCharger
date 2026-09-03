#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::array<int, 3> kHorizons{1, 6, 24};
constexpr std::size_t kFeatureCount = 11;

struct Record {
    std::int64_t timestamp_epoch{};
    int station_id{};
    int total_piles{};
    double capacity_kw{};
    double load_kw{};
};

struct Sample {
    std::int64_t timestamp_epoch{};
    std::array<double, kFeatureCount> features{};
    std::array<double, kHorizons.size()> targets{};
};

struct Model {
    std::array<double, kFeatureCount> means{};
    std::array<double, kFeatureCount> scales{};
    std::array<std::array<double, kFeatureCount>, kHorizons.size()> weights{};
    std::array<bool, kHorizons.size()> use_persistence{};
};

struct Scores {
    double model_mae{};
    double model_rmse{};
    double persistence_mae{};
    double persistence_rmse{};
};

struct Prediction {
    int station_id{};
    int horizon_hours{};
    std::int64_t predicted_for_epoch{};
    double load_kw{};
    int occupied_piles{};
    int available_piles{};
    double congestion_ratio{};
};

void ensure_parent(const fs::path& path) {
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
}

std::vector<std::string> split(const std::string& line, char delimiter = ',') {
    std::vector<std::string> values;
    std::stringstream stream(line);
    std::string value;
    while (std::getline(stream, value, delimiter)) {
        if (!value.empty() && value.back() == '\r') value.pop_back();
        values.push_back(value);
    }
    return values;
}

int positive_mod(std::int64_t value, int divisor) {
    const int result = static_cast<int>(value % divisor);
    return result < 0 ? result + divisor : result;
}

int hour_of_day(std::int64_t epoch_seconds) {
    return positive_mod(epoch_seconds / 3600, 24);
}

int day_of_week(std::int64_t epoch_seconds) {
    return positive_mod(epoch_seconds / 86400 + 4, 7);  // 1970-01-01 was Thursday.
}

void generate_data(const fs::path& output, int days, int stations, unsigned seed = 42) {
    if (days < 10 || stations < 1) {
        throw std::invalid_argument("days must be >= 10 and stations must be >= 1");
    }
    ensure_parent(output);
    std::ofstream file(output);
    if (!file) throw std::runtime_error("cannot open output CSV: " + output.string());

    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const std::int64_t end = static_cast<std::int64_t>(now) / 3600 * 3600;
    const std::int64_t start = end - static_cast<std::int64_t>(days * 24 - 1) * 3600;
    std::mt19937 random(seed);
    std::normal_distribution<double> noise(0.0, 4.0);

    file << "timestamp_epoch,station_id,total_piles,capacity_kw,load_kw,occupied_piles\n";
    file << std::fixed << std::setprecision(3);
    for (int station_id = 1; station_id <= stations; ++station_id) {
        const int total_piles = 16 + station_id * 4;
        const double station_factor = 0.85 + station_id * 0.12;
        for (int index = 0; index < days * 24; ++index) {
            const std::int64_t timestamp = start + static_cast<std::int64_t>(index) * 3600;
            const int hour = hour_of_day(timestamp);
            const bool weekend = day_of_week(timestamp) >= 5;
            const double morning = 32.0 * std::exp(-std::pow((hour - 8) / 2.6, 2));
            const double evening = 48.0 * std::exp(-std::pow((hour - 18) / 3.2, 2));
            double load = (12.0 + morning + evening) * station_factor * (weekend ? 0.88 : 1.0);
            load = std::clamp(load + noise(random), 0.0, total_piles * 7.0);
            const double capacity = total_piles * 7.0;
            const int occupied = std::min(total_piles, static_cast<int>(std::ceil(load / 7.0)));
            file << timestamp << ',' << station_id << ',' << total_piles << ',' << capacity << ',' << load << ','
                 << occupied << '\n';
        }
    }
}

std::vector<Record> read_csv(const fs::path& input) {
    std::ifstream file(input);
    if (!file) throw std::runtime_error("cannot open input CSV: " + input.string());
    std::string line;
    if (!std::getline(file, line)) throw std::runtime_error("input CSV is empty");
    const auto headers = split(line);
    const auto index_of = [&](const std::string& name) {
        const auto found = std::find(headers.begin(), headers.end(), name);
        if (found == headers.end()) throw std::runtime_error("missing required CSV column: " + name);
        return static_cast<std::size_t>(std::distance(headers.begin(), found));
    };
    const auto timestamp_index = index_of("timestamp_epoch");
    const auto station_index = index_of("station_id");
    const auto piles_index = index_of("total_piles");
    const auto capacity_index = index_of("capacity_kw");
    const auto load_index = index_of("load_kw");
    const auto required_size = std::max({timestamp_index, station_index, piles_index, capacity_index,
                                         load_index}) + 1;

    std::vector<Record> records;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        const auto values = split(line);
        if (values.size() < required_size) throw std::runtime_error("malformed CSV row: " + line);
        records.push_back({std::stoll(values[timestamp_index]), std::stoi(values[station_index]),
                           std::stoi(values[piles_index]), std::stod(values[capacity_index]),
                           std::stod(values[load_index])});
    }
    if (records.empty()) throw std::runtime_error("input CSV contains no data rows");
    std::sort(records.begin(), records.end(), [](const Record& left, const Record& right) {
        return std::tie(left.station_id, left.timestamp_epoch) <
               std::tie(right.station_id, right.timestamp_epoch);
    });
    return records;
}

std::array<double, kFeatureCount> make_features(const std::vector<Record>& rows,
                                                std::size_t index) {
    if (index < 168) throw std::invalid_argument("at least 168 prior hours are required");
    const auto& current = rows[index];
    const int hour = hour_of_day(current.timestamp_epoch);
    const int weekday = day_of_week(current.timestamp_epoch);
    double rolling_24 = 0.0;
    for (std::size_t offset = 1; offset <= 24; ++offset) {
        rolling_24 += rows[index - offset].load_kw;
    }
    return {1.0,
            std::sin(2.0 * kPi * hour / 24.0),
            std::cos(2.0 * kPi * hour / 24.0),
            std::sin(2.0 * kPi * weekday / 7.0),
            std::cos(2.0 * kPi * weekday / 7.0),
            weekday >= 5 ? 1.0 : 0.0,
            current.load_kw,
            rows[index - 1].load_kw,
            rows[index - 24].load_kw,
            rows[index - 168].load_kw,
            rolling_24 / 24.0};
}

std::map<int, std::vector<Record>> group_by_station(const std::vector<Record>& records) {
    std::map<int, std::vector<Record>> stations;
    for (const auto& record : records) stations[record.station_id].push_back(record);
    for (const auto& [station_id, rows] : stations) {
        for (std::size_t index = 0; index < rows.size(); ++index) {
            if (rows[index].total_piles <= 0) {
                throw std::runtime_error("station " + std::to_string(station_id) +
                                         " has non-positive total_piles");
            }
            if (rows[index].capacity_kw <= 0.0) {
                throw std::runtime_error("station " + std::to_string(station_id) +
                                         " has non-positive capacity_kw");
            }
            if (index && rows[index].timestamp_epoch - rows[index - 1].timestamp_epoch != 3600) {
                throw std::runtime_error("station " + std::to_string(station_id) +
                                         " has missing or duplicate hourly records");
            }
        }
    }
    return stations;
}

std::vector<Sample> make_samples(const std::vector<Record>& records) {
    std::vector<Sample> samples;
    for (const auto& [station_id, rows] : group_by_station(records)) {
        static_cast<void>(station_id);
        if (rows.size() <= static_cast<std::size_t>(168 + kHorizons.back())) {
            throw std::runtime_error("each station needs more than 192 hourly rows");
        }
        for (std::size_t index = 168; index + kHorizons.back() < rows.size(); ++index) {
            Sample sample;
            sample.timestamp_epoch = rows[index].timestamp_epoch;
            sample.features = make_features(rows, index);
            for (std::size_t horizon = 0; horizon < kHorizons.size(); ++horizon) {
                sample.targets[horizon] = rows[index + kHorizons[horizon]].load_kw;
            }
            samples.push_back(sample);
        }
    }
    std::sort(samples.begin(), samples.end(), [](const Sample& left, const Sample& right) {
        return left.timestamp_epoch < right.timestamp_epoch;
    });
    return samples;
}

std::array<double, kFeatureCount> solve(
    std::array<std::array<double, kFeatureCount + 1>, kFeatureCount> matrix) {
    for (std::size_t column = 0; column < kFeatureCount; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < kFeatureCount; ++row) {
            if (std::abs(matrix[row][column]) > std::abs(matrix[pivot][column])) pivot = row;
        }
        if (std::abs(matrix[pivot][column]) < 1e-12) {
            throw std::runtime_error("regression matrix is singular");
        }
        std::swap(matrix[column], matrix[pivot]);
        const double divisor = matrix[column][column];
        for (std::size_t value = column; value <= kFeatureCount; ++value) {
            matrix[column][value] /= divisor;
        }
        for (std::size_t row = 0; row < kFeatureCount; ++row) {
            if (row == column) continue;
            const double factor = matrix[row][column];
            for (std::size_t value = column; value <= kFeatureCount; ++value) {
                matrix[row][value] -= factor * matrix[column][value];
            }
        }
    }
    std::array<double, kFeatureCount> result{};
    for (std::size_t row = 0; row < kFeatureCount; ++row) result[row] = matrix[row][kFeatureCount];
    return result;
}

std::array<double, kFeatureCount> standardize(const Model& model,
                                               std::array<double, kFeatureCount> features) {
    for (std::size_t index = 1; index < kFeatureCount; ++index) {
        features[index] = (features[index] - model.means[index]) / model.scales[index];
    }
    return features;
}

double ridge_forecast(const Model& model, std::size_t horizon,
                      const std::array<double, kFeatureCount>& raw_features) {
    const auto features = standardize(model, raw_features);
    return std::inner_product(features.begin(), features.end(), model.weights[horizon].begin(), 0.0);
}

double forecast(const Model& model, std::size_t horizon,
                const std::array<double, kFeatureCount>& raw_features) {
    return model.use_persistence[horizon] ? raw_features[6]
                                          : ridge_forecast(model, horizon, raw_features);
}

std::array<Scores, kHorizons.size()> train_model(const fs::path& input, Model& model) {
    const auto samples = make_samples(read_csv(input));
    const std::int64_t train_end = samples[samples.size() * 7 / 10].timestamp_epoch;
    const std::int64_t validation_end = samples[samples.size() * 8 / 10].timestamp_epoch;
    std::vector<Sample> training;
    std::vector<Sample> validation;
    std::vector<Sample> testing;
    for (const auto& sample : samples) {
        if (sample.timestamp_epoch < train_end) {
            training.push_back(sample);
        } else if (sample.timestamp_epoch < validation_end) {
            validation.push_back(sample);
        } else {
            testing.push_back(sample);
        }
    }
    if (training.empty() || validation.empty() || testing.empty()) {
        throw std::runtime_error("not enough data for temporal train/validation/test split");
    }

    model.scales.fill(1.0);
    for (std::size_t feature = 1; feature < kFeatureCount; ++feature) {
        for (const auto& sample : training) model.means[feature] += sample.features[feature];
        model.means[feature] /= static_cast<double>(training.size());
        double variance = 0.0;
        for (const auto& sample : training) {
            variance += std::pow(sample.features[feature] - model.means[feature], 2);
        }
        model.scales[feature] = std::sqrt(variance / static_cast<double>(training.size()));
        if (model.scales[feature] < 1e-9) model.scales[feature] = 1.0;
    }

    for (std::size_t horizon = 0; horizon < kHorizons.size(); ++horizon) {
        std::array<std::array<double, kFeatureCount + 1>, kFeatureCount> matrix{};
        for (const auto& sample : training) {
            const auto features = standardize(model, sample.features);
            for (std::size_t row = 0; row < kFeatureCount; ++row) {
                for (std::size_t column = 0; column < kFeatureCount; ++column) {
                    matrix[row][column] += features[row] * features[column];
                }
                matrix[row][kFeatureCount] += features[row] * sample.targets[horizon];
            }
        }
        for (std::size_t feature = 1; feature < kFeatureCount; ++feature) {
            matrix[feature][feature] += 0.01;
        }
        model.weights[horizon] = solve(matrix);
    }

    const auto evaluate = [&](const std::vector<Sample>& rows, std::size_t horizon,
                              bool persistence) {
        double model_absolute = 0.0, model_squared = 0.0;
        double persistence_absolute = 0.0, persistence_squared = 0.0;
        for (const auto& sample : rows) {
            const double target = sample.targets[horizon];
            const double predicted = persistence ? sample.features[6]
                                                 : ridge_forecast(model, horizon, sample.features);
            const double model_error = predicted - target;
            const double persistence_error = sample.features[6] - target;
            model_absolute += std::abs(model_error);
            model_squared += model_error * model_error;
            persistence_absolute += std::abs(persistence_error);
            persistence_squared += persistence_error * persistence_error;
        }
        const double count = static_cast<double>(rows.size());
        return Scores{model_absolute / count, std::sqrt(model_squared / count),
                      persistence_absolute / count, std::sqrt(persistence_squared / count)};
    };

    std::array<Scores, kHorizons.size()> scores{};
    for (std::size_t horizon = 0; horizon < kHorizons.size(); ++horizon) {
        const auto validation_scores = evaluate(validation, horizon, false);
        model.use_persistence[horizon] = validation_scores.persistence_mae <= validation_scores.model_mae;
        scores[horizon] = evaluate(testing, horizon, model.use_persistence[horizon]);
    }
    return scores;
}

void save_model(const fs::path& output, const Model& model) {
    ensure_parent(output);
    std::ofstream file(output);
    if (!file) throw std::runtime_error("cannot open model output: " + output.string());
    file << "PKLOT_ML_V2\n" << kFeatureCount << '\n' << std::setprecision(17);
    for (const auto value : model.means) file << value << ' ';
    file << '\n';
    for (const auto value : model.scales) file << value << ' ';
    file << '\n';
    for (std::size_t horizon = 0; horizon < kHorizons.size(); ++horizon) {
        file << kHorizons[horizon] << ' ';
        for (const auto value : model.weights[horizon]) file << value << ' ';
        file << '\n';
    }
    for (const bool value : model.use_persistence) file << (value ? 1 : 0) << ' ';
    file << '\n';
}

Model load_model(const fs::path& input) {
    std::ifstream file(input);
    std::string signature;
    std::size_t feature_count = 0;
    if (!(file >> signature >> feature_count) || signature != "PKLOT_ML_V2" ||
        feature_count != kFeatureCount) {
        throw std::runtime_error("invalid or unsupported model file");
    }
    Model model;
    for (auto& value : model.means) file >> value;
    for (auto& value : model.scales) file >> value;
    for (std::size_t horizon = 0; horizon < kHorizons.size(); ++horizon) {
        int stored_horizon = 0;
        file >> stored_horizon;
        if (stored_horizon != kHorizons[horizon]) {
            throw std::runtime_error("model horizons do not match this executable");
        }
        for (auto& value : model.weights[horizon]) file >> value;
    }
    for (auto& value : model.use_persistence) {
        int stored_value = 0;
        file >> stored_value;
        if (stored_value != 0 && stored_value != 1) throw std::runtime_error("invalid forecast strategy");
        value = stored_value == 1;
    }
    if (!file) throw std::runtime_error("truncated model file");
    return model;
}

void write_metrics(const fs::path& output, const std::array<Scores, kHorizons.size()>& scores,
                   const Model& model) {
    ensure_parent(output);
    std::ofstream file(output);
    if (!file) throw std::runtime_error("cannot open metrics output");
    file << "{\n" << std::fixed << std::setprecision(4);
    for (std::size_t index = 0; index < scores.size(); ++index) {
        const auto& score = scores[index];
        file << "  \"" << kHorizons[index] << "\": {\"strategy\": \""
             << (model.use_persistence[index] ? "persistence" : "ridge")
             << "\", \"model_mae\": " << score.model_mae
             << ", \"model_rmse\": " << score.model_rmse
             << ", \"persistence_mae\": " << score.persistence_mae
             << ", \"persistence_rmse\": " << score.persistence_rmse << "}"
             << (index + 1 == scores.size() ? "\n" : ",\n");
    }
    file << "}\n";
}

std::vector<Prediction> make_predictions(const std::vector<Record>& records, const Model& model) {
    std::vector<Prediction> predictions;
    for (const auto& [station_id, rows] : group_by_station(records)) {
        if (rows.size() <= 168) {
            throw std::runtime_error("each station needs at least 169 hourly rows for prediction");
        }
        const auto features = make_features(rows, rows.size() - 1);
        const int total_piles = rows.back().total_piles;
        const double capacity_kw = rows.back().capacity_kw;
        const double kw_per_pile = capacity_kw / total_piles;
        for (std::size_t horizon = 0; horizon < kHorizons.size(); ++horizon) {
            const double load = std::clamp(forecast(model, horizon, features), 0.0, capacity_kw);
            const int occupied = std::min(total_piles, static_cast<int>(std::ceil(load / kw_per_pile)));
            predictions.push_back({station_id, kHorizons[horizon],
                                   rows.back().timestamp_epoch + kHorizons[horizon] * 3600,
                                   load, occupied,
                                   total_piles - occupied,
                                   static_cast<double>(occupied) / total_piles});
        }
    }
    return predictions;
}

void write_predictions(const fs::path& output, const std::vector<Prediction>& predictions) {
    ensure_parent(output);
    std::ofstream file(output);
    if (!file) throw std::runtime_error("cannot open prediction output");
    file << "{\n  \"generated_at_epoch\": "
         << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())
         << ",\n  \"predictions\": [\n" << std::fixed << std::setprecision(4);
    for (std::size_t index = 0; index < predictions.size(); ++index) {
        const auto& item = predictions[index];
        file << "    {\"station_id\": " << item.station_id
             << ", \"horizon_hours\": " << item.horizon_hours
             << ", \"predicted_for_epoch\": " << item.predicted_for_epoch
             << ", \"predicted_load_kw\": " << item.load_kw
             << ", \"predicted_occupied_piles\": " << item.occupied_piles
             << ", \"predicted_available_piles\": " << item.available_piles
             << ", \"congestion_ratio\": " << item.congestion_ratio << "}"
             << (index + 1 == predictions.size() ? "\n" : ",\n");
    }
    file << "  ],\n  \"recommended_station_ids\": [";
    std::vector<Prediction> recommendations;
    std::copy_if(predictions.begin(), predictions.end(), std::back_inserter(recommendations),
                 [](const Prediction& item) { return item.horizon_hours == 1; });
    std::sort(recommendations.begin(), recommendations.end(), [](const Prediction& left,
                                                                 const Prediction& right) {
        return std::tie(left.congestion_ratio, left.station_id) <
               std::tie(right.congestion_ratio, right.station_id);
    });
    for (std::size_t index = 0; index < recommendations.size(); ++index) {
        if (index) file << ", ";
        file << recommendations[index].station_id;
    }
    file << "]\n}\n";
}

void train_command(const fs::path& input, const fs::path& model_path,
                   const fs::path& metrics_path) {
    Model model;
    const auto scores = train_model(input, model);
    save_model(model_path, model);
    write_metrics(metrics_path, scores, model);
}

void predict_command(const fs::path& input, const fs::path& model_path,
                     const fs::path& output) {
    write_predictions(output,
                      make_predictions(read_csv(input), load_model(model_path)));
}

void demo() {
    const auto suffix = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() / ("pklot_ml_demo_" + std::to_string(suffix));
    fs::create_directories(root);
    try {
        const auto data = root / "history.csv";
        const auto model = root / "model.txt";
        const auto metrics = root / "metrics.json";
        const auto output = root / "predictions.json";
        generate_data(data, 45, 2);
        train_command(data, model, metrics);
        predict_command(data, model, output);
        const auto predictions = make_predictions(read_csv(data), load_model(model));
        assert(predictions.size() == 6);
        assert(fs::file_size(metrics) > 0 && fs::file_size(output) > 0);
        auto capped = load_model(model);
        capped.use_persistence.fill(false);
        for (auto& weights : capped.weights) {
            weights.fill(0.0);
            weights[0] = 100000.0;
        }
        assert(make_predictions(read_csv(data), capped).front().load_kw == 140.0);
        auto invalid = read_csv(data);
        invalid[169].timestamp_epoch += 3600;
        try {
            static_cast<void>(make_samples(invalid));
            assert(false);
        } catch (const std::runtime_error&) {
        }
        invalid = read_csv(data);
        invalid.front().total_piles = 0;
        try {
            static_cast<void>(make_predictions(invalid, load_model(model)));
            assert(false);
        } catch (const std::runtime_error&) {
        }
        invalid = read_csv(data);
        invalid.front().capacity_kw = 0.0;
        try {
            static_cast<void>(make_predictions(invalid, load_model(model)));
            assert(false);
        } catch (const std::runtime_error&) {
        }
        std::cout << "demo OK: 2 stations x 3 horizons\n";
    } catch (...) {
        fs::remove_all(root);
        throw;
    }
    fs::remove_all(root);
}

void usage() {
    std::cerr << "Usage:\n"
              << "  pklot_ml generate [csv] [days] [stations]\n"
              << "  pklot_ml train [csv] [model] [metrics]\n"
              << "  pklot_ml predict [csv] [model] [json]\n"
              << "  pklot_ml demo\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            usage();
            return 2;
        }
        const std::string command = argv[1];
        if (command == "generate") {
            generate_data(argc > 2 ? argv[2] : "data/history.csv",
                          argc > 3 ? std::stoi(argv[3]) : 90,
                          argc > 4 ? std::stoi(argv[4]) : 3);
        } else if (command == "train") {
            train_command(argc > 2 ? argv[2] : "data/history.csv",
                          argc > 3 ? argv[3] : "models/load_forecaster.txt",
                          argc > 4 ? argv[4] : "models/metrics.json");
        } else if (command == "predict") {
            predict_command(argc > 2 ? argv[2] : "data/history.csv",
                            argc > 3 ? argv[3] : "models/load_forecaster.txt",
                            argc > 4 ? argv[4] : "outputs/predictions.json");
        } else if (command == "demo") {
            demo();
        } else {
            usage();
            return 2;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}

