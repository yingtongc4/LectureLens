#pragma once

#include <string>
#include <string_view>

namespace ll {

// Where a model runs. Cpu, Cuda and TensorRt are ONNX Runtime execution
// providers; OpenVino is the native OpenVINO runtime reading the same .onnx
// file. Pre/post-processing code is shared by all of them.
enum class Backend { Cpu, OpenVino, Cuda, TensorRt };

Backend parse_backend(std::string_view name);  // "cpu", "openvino", "cuda", "tensorrt"
std::string backend_name(Backend b);

}  // namespace ll
