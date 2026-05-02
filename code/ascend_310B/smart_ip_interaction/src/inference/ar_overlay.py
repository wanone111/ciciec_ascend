"""
AR 道具贴合模块
===============
利用 COCO 姿态关键点（鼻子、双眼、双耳）计算人头的
位置、尺寸、旋转角，将 PNG 道具精准叠加到画面上。

支持多人同时佩戴。
"""

import cv2
import numpy as np
from PIL import Image
import math
import os

# COCO 关键点索引
_NOSE       = 0
_LEFT_EYE   = 1
_RIGHT_EYE  = 2
_LEFT_EAR   = 3
_RIGHT_EAR  = 4


def _load_rgba(path: str) -> np.ndarray:
    """加载 PNG（含 Alpha），返回 RGBA numpy 数组。"""
    img = Image.open(path).convert("RGBA")
    return np.array(img)


def _overlay_rgba(base_bgr: np.ndarray, overlay_rgba: np.ndarray,
                  x: int, y: int) -> np.ndarray:
    """
    将 overlay_rgba 叠加到 base_bgr 的 (x, y) 位置。
    处理越界裁剪，使用 alpha 混合。
    """
    oh, ow = overlay_rgba.shape[:2]
    bh, bw = base_bgr.shape[:2]

    # 计算有效区域
    x1_o = max(0, -x);      y1_o = max(0, -y)
    x1_b = max(0,  x);      y1_b = max(0,  y)
    x2_o = min(ow, bw - x); y2_o = min(oh, bh - y)

    if x2_o <= x1_o or y2_o <= y1_o:
        return base_bgr

    roi_o = overlay_rgba[y1_o:y2_o, x1_o:x2_o]
    roi_b = base_bgr[y1_b:y1_b + (y2_o - y1_o),
                     x1_b:x1_b + (x2_o - x1_o)]

    alpha = roi_o[:, :, 3:4].astype(np.float32) / 255.0
    rgb_o = roi_o[:, :, :3][:, :, ::-1]          # RGBA→BGR

    blended = (rgb_o * alpha + roi_b * (1 - alpha)).astype(np.uint8)
    base_bgr[y1_b:y1_b + (y2_o - y1_o),
             x1_b:x1_b + (x2_o - x1_o)] = blended
    return base_bgr


class AROverlay:
    def __init__(self, prop_path: str, y_offset_ratio: float = 0.55,
                 scale_ratio: float = 1.4):
        """
        prop_path      : PNG 道具路径（带透明背景）
        y_offset_ratio : 道具中心相对于双眼中点向上偏移的比例（相对于头宽）
        scale_ratio    : 道具宽度相对于双眼间距的缩放比例
        """
        if not os.path.exists(prop_path):
            raise FileNotFoundError(f"道具文件不存在: {prop_path}")
        self._prop_rgba = _load_rgba(prop_path)
        self._y_offset_ratio = y_offset_ratio
        self._scale_ratio    = scale_ratio

    def apply(self, frame: np.ndarray, all_keypoints) -> np.ndarray:
        """
        all_keypoints: 每个人的关键点列表，支持 tensor / ndarray / list，
                       shape (N_persons, 17, 2) 或单人 (17, 2)
        返回叠加后的 frame（in-place 修改）。
        """
        if all_keypoints is None:
            return frame

        kps = np.array(all_keypoints, dtype=float)
        # 统一为 (N, 17, 2)
        if kps.ndim == 2:
            kps = kps[np.newaxis]

        for kp in kps:
            frame = self._apply_one(frame, kp)
        return frame

    def _apply_one(self, frame: np.ndarray, kp: np.ndarray) -> np.ndarray:
        """对单人贴道具。"""
        le = kp[_LEFT_EYE]
        re = kp[_RIGHT_EYE]

        # 如果眼睛关键点无效（坐标为0），尝试用耳朵
        if le[0] < 1 and le[1] < 1:
            le = kp[_LEFT_EAR]
        if re[0] < 1 and re[1] < 1:
            re = kp[_RIGHT_EAR]

        if le[0] < 1 or re[0] < 1:
            return frame  # 关键点不可用

        # ── 计算头部参数 ──
        eye_dist = math.dist(le, re)
        if eye_dist < 5:
            return frame

        # 双眼中点
        eye_mid = ((le[0] + re[0]) / 2, (le[1] + re[1]) / 2)

        # 头部旋转角（以双眼连线为基准）
        angle_deg = math.degrees(math.atan2(
            float(le[1]) - float(re[1]),
            float(le[0]) - float(re[0])
        ))

        # 道具目标宽度
        target_w = int(eye_dist * self._scale_ratio)
        if target_w < 10:
            return frame

        ph, pw = self._prop_rgba.shape[:2]
        target_h = int(ph * target_w / pw)

        # ── 缩放 ──
        prop_resized = cv2.resize(
            self._prop_rgba, (target_w, target_h),
            interpolation=cv2.INTER_AREA
        )

        # ── 旋转 ──
        if abs(angle_deg) > 1.0:
            prop_pil = Image.fromarray(prop_resized)
            prop_pil = prop_pil.rotate(-angle_deg, expand=True,
                                        resample=Image.BICUBIC)
            prop_resized = np.array(prop_pil)
            target_h, target_w = prop_resized.shape[:2]

        # ── 定位：道具底部中心对齐双眼中点上方 ──
        offset_y = int(eye_dist * self._y_offset_ratio)
        x = int(eye_mid[0]) - target_w // 2
        y = int(eye_mid[1]) - offset_y - target_h

        frame = _overlay_rgba(frame, prop_resized, x, y)
        return frame
