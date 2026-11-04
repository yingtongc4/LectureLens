#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "inference/model.hpp"

namespace sentencepiece {
class SentencePieceProcessor;
}

namespace ll::embed {

// multilingual-e5-small: XLM-R SentencePiece tokenizer + BERT-style encoder.
// E5 expects a "query: " or "passage: " prefix; use embed_query / embed_passages.
class TextEmbedder {
public:
    // `dir` holds the ONNX model (model.onnx, or a variant such as
    // model_int8.onnx) and sentencepiece.bpe.model.
    TextEmbedder(const std::filesystem::path& dir, Backend backend, int threads = 0,
                 const std::string& model_file = "model.onnx", int max_tokens = 512);
    ~TextEmbedder();

    std::vector<float> embed_query(const std::string& text) const;
    std::vector<std::vector<float>> embed_passages(const std::vector<std::string>& texts) const;

    // Fairseq/XLM-R token ids including <s> ... </s>, truncated to max_tokens.
    std::vector<std::int64_t> tokenize(const std::string& text) const;

    int dim() const { return dim_; }

private:
    std::vector<std::vector<float>> embed(const std::vector<std::string>& texts) const;

    std::unique_ptr<sentencepiece::SentencePieceProcessor> sp_;
    std::unique_ptr<Model> model_;
    bool has_token_type_ids_ = false;
    int max_tokens_;
    int dim_ = 0;
};

}  // namespace ll::embed
