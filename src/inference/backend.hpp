#pragma once

#include <string>
#include <string_view>

namespace ll {

// Where a model runs. All backends go through ONNX Runtime execution
// providers, so the same .onnx file and the same pre/post-processing code are
// used everywhere and only the provider changes.
enum class Backend { Cpu, OpenVino, Cuda, TensorRt };

Backend parse_backend(std::string_view name);  // "cpu", "openvino", "cuda", "tensorrt"
std::string backend_name(Backend b);

}  // namespace ll
