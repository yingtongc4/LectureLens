#include "pdf/pdf_reader.hpp"

#include <poppler-document.h>
#include <poppler-image.h>
#include <poppler-page-renderer.h>
#include <poppler-page.h>

#include <bit>
#include <stdexcept>

namespace ll {
namespace {

std::unique_ptr<poppler::page> load_page(const poppler::document& doc, int page_no) {
    if (page_no < 1 || page_no > doc.pages()) {
        throw std::out_of_range("page " + std::to_string(page_no) + " out of range");
    }
    std::unique_ptr<poppler::page> page(doc.create_page(page_no - 1));
    if (!page) throw std::runtime_error("cannot load page " + std::to_string(page_no));
    return page;
}

}  // namespace

PdfReader::PdfReader(const std::filesystem::path& path)
    : doc_(poppler::document::load_from_file(path.string())) {
    if (!doc_) throw std::runtime_error("cannot open PDF: " + path.string());
    if (doc_->is_locked()) throw std::runtime_error("PDF is encrypted: " + path.string());
}

PdfReader::~PdfReader() = default;
PdfReader::PdfReader(PdfReader&&) noexcept = default;
PdfReader& PdfReader::operator=(PdfReader&&) noexcept = default;

int PdfReader::page_count() const { return doc_->pages(); }

std::string PdfReader::page_text(int page_no) const {
    const auto page = load_page(*doc_, page_no);
    const auto bytes = page->text(poppler::rectf(), poppler::page::raw_order_layout).to_utf8();
    return {bytes.begin(), bytes.end()};
}

RgbImage PdfReader::render_page(int page_no, double dpi) const {
    const auto page = load_page(*doc_, page_no);
    poppler::page_renderer renderer;
    renderer.set_render_hint(poppler::page_renderer::antialiasing, true);
    renderer.set_render_hint(poppler::page_renderer::text_antialiasing, true);
    renderer.set_image_format(poppler::image::format_argb32);

    const poppler::image img = renderer.render_page(page.get(), dpi, dpi);
    if (!img.is_valid()) throw std::runtime_error("render failed for page " + std::to_string(page_no));

    RgbImage out;
    out.width = img.width();
    out.height = img.height();
    out.pixels.resize(static_cast<std::size_t>(out.width) * out.height * 3);
    // argb32 pixels are native-endian 0xAARRGGBB words: on little-endian x86
    // the bytes are B, G, R, A.
    static_assert(std::endian::native == std::endian::little);
    const char* src = img.const_data();
    for (int y = 0; y < out.height; ++y) {
        const auto* row = reinterpret_cast<const std::uint8_t*>(src + static_cast<std::size_t>(y) * img.bytes_per_row());
        auto* dst = out.pixels.data() + static_cast<std::size_t>(y) * out.width * 3;
        for (int x = 0; x < out.width; ++x) {
            dst[3 * x + 0] = row[4 * x + 2];
            dst[3 * x + 1] = row[4 * x + 1];
            dst[3 * x + 2] = row[4 * x + 0];
        }
    }
    return out;
}

}  // namespace ll
