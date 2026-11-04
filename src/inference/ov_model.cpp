#include "inference/ov_model.hpp"

#include <openvino/openvino.hpp>

#include <cstdlib>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace ll {
namespace {

ov::Core& ov_core() {
    static ov::Core core;  // plugin registry; thread-safe after construction
    return core;
}

// LL_OV_CACHE_DIR, else ~/.cache/lecturelens/openvino; LL_OV_CACHE_DIR="" disables.
std::string ov_cache_dir() {
    if (const char* d = std::getenv("LL_OV_CACHE_DIR")) return d;
    if (const char* home = std::getenv("HOME")) return std::string(home) + "/.cache/lecturelens/openvino";
    return {};
}

ov::Shape to_ov_shape(const std::vector<std::int64_t>& shape) {
    return ov::Shape(shape.begin(), shape.end());
}

TensorF fetch(const ov::Tensor& t) {
    if (t.get_element_type() != ov::element::f32) {
        throw std::runtime_error("OpenVINO output is " + t.get_element_type().get_type_name() + ", expected f32");
    }
    TensorF out;
    for (const auto d : t.get_shape()) out.shape.push_back(static_cast<std::int64_t>(d));
    const float* p = t.data<const float>();
    out.data.assign(p, p + t.get_size());
    return out;
}

}  // namespace

struct OvModel::Impl {
    std::string path;
    ov::CompiledModel compiled;
    std::vector<std::string> inputs;

    // The source graph is only needed for metadata and describe(), so it is
    // read on first use.
    std::once_flag model_once;
    std::shared_ptr<ov::Model> model_;
    const ov::Model& model() {
        std::call_once(model_once, [this] { model_ = ov_core().read_model(path); });
        return *model_;
    }

    std::mutex mu;
    std::vector<ov::InferRequest> idle;

    // An infer request holds per-call state, so concurrent callers each borrow
    // one; requests are created on demand and reused afterwards.
    class Lease {
    public:
        explicit Lease(Impl& impl) : impl_(impl) {
            std::lock_guard lock(impl_.mu);
            if (impl_.idle.empty()) {
                req_ = impl_.compiled.create_infer_request();
            } else {
                req_ = std::move(impl_.idle.back());
                impl_.idle.pop_back();
            }
        }
        ~Lease() {
            std::lock_guard lock(impl_.mu);
            impl_.idle.push_back(std::move(req_));
        }
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        ov::InferRequest& operator*() { return req_; }
        ov::InferRequest* operator->() { return &req_; }

    private:
        Impl& impl_;
        ov::InferRequest req_;
    };
};

OvModel::OvModel(const std::filesystem::path& model_path, int threads) : impl_(std::make_unique<Impl>()) {
    if (!std::filesystem::exists(model_path)) {
        throw std::runtime_error("model not found: " + model_path.string());
    }
    impl_->path = model_path.string();
    ov::AnyMap config{ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY)};
    if (threads > 0) config.insert(ov::inference_num_threads(threads));
    // Compiling for the CPU takes seconds. Compiling from the path (not from
    // a read ov::Model) lets OpenVINO find a cached blob without reading or
    // hashing the weights first.
    if (const auto dir = ov_cache_dir(); !dir.empty()) config.insert(ov::cache_dir(dir));
    impl_->compiled = ov_core().compile_model(impl_->path, "CPU", config);
    for (const auto& in : impl_->compiled.inputs()) impl_->inputs.push_back(in.get_any_name());
}

OvModel::~OvModel() = default;

TensorF OvModel::run(const TensorF& input) const {
    if (impl_->inputs.size() != 1) throw std::logic_error("model has more than one input");
    Impl::Lease req(*impl_);
    // Wraps the caller's buffer without copying; OpenVINO does not write inputs.
    req->set_input_tensor(ov::Tensor(ov::element::f32, to_ov_shape(input.shape), const_cast<float*>(input.data.data())));
    req->infer();
    return fetch(req->get_output_tensor(0));
}

TensorF OvModel::run(const std::vector<TensorI64>& inputs, const std::string& output) const {
    Impl::Lease req(*impl_);
    for (const auto& t : inputs) {
        req->set_tensor(t.name, ov::Tensor(ov::element::i64, to_ov_shape(t.shape), const_cast<std::int64_t*>(t.data.data())));
    }
    req->infer();
    return fetch(req->get_tensor(output));
}

std::vector<std::string> OvModel::input_names() const { return impl_->inputs; }

std::optional<std::string> OvModel::metadata(const std::string& key) const {
    // The ONNX frontend keeps the model's metadata_props in rt_info["framework"].
    const auto& m = impl_->model();
    if (m.has_rt_info("framework", key)) return m.get_rt_info<std::string>("framework", key);
    if (m.has_rt_info(key)) return m.get_rt_info<std::string>(key);
    return std::nullopt;
}

std::string OvModel::describe() const {
    std::ostringstream os;
    os << "backend: openvino " << ov::get_openvino_version().buildNumber << " (CPU)\n";
    for (const auto& in : impl_->model().inputs()) {
        os << "input:  " << in.get_any_name() << ' ' << in.get_partial_shape() << ' ' << in.get_element_type() << '\n';
    }
    for (const auto& out : impl_->model().outputs()) {
        os << "output: " << out.get_any_name() << ' ' << out.get_partial_shape() << ' ' << out.get_element_type() << '\n';
    }
    os << "rt_info keys:";
    for (const auto& [k, v] : impl_->model().get_rt_info()) {
        os << ' ' << k;
        if (v.is<ov::AnyMap>()) {
            os << " {";
            for (const auto& [k2, v2] : v.as<ov::AnyMap>()) os << ' ' << k2;
            os << " }";
        }
    }
    os << '\n';
    return os.str();
}

}  // namespace ll
