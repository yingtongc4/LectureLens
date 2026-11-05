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
                                   ▲
              Claude ◄── ll-agent ─┴─ MCP (stdio) ── ll-mcp: search_lectures, get_page, list_lectures
```

## Components

| Path | What it does |
|---|---|
| `src/pdf` | poppler-cpp wrapper: text layer and page rendering |
| `src/core` | UTF-8, slide-text normalization, lecture splitting, chunking, CER/LCS, RRF |
| `src/inference` | `Model` interface: ONNX Runtime (CPU / CUDA / TensorRT providers) and native OpenVINO, chosen by `--backend` |
| `src/ocr` | DBNet detection post-processing, CTC recognition, reading order |
| `src/embed` | XLM-R SentencePiece tokenization, mean pooling, E5 query/passage prefixes |
| `src/vector` | Qdrant REST client |
| `src/index` | Transactional-outbox indexer: `FOR UPDATE SKIP LOCKED`, idempotent upserts, backoff, dead-letter |
| `src/search` | Hybrid retrieval: vector + full-text, Reciprocal Rank Fusion, per-page dedup |
| `src/mcp` | Model Context Protocol server and client (JSON-RPC over stdio), both protocol eras |
| `src/agent` | Tool-use loop over MCP tools; Claude Messages API client (raw HTTPS) |
| `src/apps` | `ll-ingest`, `ll-ocr`, `ll-indexer`, `ll-search`, `ll-api`, `ll-mcp`, `ll-agent`, `ll-bench`, `ll-model-info` |
| `migrations` | Schema, applied by `scripts/migrate.sh` |
| `eval` | Labeled retrieval queries |
| `bench/results` | Benchmark output (JSON) |

## Running (Ubuntu 24.04 / WSL2)

```bash
bash scripts/setup_wsl.sh        # once: compilers, libraries, Docker (asks for sudo)
sudo apt-get install -y libsentencepiece-dev
bash scripts/fetch_models.sh     # ONNX Runtime, OpenVINO, PP-OCRv4, multilingual-e5-small
LL_CMAKE_ARGS=-DLL_ENABLE_OPENVINO=ON bash scripts/dev.sh test   # build + unit tests
bash scripts/dev.sh ingest       # PostgreSQL + Qdrant up, migrate, ingest PDFs
bash scripts/dev.sh ocr --backend openvino run   # OCR pages whose text layer is too thin
~/build/lecturelens/src/ll-indexer --once
~/build/lecturelens/src/ll-api     # http://127.0.0.1:8080
```

Models are kept in `~/.local/share/lecturelens/models` (`LL_MODELS_DIR`), on
the Linux filesystem: under WSL2, loading a 450 MB model from `/mnt/c` took
21.7 s versus 3.0 s from ext4. Every tool takes `--backend cpu|openvino|cuda|tensorrt`.

Benchmarks:

```bash
python3 -m venv ~/.venvs/ll-quant && ~/.venvs/ll-quant/bin/pip install onnxruntime onnx
~/.venvs/ll-quant/bin/python -I scripts/quantize_e5.py ~/.local/share/lecturelens/models/multilingual-e5-small
~/build/lecturelens/src/ll-bench embed --out bench/results/embed.json
~/build/lecturelens/src/ll-ocr --backend openvino --every 5 eval
```

```bash
curl -s -X POST localhost:8080/api/search -H 'Content-Type: application/json' \
  -d '{"query": "零知識證明是什麼", "top_k": 3}'
```

Agent and MCP:

```bash
export ANTHROPIC_API_KEY=...                       # ll-agent only
~/build/lecturelens/src/ll-agent "SQL injection 的根本原因是什麼？要怎麼防禦？"
~/build/lecturelens/src/ll-agent --list-tools      # MCP handshake + tools, no key needed
python3 -I tests/e2e/mcp_smoke.py ~/build/lecturelens/src/ll-mcp
```

`ll-mcp` works with any MCP client. From Windows, through WSL:

```json
{"command": "wsl", "args": ["-d", "Ubuntu-24.04", "--", "/home/USER/build/lecturelens/src/ll-mcp"]}
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

**Inference backends** (same ONNX files, same pre/post-processing; only the
runtime changes; `bench/results/`)

OCR, 104 pages:

| | ONNX Runtime CPU | OpenVINO CPU | |
|---|---|---|---|
| Detection p50 / p95 | 185 / 287 ms | 86 / 120 ms | 2.1× |
| Recognition p50 / p95 | 1018 / 2827 ms | 427 / 787 ms | 2.4× (p95 3.6×) |
| Pages / s (incl. rendering) | 0.51 | 0.95 | 1.9× |
| Recall / clean-page CER | 95.3% / 1.7% | 95.3% / 1.7% | identical |

