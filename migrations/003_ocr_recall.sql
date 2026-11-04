-- CER alone penalizes OCR for reading text the PDF text layer lacks (code in
-- screenshots, labels in diagrams). recall = LCS(ocr, text layer) / |text layer|
-- measures how much of the known text OCR recovered, ignoring extra text.
ALTER TABLE ocr_page_results ADD COLUMN recall REAL;
ALTER TABLE ocr_page_results ADD COLUMN extra_chars INT;  -- max(0, |ocr| - |text layer|)
