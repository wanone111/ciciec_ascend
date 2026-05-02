"""
表情分类模型导出为 ONNX (Ascend 310B 生产环境版 - JIT 强制降级)
========================
使用 torch.jit.trace 冻结计算图，强制输出 opset=12，彻底避开 PyTorch 2.x 新版导出器升版 Bug。
"""

import os
import numpy as np
import torch
import torch.nn as nn
from torchvision import models
import onnx
import onnxruntime as ort
from onnx import helper, TensorProto

PROJECT_ROOT  = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CKPT_PATH     = os.path.join(PROJECT_ROOT, "models", "checkpoints", "emotion_mobilenetv3_6cls_best.pth")
ONNX_OUT_PATH = os.path.join(PROJECT_ROOT, "models", "onnx", "emotion_mobilenetv3_6cls.onnx")
NUM_CLASSES   = 6
IMG_SIZE      = 112


def load_model(ckpt_path, num_classes, device):
    model = models.mobilenet_v3_small(weights=None)
    model.classifier[3] = nn.Linear(model.classifier[3].in_features, num_classes)
    model.load_state_dict(torch.load(ckpt_path, map_location=device, weights_only=False))
    model.to(device).eval()
    return model


def export(model, onnx_path, img_size, device):
    model.eval()
    dummy = torch.randn(1, 3, img_size, img_size, device=device)
    
    # 🔑 核心1: 使用 torch.jit.trace 冻结动态图，强制走老版 C++ 导出器
    print("[INFO] 使用 torch.jit.trace 冻结模型并强制导出 opset=12...")
    with torch.no_grad():
        traced_model = torch.jit.trace(model, dummy)
    
    torch.onnx.export(
        traced_model,
        dummy,
        onnx_path,
        input_names=["image"],
        output_names=["logits"],
        opset_version=12,          # ← JIT 导出器会严格遵守此版本
        do_constant_folding=True,  # 权重融合必需
        dynamic_axes=None,         # 强制静态
        export_params=True,        # 权重内联为属性
        keep_initializers_as_inputs=False, # Ascend 要求权重不作为输入节点
        verbose=False
    )
    print(f"[OK] 原生导出完成：{onnx_path}")

    # 🔑 核心2: 校验实际 Opset 版本（防升版）
    onnx_model = onnx.load(onnx_path)
    actual_opset = onnx_model.opset_import[0].version
    if actual_opset != 12:
        raise RuntimeError(f"[ERROR] 导出失败：期望 opset 12，实际为 {actual_opset}。请检查 PyTorch 环境或回退至 2.1.0。")

    # 🔑 核心3: Ascend 图结构清洗与形状固化
    print("[INFO] 执行 Ascend 兼容清洗...")
    empty_fix = 0
    for node in onnx_model.graph.node:
        if "" in node.input:
            node.input[:] = [inp for inp in node.input if inp != ""]
            empty_fix += 1

    # 强制固化输入形状 [1, 3, 112, 112]
    inp = onnx_model.graph.input[0]
    inp.name = "image"
    for idx, val in enumerate([1, 3, img_size, img_size]):
        dim = inp.type.tensor_type.shape.dim[idx]
        dim.dim_value = val
        dim.ClearField("dim_param")

    # 注入显式 value_info 阻断 ATC fallback
    existing_vi = {vi.name for vi in onnx_model.graph.value_info}
    if inp.name not in existing_vi:
        onnx_model.graph.value_info.append(
            helper.make_tensor_value_info(inp.name, TensorProto.FLOAT, [1, 3, img_size, img_size])
        )

    onnx_model = onnx.shape_inference.infer_shapes(onnx_model)
    onnx.checker.check_model(onnx_model)
    onnx.save(onnx_model, onnx_path)
    
    first_conv = [n for n in onnx_model.graph.node if n.op_type == "Conv"][0]
    print(f"\n[DIAG] 清洗完成 | Opset: {actual_opset} | 移除空输入: {empty_fix} 个")
    print(f"  First Conv : {first_conv.name} | inputs: {list(first_conv.input)}")
    print(f"[OK] 可安全执行 ATC 转换")


def verify(ckpt_path, onnx_path, num_classes, img_size, device):
    model = load_model(ckpt_path, num_classes, device)
    dummy_np = np.random.randn(1, 3, img_size, img_size).astype(np.float32)
    with torch.no_grad():
        pt_out = torch.softmax(model(torch.from_numpy(dummy_np).to(device)), dim=1).cpu().numpy()
    sess = ort.InferenceSession(onnx_path, providers=["CPUExecutionProvider"])
    ort_out = np.exp(sess.run(["logits"], {"image": dummy_np})[0])
    ort_out /= ort_out.sum(axis=1, keepdims=True)
    err = float(np.abs(pt_out - ort_out).max())
    print(f"[验证] 最大误差: {err:.6f} {'✓' if err < 1e-4 else '⚠️'}")
    return err


if __name__ == "__main__":
    device = "cpu"
    print(f"[INFO] checkpoint : {CKPT_PATH}")
    print(f"[INFO] 输出路径   : {ONNX_OUT_PATH}")

    model = load_model(CKPT_PATH, NUM_CLASSES, device)
    export(model, ONNX_OUT_PATH, IMG_SIZE, device)
    verify(CKPT_PATH, ONNX_OUT_PATH, NUM_CLASSES, IMG_SIZE, device)