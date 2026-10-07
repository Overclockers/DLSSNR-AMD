# EXP-043：Persistent 参数加载复用

日期：2026-10-07（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261007-152904-schedule9070xt` 第 14/20 个假设；3 版实现，0 次追加确认。
主仓基线 `8b03c4839f2577dd6beb74ea4e59d6e8fb770935`；宿主 `3a88c92d7d85933cf7c290cbb9fa25f9faa6d688`。

## 假设与实现

六个 DS/UPS 的 item 循环反复读取不变的辅助表。第一版将 DS/UPS 参数块移到循环外；第二版恢复辅助表位置，只在层号改变时更新 20 个普通层参数；第三版仅在三个 DS 上外提辅助参数。原层号查找、队列、同步、浮点与权重不改。与 EXP-014 仅缓存层号边界不同，本项测试实际参数值的读取和存活期。

六份禁用宏 SPIR-V 逐字节复现基线，三版 FP32 全图及宿主 packed 回读均一致。外提使 C256 DS 机器码 224384→222208 B、VGPR 229→225，但 UPS128 VGPR 191→230；普通层缓存则使 UPS128 191→193，并提高若干 SGPR。均无 scratch。静态 scalar-load 数量未减少，移动的是执行位置；低静态指令/寄存器数不足以证明动态收益。最后 DS-only 排除了 UPS 的寄存器变化，仍未获得收益。

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
| ab-cached | 10.810029 | 10.877685 | -0.067656 | -0.085984 / -0.062698 / -0.054285 |
| ab-hoist-ds | 10.817597 | 10.880538 | -0.062940 | -0.078132 / -0.049947 / -0.060743 |
| ab-hoist | 10.822227 | 10.925151 | -0.102923 | -0.086863 / -0.113514 / -0.108393 |
| control-cached | 10.889842 | 10.881589 | 0.008253 | 0.003429 / 0.013078 |
| control-hoist-ds | 10.884826 | 10.882770 | 0.002055 | 0.007588 / -0.003477 |
| control-hoist | 10.880240 | 10.880849 | -0.000608 | -0.001825 / 0.000608 |

三版各 ABBA×3，全部九组变慢：全量外提增加 0.102923 ms，普通层缓存增加 0.067656 ms，仅 DS 外提增加 0.062940 ms。对应控制最大绝对组差约 0.001825/0.013078/0.007588 ms，均不足以解释候选退步。不再追加确认，判为 regressed，三版均不保留。

另保存 64 份原独立宿主回读、12012 个测量帧；回读逐一与独立基线比较。
保存 3 次 standalone 结果（其中 3 份与基线完全一致）；具体失败见上文。
未扩大到中间值、其它格式/边缘尺寸、history/replay、fallback 及宿主集成验收，因为候选未通过前置筛选。
静态输出检查不代替这些未测项。生产和 Linux 源码未应用原型，宿主没有修改或提交。

## 身份与复核

- cached / fswinpds64 SPIR-V SHA-256：`01f5a6b48fe2c17869a7ca397d4231b769dc4c2a776594db0b792a79942d520c`。
- cached / fswinpds128 SPIR-V SHA-256：`bd428c093697b7798cbeb00e437bddd18e35f198f8be7e688deb3c90e255027c`。
- cached / fswinpds256 SPIR-V SHA-256：`6552051f0273bc7c01dbf6a31074ce6b00fdd1b855b44ff5854f1f9c910f226c`。
- cached / fswinpup64 SPIR-V SHA-256：`de546785bcc22d66a4b0bf1a2e3e5a8b558e73f4bfcefddf21351226afbd88ee`。
- cached / fswinpup128 SPIR-V SHA-256：`b826d60e40ebb511b27daaec92f3f24984a99278489bdfac844fc207bf62e251`。
- cached / fswinpup256 SPIR-V SHA-256：`4bf5c5bc0db72a482314ec0e7cc216a033da1c783bfa3e580aea87d3041cbd22`。
- disabled / fswinpds64 SPIR-V SHA-256：`bd003438c359949f70035c9e3c6b94c9dd4fcdc42fe5bd297800965378049f0a`。
- disabled / fswinpds128 SPIR-V SHA-256：`76c494f152893b90cbcff8bf4641b2ea84531cd10e1cd15dce7dfb58e95b73ef`。
- disabled / fswinpds256 SPIR-V SHA-256：`ec8a83af07742e3049a8318a2fd651ea50daee446ab43d3c2aa2f07c24962122`。
- disabled / fswinpup64 SPIR-V SHA-256：`763a81fb44653494dbc59bb9501786f2b0f7582c40f527f35e167136ad47a6ba`。
- disabled / fswinpup128 SPIR-V SHA-256：`7b7ea858828c7de2ba34c509fd8c1f9117c273b5978dfe1bbfb1025df504f8db`。
- disabled / fswinpup256 SPIR-V SHA-256：`dbe2b443324d954ff60d9a24d79453ecc6c8fe9e29af9835f5772ba177e83e21`。
- hoist / fswinpds64 SPIR-V SHA-256：`1460fb8a80fa61d1cde1eb3fd9b83c2ae947729d8e33f7f29284b7af03080423`。
- hoist / fswinpds128 SPIR-V SHA-256：`39b67861cc5d040b7e43fe7585f05c2e5749de972d54820bfbd3d7638d7ed8ad`。
- hoist / fswinpds256 SPIR-V SHA-256：`9a4aef12e3e06905327764104d96a6980c8de56af9047c940e0e45d7fbf11a25`。
- hoist / fswinpup64 SPIR-V SHA-256：`65ac226224ed3a6805039e8ef7852065f9c99ed377396e1343769f430b466ab1`。
- hoist / fswinpup128 SPIR-V SHA-256：`1629d7c52ee7ed19d457376aef8f4a8db2c7bf67ab1d5896a0815de15c6a8984`。
- hoist / fswinpup256 SPIR-V SHA-256：`faebd4150ee94600ed31500e13a9c1a0d69a0e2e02971b2f6b30436962c21993`。
- hoist-ds / fswinpds64 SPIR-V SHA-256：`1460fb8a80fa61d1cde1eb3fd9b83c2ae947729d8e33f7f29284b7af03080423`。
- hoist-ds / fswinpds128 SPIR-V SHA-256：`39b67861cc5d040b7e43fe7585f05c2e5749de972d54820bfbd3d7638d7ed8ad`。
- hoist-ds / fswinpds256 SPIR-V SHA-256：`9a4aef12e3e06905327764104d96a6980c8de56af9047c940e0e45d7fbf11a25`。
- prototype-hoist.patch SHA-256：`e6cad2812e3b741f6299d4f4cfde4d3dae47d6e531f6488a617e2f10f1b55985`。
- prototype.patch SHA-256：`7f610ce290f7edb5c0128637448664c3e1eb45cd03539fb38173003bcb52c3e0`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

源快照、原型、ISA、工具/产物哈希和全部私有结果在忽略的运行目录；本机定位见 local ledger。
只将本记录、索引、状态提交主仓；没有游戏、FSR4/FG、部署、安装、打包、发布、推送或外部消息。

下一步：C512 attention 的实际 ISA 有 176 条 0x3020104 恒等字节重排；测试用对齐 uvec2 构造原 FP8 片段，保持每个字节和 MMA 顺序。ViT 的同类问题作为同一机制的后续变体。
