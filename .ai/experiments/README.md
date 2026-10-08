# 优化实验

已记录至 EXP-089；EXP-001/002/007/009/011/012/013/044/048/056/061/076/088 保留优化，其余轮次未建立可靠新收益。
另完成 EXP-050：当前组合对指定上游的四分辨率直接验收，不计入优化假设预算。
EXP-026/027 补齐队列、硬件计数器和 wave 驻留观测；代码缩减原型未建立可靠完整路径收益。
EXP-028 恢复旧 SQTT 捕获的地址/控制流，获得经过时序检查的 C256 DS 样本；未做新性能 A/B。
按用户要求暂缓游戏测试。

## 索引

| ID | 假设/变更 | 性能结论 | 正确性/画质 | 宿主整合 | 游戏验证 |
| --- | --- | --- | --- | --- | --- |
| [EXP-001](EXP-001-windows-2258-baseline.md) | 恢复两个 C256 边缘专用体 | 离线平均约 -0.097 ms / -0.88% | 静态字节一致；宿主 GPU 套件通过 | 运行时构建、ABI、manifest 通过 | 未测 |
| [EXP-002](EXP-002-windows-small-vit-gemm.md) | 小尺寸 ViT 收缩按 token 数选择线程分工 | 在 EXP-001 基础上约 -0.192 ms / -1.79% | 多输入、边界尺寸字节一致；宿主 GPU 套件通过 | 新内核、权重打包、manifest 与运行时通过 | 用户暂缓 |
| [EXP-003](EXP-003-windows-c32-ffwd-screening.md) | C32 调度、前馈访存/驻留、pre 完整窗口特化，共 20 组候选 | 未保留；pre 最后六对 10.4753 vs 10.4762 ms，收益未复现 | 目标照片一致；未扩大验收范围 | 生产源与宿主未改 | 用户暂缓 |
| [EXP-004](EXP-004-windows-conversion-boundaries.md) | 半精度量化与指数转换链，共 5 组候选；验证融合舍入边界 | 未保留；最后四对 10.4808 vs 10.4860 ms，无提速证据 | 3 组照片一致；2 组改图；38 个融合舍入反例经精确计算复现 | 生产源与宿主未改 | 用户暂缓 |
| [EXP-005](EXP-005-windows-synchronization-arena.md) | 同进程比较同步、绑定与缓冲复用；5 组 GPU 候选 | 未保留；5,960 个测量帧，无稳定收益；完整复用距离多用约 48.4 MB | 23 份照片输出一致；四种尺寸做 CPU 布局审计 | 生产源与宿主未改 | 用户暂缓 |
| [EXP-006](EXP-006-windows-ffwd-projection-fusion.md) | C512 FFWD→projection 融合，4 种跨组 LDS 布局 | 未保留；49,152 对局部测量中候选慢约 11.83%～30.03%，未推进融合整网 | 前/后两层的 24 份算子输出一致，输入和 guard 未改写 | 生产源与宿主未改 | 用户暂缓 |
| [EXP-007](EXP-007-windows-host-input-encode.md) | 完整 NR 宿主路径计时、直接输入编码 | 保留；4,800 帧平均 11.209948→11.194527 ms，约 0.138%；原格式中转候选无收益 | FP32 proxy/keep、目标输出与 15 份序列输出一致 | 核心/着色器同步；无插桩构建、ABI、GPU/切换检查通过；工具留主仓 | 用户暂缓 |
| [EXP-008](EXP-008-windows-attention-empty-query.md) | C512 attention 完全越界查询跳过 | 未保留；12 批 A/B 平均 11.051865→11.057542 ms，无可靠收益 | 22 份照片输出一致；未扩大候选验收 | 两仓生产源未改 | 用户暂缓 |
| [EXP-009](EXP-009-windows-packed-output-alpha.md) | R11G11B10 跳过无效 alpha 整图回写 | 保留；12 批 A/B 平均 11.045790→10.931800 ms，节省 1.032% | 15 份序列、五对额外输出字节一致；两版 ABI/GPU/normal 切换通过 | 核心两仓同步；C++17 与无诊断 DLL 通过 | 用户暂缓 |
| [EXP-010](EXP-010-windows-attention-bias-layout.md) | C512 bias 完全转置/成对布局 | 未保留；确认差约 0.0017 ms，方向不稳且落入控制波动 | 56 份照片输出一致；CPU 索引双射通过 | 原型仅在忽略目录；两仓生产未改 | 用户暂缓 |
| [EXP-011](EXP-011-windows-direct-original-sampling.md) | 合成直接读取原图，省去 FP32 keep 副本流量 | 保留；完整路径初测 -0.048548 ms，反序确认 -0.037930 ms | 48 份 FP32 数组、15 份序列、五对图像一致；fallback 切换通过 | 两仓同步；63 shaders/无诊断 DLL/C++17/工具通过 | 用户暂缓 |
| [EXP-012](EXP-012-windows-packed-output-store.md) | 合成直接写入 R11G11B10，省 FP32 写回与 blit | 保留；初测 -0.075179 ms，最终 production 确认 -0.081756 ms | 转换/原地合成、15 份序列、五对图像、功能/usage 回退一致 | 两仓同步；64 shaders、无诊断 DLL/C++17/工具通过 | 用户暂缓 |
| [EXP-013](EXP-013-windows-shared-workgroup-claim.md) | 用已有 LDS 广播工作组内任务编号 | 保留；初测 -0.035528 ms，确认 -0.033269 ms；独立 slot 版本退步 | 宿主/主仓 recipe 分别逐位对照；历史/重放/多遍通过 | 两仓仅改 shader；各自 58 模块不变、六个受影响 | 用户暂缓 |
| [EXP-014](EXP-014-windows-persistent-layer-lookup.md) | 查表/缓存/二分替代任务层号线性查找 | 未保留；缓存反序确认仅 -0.000608 ms，方向不稳；初测含异常批次 | 59 份照片逐字节一致；未扩大验收 | 两仓生产未改 | 用户暂缓 |
| [EXP-015](EXP-015-windows-packed-compose-specialization.md) | 固定 packed 合成已知尺寸/编码分支 | 未保留；平均增加约 0.006743 ms。附本轮最终直接对照：整体 -0.276771 ms / 2.505% | 转换/原地合成探针、21 份候选照片一致；收尾 20 份输出一致 | 两仓生产未改 | 用户暂缓 |
| [EXP-016](EXP-016-windows-vit-expand-n-split.md) | 新基线/矩阵形状排名；ViT 扩张只拆 N 及加载安排 | 未保留；两版完整路径均退步 | 32 批宿主及六份 standalone 输出一致；未扩大验收 | 两仓生产未改 | 用户暂缓 |
| [EXP-017](EXP-017-windows-ffwd-token-ownership.md) | C512 FFWD 每 wave 从双 tile 改为单 tile | 未保留；反序确认三组略慢 | 32 批宿主、三份 standalone 输出一致 | 两仓生产未改 | 用户暂缓 |
| [EXP-018](EXP-018-windows-vit-projection-token-split.md) | ViT 投影拆 token、增加 wave 或工作组 | 未保留；整网差异不支持净收益 | 24 份中间数组、72 批全图回读一致 | 形状隔离私有工具；两仓生产未改 | 用户暂缓 |
| [EXP-019](EXP-019-windows-projection-residual-fragment.md) | C512 投影用转置片段读取残差 | 未保留；确认差 -0.002546 ms，方向混合且处于控制波动 | 62 份中间数组、48 批全图回读一致 | 两仓生产未改 | 用户暂缓 |
| [EXP-020](EXP-020-windows-c256-expansion-live-results.md) | C256 DS 扩张分别拆 token/hidden-channel 结果块 | 未保留；token 确认与初测反向，通道拆分更慢 | 44 份宿主、六份 standalone 输出一致 | 两仓生产未改 | 用户暂缓 |
| [EXP-021](EXP-021-windows-qkv-head-ownership.md) | QKV 每 wave 单 head、恢复权重向量加载 | 未保留；两版整网约慢 0.024–0.025 ms | 16 份中间数组、48 批全图回读一致 | 两仓生产未改 | 用户暂缓 |
| [EXP-022](EXP-022-windows-c256-uniform-head-addresses.md) | C256 DS 显式 wave/head 标量地址 | 未保留；完整路径三组均慢，平均 +0.031888 ms；附本轮 7/10 收尾 | 20 份宿主、三份 standalone 输出一致 | 两仓生产未改 | 用户暂缓 |
| [EXP-023](EXP-023-windows-vit-key-value-prefetch.md) | ViT attention 完整 K/V 块预取 | 未保留；unproven | 40 份 packed 和 3 份 FP32 全图一致；未扩大验收 | 两仓生产未改 | 用户暂缓 |
| [EXP-024](EXP-024-windows-packed-input-feature-lds.md) | 输入特征 LDS 成对存储 | 未保留；unproven | 40 份 packed 和 3 份 FP32 全图一致；未扩大验收 | 两仓生产未改 | 用户暂缓 |
| [EXP-025](EXP-025-windows-transposed-input-feature-lds.md) | 输入特征 LDS 转置与对齐填充 | 未保留；unproven | 60 份 packed 和 6 份 FP32 全图一致；未扩大验收 | 两仓生产未改 | 用户暂缓 |
| [EXP-026](EXP-026-windows-swin-bottleneck-profiling.md) | 队列时间线、RGP 计数器、C256 contract 局部循环 | 定位有效；C256 DS 指令命中约 44.4%，原型机器码反增 1 KiB，未保留 | 36,555 任务恰好一次；18 份静态 FP32 输出一致 | 私有诊断；两仓生产未改 | 用户暂缓 |
| [EXP-027](EXP-027-windows-c256-common-code.md) | SQTT 驻留解码、C256 expansion 局部循环、公共 DS 投影 | 公共 DS 代码 -12.8%，完整宿主初测/确认方向不同，unproven；循环版 spill | 21 份 FP32、32 份 packed 输出一致；6,400 宿主测量帧 | 两仓生产未改；详细指令解码未验证 | 用户暂缓 |
| [EXP-028](EXP-028-windows-sqtt-compatibility.md) | SQTT 执行 token、计算路由及驱动 ISA 兼容 | C256 DS 样本等待占归属时长 43.68%；无新性能 A/B | 3,429 waves / 1,442 万指令地址与控制流通过；C256 DS 时序无异常，其他内核有限制 | 生产/宿主未改 | 用户暂缓 |
| [EXP-029](EXP-029-windows-swin-bias-prefetch.md) | Swin FP32 偏置提前加载 | 未保留；correctness_failed | 见记录中的精确检查和未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-030](EXP-030-windows-contract-weight-lookahead.md) | C256 contract 权重提前一步加载 | 未保留；unproven | 见记录中的精确检查和未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-031](EXP-031-windows-stream-hidden-middle.md) | 扩张 hidden 到 middle 流式消费 | 未保留；regressed | 见记录中的精确检查和未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-032](EXP-032-windows-paired-context-control.md) | 同进程完整宿主双上下文控制（discovery） | 均差 0.001459 / 0.000136 ms；未证明噪声上限下降 | 32 份逐块 packed 回读一致 | 私有测量工具，生产未改 | 用户暂缓 |
| [EXP-033](EXP-033-windows-qk-normalization-pair.md) | Q/K 归一化成对 lane 交换 | 未保留；unproven | 见记录中的精确检查和未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-034](EXP-034-windows-spatial-ready-order.md) | 初始 ready items 的空间块顺序 | 未保留；regressed | 见记录中的精确检查和未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-035](EXP-035-windows-downsample-input-reuse.md) | 下采样 projection 输入片段复用 | 未保留；unproven | 见记录中的精确检查和未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-036](EXP-036-windows-defer-value-projection.md) | 延后 V 投影以缩短存活期 | 未保留；unproven | 见记录中的精确检查和未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-037](EXP-037-windows-stream-probability-context.md) | 概率到 context 的逐 query 流式消费 | 未保留；unproven | 见记录中的精确检查和未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-038](EXP-038-windows-image-window-wave-ownership.md) | 输入输出 image Swin 单 wave 窗口所有权 | 未保留；rejected | 见记录中的精确检查和未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-039](EXP-039-windows-lds-cse-address-control.md) | Windows LDS 地址 CSE 控制 | 未保留；unproven | 见记录中的精确检查和未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-040](EXP-040-windows-compose-workgroup-shape.md) | Packed compose 工作组形状 | 未保留；unproven | 见记录中的精确检查和未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-041](EXP-041-windows-cached-middle-recording.md) | 固定网络步骤的命令录制复用 | 未保留；unproven | 见记录中的精确检查和未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-042](EXP-042-windows-integer-image-fetch.md) | 图像 pre/post 整数取样 | 未保留；unproven | 见记录中的精确检查和未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-043](EXP-043-windows-persistent-parameter-reuse.md) | Persistent 参数加载复用 | 未保留；regressed | 见记录中的精确检查和未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-044](EXP-044-windows-attention-fragment-word-loads.md) | Attention 对齐 8 字节片段读取 | 保留；初测 -0.021130 ms，确认 -0.030290 ms | 两仓配方各 15 对序列、6 对输入与多遍切换完全一致 | 两仓 Windows shader 同步 | 用户暂缓 |
| [EXP-045](EXP-045-windows-vector-layer-parameters.md) | Persistent 层参数向量读取 | 未保留；regressed | 见记录中的精确检查和未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-046](EXP-046-windows-subgroup-queue-append.md) | Subgroup 合并就绪队列入队 | 未保留；unproven | 见记录中的精确检查和未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-047](EXP-047-windows-post-compose-fusion.md) | Image post 与 packed compose 融合 | 未保留；unproven | 见记录中的精确检查和未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-048](EXP-048-windows-encode-image-pre-fusion.md) | 线性编码与 image pre 融合 | 保留；初测 -0.028813 ms，确认 -0.026916 ms | 两仓序列/边界图/FP32 proxy 精确一致，缺模块回退通过 | 全量离线构建与 ABI 通过 | 用户暂缓 |
| [EXP-049](EXP-049-windows-post-rgb-weight-panels.md) | Image post 的 RGB 权重面板读取 | 未保留；unproven | 见记录中的精确检查和未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-050](EXP-050-windows-upstream-resolution-comparison.md) | 当前分支对 82560c4，四分辨率直接比较 | 核心完整区间：1080p -0.485%、2258×1270 -3.106%、1440p -3.096%、4K -3.483% | 136 份静态图逐字节一致；25,600 个正式测量帧 | 直接编译两端核心；非宿主 DLL A/B | 用户暂缓 |
| [EXP-051](EXP-051-windows-vit-fragment-word-loads.md) | ViT 片段按 8 字节读取 | 未保留；unproven | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-052](EXP-052-windows-vit-tile-dependency.md) | ViT 扩张/收缩的分块依赖 | 未保留；regressed | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-053](EXP-053-windows-vit-token-chunk-locality.md) | ViT token 分块的生产消费局部性 | 未保留；regressed | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-054](EXP-054-windows-vit-contract-transposed-mma.md) | ViT 收缩的转置矩阵与输出布局 | 未保留；unproven | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-055](EXP-055-windows-vit-cross-chunk-prefetch.md) | ViT 扩张的跨 K-chunk 预取 | 未保留；unproven | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-056](EXP-056-windows-vit-contract-fixed-dimensions.md) | ViT 收缩固定 K/N | 保留；确认节省 0.037738 ms | 126 对目标历史、边界/回退、两仓构建精确通过 | 两仓 Windows shader 同步 | 用户暂缓 |
| [EXP-057](EXP-057-windows-vit-empty-wave-exit.md) | ViT 收缩尾部空 wave 提前退出 | 未保留；unproven | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-058](EXP-058-windows-vit-activation-buffer-reference.md) | ViT 激活片段的 buffer-reference 读取 | 未保留；regressed | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-059](EXP-059-windows-gemm-full-subgroups.md) | GEMM 的完整 subgroup 编译契约 | 未保留；no_codegen_change | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-060](EXP-060-windows-vit-paired-k-activation-layout.md) | ViT 相邻 K16 激活 tile 的成对布局 | 未保留；regressed | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-061](EXP-061-windows-gemm-remap-pair-division.md) | GEMM 两列映射整数除法 | 保留；初测 -0.014475 ms，确认 -0.021672 ms | 126 对目标历史、边界/回退、两仓构建通过 | 两仓 Windows shader 同步 | 用户暂缓 |
| [EXP-062](EXP-062-windows-gemm-residual-gain-lookahead.md) | GEMM 尾段残差增益提前加载 | 未保留；unproven | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-063](EXP-063-windows-quant-half-integer-range.md) | FP16 量化范围的整数位域限幅 | 未保留；regressed | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-064](EXP-064-windows-compatible-buffer-binding.md) | 相邻不同 kernel 的兼容 buffer 绑定复用 | 未保留；unproven | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-065](EXP-065-windows-cached-noise-half-transfer.md) | 缓存噪声的 half 直接传递与 NaN 语义 | 未保留；unproven | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-066](EXP-066-windows-vit-expand-qkv-fixed-dimensions.md) | ViT 扩张与 QKV 固定通道尺寸 | 未保留；regressed | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-067](EXP-067-windows-persistent-bounded-window-division.md) | Persistent 窗口坐标的有界整数商 | 未保留；unproven | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-068](EXP-068-windows-persistent-activation-word-loads.md) | Persistent 激活片段的 8 字节读取 | 未保留；mechanism_not_realized | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-069](EXP-069-windows-persistent-scaled-activation-address.md) | Persistent 激活地址的提前缩放 | 未保留；regressed | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-070](EXP-070-windows-post-original-texture-lookahead.md) | Image post 原图纹理提前读取 | 未保留；unproven | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-071](EXP-071-windows-input-lift-weight-reuse.md) | Image pre 的输入投影权重复用 | 未保留；mechanism_not_realized | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-072](EXP-072-windows-preencode-common-rgb-branch.md) | 融合编码的 RGB 共同分支 | 未保留；unproven | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-073](EXP-073-windows-ffwd-middle-pair-stream.md) | C512 前馈 middle 成对流式消费 | 未保留；regressed | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-074](EXP-074-windows-ffwd-cooperative-q0-weights.md) | C512 Q0 权重的工作组协作缓存 | 未保留；regressed | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-075](EXP-075-windows-ffwd-branchless-half-activation-clamp.md) | C512 half 激活的无分支位域限幅 | 未保留；regressed | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-076](EXP-076-windows-post-empty-window-cull.md) | 最终图像层空窗口提前退出 | 保留；初测 -0.046840 ms，确认 -0.058513 ms | 两套配方各 126 对历史、边界/生命周期精确通过 | 两仓 Windows shader 同步 | 用户暂缓 |
| [EXP-077](EXP-077-windows-persistent-task-barrier-coalescing.md) | Persistent 任务边界 LDS 屏障合并 | 未保留；unproven | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-078](EXP-078-windows-input-feature-subgroup-exchange.md) | 输入特征的 subgroup 寄存器交换 | 未保留；unproven | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-079](EXP-079-windows-gemm-remap-power-of-two-span.md) | GEMM 映射的 power-of-two span 整数除法 | 未保留；unproven | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-080](EXP-080-windows-reference-conditioning-control.md) | 原基线统一预热控制（discovery） | 首末控制差 -0.002336 ms；非 NR 提速 | 10 份参考输出精确一致 | 私有测量协议，生产未改 | 用户暂缓 |
| [EXP-081](EXP-081-windows-swin-wave-uniform-residual-gains.md) | Swin 残差增益的 wave-uniform 读取 | 未保留；regressed | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-082](EXP-082-windows-input-lift-shared-weight-cache.md) | 输入投影权重的工作组共享缓存 | 未保留；regressed | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-083](EXP-083-windows-post-interior-blend-addresses.md) | Image post 内部窗口的整数地址路径 | 未保留；regressed | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-084](EXP-084-windows-swin-probability-denominator-bound.md) | Swin 概率分母的严格范围恒等式 | 未保留；unproven | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-085](EXP-085-windows-persistent-atomic-poll-loads.md) | Persistent 队列的显式原子加载 | 未保留；unproven | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-086](EXP-086-windows-vit-attention-tail-epilogue.md) | ViT attention 完整键块循环与尾块分离 | 未保留；correctness_failed | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-087](EXP-087-windows-swin-weight-pair-wide-load.md) | C256 成对权重的显式 16 字节读取 | 未保留；regressed | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |
| [EXP-088](EXP-088-windows-swin-projection-token-batches.md) | C256 DS 输出投影按 query 对分批 | 保留；初测省0.012494 ms，确认省0.007304 ms | 两套配方池化中间值、各126对历史、边界/生命周期精确通过 | 两仓Windows同步 | 用户暂缓 |
| [EXP-089](EXP-089-windows-c32-shared-qk-rsqrt.md) | C32 Q/K 归一化共享 rsqrt | 未保留；regressed | 见记录中的精确检查与未测项 | 两仓生产未改 | 用户暂缓 |

