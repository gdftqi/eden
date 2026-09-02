#!/usr/bin/env python3
"""合成"刀砍到人"音效, 输出 44.1kHz / 16bit / 单声道 WAV.

设计要点(与砍在金属/盾牌上的区别):
  * 【没有金属分音】肉体不共振, 加了非谐波分音立刻变成砍铁皮
  * 频谱重心压在 200~2000Hz, 6kHz 以上整体滚降 -- 亮就假
  * 噪声带做【向下扫频】, 这是"切进去"而不是"拍上去"的关键
  * 用不规则的振幅起伏做出湿润感, 而不是一条平滑的指数衰减

纯标准库实现, 不依赖 numpy.

用法:
    python3 make_slash_sfx.py
    python3 make_slash_sfx.py --seed 7 --out SFX_SlashHit_b.wav
"""

import argparse
import math
import os
import random
import struct
import wave

SR = 44100
DUR = 0.34


def lp_var(x, cutoffs):
    """一阶低通, 截止频率逐样本变化(用于扫频)"""
    dt = 1.0 / SR
    y, prev = [], 0.0
    for v, fc in zip(x, cutoffs):
        rc = 1.0 / (2 * math.pi * max(fc, 20.0))
        a = dt / (rc + dt)
        prev += a * (v - prev)
        y.append(prev)
    return y


def hp_var(x, cutoffs):
    dt = 1.0 / SR
    y, prev_y, prev_x = [], 0.0, 0.0
    for v, fc in zip(x, cutoffs):
        rc = 1.0 / (2 * math.pi * max(fc, 20.0))
        a = rc / (rc + dt)
        prev_y = a * (prev_y + v - prev_x)
        prev_x = v
        y.append(prev_y)
    return y


def lp(x, fc):
    return lp_var(x, [fc] * len(x))


def hp(x, fc):
    return hp_var(x, [fc] * len(x))


def build(seed):
    rng = random.Random(seed)
    n = int(SR * DUR)
    out = [0.0] * n
    noise = [rng.uniform(-1.0, 1.0) for _ in range(n)]

    # ── ① 切入瞬间 ────────────────────────────────────────────────
    # 短促的中高频, 但上限只到 5kHz. 砍金属那版开到 11kHz, 一下就"锵"了
    cut = lp(hp(noise, 700), 5000)
    for i in range(n):
        t = i / SR
        out[i] += cut[i] * math.exp(-t / 0.005) * 0.85

    # ── ② 切开血肉的主体: 向下扫频的噪声 ─────────────────────────
    # 中心频率 1900Hz 迅速滑到 300Hz. 这个下滑就是"切进去"的听感来源 --
    # 固定频段听起来只是"拍在东西上"
    hi = [1900.0 * math.exp(-(i / SR) / 0.030) + 300.0 for i in range(n)]
    lo = [f * 0.35 for f in hi]
    meat = lp_var(hp_var(noise, lo), hi)

    # 湿润感: 用几处随机起伏打断平滑衰减, 模拟组织撕裂的不连续
    wet = [1.0] * n
    for _ in range(4):
        c = rng.uniform(0.004, 0.055)
        w = rng.uniform(0.004, 0.012)
        amp = rng.uniform(0.25, 0.6)
        for i in range(n):
            t = i / SR
            wet[i] += amp * math.exp(-((t - c) ** 2) / (2 * w * w))

    for i in range(n):
        t = i / SR
        out[i] += meat[i] * wet[i] * math.exp(-t / 0.055) * 0.95

    # ── ③ 身体闷响: 打在有质量的东西上 ───────────────────────────
    # 比砍金属那版衰减慢一倍, 因为肉体吸收能量, 是"闷"不是"弹"
    for i in range(n):
        t = i / SR
        f = 190.0 * math.exp(-t / 0.055) + 68.0
        out[i] += math.sin(2 * math.pi * f * t) * 0.60 * math.exp(-t / 0.075)
        # 略微失谐的第二条, 让低频不那么"电子"
        out[i] += math.sin(2 * math.pi * f * 1.47 * t + 0.7) * 0.22 * math.exp(-t / 0.045)

    # ── ④ 次低频冲击 ─────────────────────────────────────────────
    for i in range(n):
        t = i / SR
        out[i] += math.sin(2 * math.pi * 52.0 * t) * 0.35 * math.exp(-t / 0.05)

    # ── 整体滚降: 砍肉不该有 6kHz 以上的亮部, 留着就发假 ─────────
    out = lp(out, 6200)

    out = [math.tanh(v * 1.25) for v in out]

    peak = max(abs(v) for v in out) or 1.0
    g = 0.89 / peak
    out = [v * g for v in out]

    fi, fo = int(SR * 0.0006), int(SR * 0.02)
    for i in range(fi):
        out[i] *= i / fi
    for i in range(fo):
        out[n - 1 - i] *= i / fo

    return out


def write_wav(path, samples):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(b"".join(
            struct.pack("<h", max(-32768, min(32767, int(v * 32767)))) for v in samples
        ))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--out", default="SFX_SlashHit.wav")
    a = ap.parse_args()

    dst = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "..", "Assets", "Audio", "SFX", a.out)
    write_wav(os.path.normpath(dst), build(a.seed))
    print("已生成:", os.path.normpath(dst))
