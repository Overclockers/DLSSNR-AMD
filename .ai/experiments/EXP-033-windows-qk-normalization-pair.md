# EXP-033：Q/K 归一化成对 lane 交换

日期：2026-10-07（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261007-152904-schedule9070xt` 第 4/20 个假设；1 版实现，1 次追加确认。
主仓基线 `7c8a81e05e88decc46523abf4ef61358b72fdbf6`；宿主 `3a88c92d7d85933cf7c290cbb9fa25f9faa6d688`。

## 假设与实现

head-split 路径分别把 Q/K 的半精度总和与一个零打包，做两次相同 partner 的 lane exchange。候选维持每个 FP32 partial 的 FMA 次序、最终加法树、窄化和半精度加法，仅把 Q/K 两个总和放进同一 f16vec2 后交换一次。只替换 NR_LIVE=15 的完整 body，边缘 body 保留原实现；覆盖六个 C64/C128/C256 DS/UPS 内核。与修改归约树或精度的旧实验不同。

六个禁用版 SPIR-V 全部与基线一致。候选六个驱动机器码均改变，各减少约 44–73 行静态指令；C128 DS 从 217 降至 205 VGPR，其余主要资源不变，所有内核零 scratch。FP32 全图一致后，两个原宿主正确性调用也逐字节一致。随后 20 个同进程筛选/control blocks 和 12 个独立进程确认回读均一致。

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
| ba | 10.849317 | 10.836799 | 0.012518 | 0.023292 / 0.010430 / 0.003831 |
| pair-ab | 10.845058 | 10.851981 | -0.006923 | -0.013092 / -0.002160 / -0.005516 |
| pair-control | 10.940372 | 10.948700 | -0.008329 | -0.023446 / 0.006789 |

同进程筛选三组均慢，平均 +0.006923 ms；唯一原生产路径反序确认平均快 0.012518 ms，三组 +0.023292、+0.010430、+0.003831 ms。初测/确认方向不同，且同路径控制组有 -0.023446 / +0.006789 ms 变化。两个 context 的分配影响不能排除，现有证据不足以认定可重复净收益，判为 unproven，不保留且不追加确认。

同进程筛选采用 EXP-032 私有夹具；两个 context 各预热 100 帧，切换后再预热 10 帧，逐块尾 fence 收齐后回读。保存 20 份逐 block 回读、4000 个测量帧，全部逐字节与基线比较。两个 context 同时驻留的分配影响属于该筛选限制。
另保存 14 份原独立宿主回读、2406 个测量帧；回读逐一与独立基线比较。
保存 1 次 standalone 结果（其中 1 份与基线完全一致）；具体失败见上文。
未扩大到中间值、其它格式/边缘尺寸、history/replay、fallback 及宿主集成验收，因为候选未通过前置筛选。
静态输出检查不代替这些未测项。生产和 Linux 源码未应用原型，宿主没有修改或提交。

## 身份与复核

- disabled / fswinpds64 SPIR-V SHA-256：`bd003438c359949f70035c9e3c6b94c9dd4fcdc42fe5bd297800965378049f0a`。
- disabled / fswinpds128 SPIR-V SHA-256：`76c494f152893b90cbcff8bf4641b2ea84531cd10e1cd15dce7dfb58e95b73ef`。
- disabled / fswinpds256 SPIR-V SHA-256：`ec8a83af07742e3049a8318a2fd651ea50daee446ab43d3c2aa2f07c24962122`。
- disabled / fswinpup64 SPIR-V SHA-256：`763a81fb44653494dbc59bb9501786f2b0f7582c40f527f35e167136ad47a6ba`。
- disabled / fswinpup128 SPIR-V SHA-256：`7b7ea858828c7de2ba34c509fd8c1f9117c273b5978dfe1bbfb1025df504f8db`。
- disabled / fswinpup256 SPIR-V SHA-256：`dbe2b443324d954ff60d9a24d79453ecc6c8fe9e29af9835f5772ba177e83e21`。
- norm-pair / fswinpds64 SPIR-V SHA-256：`a156a2057914ea2270fef4b6119b8397d8a22c3a5946aa47bef8ee4d2f861f2c`。
- norm-pair / fswinpds128 SPIR-V SHA-256：`b86315c84bd0110cca6f311229bd1b977df1e65e20b0ee496a99fc88e0493721`。
- norm-pair / fswinpds256 SPIR-V SHA-256：`7531a23b24212ffbbe9a6e72226ccda5cc17f758b6eb6cb2e16711e42fe3426a`。
- norm-pair / fswinpup64 SPIR-V SHA-256：`1e10c04bb0f694d8cdcb9a6d31bddb668d471528e32f428392ac18f33f754cb5`。
- norm-pair / fswinpup128 SPIR-V SHA-256：`6e03ad0b72ac9cd5ebcd3698ec9a407126c18e1d0ad64f5de33a402afa34247c`。
- norm-pair / fswinpup256 SPIR-V SHA-256：`5dd8f2651d192b02799cf90b57648a753db2eb624efef36df02ef588b742fc20`。
- prototype.patch SHA-256：`90318ab7aa18a8f8dc6d71ffcafe70244f2dba4ece0d0d55ea9ef4183e05687a`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

源快照、原型、ISA、工具/产物哈希和全部私有结果在忽略的运行目录；本机定位见 local ledger。
只将本记录、索引、状态提交主仓；没有游戏、FSR4/FG、部署、安装、打包、发布、推送或外部消息。

下一步：研究 ready queue 初始无依赖项的空间块顺序，保持 item ID、依赖计数、发布协议和每个输出的算术不变，检查缓存局部性机制。
