# EXP-031：扩张 hidden 到 middle 流式消费

日期：2026-10-07（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261007-152904-schedule9070xt` 第 3/20 个假设；2 版实现，0 次追加确认。
主仓基线 `754ddb5c6fb5c2c0bfa25ec415a292d8e0fbd228`；宿主 `3a88c92d7d85933cf7c290cbb9fa25f9faa6d688`。

## 假设与实现

C256 DS 原路径保存每 token 的八个 FP8 hidden fragments 后才执行 middle。候选在每组扩张与原有激活/量化完成后立即喂给 middle，保留一次零初始化与递增 K 累加，最后沿原路量化、LDS 写入与同步。不改变任何权重或归约。与 EXP-020 仅调整 expansion 结果块不同，本次缩短生产者到消费者之间的 hidden 存活期；第二版的分组缩小用于补偿新增 middle 累加器的压力。

原四行分组流式版保持 FP32 全图一致、零 scratch，但 VGPR 从 229 增至 255，没有实现降低寄存器压力的目标，因此未做完整宿主 A/B。第二版使用两行分组，206 VGPR、零 scratch、32768 B LDS，FP32 全图一致后进入完整宿主。重编禁用版 SPIR-V 与基线相同。两版都保留原边缘 body、K 顺序、量化和同步。

## 环境与验证

RX 9070 XT，Vulkan 新查询为 AMD proprietary / 26.8.1 (LLPC)，glslang 16.5.0。
核对 65 个宿主源文件、76 个产物身份；MSVC 14.51 构建的现有离线程序与 production DLL 按身份复用。
2258×1270 输入，2368×1280 工作尺寸，800 ViT tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32，宿主为线性 R11G11B10、depth=3；静态照片、无运动历史/SR/FG。
新鲜完整宿主基线四批：10.713152 / 10.765059 / 10.755895 / 10.752117 ms（各预热 100、测量 200 帧）。
分段诊断完整路径 10.774323 ms、算子 10.355108 ms；不能和普通性能区间混减。
时钟、温度及持续后台负载未采样；未改变时钟或驱动。

完整路径为 D3D12 输入复制至输出复制。每批预热 100、测量 200 帧；初测 ABBA×3、同路径×2，若有 BAAB 表则为唯一反序确认。所有批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-stream-pair | 10.813249 | 10.858962 | -0.045713 | -0.040415 / -0.044332 / -0.052392 |
| control-stream-pair | 10.884748 | 10.884590 | 0.000158 | -0.008232 / 0.008548 |

两行流式版完整宿主三组均退步，平均增加 0.045713 ms（各组 +0.040415、+0.044332、+0.052392 ms）。同路径控制均差 0.000158 ms，组差约 ±0.0085 ms，退步足够明确，不使用追加确认额度。两个原型均不保留，寄存器下降不能替代完整路径收益。

保存 22 份宿主回读、4006 个测量帧；回读逐一与独立基线比较。
保存 2 次 standalone 结果（其中 2 份与基线完全一致）；具体失败见上文。
未扩大到中间值、其它格式/边缘尺寸、history/replay、fallback 及宿主集成验收，因为候选未通过前置筛选。
静态输出检查不代替这些未测项。生产和 Linux 源码未应用原型，宿主没有修改或提交。

## 身份与复核

- disabled / fswinpds256 SPIR-V SHA-256：`ec8a83af07742e3049a8318a2fd651ea50daee446ab43d3c2aa2f07c24962122`。
- stream / fswinpds256 SPIR-V SHA-256：`64a27f5c514b3c5b0b5afbe37c332336a435fefc0671e1fec6d794ce4dde777a`。
- stream-pair / fswinpds256 SPIR-V SHA-256：`52e087eafff6c08e853f779e9278279c3fca718aebbf7ac2d95839c31e5404cd`。
- prototype.patch SHA-256：`22dd80ab217ce69b65a3cf4b11c24866170c5e6ab70e77e851825faf1d78efee`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

源快照、原型、ISA、工具/产物哈希和全部私有结果在忽略的运行目录；本机定位见 local ledger。
只将本记录、索引、状态提交主仓；没有游戏、FSR4/FG、部署、安装、打包、发布、推送或外部消息。

下一步：完成同进程双上下文工具的原路径控制验证，然后研究 Q/K 归一化中两次独立半精度 lane 交换能否保持逐位行为地合并。
