# EXP-040：Packed compose 工作组形状

日期：2026-10-07（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261007-152904-schedule9070xt` 第 11/20 个假设；2 版实现，1 次追加确认。
主仓基线 `5a7fd21e19620abd3af813a321143b47c116ae99`；宿主 `3a88c92d7d85933cf7c290cbb9fa25f9faa6d688`。

## 假设与实现

针对约 0.133 ms 的 packed compose，将原 8×8 工作组改为 16×8、32×8；仅 packed shader 的 local_size_x 和对应 runtime dispatch ceiling 成对改变。逐像素算术、格式转换和其它路径保持原样。原 8×8 重编译 SPIR-V 与独立基线相同。

隔离 runtime shader 探针取得三种形状的实际 driver ISA：均为 32 VGPR、31 SGPR、0 LDS、0 scratch，未出现预想的寄存器差异。现有转换与合成探针在三版均通过：每版 40 个合成数组、1,326,120 个值，另含穷举格式转换边界；完整目标输出逐字节一致。该探针是实际 shader 比较，不能替代完整历史与生命周期覆盖。

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
| ab-wide | 10.792263 | 10.790173 | 0.002090 | 0.000511 / -0.000282 / 0.006040 |
| ab-wide32 | 10.794152 | 10.806106 | -0.011954 | -0.014081 / -0.015110 / -0.006672 |
| ba-wide | 10.811868 | 10.817161 | -0.005294 | 0.002291 / -0.021615 / 0.003443 |
| confirm-control | 10.885576 | 10.879022 | 0.006553 | 0.009235 / 0.003872 |
| control-wide | 10.867643 | 10.862378 | 0.005265 | 0.009247 / 0.001283 |
| control-wide32 | 10.865338 | 10.881726 | -0.016389 | -0.018439 / -0.014339 |

16×8 初测节省 0.002090 ms，小于同路径控制 0.005265 ms；唯一反序确认反而平均增加 0.005294 ms，组间方向不一致，确认控制均差 0.006553 ms。32×8 初测三组均慢，平均增加 0.011954 ms，控制也有 0.016389 ms 同级偏差。未建立完整路径收益，判为 unproven，两版均不保留。

另保存 63 份原独立宿主回读、12009 个测量帧；回读逐一与独立基线比较。
保存 0 次 standalone 结果（其中 0 份与基线完全一致）；具体失败见上文。
未扩大到中间值、其它格式/边缘尺寸、history/replay、fallback 及宿主集成验收，因为候选未通过前置筛选。
静态输出检查不代替这些未测项。生产和 Linux 源码未应用原型，宿主没有修改或提交。

## 身份与复核

- disabled / runtime_transfer_packed SPIR-V SHA-256：`7f25cb1fac16344f1a49d170d51f24297d4c041009400339ed9f61fa888022e0`。
- wide / runtime_transfer_packed SPIR-V SHA-256：`5b1b4f42d9bc88d908d77b5c08e268bf2f5de9bdce174ff22e619806bb4c274b`。
- wide32 / runtime_transfer_packed SPIR-V SHA-256：`317d55d9a9d8098b0716b39d38a4f2bcdb88fc22a8b1f77f0b06718365057af4`。
- prototype.patch SHA-256：`c2e6c9a70204e32619a5dffa678b4248ab27803dfd7b9d9fa76b03e233d07be3`。
- baseline DLL SHA-256：`af3e6ae9fd861153f7de3d6d4b61dcb39f9fe6c7652fa0da0e92fe7159d307a8`；源快照、原编译配方及重用 adapter object 哈希保存在 build-source.json。
- wide DLL SHA-256：`b95f348f02b05c8b209c4395224efa81ca13ea932fb5555265c05631b31381b6`；源快照、原编译配方及重用 adapter object 哈希保存在 build-source.json。
- wide32 DLL SHA-256：`3d2f831113bd76716bb08cb8911dba7f0062d03b00bb906227f537880ed27ff9`；源快照、原编译配方及重用 adapter object 哈希保存在 build-source.json。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

源快照、原型、ISA、工具/产物哈希和全部私有结果在忽略的运行目录；本机定位见 local ledger。
只将本记录、索引、状态提交主仓；没有游戏、FSR4/FG、部署、安装、打包、发布、推送或外部消息。

下一步：测量固定中间网络步骤的 secondary command-buffer 复用，保留全部 dispatch/barrier，检查 CPU 录制缩短能否减少完整 NR 间隔。
