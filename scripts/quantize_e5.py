"""Dynamic INT8 quantization of multilingual-e5-small.

Weights of MatMul/Gather become int8; activations are quantized at run time,
so no calibration data is needed. Output: model_int8.onnx next to the input.

    ~/.venvs/ll-quant/bin/python -I scripts/quantize_e5.py models/multilingual-e5-small
"""

import pathlib
import sys

from onnxruntime.quantization import QuantType, quantize_dynamic
from onnxruntime.quantization.shape_inference import quant_pre_process


def main() -> None:
    model_dir = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "models/multilingual-e5-small")
    src = model_dir / "model.onnx"
    pre = model_dir / "model_pre.onnx"
    dst = model_dir / "model_int8.onnx"

    # Shape inference + graph cleanup first, as ONNX Runtime recommends.
    quant_pre_process(str(src), str(pre), skip_symbolic_shape=True)
    quantize_dynamic(str(pre), str(dst), weight_type=QuantType.QInt8, per_channel=True)
    pre.unlink()
    print(f"{src.name}: {src.stat().st_size / 2**20:.1f} MiB -> {dst.name}: {dst.stat().st_size / 2**20:.1f} MiB")


if __name__ == "__main__":
    main()
