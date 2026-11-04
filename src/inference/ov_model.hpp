#pragma once

#include <filesystem>
#include <memory>

#include "inference/model.hpp"

namespace ll {

// A model compiled by the native OpenVINO runtime for the CPU device. The
// ONNX file is read directly by OpenVINO's ONNX frontend (no conversion step).
// Concurrent run() calls each borrow an infer request from a small pool.
class OvModel final : public Model {
public:
    OvModel(const std::filesystem::path& model_path, int threads = 0);
    ~OvModel() override;
    OvModel(const OvModel&) = delete;
    OvModel& operator=(const OvModel&) = delete;

    TensorF run(const TensorF& input) const override;
    TensorF run(const std::vector<TensorI64>& inputs, const std::string& output) const override;
    std::vector<std::string> input_names() const override;
    std::optional<std::string> metadata(const std::string& key) const override;
    std::string describe() const override;
    Backend backend() const override { return Backend::OpenVino; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ll