PP-OCR feeds every page and every text-line batch at a different size;
OpenVINO handles these dynamic shapes much better here. Rendering (~450 ms
per page) is now the largest share of OCR time.

Embeddings, 646 passages and 30 queries (cosine is against the ORT FP32
embedding of the same text; retrieval is vector-only, exact search):

| Variant | Size | Passages / s | Query p50 | Cosine (mean / min) | Recall@5 | Recall@10 |
|---|---|---|---|---|---|---|
| ORT FP32 | 449 MB | 22–31 | 7.1 ms | 1 | 63.3% | 76.7% |
| OpenVINO FP32 | 449 MB | 24 | 8.1 ms | 1.0000 / 1.0000 | 63.3% | 76.7% |
| ORT INT8 (dynamic) | 113 MB | 32–41 | 4.9 ms | 0.9959 / 0.9891 | 66.7% | 70.0% |
| OpenVINO + the same INT8 file | 113 MB | 15 | 15.6 ms | 0.9966 / 0.9906 | 63.3% | 73.3% |

- For the encoder, FP32 throughput on the two runtimes is within run-to-run
  noise on this laptop (ORT ranged from 22 to 31 passages/s across runs).
  OpenVINO loads faster from its compiled-model cache (1.2 s vs 3.7–4.2 s).
- Dynamic INT8 quantization is 4× smaller and about 1.4× faster on ONNX
  Runtime. Embeddings stay at cosine 0.996, but Recall@10 drops from 23/30 to
  21/30 queries: worth measuring before switching, not a free win.
- The INT8 file uses ORT's `MatMulInteger` format, which OpenVINO runs slower
  than FP32. OpenVINO's own path (NNCF weight compression) is the fair
  comparison and is not done yet.
- An AMD CPU has none of the Intel-specific paths (AMX, VNNI on recent Xeons
  and Core Ultra). The same `ll-bench` run on an Intel machine is still to come.

Hybrid search through OpenVINO gives the same results as through ONNX
Runtime (Recall@5 76.7%, MRR 0.589).

**API**: 50 searches at concurrency 8: all 200, p50 109 ms, p95 191 ms, 52 req/s.

**Indexing**: 1,930 outbox events → 708 vectors in 31 s.

## Agent and MCP

`ll-mcp` exposes three read-only tools. Results carry both a readable text
block and `structuredContent`; a bad argument or a missing page comes back as
an `isError` result the model can read and correct, not as a protocol error.

| Tool | Input | Returns |
|---|---|---|
| `search_lectures` | `query`, `top_k` (1-20), `mode`, `document_id` | Slides (one per page): lecture, page, document_id, snippet |
| `get_page` | `document_id`, `page` | Slide text layer plus OCR text from images |
| `list_lectures` | `document_id` (optional) | Lectures with page ranges |

The protocol layer is written from the specification (no SDK) and serves both
protocol eras on one stdio process:

- **2026-07-28 (stateless):** version, client info and capabilities in every
  request's `_meta`; `server/discover`; `UnsupportedProtocolVersionError`
  (-32022) listing supported versions; cacheable `tools/list` (`ttlMs`,
  `cacheScope`); `resultType` on results.
- **2025-03-26 to 2025-11-25:** `initialize` handshake with version
  negotiation.

The client side follows the spec's stdio compatibility rule: probe with
`server/discover`, retry on -32022 with a shared version, otherwise fall back
to `initialize`. Unit tests run client against server in-process for every
era combination, and `tests/e2e/mcp_smoke.py` checks the real server from a
separate (Python) client.

`ll-agent` launches `ll-mcp`, lists its tools, and runs Claude's tool-use loop:
assistant content is sent back unchanged (thinking blocks included), all
tool results of one turn go back in one user message, and the loop ends on
`end_turn`, `refusal`, `max_tokens` or a turn limit. Requests use
`claude-opus-5-5` at effort `medium`, API version `2026-10-21`, prompt caching
of tools and system prompt, and `fallbacks: "default"` so a refused request is
retried on a fallback model by the API (`--no-fallbacks` turns it off). The
loop is tested with a scripted model; there is no official C++ SDK, so the
client is raw HTTPS with retries on 408/409/429/5xx.

## Known issues

- Ubuntu's libpqxx 7.8 is built as C++17; under C++20 its headers reference a
  `std::source_location` constructor the library lacks, so floating-point and
  `std::optional` parameters are passed as text (see `src/storage/ocr_repo.cpp`).
