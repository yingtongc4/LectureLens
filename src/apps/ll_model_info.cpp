// ll-model-info: print an ONNX model's inputs, outputs and metadata as one
// backend sees it.
//
//   ll-model-info [--backend NAME] MODEL.onnx [METADATA_KEY]

#include <cstring>
#include <exception>
#include <iostream>

#include "inference/model.hpp"

int main(int argc, char** argv) {
    ll::Backend backend = ll::Backend::Cpu;
    int i = 1;
    if (argc > 2 && std::strcmp(argv[1], "--backend") == 0) {
        backend = ll::parse_backend(argv[2]);
        i = 3;
    }
    if (argc <= i) {
        std::cerr << "usage: ll-model-info [--backend NAME] MODEL.onnx [METADATA_KEY]\n";
        return 2;
    }
    try {
        const auto model = ll::load_model(argv[i], backend);
        std::cout << model->describe();
        if (argc > i + 1) {
            const auto v = model->metadata(argv[i + 1]);
            std::cout << argv[i + 1] << ": " << (v ? v->substr(0, 80) + (v->size() > 80 ? "..." : "") : "<missing>")
                      << '\n';
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
