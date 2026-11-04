-- LectureLens core schema. PostgreSQL is the source of truth; Qdrant is a
-- rebuildable index fed through outbox_events.

CREATE TABLE documents (
    id          BIGSERIAL PRIMARY KEY,
    source_path TEXT        NOT NULL,
    sha256      TEXT        NOT NULL UNIQUE,
    title       TEXT        NOT NULL,
    page_count  INT         NOT NULL CHECK (page_count >= 0),
    created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE lectures (
    id          BIGSERIAL PRIMARY KEY,
    document_id BIGINT NOT NULL REFERENCES documents(id) ON DELETE CASCADE,
    seq         INT    NOT NULL,
    title       TEXT   NOT NULL,
    first_page  INT    NOT NULL,
    last_page   INT    NOT NULL,
    UNIQUE (document_id, seq),
    CHECK (first_page <= last_page)
);

CREATE TABLE pages (
    id           BIGSERIAL PRIMARY KEY,
    document_id  BIGINT  NOT NULL REFERENCES documents(id) ON DELETE CASCADE,
    lecture_id   BIGINT  NOT NULL REFERENCES lectures(id)  ON DELETE CASCADE,
    page_no      INT     NOT NULL,
    text_layer   TEXT    NOT NULL,          -- normalized text from the PDF
    visible_chars INT    NOT NULL,
    needs_ocr    BOOLEAN NOT NULL,          -- text layer too thin to trust alone
    UNIQUE (document_id, page_no)
);
CREATE INDEX pages_lecture_idx ON pages (lecture_id);

-- OCR output; one row per detected text line, per inference backend so that
-- backends can be compared against the text layer.
CREATE TABLE ocr_blocks (
    id         BIGSERIAL PRIMARY KEY,
    page_id    BIGINT NOT NULL REFERENCES pages(id) ON DELETE CASCADE,
    backend    TEXT   NOT NULL,
    line_no    INT    NOT NULL,
    text       TEXT   NOT NULL,
    confidence REAL   NOT NULL,
    x INT NOT NULL, y INT NOT NULL, w INT NOT NULL, h INT NOT NULL,
    UNIQUE (page_id, backend, line_no)
);

-- Retrieval units. Each chunk maps to one Qdrant point (id = chunks.id).
CREATE TABLE chunks (
    id       BIGSERIAL PRIMARY KEY,
    page_id  BIGINT NOT NULL REFERENCES pages(id) ON DELETE CASCADE,
    source   TEXT   NOT NULL CHECK (source IN ('text_layer', 'ocr', 'asr')),
    ordinal  INT    NOT NULL,
    content  TEXT   NOT NULL,
    version  INT    NOT NULL DEFAULT 1,
    content_tsv TSVECTOR GENERATED ALWAYS AS (to_tsvector('english', content)) STORED,
    UNIQUE (page_id, source, ordinal)
);
CREATE INDEX chunks_tsv_idx ON chunks USING GIN (content_tsv);

-- Transactional outbox: written in the same transaction as the data it
-- describes, drained by the indexer. (aggregate, aggregate_id, version) makes
-- delivery idempotent.
CREATE TABLE outbox_events (
    id              BIGSERIAL PRIMARY KEY,
    aggregate       TEXT        NOT NULL,
    aggregate_id    BIGINT      NOT NULL,
    version         INT         NOT NULL,
    op              TEXT        NOT NULL CHECK (op IN ('upsert', 'delete')),
    payload         JSONB       NOT NULL DEFAULT '{}'::jsonb,
    attempts        INT         NOT NULL DEFAULT 0,
    next_attempt_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    processed_at    TIMESTAMPTZ,
    dead            BOOLEAN     NOT NULL DEFAULT false,
    last_error      TEXT,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX outbox_pending_idx ON outbox_events (next_attempt_at)
    WHERE processed_at IS NULL AND NOT dead;

CREATE TABLE jobs (
    id         BIGSERIAL PRIMARY KEY,
    kind       TEXT        NOT NULL,
    status     TEXT        NOT NULL CHECK (status IN ('queued', 'running', 'succeeded', 'failed')),
    input      JSONB       NOT NULL,
    result     JSONB,
    error      TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
