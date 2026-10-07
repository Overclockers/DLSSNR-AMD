# EXP-045：Persistent 层参数向量读取

日期：2026-10-07（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261007-152904-schedule9070xt` 第 16/20 个假设；1 版实现，0 次追加确认。
主仓基线 `9b0a9cd640a118c7afed71690c9582ed3e39ef99`；宿主 `137b3859a7572bee73c50a6975ab87f11adc4abf`。

## 假设与实现

在 EXP-044 新基线上，将原位置的 20 个普通层参数读取表达为五个 uvec4；24-word 记录由 put(recs,16) 对齐，字段原值、读取时机、item 生命周期、队列与浮点均保持。与 EXP-043 的跨 item 缓存不同，本项只合并 memory 指令。

六份禁用宏 SPIR-V 精确匹配，FP32 与完整宿主 packed 回读均一致。driver ISA 的 DS scalar b32 loads 减少 17 条，并出现 4 条 b128、1 条 b64；UPS 减少 14 条 b32，出现 3 条 b128、1 条 b64。资源并非全面下降：DS128 VGPR 217→241，多个内核 SGPR 增加；均无 scratch。读取指令减少没有构成运行时间收益。

## 环境与验证

RX 9070 XT，Vulkan 新查询为 AMD proprietary / 26.8.1 (LLPC)，glslang 16.5.0。
核对 65 个宿主源文件、76 个产物身份；MSVC 14.51 构建的现有离线程序与 production DLL 按身份复用。
2258×1270 输入，2368×1280 工作尺寸，800 ViT tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32，宿主为线性 R11G11B10、depth=3；静态照片、无运动历史/SR/FG。
本轮起点完整宿主基线四批：10.713152 / 10.765059 / 10.755895 / 10.752117 ms（各预热 100、测量 200 帧）。
分段诊断完整路径 10.774323 ms、算子 10.355108 ms；不能和普通性能区间混减。
时钟、温度及持续后台负载未采样；未改变时钟或驱动。

完整路径为 D3D12 输入复制至输出复制。每批预热 100、测量 200 帧；初测 ABBA×3、同路径×2，若有 BAAB 表则为唯一反序确认。所有批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-vec | 10.767261 | 10.890465 | -0.123205 | -0.136133 / -0.116989 / -0.116492 |
| control-vec | 10.844456 | 10.852893 | -0.008437 | -0.007984 / -0.008891 |

完整路径三组分别慢 0.136133/0.116989/0.116492 ms，平均增加 0.123205 ms；同路径控制组差 -0.007984/-0.008891 ms。性能退步明确，不追加确认，判为 regressed，不保留。此前 EXP-044 的保留优化继续有效。

另保存 22 份原独立宿主回读、4006 个测量帧；回读逐一与独立基线比较。
保存 1 次 standalone 结果（其中 1 份与基线完全一致）；具体失败见上文。
本项未扩大完整 NR 到中间值、其它格式/边缘尺寸、history/replay、fallback 及宿主集成验收，因为候选未通过前置筛选。
静态输出检查不代替这些未测项。生产和 Linux 源码未应用原型，宿主没有修改或提交。

## 身份与复核

- disabled / fswinpds64 SPIR-V SHA-256：`bd003438c359949f70035c9e3c6b94c9dd4fcdc42fe5bd297800965378049f0a`。
- disabled / fswinpds128 SPIR-V SHA-256：`76c494f152893b90cbcff8bf4641b2ea84531cd10e1cd15dce7dfb58e95b73ef`。
- disabled / fswinpds256 SPIR-V SHA-256：`ec8a83af07742e3049a8318a2fd651ea50daee446ab43d3c2aa2f07c24962122`。
- disabled / fswinpup64 SPIR-V SHA-256：`763a81fb44653494dbc59bb9501786f2b0f7582c40f527f35e167136ad47a6ba`。
- disabled / fswinpup128 SPIR-V SHA-256：`7b7ea858828c7de2ba34c509fd8c1f9117c273b5978dfe1bbfb1025df504f8db`。
- disabled / fswinpup256 SPIR-V SHA-256：`dbe2b443324d954ff60d9a24d79453ecc6c8fe9e29af9835f5772ba177e83e21`。
- vec / fswinpds64 SPIR-V SHA-256：`2eb41e1aeee2198fa5124ad2e3864596cd61d58b764f880a236f8a23a9e8f5ef`。
- vec / fswinpds128 SPIR-V SHA-256：`afbc2c2a688f89228b059a62958d6c716e4affb0bd42ffc2a92fd4b07a938a3d`。
- vec / fswinpds256 SPIR-V SHA-256：`85f06f39a8b39fcf76be7f52aa83f6d78180abce019ed0c077403a3e431f9dfc`。
- vec / fswinpup64 SPIR-V SHA-256：`8c57e44c2f1ab07d00f799f29edcce44e9056195e1ba1fd4aa1e3cbf7cdec0cb`。
- vec / fswinpup128 SPIR-V SHA-256：`0e9d1104324f6d05df45e8f291a4c05ac4e7fbc40d09e803f8242ee72b3b54e4`。
- vec / fswinpup256 SPIR-V SHA-256：`b4698832602ceb50bedd0014ded6573a205dfe1617a1bbd4da82bac7594e29a2`。
- prototype.patch SHA-256：`803dd9b533baa11482081ffa6a8372c9cf7c8f8f646c3a02fb002ed1070ee121`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

源快照、原型、ISA、工具/产物哈希和全部私有结果在忽略的运行目录；本机定位见 local ledger。
只将本记录、索引、状态提交主仓；没有游戏、FSR4/FG、部署、安装、打包、发布、推送或外部消息。

下一步：将同一 subgroup 的就绪消费者合并预留队列位置；保持每个依赖计数、epoch、发布原子操作和工作组屏障，比较实际原子指令及完整路径。
