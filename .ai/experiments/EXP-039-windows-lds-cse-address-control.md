# EXP-039：Windows LDS 地址 CSE 控制

日期：2026-10-07（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261007-152904-schedule9070xt` 第 10/20 个假设；1 版实现，1 次追加确认。
主仓基线 `33343054ec7ee3524c3cb489020bf0e80ee8661e`；宿主 `3a88c92d7d85933cf7c290cbb9fa25f9faa6d688`。

## 假设与实现

原 NR_OPQ 用 gl_WorkGroupID.z 构造 opaque zero，以避免 ACO 跨阶段保留过多 LDS fragments。fresh plan 的目标 C256 DS grid z=1，与源码硬约束相符，因此实际每个偏移均为零。候选将其显式化，仅改变 Windows LLPC 可见的地址表达式；不改变任何实际地址、16-byte 对齐、存储、算术、同步或队列语义。区别于 LDS 布局、预取或 expansion 分块。

disabled SPIR-V 与基线完全相同；显式零后 FP32/packed 全图一致、零 scratch，VGPR 229→233，机器码 224384→223232 字节，静态行 31956→31817。实际 LDS load opcode 数量未下降，主要只是地址/等待调度变化；没有建立减少重复 LDS 读取的机制证据。没有继续扫描部分折叠参数。

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
| ba | 10.809168 | 10.805602 | 0.003565 | 0.023219 / -0.004470 / -0.008052 |
| confirm-control | 10.880167 | 10.876842 | 0.003325 | -0.002621 / 0.009271 |
| pair-ab | 10.828235 | 10.841864 | -0.013629 | -0.028829 / -0.005831 / -0.006227 |
| pair-control | 10.924328 | 10.928476 | -0.004147 | -0.006432 / -0.001863 |

初测三组均慢，平均增加 0.013629 ms；唯一原路径反序确认均差 +0.003565 ms，但三组 +0.023219/-0.004470/-0.008052 ms。确认同路径控制均差 +0.003325 ms，组差 -0.002621/+0.009271 ms。候选效果与控制同级且方向不稳，判为 unproven，不保留。

同进程筛选采用 EXP-032 私有夹具；两个 context 各预热 100 帧，切换后再预热 10 帧，逐块尾 fence 收齐后回读。保存 20 份逐 block 回读、4000 个测量帧，全部逐字节与基线比较。两个 context 同时驻留的分配影响属于该筛选限制。
另保存 22 份原独立宿主回读、4006 个测量帧；回读逐一与独立基线比较。
保存 1 次 standalone 结果（其中 1 份与基线完全一致）；具体失败见上文。
未扩大到中间值、其它格式/边缘尺寸、history/replay、fallback 及宿主集成验收，因为候选未通过前置筛选。
静态输出检查不代替这些未测项。生产和 Linux 源码未应用原型，宿主没有修改或提交。

## 身份与复核

- disabled / fswinpds256 SPIR-V SHA-256：`ec8a83af07742e3049a8318a2fd651ea50daee446ab43d3c2aa2f07c24962122`。
- fold / fswinpds256 SPIR-V SHA-256：`9bbffd23b05028306326445bc5d9e0268180e0d540a2c1cdf83db4e43094be7e`。
- prototype.patch SHA-256：`a258d340d56e8c0149ee7fe1d5db6c2f8fabb83db97a0e24528bd8784c34a0e9`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

源快照、原型、ISA、工具/产物哈希和全部私有结果在忽略的运行目录；本机定位见 local ledger。
只将本记录、索引、状态提交主仓；没有游戏、FSR4/FG、部署、安装、打包、发布、推送或外部消息。

下一步：研究约 0.133 ms 的 packed compose dispatch：先检查实际 driver VGPR，再测较大 workgroup 是否改善每个工作组的 wave 数和图像访问排列。另保留中间网络 command-buffer 录制复用方向，需先检查 controls/timing/lifetime 的依赖。
