# EXP-038：输入输出 image Swin 单 wave 窗口所有权

日期：2026-10-07（Asia/Hong_Kong）。结论：未保留（rejected）。
运行 `20261007-152904-schedule9070xt` 第 9/20 个假设；3 版实现，0 次追加确认。
主仓基线 `5813758f6ea9bb4c738e81345833fbc4b4baa4b7`；宿主 `3a88c92d7d85933cf7c290cbb9fa25f9faa6d688`。

## 假设与实现

目标形状中 pre/post 各约 0.9 ms。原两-wave 窗口需要 K/V 的 LDS 交换；候选使用受支持的 NR_FWAVES=1、NR_K_REGS=1，使 MF==JF，V_SWAP 也可合法将 V 留在寄存器。源中每像素 feature loop 与最终 projection pair loop 已覆盖完整 64 tokens，每个 token 仍走原数值链。EXP-003 的普通 C32 K 和不受支持 pre 四-wave 不验证这一 Windows image 形状；Linux 注释只提供结构线索，不是 Windows 性能证据。

组合 pre/post 版编译成功、零 scratch，但 FP32 全图不同。分开编译两版定位：pre-only 全图完全相同，post-only 重现组合版的同一输出哈希，因此只有 pre-only 进入性能筛选。三版额度已用完，未弱化比较或尝试额外数学修补。pre VGPR 124→190、LDS 6144→2048 B；post VGPR 122→188、LDS 4096→0 B，但后者正确性不通过，资源下降不能成为采用理由。两个基线 shader 重编字节相同。

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
| pair-ab | 10.842880 | 10.870291 | -0.027411 | -0.042279 / -0.013145 / -0.026809 |
| pair-control | 10.927225 | 10.931179 | -0.003953 | -0.001662 / -0.006244 |

pre-only 完整宿主三组均退步，平均慢 0.027411 ms，各组慢 0.042279/0.013145/0.026809 ms；同路径控制组差约 -0.001662/-0.006244 ms。退步明确，不消耗追加确认。组合版及 post-only 因正确性失败拒绝，pre-only 因性能退步拒绝；无生产修改。

同进程筛选采用 EXP-032 私有夹具；两个 context 各预热 100 帧，切换后再预热 10 帧，逐块尾 fence 收齐后回读。保存 20 份逐 block 回读、4000 个测量帧，全部逐字节与基线比较。两个 context 同时驻留的分配影响属于该筛选限制。
另保存 2 份原独立宿主回读、6 个测量帧；回读逐一与独立基线比较。
保存 3 次 standalone 结果（其中 1 份与基线完全一致）；具体失败见上文。
未扩大到中间值、其它格式/边缘尺寸、history/replay、fallback 及宿主集成验收，因为候选未通过前置筛选。
静态输出检查不代替这些未测项。生产和 Linux 源码未应用原型，宿主没有修改或提交。

## 身份与复核

- disabled / fswinimagepreds32 SPIR-V SHA-256：`6ec78cb87f11f452e7c8b36c70cd325f81033c541faac48259175fc48fa6813f`。
- disabled / fswinimagepost32 SPIR-V SHA-256：`5c45fc50685b8ee7c18ef1aae0ad12d8ac764dbceabeec569d07e3f812c5d76b`。
- post-one / fswinimagepost32 SPIR-V SHA-256：`ac581d07c0da62c03368ecc561809d238442de69e4e2aa6c068f37aa1357583f`。
- pre-one / fswinimagepreds32 SPIR-V SHA-256：`ff1b123b70091435f76a7505e3fc967bf055eb8fdbc6be5bd388724fc0a3d7ff`。
- single-wave / fswinimagepreds32 SPIR-V SHA-256：`ff1b123b70091435f76a7505e3fc967bf055eb8fdbc6be5bd388724fc0a3d7ff`。
- single-wave / fswinimagepost32 SPIR-V SHA-256：`ac581d07c0da62c03368ecc561809d238442de69e4e2aa6c068f37aa1357583f`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

源快照、原型、ISA、工具/产物哈希和全部私有结果在忽略的运行目录；本机定位见 local ledger。
只将本记录、索引、状态提交主仓；没有游戏、FSR4/FG、部署、安装、打包、发布、推送或外部消息。

下一步：检查 Windows LLPC 中 opaque-zero LDS 地址是否还需要抑制 CSE；以真实机器码、VGPR/scratch 和精确输出作为首轮门槛。
