#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace poppler {
class document;
}

namespace ll {

struct RgbImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;  // row-major, 3 bytes per pixel (R, G, B)
};

// Thin RAII wrapper over poppler-cpp. Page numbers are 1-based.
class PdfReader {
public:
    explicit PdfReader(const std::filesystem::path& path);
    ~PdfReader();
    PdfReader(const PdfReader&) = delete;
    PdfReader& operator=(const PdfReader&) = delete;
    PdfReader(PdfReader&&) noexcept;
    PdfReader& operator=(PdfReader&&) noexcept;

    int page_count() const;

    // Text layer in reading order, UTF-8.
    std::string page_text(int page_no) const;

    // Renders the page at `dpi` (72 = 1 PDF point per pixel).
    RgbImage render_page(int page_no, double dpi) const;

private:
    std::unique_ptr<poppler::document> doc_;
};

}  // namespace ll
