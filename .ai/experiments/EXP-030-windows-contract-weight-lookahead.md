# EXP-030：C256 contract 权重提前一步加载

日期：2026-10-07（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261007-152904-schedule9070xt` 第 2/20 个假设；1 版实现，1 次追加确认。
主仓基线 `7e20a21441d63a88551344cda776f96c34da1ffc`；宿主 `3a88c92d7d85933cf7c290cbb9fa25f9faa6d688`。

## 假设与实现

contract 在每个 K 步读取 LDS 与一对 immutable weights 后执行两个 N 行的 WMMA。候选把两个相邻 K 步的权重提前读取，仍按 kp+t 的原递增 K 次序消费。每个输出的残差初始化、运算顺序、量化、存储、tile guard 和 barrier 未变。它测试加载等待，与 EXP-026 的代码缩减循环及 EXP-020 扩张结果分块不同。

关闭原型时 C256 DS SPIR-V 与独立基线字节相同；开启后只有该模块改变。驱动 ISA 确实改变（memory clause 380→388），保持 229 VGPR、107 SGPR、32768 B LDS、零 scratch。stats standalone 的 FP32 全图与本轮独立基线一致；性能筛选前基线和候选各做三帧宿主回读，随后所有 32 批性能/控制/确认回读也相同。

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
| ab-lookahead | 10.797404 | 10.801098 | -0.003693 | 0.009336 / -0.018287 / -0.002128 |
| ba | 10.885504 | 10.879130 | 0.006373 | 0.032000 / -0.001198 / -0.011682 |
| control-lookahead | 10.872881 | 10.879152 | -0.006271 | -0.011633 / -0.000910 |

初测平均慢 0.003693 ms，唯一反序确认平均快 0.006373 ms；确认三组为 +0.032000、-0.001198、-0.011682 ms，同路径控制平均偏差 -0.006271 ms。收益方向和控制噪声均不足以证明净收益，判为 unproven，不保留；没有追加更多确认。

保存 34 份宿主回读、6406 个测量帧；回读逐一与独立基线比较。
保存 1 次 standalone 结果（其中 1 份与基线完全一致）；具体失败见上文。
未扩大到中间值、其它格式/边缘尺寸、history/replay、fallback 及宿主集成验收，因为候选未通过前置筛选。
静态输出检查不代替这些未测项。生产和 Linux 源码未应用原型，宿主没有修改或提交。

## 身份与复核

- disabled / fswinpds256 SPIR-V SHA-256：`ec8a83af07742e3049a8318a2fd651ea50daee446ab43d3c2aa2f07c24962122`。
- lookahead / fswinpds256 SPIR-V SHA-256：`63345764151c0a863ff60c5e130cc6d9b5f598114e65f07fe1a1c744ce3810e4`。
- prototype.patch SHA-256：`3a8ec392c2ee20b6dce476855e57a30a9c41bec607f8c4c9c99cbb580a9e12b8`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

源快照、原型、ISA、工具/产物哈希和全部私有结果在忽略的运行目录；本机定位见 local ledger。
只将本记录、索引、状态提交主仓；没有游戏、FSR4/FG、部署、安装、打包、发布、推送或外部消息。

下一步：研究 expansion 到 middle 的流式消费，缩短量化后的 hidden 片段存活期；仍保持每个 middle 累加器的 ascending K。
