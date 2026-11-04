#include "inference/ort_model.hpp"

#include <onnxruntime_cxx_api.h>

#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace ll {
namespace {

Ort::Env& ort_env() {
    // One environment per process, as ORT requires.
    static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "lecturelens");
    return env;
}

std::string shape_str(const std::vector<std::int64_t>& shape) {
    std::string s = "[";
    for (std::size_t i = 0; i < shape.size(); ++i) {
        if (i) s += ", ";
        s += shape[i] < 0 ? "?" : std::to_string(shape[i]);
    }
    return s + "]";
}

void add_execution_provider(Ort::SessionOptions& opts, Backend backend) {
    switch (backend) {
        case Backend::Cpu:
            return;
        case Backend::OpenVino: {
            std::unordered_map<std::string, std::string> ov{{"device_type", "CPU"}};
            opts.AppendExecutionProvider_OpenVINO_V2(ov);
            return;
        }
        case Backend::Cuda: {
            OrtCUDAProviderOptions cuda{};
            opts.AppendExecutionProvider_CUDA(cuda);
            return;
        }
        case Backend::TensorRt: {
            OrtTensorRTProviderOptions trt{};
            opts.AppendExecutionProvider_TensorRT(trt);
            return;
        }
    }
}

}  // namespace

Backend parse_backend(std::string_view name) {
    if (name == "cpu") return Backend::Cpu;
    if (name == "openvino") return Backend::OpenVino;
    if (name == "cuda") return Backend::Cuda;
    if (name == "tensorrt") return Backend::TensorRt;
    throw std::invalid_argument("unknown backend: " + std::string(name));
}

std::string backend_name(Backend b) {
    switch (b) {
        case Backend::Cpu: return "cpu";
        case Backend::OpenVino: return "openvino";
        case Backend::Cuda: return "cuda";
        case Backend::TensorRt: return "tensorrt";
    }
    return "unknown";
}

struct OrtModel::Impl {
    Ort::Session session{nullptr};
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;

    TensorF fetch(std::vector<Ort::Value>& values) const {
        auto info = values[0].GetTensorTypeAndShapeInfo();
        TensorF out;
        out.shape = info.GetShape();
        const float* p = values[0].GetTensorData<float>();
        out.data.assign(p, p + info.GetElementCount());
        return out;
    }
};

OrtModel::OrtModel(const std::filesystem::path& model_path, Backend backend, int intra_op_threads)
    : impl_(std::make_unique<Impl>()), backend_(backend) {
    if (!std::filesystem::exists(model_path)) {
        throw std::runtime_error("model not found: " + model_path.string());
    }
    Ort::SessionOptions opts;
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    if (intra_op_threads > 0) opts.SetIntraOpNumThreads(intra_op_threads);
    add_execution_provider(opts, backend);

    impl_->session = Ort::Session(ort_env(), model_path.c_str(), opts);
    Ort::AllocatorWithDefaultOptions alloc;
    for (std::size_t i = 0; i < impl_->session.GetInputCount(); ++i) {
        impl_->inputs.emplace_back(impl_->session.GetInputNameAllocated(i, alloc).get());
    }
    for (std::size_t i = 0; i < impl_->session.GetOutputCount(); ++i) {
        impl_->outputs.emplace_back(impl_->session.GetOutputNameAllocated(i, alloc).get());
    }
}

OrtModel::~OrtModel() = default;

TensorF OrtModel::run(const TensorF& input) const {
    if (impl_->inputs.size() != 1) throw std::logic_error("model has more than one input");
    const auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    // ORT takes a non-const pointer but does not write to input tensors.
    auto in = Ort::Value::CreateTensor<float>(mem, const_cast<float*>(input.data.data()), input.data.size(),
                                              input.shape.data(), input.shape.size());
    const char* in_names[] = {impl_->inputs[0].c_str()};
    const char* out_names[] = {impl_->outputs[0].c_str()};
    auto outputs = impl_->session.Run(Ort::RunOptions{nullptr}, in_names, &in, 1, out_names, 1);
    return impl_->fetch(outputs);
}

TensorF OrtModel::run(const std::vector<TensorI64>& inputs, const std::string& output) const {
    const auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::vector<Ort::Value> values;
    std::vector<const char*> names;
    for (const auto& t : inputs) {
        values.push_back(Ort::Value::CreateTensor<std::int64_t>(mem, const_cast<std::int64_t*>(t.data.data()),
                                                                t.data.size(), t.shape.data(), t.shape.size()));
        names.push_back(t.name.c_str());
    }
    const char* out_names[] = {output.c_str()};
    auto outputs = impl_->session.Run(Ort::RunOptions{nullptr}, names.data(), values.data(), values.size(),
                                      out_names, 1);
    return impl_->fetch(outputs);
}

std::vector<std::string> OrtModel::input_names() const { return impl_->inputs; }

std::optional<std::string> OrtModel::metadata(const std::string& key) const {
    Ort::AllocatorWithDefaultOptions alloc;
    const auto md = impl_->session.GetModelMetadata();
    auto value = md.LookupCustomMetadataMapAllocated(key.c_str(), alloc);
    if (!value) return std::nullopt;
    return std::string(value.get());
}

std::string OrtModel::describe() const {
    std::ostringstream os;
    const auto& s = impl_->session;
    os << "backend: " << backend_name(backend_) << '\n';
    for (std::size_t i = 0; i < impl_->inputs.size(); ++i) {
        os << "input:  " << impl_->inputs[i] << ' '
           << shape_str(s.GetInputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape()) << '\n';
    }
    for (std::size_t i = 0; i < impl_->outputs.size(); ++i) {
        os << "output: " << impl_->outputs[i] << ' '
           << shape_str(s.GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape()) << '\n';
    }
    Ort::AllocatorWithDefaultOptions alloc;
    const auto md = s.GetModelMetadata();
    os << "metadata keys:";
    for (const auto& k : md.GetCustomMetadataMapKeysAllocated(alloc)) os << ' ' << k.get();
    os << '\n';
    return os.str();
}

}  // namespace ll