新增实验使用 `EXP-001-short-description.md`，编号为记录标识，不是产品版本。
结果使用明确状态：待执行、有效、无收益、回退、待验证。注明结果适用的 GPU、驱动和尺寸。

## 单个记录的必填内容

1. **假设和范围**：预期解决的瓶颈、涉及文件、默认/有损路径、为何值得测量。
2. **源与产物身份**：基线和候选提交、未提交 diff 的保存位置/哈希、编译器及参数、
   着色器配置与哈希、宿主版本和必要补丁；不保存或分发模型内容。
3. **输入和环境**：GPU、完整驱动、实际输入/输出/填充尺寸、格式、模型尺度、passes、
   效果与历史设置、场景/序列、SR/FG 状态，以及温度/时钟/后台负载等已知条件。
4. **复现命令**：使用可替换路径参数；预热、采样帧数、重复次数和缓存条件。
5. **性能结果**：每轮数据、统计口径、基线与候选的绝对毫秒数及变化幅度。
   总网络、逐内核、宿主搬运、整帧分别列出；未测项目明确写未测。
   交错运行 A/B，避免把升温、时钟变化或插桩开销当作收益。
6. **正确性**：保存独立基线；算术不变时检查输出一致性。
   着色器调度变更可能改变 SPIR-V，不能只凭 SPIR-V 不同判定画质改变。
   数值变化按项目方法比较误差与图像，覆盖运动、历史和不整齐尺寸的边缘。
   有损方案另列开关默认值、恢复路径和可见代价。
7. **集成状态**：内核验证、宿主构建/ABI、真实游戏分别记录通过/失败/未测及证据。
8. **结论**：保留、回退或继续调查；失败原因、适用边界和下一步。

原始采集与对照输出存于忽略目录。共享记录保留足以复现的摘要，本机路径记入 `local/`。
不要把一次最快结果当作结论，不把离线合成输入验证写成真实游戏验证。
