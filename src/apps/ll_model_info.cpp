// ll-model-info: print an ONNX model's inputs, outputs and metadata.
//
//   ll-model-info MODEL.onnx [METADATA_KEY]

#include <exception>
#include <iostream>

#include "inference/ort_model.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: ll-model-info MODEL.onnx [METADATA_KEY]\n";
        return 2;
    }
    try {
        const ll::OrtModel model(argv[1], ll::Backend::Cpu);
        std::cout << model.describe();
        if (argc > 2) {
            const auto v = model.metadata(argv[2]);
            std::cout << argv[2] << ": " << (v ? *v : "<missing>") << '\n';
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
