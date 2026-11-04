#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "inference/backend.hpp"

namespace ll {

struct TensorF {
    std::vector<std::int64_t> shape;
    std::vector<float> data;
};

struct TensorI64 {
    std::string name;  // model input name
    std::vector<std::int64_t> shape;
    std::vector<std::int64_t> data;
};

// One ONNX model run through ONNX Runtime. Thread-safe for concurrent run
// calls (ORT sessions are).
class OrtModel {
public:
    OrtModel(const std::filesystem::path& model_path, Backend backend, int intra_op_threads = 0);
    ~OrtModel();
    OrtModel(const OrtModel&) = delete;
    OrtModel& operator=(const OrtModel&) = delete;

    // Single float input -> first output (the OCR models).
    TensorF run(const TensorF& input) const;

    // Named int64 inputs -> the output called `output` (transformer encoders).
    TensorF run(const std::vector<TensorI64>& inputs, const std::string& output) const;

    std::vector<std::string> input_names() const;

    // Custom metadata stored in the model (e.g. the OCR character set).
    std::optional<std::string> metadata(const std::string& key) const;

    // Human-readable input/output names, shapes and metadata keys.
    std::string describe() const;

    Backend backend() const { return backend_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Backend backend_;
};

}  // namespace ll
