# EXP-037：概率到 context 的逐 query 流式消费

日期：2026-10-07（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261007-152904-schedule9070xt` 第 8/20 个假设；1 版实现，1 次追加确认。
主仓基线 `8593bfdaa02c18fe26cb3474526a0a51cfb91c10`；宿主 `3a88c92d7d85933cf7c290cbb9fa25f9faa6d688`。

## 假设与实现

原 head-split attention 保存四个 query 的 FP8 概率后才执行 context。候选在每个 query 归一化和量化完成后立即以原 j 顺序乘 V、量化并写 lds_y，只用于 V 完全在寄存器中的 DS64/128/256 和 UPS64。Q/K 与 V 的生成位置不变；下一 query 只读 Q/K/V 寄存器，不会读取刚写的 context LDS。最终跨 head barrier 与输出投影保持。与 EXP-036 调整 V 投影时间不同，本次改变概率结果存活期。

四份 disabled SPIR-V 与基线一致，候选 FP32/packed 输出一致、零 scratch，但最大 VGPR/LDS 均未下降；静态指令行略增 24–41。没有使用旧 stage markers 做阶段归因，因为流式 context 已移动到那些 marker 之前；只用完整 dispatch/完整宿主范围。

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
| ba | 10.851840 | 10.846313 | 0.005527 | 0.015107 / 0.004396 / -0.002923 |
| confirm-control | 10.898008 | 10.902244 | -0.004236 | -0.005947 / -0.002525 |
| pair-ab | 10.855026 | 10.850603 | 0.004422 | 0.004715 / 0.001376 / 0.007175 |
| pair-control | 10.942648 | 10.944776 | -0.002128 | -0.014601 / 0.010346 |

初测均差 +0.004422 ms，原路径唯一反序确认 +0.005527 ms，但确认三组为 +0.015107/+0.004396/-0.002923 ms。初测控制可达 ±0.014601 ms，确认控制均差 -0.004236 ms；当前效果未可靠超过控制波动，判为 unproven，不保留。

同进程筛选采用 EXP-032 私有夹具；两个 context 各预热 100 帧，切换后再预热 10 帧，逐块尾 fence 收齐后回读。保存 20 份逐 block 回读、4000 个测量帧，全部逐字节与基线比较。两个 context 同时驻留的分配影响属于该筛选限制。
另保存 22 份原独立宿主回读、4006 个测量帧；回读逐一与独立基线比较。
保存 1 次 standalone 结果（其中 1 份与基线完全一致）；具体失败见上文。
未扩大到中间值、其它格式/边缘尺寸、history/replay、fallback 及宿主集成验收，因为候选未通过前置筛选。
静态输出检查不代替这些未测项。生产和 Linux 源码未应用原型，宿主没有修改或提交。

## 身份与复核

- disabled / fswinpds64 SPIR-V SHA-256：`bd003438c359949f70035c9e3c6b94c9dd4fcdc42fe5bd297800965378049f0a`。
- disabled / fswinpds128 SPIR-V SHA-256：`76c494f152893b90cbcff8bf4641b2ea84531cd10e1cd15dce7dfb58e95b73ef`。
- disabled / fswinpds256 SPIR-V SHA-256：`ec8a83af07742e3049a8318a2fd651ea50daee446ab43d3c2aa2f07c24962122`。
- disabled / fswinpup64 SPIR-V SHA-256：`763a81fb44653494dbc59bb9501786f2b0f7582c40f527f35e167136ad47a6ba`。
- stream-ctx / fswinpds64 SPIR-V SHA-256：`a134e90420fdab724034821799f280950b7f6fc7aec4cf997549214085e2b39a`。
- stream-ctx / fswinpds128 SPIR-V SHA-256：`6c4e425484c4001e568709d9b1b36fd3d56807a998972effd93a6ebefac497e9`。
- stream-ctx / fswinpds256 SPIR-V SHA-256：`68f5101a4cdf85f70bca93555e385c5e660103ff8c8d0682b13a4062261b8b28`。
- stream-ctx / fswinpup64 SPIR-V SHA-256：`30a80e71586b29af9dbf10b894441aedb82be7784aeec6333b44dd3287cea6cc`。
- prototype.patch SHA-256：`e972af03f56bd68319b8d63f84b0628ae605c8a3ed28e6584d693bdd44b1b3c3`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

源快照、原型、ISA、工具/产物哈希和全部私有结果在忽略的运行目录；本机定位见 local ledger。
只将本记录、索引、状态提交主仓；没有游戏、FSR4/FG、部署、安装、打包、发布、推送或外部消息。

下一步：转向新的高成本形状：输入/输出 image Swin 各约 0.9 ms，检查单 wave 拥有完整 64-token 窗口、合法保留 K/V 寄存器是否能减少跨 wave 交换。decoder upsample 仅约 0.0054 ms，先降低其 gain 向量读/地址简化优先级，不为未实现想法计数。
