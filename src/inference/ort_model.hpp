#pragma once

#include <filesystem>
#include <memory>

#include "inference/model.hpp"

namespace ll {

// A model run through ONNX Runtime with the CPU, CUDA or TensorRT execution
// provider. ORT sessions are safe for concurrent Run calls.
class OrtModel final : public Model {
public:
    OrtModel(const std::filesystem::path& model_path, Backend backend, int intra_op_threads = 0);
    ~OrtModel() override;
    OrtModel(const OrtModel&) = delete;
    OrtModel& operator=(const OrtModel&) = delete;

    TensorF run(const TensorF& input) const override;
    TensorF run(const std::vector<TensorI64>& inputs, const std::string& output) const override;
    std::vector<std::string> input_names() const override;
    std::optional<std::string> metadata(const std::string& key) const override;
    std::string describe() const override;
    Backend backend() const override { return backend_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Backend backend_;
};

}  // namespace ll
