#include "inference/model.hpp"

#include <cstdlib>
#include <stdexcept>

#include "inference/ort_model.hpp"
#ifdef LL_HAVE_OPENVINO
#include "inference/ov_model.hpp"
#endif

namespace ll {

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

std::unique_ptr<Model> load_model(const std::filesystem::path& path, Backend backend, int threads) {
    if (backend == Backend::OpenVino) {
#ifdef LL_HAVE_OPENVINO
        return std::make_unique<OvModel>(path, threads);
#else
        throw std::runtime_error("built without OpenVINO (configure with -DLL_ENABLE_OPENVINO=ON)");
#endif
    }
    return std::make_unique<OrtModel>(path, backend, threads);
}

std::filesystem::path default_models_dir() {
    if (const char* d = std::getenv("LL_MODELS_DIR"); d && *d) return d;
    if (const char* home = std::getenv("HOME")) return std::filesystem::path(home) / ".local/share/lecturelens/models";
    return "models";
}

}  // namespace ll
