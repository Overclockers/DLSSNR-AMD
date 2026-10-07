# EXP-019：C512 投影残差片段读取

日期：2026-10-07。结论：未保留；第 4 个假设，一版实现、一次反序确认，平均差异未超过噪声。
主仓 `aa5d8fda062df77b06e073216dcec4d65cc5f734`，宿主 `3a88c92d7d85933cf7c290cbb9fa25f9faa6d688`；新基线见 [EXP-016](EXP-016-windows-vit-expand-n-split.md)。

## 具体机制

目标为 31 次 C512 普通/FFWD 投影，M/N/K=3040/512/512、每次 380 组×4 waves、每 wave 32×32。
基线 `gemmproj` 的四个输出片段共含 32 条 `buffer_load_u8` 残差读取。
原型启用已有但未发布的 `NR_RBDA=1`，用等价 RowMajor B cooperative fragment 读取同一 FP8 残差列。
当前 Windows ISA 确认换成 **4 条 `global_load_tr_b64`**；VGPR **75→100**，LDS/scratch 为零。
仅改变读取方式，MMA、每输出 K 次序、FP32/FP16/FP8 舍入、skip 权重、N-pair packing 均保持。
源码记有旧 RADV/4K hang，本实验仅 Windows 目标形状；没有把未查明的其它平台边界当作通过。

## 正确性、范围和身份

先重建 baseline 并要求 SPIR-V 字节相同。统计构建整网图像一致；正式 private shape study 仅替换 C512 的 31 个目标步骤，
不影响共用内核的 ViT 形状。初测/反序确认共 **62 份 live 中间数组**、4 份保存图像和 48 批全图回读逐字节一致。
每份目标数组为 1556480 字节；各 prefix 独立执行，避免读已被 arena 复用的中间值。

- rebuilt SPIR-V SHA-256: `77ef66c73f2c9b25576d4bfd4d753b341a188882d1004fd70ff1c15d26bdaa08`。
- fragment SPIR-V SHA-256: `2d206a3f56ae70225cb3fd691336d33d91239e7a9504de3464e321c36bb8dc62`。
- shape-study 二进制：`b495d060d79dd2034d8caadbeb1ab19403171125ceed5c096ca6faae49cf2b8d`；源码：`ed75a1a263383970abf16906cbcfde9f507592692ec62d4f86e1bf802b253c94`。

## 测量与结论

RX 9070 XT / 26.8.1 (LLPC)，glslang 16.5.0 / MSVC 14.51.36231。
2258×1270、2368×1280 工作尺寸、FP32 照片、scale 1、单遍、无历史/SR/FG。
计时为 **standalone 整网**，不包含宿主搬运/编码/合成。每批预热 100、测量 200 帧。
初测 ABBA×3 + 同路径 ABBA×3；唯一确认 BAAB×3 + 同路径 BAAB×3，共 9600 个测量帧。

| 比较 | 基线 ms | 候选 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| fragment-study ab | 10.540500 | 10.544757 | -0.004256 | -0.015785 / 0.001843 / 0.001174 |
| fragment-study control | 10.641282 | 10.642893 | -0.001611 | -0.003582 / 0.000586 / -0.001837 |
| confirmation-study ab | 10.595447 | 10.597993 | -0.002546 | 0.025525 / -0.036330 / 0.003167 |
| confirmation-study control | 10.662947 | 10.657636 | 0.005311 | 0.011919 / -0.002336 / 0.006349 |

保留了所有批次，包括确认第二组的波动；没有择优删除。初测和确认方向不稳且控制组差异更大，不能认定收益。
更少加载指令同时提高寄存器占用，不能直接推断净提速。没有继续改动语义或堆叠其它参数。
时钟、温度、持续后台利用率未采样；没有部署、游戏、FSR4/FG 或发布测试。
因性能不成立，未进入 host 完整区间、设备地址缺失 fallback、格式/历史/边界尺寸的扩大验收。
如果将来有新证据重新启用，必须限定通过的形状和设备地址能力，保留原 SSBO 路径，并同步新入口 packing/marker/常量。

```text
shape_study 2258 1270 <plan> <prefix> CCSplitSwin16HFfwdProj,CCSplitSwin16HProj <candidate-shaders> --reuse --model-pack <private-model> --spv-dir <baseline-shaders> --pipeline-cache <cache> --in-image <photo>
```

反序确认加 `--study-reverse`。原型、源/产物身份、ISA、CSV 及输出留忽略目录；只提交公开实验记录/索引/状态。
生产、宿主和 Linux 未改，没有推送。下一步测试 C256 persistent DS 扩张阶段的同时存活结果块。
