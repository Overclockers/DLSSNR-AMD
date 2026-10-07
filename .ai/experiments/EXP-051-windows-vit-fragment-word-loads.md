# EXP-051：ViT 片段按 8 字节读取

日期：2026-10-07（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261007-214238-codegen9070xt` 第 1/10 个假设；1 版实现，1 次追加确认。
起始主仓 `d42d948cca9ba419847dec55781d199b9c65aa5a`；宿主 `1cc36f96205e361afe5242ea2c330f84b5485b2a`。

## 假设与实现

ViT attention 原 Q/K 片段从两个独立 uint 构造，每 lane 的八个 FP8 字节已连续且对齐。候选增加同 binding 的 uvec2 视图，一次读取再按原序 unpack8。偏移由 256-byte arena tile、16-byte 行和 8-byte half-lane 构成，除以 8 精确；不改变片段、MMA 次序、概率量化、归约、padding 或权重。区别于 EXP-023 的预取与 EXP-021 的 QKV wave 分工，是 EXP-044 明确未实施的 ViT 对应点。

禁用宏重编译与独立基线 SPIR-V 相同；候选 FP32 全图和所有宿主 packed 输出逐字节一致。新鲜驱动 ISA 中同源 identity v_perm 28→16，机器码 14976→14720 字节，静态行 2137→2126；VGPR 132、SGPR 33、LDS/scratch 0 均保持。结构改善成立，但性能必须单独验收。该实验未采样频率/温度或持续后台负载，未修改驱动与时钟。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.640660 / 10.627673 / 10.667158 / 10.678415 ms，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-word8 | 10.758636 | 10.740095 | 0.018541 | 0.012351 / 0.021488 / 0.021784 |
| ba | 10.771779 | 10.780367 | -0.008588 | 0.009551 / -0.021131 / -0.014185 |
| confirm-control | 10.843283 | 10.841822 | 0.001461 | -0.004562 / 0.007484 |
| control-word8 | 10.816043 | 10.819008 | -0.002965 | -0.005025 / -0.000904 |

初测完整路径 10.758636→10.740095 ms，平均省 0.018541 ms；唯一反序确认 10.771779→10.780367 ms，平均慢 0.008588 ms，三组中两组变慢。两次控制最大绝对组差分别约 0.005025、0.007484 ms。初测收益未在反序复现，判为 unproven，不保留。

保存 42 份宿主回读、8006 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / vitattn SPIR-V：`1aa2e32b083a5b776c92e751713250b80163897fcac3cbd170ccca4aa2b84570`。
- word8 / vitattn SPIR-V：`93d2eedefce3a0b8f68fcb4f982d0bec8895e568da72187e1f6fbf3d210d180a`。
- prototype.patch：`b5a53fed1595f8b422183413b95482b017c23c95a4ebd6efe4420505c46bb4c8`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：核对 Linux 分块依赖机制在 Windows 的正向进展与缓冲寿命约束，先以独立算子对建立最小验证，再决定原型边界。
