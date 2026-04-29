"""
表情分类模型导出为 ONNX
========================
从 .pth checkpoint 导出为 .onnx，并验证推理精度一致性。

运行：
  python training/emotion/export_emotion_onnx.py
"""

import os
import sys
import numpy as np

import torch
import torch.nn as nn
from torchvision import models
import onnxruntime as ort

PROJECT_ROOT  = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CKPT_PATH     = os.path.join(PROJECT_ROOT, "models", "checkpoints", "emotion_mobilenetv3_6cls_best.pth")
ONNX_OUT_PATH = os.path.join(PROJECT_ROOT, "models", "onnx", "emotion_mobilenetv3_6cls.onnx")
NUM_CLASSES   = 6
IMG_SIZE      = 112


def load_model(ckpt_path, num_classes, device):
    model = models.mobilenet_v3_small(weights=None)
    model.classifier[3] = nn.Linear(model.classifier[3].in_features, num_classes)
    model.load_state_dict(torch.load(ckpt_path, map_location=device))
    model.to(device)
    model.eval()
    return model


def export(model, onnx_path, img_size, device):
    dummy = torch.randn(1, 3, img_size, img_size).to(device)
    torch.onnx.export(
        model,
        dummy,
        onnx_path,
        input_names=["image"],
        output_names=["logits"],
        dynamic_axes={"image": {0: "batch"}, "logits": {0: "batch"}},
        opset_version=11,
    )
    print(f"[OK] 导出完成: {onnx_path}")


def verify(ckpt_path, onnx_path, num_classes, img_size, device):
    """对比 PyTorch 和 ONNX Runtime 输出，计算最大误差。"""
    model = load_model(ckpt_path, num_classes, device)

    dummy_np = np.random.randn(1, 3, img_size, img_size).astype(np.float32)
    dummy_t  = torch.from_numpy(dummy_np).to(device)

    with torch.no_grad():
        pt_out = torch.softmax(model(dummy_t), dim=1).cpu().numpy()

    sess = ort.InferenceSession(onnx_path, providers=["CPUExecutionProvider"])
    ort_logits = sess.run(["logits"], {"image": dummy_np})[0]
    ort_out = np.exp(ort_logits) / np.exp(ort_logits).sum(axis=1, keepdims=True)

    max_err = float(np.abs(pt_out - ort_out).max())
    print(f"[验证] PyTorch vs ONNX 最大误差: {max_err:.6f}")
    if max_err < 1e-4:
        print("[验证] 精度损失可接受 ✓")
    else:
        print("[验证] 警告：误差偏大，请检查导出配置")
    return max_err


if __name__ == "__main__":
    device = "cpu"   # ONNX 导出用 CPU 保证跨平台兼容
    print(f"[INFO] checkpoint : {CKPT_PATH}")
    print(f"[INFO] 输出路径   : {ONNX_OUT_PATH}")

    model = load_model(CKPT_PATH, NUM_CLASSES, device)
    export(model, ONNX_OUT_PATH, IMG_SIZE, device)
    verify(CKPT_PATH, ONNX_OUT_PATH, NUM_CLASSES, IMG_SIZE, device)
