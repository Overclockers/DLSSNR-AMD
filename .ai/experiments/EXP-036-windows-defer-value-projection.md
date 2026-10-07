# EXP-036：延后 V 投影以缩短存活期

日期：2026-10-07（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261007-152904-schedule9070xt` 第 7/20 个假设；2 版实现，1 次追加确认。
主仓基线 `57cc650b1d3e3c908fb475856b4636c538d24dcd`；宿主 `3a88c92d7d85933cf7c290cbb9fa25f9faa6d688`。

## 假设与实现

V 在 context product 前才被消费，却原先在 QK attention 前计算。候选逐字移动 V 投影/量化 block 到概率计算之后，保持独立累加器、每个 K 次序、所有转换、guards 和 barrier。QK/概率不读 V，V 不读概率，lds_x 在这段期间不被修改，因此源级没有交换任何依赖计算。第一版覆盖六个 persistent DS/UPS；第二版根据新逐段证据只覆盖 C64 UPS。

六个禁用版模块与基线逐字节一致。全覆盖版零 scratch，C64 DS/UPS 分别 192→189、194→187 VGPR，其他主要资源不变。普通 standalone 分段 C64 UPS 610.6→588.4 us，但 C128 DS/C256 DS/C128 UPS 分别慢 6.8/3.0/1.7 us，因此第二版只替换同一份 C64 UPS SPIR-V；其余模块恢复基线。源级没有新增算术或变化精度/归约；四份 FP32 与全部 packed 输出一致。不从静态 VGPR 推断实际 wave 驻留已改善。

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
| ba | 10.840283 | 10.817464 | 0.022818 | 0.040029 / 0.014684 / 0.013742 |
| confirm-control | 10.919870 | 10.902977 | 0.016893 | 0.034454 / -0.000668 |
| pair-ab | 10.842186 | 10.856305 | -0.014119 | -0.024849 / -0.013337 / -0.004172 |
| pair-control | 10.933817 | 10.940870 | -0.007053 | -0.014304 / 0.000197 |
| up64-pair-ab | 10.884289 | 10.868248 | 0.016041 | 0.013609 / 0.016506 / 0.018007 |
| up64-pair-control | 10.982413 | 10.960593 | 0.021819 | 0.003663 / 0.039975 |

全覆盖版初测慢 0.014119 ms。C64 UPS 专用版初测三组都快，平均 0.016041 ms；唯一原生产路径反序确认也三组都快，平均 0.022818 ms。但专用版同路径控制均差 0.021819 ms、最大组差 0.039975 ms，确认控制均差 0.016893 ms、最大组差 0.034454 ms。局部改善线索存在，完整路径效果尚未可靠区别于控制波动，判为 unproven、不保留；全部控制异常和批次保留，没有追加第二次确认。

同进程筛选采用 EXP-032 私有夹具；两个 context 各预热 100 帧，切换后再预热 10 帧，逐块尾 fence 收齐后回读。保存 40 份逐 block 回读、8000 个测量帧，全部逐字节与基线比较。两个 context 同时驻留的分配影响属于该筛选限制。
另保存 23 份原独立宿主回读、4009 个测量帧；回读逐一与独立基线比较。
保存 4 次 standalone 结果（其中 4 份与基线完全一致）；具体失败见上文。
未扩大到中间值、其它格式/边缘尺寸、history/replay、fallback 及宿主集成验收，因为候选未通过前置筛选。
静态输出检查不代替这些未测项。生产和 Linux 源码未应用原型，宿主没有修改或提交。

## 身份与复核

- disabled / fswinpds64 SPIR-V SHA-256：`bd003438c359949f70035c9e3c6b94c9dd4fcdc42fe5bd297800965378049f0a`。
- disabled / fswinpds128 SPIR-V SHA-256：`76c494f152893b90cbcff8bf4641b2ea84531cd10e1cd15dce7dfb58e95b73ef`。
- disabled / fswinpds256 SPIR-V SHA-256：`ec8a83af07742e3049a8318a2fd651ea50daee446ab43d3c2aa2f07c24962122`。
- disabled / fswinpup64 SPIR-V SHA-256：`763a81fb44653494dbc59bb9501786f2b0f7582c40f527f35e167136ad47a6ba`。
- disabled / fswinpup128 SPIR-V SHA-256：`7b7ea858828c7de2ba34c509fd8c1f9117c273b5978dfe1bbfb1025df504f8db`。
- disabled / fswinpup256 SPIR-V SHA-256：`dbe2b443324d954ff60d9a24d79453ecc6c8fe9e29af9835f5772ba177e83e21`。
- late-v / fswinpds64 SPIR-V SHA-256：`0b03cdeb7a027d45f53809617c3fdafea0640bf0eaf1af4bb1cd2bc8edfef5dd`。
- late-v / fswinpds128 SPIR-V SHA-256：`35319dc860b3b9094ccc28339ab7e1458b5f1e3afff588f745a2f716090e013c`。
- late-v / fswinpds256 SPIR-V SHA-256：`9f6e8fa00ae66882be9324120eeb4b52bd92ffe7ab176efd1459a5254c513d50`。
- late-v / fswinpup64 SPIR-V SHA-256：`4a73f9aec9577245f5437690adfc1cfeb006cf6a88916f06f2e603c504326876`。
- late-v / fswinpup128 SPIR-V SHA-256：`bac9b9cc5aeafafb96067058c053bc6e3d8a9528321059cc88c0313d2f1d0eb3`。
- late-v / fswinpup256 SPIR-V SHA-256：`0837cc36b2d41ea7525aa22993f40754a44151caf4f275bf90e2c32b2854b35b`。
- late-v-up64 / fswinpup64 SPIR-V SHA-256：`4a73f9aec9577245f5437690adfc1cfeb006cf6a88916f06f2e603c504326876`。
- prototype.patch SHA-256：`03dec3d271e45f5f3a9278af6a54823978c79a50827d4a3d02249e9e5fd7963c`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

源快照、原型、ISA、工具/产物哈希和全部私有结果在忽略的运行目录；本机定位见 local ledger。
只将本记录、索引、状态提交主仓；没有游戏、FSR4/FG、部署、安装、打包、发布、推送或外部消息。

下一步：在 V 完全保存在寄存器的内核中，尝试每个 query 的概率算完就立即计算 context 并写 LDS，减少整个窗口概率片段同时存活。它针对概率结果的存活期，与本次 V 调度不同。
