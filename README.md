# LectureLens

A multimodal knowledge base for lecture material, written in C++20 for Linux.
Slide decks are parsed, OCR'd and embedded; questions in English or Chinese are
answered with citations down to the lecture and page.

```
PDF ──► ll-ingest ──► PostgreSQL (documents, lectures, pages, chunks, outbox)
            │                 ▲                      │
            ▼                 │                      ▼ outbox_events
        ll-ocr ── PP-OCRv4 ───┘               ll-indexer ── multilingual-e5 ──► Qdrant
        (ONNX Runtime)                                                          │
                                                                                ▼
                         ll-api / ll-search ◄── RRF( Qdrant vectors , PostgreSQL full-text )
```

## Components

| Path | What it does |
|---|---|
| `src/pdf` | poppler-cpp wrapper: text layer and page rendering |
| `src/core` | UTF-8, slide-text normalization, lecture splitting, chunking, CER/LCS, RRF |
| `src/inference` | ONNX Runtime wrapper; one switch point for CPU / OpenVINO / CUDA / TensorRT |
| `src/ocr` | DBNet detection post-processing, CTC recognition, reading order |
| `src/embed` | XLM-R SentencePiece tokenization, mean pooling, E5 query/passage prefixes |
| `src/vector` | Qdrant REST client |
| `src/index` | Transactional-outbox indexer: `FOR UPDATE SKIP LOCKED`, idempotent upserts, backoff, dead-letter |
| `src/search` | Hybrid retrieval: vector + full-text, Reciprocal Rank Fusion, per-page dedup |
| `src/apps` | `ll-ingest`, `ll-ocr`, `ll-indexer`, `ll-search`, `ll-api`, `ll-model-info` |
| `migrations` | Schema, applied by `scripts/migrate.sh` |
| `eval` | Labeled retrieval queries |

## Running (Ubuntu 24.04 / WSL2)

```bash
bash scripts/setup_wsl.sh        # once: compilers, libraries, Docker (asks for sudo)
sudo apt-get install -y libsentencepiece-dev
bash scripts/fetch_models.sh     # ONNX Runtime, PP-OCRv4, multilingual-e5-small
bash scripts/dev.sh test         # build + unit tests
bash scripts/dev.sh ingest       # PostgreSQL + Qdrant up, migrate, ingest PDFs
bash scripts/dev.sh ocr run      # OCR pages whose text layer is too thin
~/build/lecturelens/src/ll-indexer --once --models models
~/build/lecturelens/src/ll-api --models models     # http://127.0.0.1:8080
```

```bash
curl -s -X POST localhost:8080/api/search -H 'Content-Type: application/json' \
  -d '{"query": "零知識證明是什麼", "top_k": 3}'
```

Course PDFs are copyrighted and are read from outside the repository
(`LL_COURSE_DIR`); they are never committed.

## Results so far

Measured on an AMD Ryzen 5 7535HS (6 cores, no GPU), CPU execution provider.

**OCR** (PP-OCRv4, 104 pages sampled; the PDF text layer is the reference)

| Metric | Value |
|---|---|
| Recall of text-layer content (LCS) | 95.3% |
| CER on pages without extra image text, median | 1.7% |
| Pages where OCR found text the text layer lacks (code screenshots, diagrams) | 34 / 104 |
| Detection / recognition per page | 0.19 s / 1.0 s |

Plain CER first came out at 37%: OCR was being penalized for reading code in
screenshots that the text layer does not contain. Recall plus CER on clean
pages measures recognition quality without punishing that extra coverage.

**Retrieval** (30 labeled queries, 15 English paraphrases + 15 Chinese; a hit
is the right lecture and page)

| Method | Recall@5 | MRR@5 | Recall@10 |
|---|---|---|---|
| PostgreSQL full-text | 50.0% | 0.358 | 50.0% |
| Qdrant vectors (multilingual-e5-small) | 63.3% | 0.550 | 76.7% |
| **Hybrid (RRF)** | **76.7%** | **0.589** | **86.7%** |

Full-text search cannot match Chinese questions against English slides;
vectors can, but miss exact terms. Fusion wins on both. Three fixes took the
first hybrid run from 63.3% to 76.7% Recall@5: OR-ing query lexemes (a full
question never has every term on one slide), one result per page, and
excluding cover/outline slides. The query set is small and was written by the
author, so treat these numbers as indicative.

**API**: 50 searches at concurrency 8: all 200, p50 109 ms, p95 191 ms, 52 req/s.

**Indexing**: 1,930 outbox events → 708 vectors in 31 s.

## Known issues

- Ubuntu's libpqxx 7.8 is built as C++17; under C++20 its headers reference a
  `std::source_location` constructor the library lacks, so floating-point and
  `std::optional` parameters are passed as text (see `src/storage/ocr_repo.cpp`).
