-- One row per ll-ocr invocation, so OCR quality and speed can be compared
-- across backends, models and settings over time.
CREATE TABLE ocr_runs (
    id          BIGSERIAL PRIMARY KEY,
    kind        TEXT        NOT NULL CHECK (kind IN ('run', 'eval')),
    backend     TEXT        NOT NULL,
    params      JSONB       NOT NULL,
    metrics     JSONB,
    started_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    finished_at TIMESTAMPTZ
);

CREATE TABLE ocr_page_results (
    run_id  BIGINT NOT NULL REFERENCES ocr_runs(id) ON DELETE CASCADE,
    page_id BIGINT NOT NULL REFERENCES pages(id)    ON DELETE CASCADE,
    lines   INT    NOT NULL,
    det_ms  REAL   NOT NULL,
    rec_ms  REAL   NOT NULL,
    cer     REAL,              -- vs. the text layer; NULL when not evaluated
    PRIMARY KEY (run_id, page_id)
);
