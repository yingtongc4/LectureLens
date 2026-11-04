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

// One ONNX model on some inference runtime. Pre/post-processing (OCR, the
// embedder) only sees this interface, so the same code runs on every backend.
// Implementations are safe for concurrent run() calls.
class Model {
public:
    virtual ~Model() = default;

    // Single float input -> first output (the OCR models).
    virtual TensorF run(const TensorF& input) const = 0;

    // Named int64 inputs -> the output called `output` (transformer encoders).
    virtual TensorF run(const std::vector<TensorI64>& inputs, const std::string& output) const = 0;

    virtual std::vector<std::string> input_names() const = 0;

    // Custom metadata stored in the model (e.g. the OCR character set).
    virtual std::optional<std::string> metadata(const std::string& key) const = 0;

    // Human-readable input/output names, shapes and metadata keys.
    virtual std::string describe() const = 0;

    virtual Backend backend() const = 0;
};

// cpu / cuda / tensorrt -> ONNX Runtime (with that execution provider);
// openvino -> the native OpenVINO runtime (needs LL_ENABLE_OPENVINO).
// threads <= 0 lets the runtime decide.
std::unique_ptr<Model> load_model(const std::filesystem::path& path, Backend backend, int threads = 0);

// $LL_MODELS_DIR, else ~/.local/share/lecturelens/models. Models live on the
// Linux filesystem: under WSL, reading them from /mnt/c is up to 7x slower.
std::filesystem::path default_models_dir();

}  // namespace ll
