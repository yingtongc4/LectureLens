#include "embed/text_embedder.hpp"

#include <sentencepiece_processor.h>

#include <algorithm>
#include <stdexcept>

#include "embed/pooling.hpp"

namespace ll::embed {
namespace {

// XLM-R reuses fairseq's dictionary: <s>=0, <pad>=1, </s>=2, <unk>=3, and every
// SentencePiece id is shifted by one (SentencePiece's own <unk>=0 maps to 3).
constexpr std::int64_t kBos = 0, kPad = 1, kEos = 2, kUnk = 3, kFairseqOffset = 1;

}  // namespace

TextEmbedder::TextEmbedder(const std::filesystem::path& dir, Backend backend, int threads, int max_tokens)
    : sp_(std::make_unique<sentencepiece::SentencePieceProcessor>()),
      model_(std::make_unique<OrtModel>(dir / "model.onnx", backend, threads)),
      max_tokens_(max_tokens) {
    const auto status = sp_->Load((dir / "sentencepiece.bpe.model").string());
    if (!status.ok()) throw std::runtime_error("cannot load tokenizer: " + status.ToString());
    const auto names = model_->input_names();
    has_token_type_ids_ = std::find(names.begin(), names.end(), "token_type_ids") != names.end();
    dim_ = static_cast<int>(embed({"passage: probe"})[0].size());
}

TextEmbedder::~TextEmbedder() = default;

std::vector<std::int64_t> TextEmbedder::tokenize(const std::string& text) const {
    std::vector<int> pieces;
    sp_->Encode(text, &pieces);
    std::vector<std::int64_t> ids{kBos};
    for (const int p : pieces) {
        if (static_cast<int>(ids.size()) >= max_tokens_ - 1) break;
        ids.push_back(p == 0 ? kUnk : p + kFairseqOffset);
    }
    ids.push_back(kEos);
    return ids;
}

std::vector<float> TextEmbedder::embed_query(const std::string& text) const {
    return embed({"query: " + text})[0];
}

std::vector<std::vector<float>> TextEmbedder::embed_passages(const std::vector<std::string>& texts) const {
    std::vector<std::string> prefixed;
    prefixed.reserve(texts.size());
    for (const auto& t : texts) prefixed.push_back("passage: " + t);
    return embed(prefixed);
}

std::vector<std::vector<float>> TextEmbedder::embed(const std::vector<std::string>& texts) const {
    if (texts.empty()) return {};
    std::vector<std::vector<std::int64_t>> seqs;
    std::size_t len = 0;
    for (const auto& t : texts) {
        seqs.push_back(tokenize(t));
        len = std::max(len, seqs.back().size());
    }

    const auto batch = static_cast<std::int64_t>(seqs.size());
    const auto tokens = static_cast<std::int64_t>(len);
    TensorI64 ids{"input_ids", {batch, tokens}, std::vector<std::int64_t>(batch * tokens, kPad)};
    TensorI64 mask{"attention_mask", {batch, tokens}, std::vector<std::int64_t>(batch * tokens, 0)};
    for (std::int64_t b = 0; b < batch; ++b) {
        for (std::size_t t = 0; t < seqs[b].size(); ++t) {
            ids.data[b * tokens + t] = seqs[b][t];
            mask.data[b * tokens + t] = 1;
        }
    }
    std::vector<TensorI64> inputs{ids, mask};
    if (has_token_type_ids_) {
        inputs.push_back({"token_type_ids", {batch, tokens}, std::vector<std::int64_t>(batch * tokens, 0)});
    }

    const auto hidden = model_->run(inputs, "last_hidden_state");  // [batch, tokens, dim]
    return mean_pool_normalize(hidden.data.data(), static_cast<int>(batch), static_cast<int>(tokens),
                               static_cast<int>(hidden.shape[2]), mask.data);
}

}  // namespace ll::embed
