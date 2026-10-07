# EXP-049：Image post 的 RGB 权重面板读取

日期：2026-10-07（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261007-152904-schedule9070xt` 第 20/20 个假设；3 版实现，1 次追加确认。
主仓基线 `b6a7cc603554532cb0810e2d32522a3ba3ff5b47`；宿主 `1cc36f96205e361afe5242ea2c330f84b5485b2a`。

## 假设与实现

已有 image post 的实际 ISA 包含 49 次 s_buffer_load_b32，RGB 投影对每个通道对分别读取三个平面的权重。
本假设在原 uint/half 配对和通道累加顺序不变的条件下，改用同 binding 的 uvec4 视图组合读取。
nr_graph.cpp 的 out_project 使用 put(...,16)，Push 的 image_w_off 是字节地址除以 2，
nr_io_wp 再除以 2 后是 4 的倍数；每个 RGB 平面 32 个 f16、64 字节，每个 16 通道面板 32 字节，
因此所有向量读取都对齐且仅覆盖原有权重。只修改私有 fswin_t.comp / fswin_body.glsl，原 kernel 名称和 packing 不变。
这针对 post RGB 权重，与 EXP-043/045 的 persistent 控制记录和跨阶段存活期不同。

第一版把每个 n 的六个 uvec4 放在输出 MMA 后、fragment 消费前；第二版在原标量表达式位置取向量分量；
第三版把范围缩至 8 通道，仅在每个 token-pair 的低/高半段累加前读取三个 uvec4。
三版均保持原浮点表达式、FP16/FP32 边界和源码通道序列，但必须由实际输出进一步验收。

首次禁用宏编译因通用索引宏额外引入等价整数表达式而不能复现原 SPIR-V；
在任何 GPU 运行前恢复禁用分支的原标量表达式，重编译逐字节一致。这是构建等价性修复，不新增实现变体。

| 实现 | .text 字节 | 静态指令 | s_buffer_load_b32 / b128 | VGPR / SGPR | LDS / scratch |
| --- | ---: | ---: | --- | --- | --- |
| 独立基线 | 24576 | 3312 | 49 / 0 | 122 / 66 | 4096 / 0 B |
| 16 通道面板 | 24064 | 3246 | 1 / 12 | 122 / 66 | 4096 / 0 B |
| 按原位置取分量 | 24704 | 3343 | 49 / 0 | 122 / 68 | 4096 / 0 B |
| 8 通道面板 | 24064 | 3241 | 1 / 12 | 122 / 50 | 4096 / 0 B |

首版 standalone FP32 在 11470640 个分量中有 1279398 个不同，最大绝对差 1.1920928955078125e-7，
平均绝对差约 1.951e-9。差异虽小仍严格拒绝；未定位到具体算术原因，不按容差放行，也未进入其性能筛选。
随后的独立基线复测仍复现原哈希，排除基线产物漂移。
第二版 FP32 完全一致，但编译器将读取拆回原 49 个标量，还增加地址计算，机制未实现，未测宿主性能。
第三版 FP32 逐字节一致，并真实生成 12 个 b128 读取；只有该版进入宿主输出和性能筛选。
表中的 ISA/资源来自匹配的 standalone 编译条件，诊断总时间不用于完整路径收益结论。
所有版本、失败输出、修复前后的构建证据及基线复测均保留。

## 环境与验证

RX 9070 XT，Vulkan 新查询为 AMD proprietary / 26.8.1 (LLPC)，glslang 16.5.0。
当前独立基线清单含 65 个宿主源文件、77 个产物；MSVC 14.51 离线程序与 production DLL 按身份复用。
2258×1270 输入，2368×1280 工作尺寸，800 ViT tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32，宿主为线性 R11G11B10、depth=3；静态照片、无运动历史/SR/FG。
本轮起点完整宿主基线四批：10.713152 / 10.765059 / 10.755895 / 10.752117 ms（各预热 100、测量 200 帧）。
分段诊断完整路径 10.774323 ms、算子 10.355108 ms；不能和普通性能区间混减。
时钟、温度及持续后台负载未采样；未改变时钟或驱动。

完整路径为 D3D12 输入复制至输出复制。每批预热 100、测量 200 帧；初测 ABBA×3、同路径×2，若有 BAAB 表则为唯一反序确认。所有批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-half-panel | 10.762308 | 10.766419 | -0.004111 | -0.030017 / 0.014280 / 0.003406 |
| ba | 10.787492 | 10.784178 | 0.003314 | 0.017364 / 0.000101 / -0.007524 |
| confirm-control | 10.842210 | 10.839634 | 0.002576 | -0.001516 / 0.006667 |
| control-half-panel | 10.783274 | 10.773941 | 0.009332 | 0.014944 / 0.003720 |

第三版完整路径初测基线 10.762308 ms、候选 10.766419 ms，平均慢 0.004111 ms；
唯一反序确认基线 10.787492 ms、候选 10.784178 ms，平均省 0.003314 ms。
初测同路径控制均差 0.009332 ms、最大绝对组差 0.014944 ms；确认控制均差 0.002576 ms、最大绝对组差 0.006667 ms。
A/B 各组方向不一致，确认中一组仅省 0.000101 ms，另一组反而慢 0.007524 ms；收益与控制波动重叠。
因此完整收益未证实（unproven），不保留任何本项原型。

另保存 42 份原独立宿主回读、8006 个测量帧；回读逐一与独立基线比较。
保存 4 次 standalone 结果（其中 3 份与基线完全一致）；具体失败见上文。
本项未扩大完整 NR 到中间值、其它格式/边缘尺寸、history/replay、fallback 及宿主集成验收，因为候选未通过前置筛选。
静态输出检查不代替这些未测项。生产和 Linux 源码未应用原型，宿主没有修改或提交。

## 身份与复核

- demand / fswinimagepost32 SPIR-V SHA-256：`c030ffe729e313b94eba79b87dff085345f8d6f8e760a1d9beba8a6a17385c2e`。
- disabled-repaired / fswinimagepost32 SPIR-V SHA-256：`5c45fc50685b8ee7c18ef1aae0ad12d8ac764dbceabeec569d07e3f812c5d76b`。
- half-panel / fswinimagepost32 SPIR-V SHA-256：`0a768eff145e6c5a71826753bd8b9de4d0108e510033f609c2af079fb0e65f84`。
- panels / fswinimagepost32 SPIR-V SHA-256：`8dd75d9c4c767db55d8ae16418dc1804bd1d6f62f34e675c8145ed1a94971dcb`。
- demand-variant.patch SHA-256：`1fe6c926f5da9824ca7e9ae07d864e401c647f89259ed84b1bcd0ffde5cb5c0c`。
- half-panel-variant.patch SHA-256：`4164e8c16c0d7db0c1f9a2116662d5ff3390f42ad39033253182c789786019a1`。
- prototype.patch SHA-256：`eb8f23152997fff8bbe7260148e516e76e01dfbc75507eb58c8bfe6348c06950`。
- repaired-prototype.patch SHA-256：`d81924b776a5c433d952fa51df41a03cf787d78c64334d90c1c29d39c0db67f6`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

源快照、原型、ISA、工具/产物哈希和全部私有结果在忽略的运行目录；本机定位见 local ledger。
只将本记录、索引、状态提交主仓；没有游戏、FSR4/FG、部署、安装、打包、发布、推送或外部消息。

下一步：本轮 20 个假设额度已耗尽，关闭并提交最后记录后停止；继续保留已验证的 EXP-044/048。
