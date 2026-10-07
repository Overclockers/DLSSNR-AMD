# EXP-035：下采样 projection 输入片段复用

日期：2026-10-07（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261007-152904-schedule9070xt` 第 6/20 个假设；1 版实现，1 次追加确认。
主仓基线 `8b97318a2634dd8a2ea39196c3346b9fecab98a6`；宿主 `3a88c92d7d85933cf7c290cbb9fa25f9faa6d688`。

## 假设与实现

末端 downsample projection 原按四个输出片段分别遍历 pooled LDS 输入。候选提取 K 为外层，单次加载一个 FP8 输入片段，喂给四个独立 FP32 累加器。每个累加器仍按 K 递增，保持原 FP16→FP8 epilogue、padding/shear 写入规则与 barrier。只改 C64/C128/C256 DS；与 EXP-027 共享静态 DS 代码不同，这里直接减少执行读取的机会。

三份 disabled SPIR-V 与基线完全一致，候选 FP32/packed 正确性检查通过且资源占用不变、零 scratch。C256 完整 body 最后投影区间的 64 条 WMMA 不变，LDS load 指令由 32 减为 8；权重读取被驱动移至该 barrier 之前，不能把区间内零 buffer loads 误称为取消了权重读取。C256 机器码 224384→222464 字节，静态行 31956→31626。

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
| ba | 10.850398 | 11.023655 | -0.173256 | -0.515274 / 0.000922 / -0.005416 |
| pair-ab | 10.853282 | 10.848004 | 0.005278 | 0.002159 / 0.008155 / 0.005519 |
| pair-control | 10.934973 | 10.938679 | -0.003706 | -0.009469 / 0.002058 |

同进程初测三组均略快，平均 0.005278 ms，但控制组波动达到约 0.009469 ms。唯一原生产路径反序确认含一批 11.850999 ms、帧内标准差 2.031208 ms 的明显抖动，全部批次保留后均值退步 0.173256 ms；另外两组差仅 +0.000922 / -0.005416 ms，也未复现初测收益。不能删除异常批次或追加第二次确认来挑选结果，判为 unproven，不保留。后续批次已回到普通约 0.07–0.09 ms 帧内标准差；该局部失败不是整个研究的全局阻塞。

同进程筛选采用 EXP-032 私有夹具；两个 context 各预热 100 帧，切换后再预热 10 帧，逐块尾 fence 收齐后回读。保存 20 份逐 block 回读、4000 个测量帧，全部逐字节与基线比较。两个 context 同时驻留的分配影响属于该筛选限制。
另保存 14 份原独立宿主回读、2406 个测量帧；回读逐一与独立基线比较。
保存 1 次 standalone 结果（其中 1 份与基线完全一致）；具体失败见上文。
未扩大到中间值、其它格式/边缘尺寸、history/replay、fallback 及宿主集成验收，因为候选未通过前置筛选。
静态输出检查不代替这些未测项。生产和 Linux 源码未应用原型，宿主没有修改或提交。

## 身份与复核

- disabled / fswinpds64 SPIR-V SHA-256：`bd003438c359949f70035c9e3c6b94c9dd4fcdc42fe5bd297800965378049f0a`。
- disabled / fswinpds128 SPIR-V SHA-256：`76c494f152893b90cbcff8bf4641b2ea84531cd10e1cd15dce7dfb58e95b73ef`。
- disabled / fswinpds256 SPIR-V SHA-256：`ec8a83af07742e3049a8318a2fd651ea50daee446ab43d3c2aa2f07c24962122`。
- reuse / fswinpds64 SPIR-V SHA-256：`f9e5e13fd6f8a9f62a89237a7b0aadf73b708634b3ff659f02d06b0a416729d1`。
- reuse / fswinpds128 SPIR-V SHA-256：`35a8dd19ba831c4173a6f5a471b89266d3df02db3b75946d2acff94c3264e5ee`。
- reuse / fswinpds256 SPIR-V SHA-256：`2d4eca64873b8a46e87f5b74a17ff6dd7f2f900e54f1c43bcad33fc18595888f`。
- prototype.patch SHA-256：`33d1b9c5217ee4e4cbf7b676a621e705ebcfa65da1eef57311ff36f2642a9afd`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

源快照、原型、ISA、工具/产物哈希和全部私有结果在忽略的运行目录；本机定位见 local ledger。
只将本记录、索引、状态提交主仓；没有游戏、FSR4/FG、部署、安装、打包、发布、推送或外部消息。

下一步：将 V 投影延后至 attention 概率计算之后，减少 Q/K 和 V 结果同时存活；保持 V 的全部 K 次序、归一化与量化表达式及同步。
