#!/usr/bin/env bash
# Downloads ONNX Runtime, OpenVINO and the ONNX models. Idempotent.
set -euo pipefail

SRC="$(cd "$(dirname "$0")/.." && pwd)"
ORT_VERSION="${ORT_VERSION:-1.31.0}"
ORT_DIR="$HOME/opt/onnxruntime-linux-x64-$ORT_VERSION"
# On the Linux filesystem: under WSL, loading models from /mnt/c is several times slower.
MODELS="${LL_MODELS_DIR:-$HOME/.local/share/lecturelens/models}"
HF="https://huggingface.co/SWHL/RapidOCR/resolve/main/PP-OCRv4"

# Abort a transfer that stalls below 1 KB/s for 20 s, then resume it.
fetch() {
  local out="$1" url="$2"
  for attempt in 1 2 3 4 5; do
    curl -fL -C - --speed-limit 1024 --speed-time 20 -o "$out" "$url" && return 0
    echo "retrying ($attempt) $url" >&2
  done
  return 1
}

if [[ ! -d "$ORT_DIR" ]]; then
  mkdir -p "$HOME/opt"
  fetch "/tmp/ort-$ORT_VERSION.tgz" \
    "https://github.com/microsoft/onnxruntime/releases/download/v$ORT_VERSION/onnxruntime-linux-x64-$ORT_VERSION.tgz"
  tar -xzf "/tmp/ort-$ORT_VERSION.tgz" -C "$HOME/opt"
  rm "/tmp/ort-$ORT_VERSION.tgz"
fi
echo "ONNX Runtime: $ORT_DIR"

# OpenVINO runtime for -DLL_ENABLE_OPENVINO=ON (C++ API + ONNX frontend).
OV_VERSION="2026.3.1"
OV_BUILD="$OV_VERSION.22476.56d9685302d"
OV_DIR="$HOME/opt/openvino_$OV_VERSION"
if [[ ! -d "$OV_DIR" ]]; then
  tgz="openvino_toolkit_ubuntu24_${OV_BUILD}_x86_64.tgz"
  fetch "/tmp/$tgz" "https://storage.openvinotoolkit.org/repositories/openvino/packages/$OV_VERSION/linux/$tgz"
  tar -xzf "/tmp/$tgz" -C "$HOME/opt"
  mv "$HOME/opt/${tgz%.tgz}" "$OV_DIR"
  rm "/tmp/$tgz"
fi
echo "OpenVINO: $OV_DIR"

mkdir -p "$MODELS"
for f in ch_PP-OCRv4_det_infer.onnx ch_PP-OCRv4_rec_infer.onnx; do
  [[ -s "$MODELS/$f" ]] || fetch "$MODELS/$f.part" "$HF/$f" && mv -f "$MODELS/$f.part" "$MODELS/$f" 2>/dev/null || true
done
# Multilingual text embeddings (MIT): Chinese queries can find English slides.
E5="https://huggingface.co/intfloat/multilingual-e5-small/resolve/main/onnx"
mkdir -p "$MODELS/multilingual-e5-small"
for f in model.onnx sentencepiece.bpe.model; do
  out="$MODELS/multilingual-e5-small/$f"
  [[ -s "$out" ]] || { fetch "$out.part" "$E5/$f" && mv -f "$out.part" "$out"; }
done
ls -lR "$MODELS"
