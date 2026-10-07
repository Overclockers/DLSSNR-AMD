# EXP-029：Swin FP32 偏置提前加载

日期：2026-10-07（Asia/Hong_Kong）。结论：未保留（correctness_failed）。
运行 `20261007-152904-schedule9070xt` 第 1/20 个假设；1 版实现，0 次追加确认。
主仓基线 `10d56b91bcb3c65fff9505d18220dd03ec62aff1`；宿主 `3a88c92d7d85933cf7c290cbb9fa25f9faa6d688`。

## 假设与实现

EXP-028 可信 C256 DS 指令中，0x30208 / 0x31268 的 s_wait_loadcnt 7 后紧接使用 bias 的 v_fma_mixlo_f16 与指数区间。源码在 QK WMMA 后逐 j 加载 FP32 bias。候选把当前 query 的四个 bias fragment 移到 QK WMMA 之前；不重排权重或修改数值表达式，区别于 EXP-010 的布局和 EXP-023 的 K/V 预取。只编译 C256 DS。源级看似独立的加载调度仍必须通过真实位级比较。

关闭原型的 SPIR-V 与独立基线相同。候选仍为 229 VGPR / 107 SGPR / 32768 B LDS / 零 scratch，静态 ISA 31956→31932 行，主要变化为 load-wait 与调度指令。精确输出却不一致：11,470,640 个 FP32 位模式中 8,602,291 个不同，不能视作舍入容差。额外运行同一 stats 程序和原始 shader，输出与新鲜普通基线一致，排除了仅由 stats 工具或 3 帧预热造成差异的解释。未确认根因，不能归咎于驱动或宣称算术等价已经成立。

## 环境与验证

RX 9070 XT，Vulkan 新查询为 AMD proprietary / 26.8.1 (LLPC)，glslang 16.5.0。
核对 65 个宿主源文件、76 个产物身份；MSVC 14.51 构建的现有离线程序与 production DLL 按身份复用。
2258×1270 输入，2368×1280 工作尺寸，800 ViT tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32，宿主为线性 R11G11B10、depth=3；静态照片、无运动历史/SR/FG。
新鲜完整宿主基线四批：10.713152 / 10.765059 / 10.755895 / 10.752117 ms（各预热 100、测量 200 帧）。
分段诊断完整路径 10.774323 ms、算子 10.355108 ms；不能和普通性能区间混减。
时钟、温度及持续后台负载未采样；未改变时钟或驱动。

未进入完整宿主性能 A/B；诊断构建时间不用于性能结论。

候选未通过正确性门槛，拒绝保留。没有做性能 A/B，也没有从诊断时长推断提速；只保存这一版失败原型和精确差异证据。

保存 2 次 standalone 结果（其中 1 份与基线完全一致）；具体失败见上文。
未扩大到中间值、其它格式/边缘尺寸、history/replay、fallback 及宿主集成验收，因为候选未通过前置筛选。
静态输出检查不代替这些未测项。生产和 Linux 源码未应用原型，宿主没有修改或提交。

## 身份与复核

- disabled / fswinpds256 SPIR-V SHA-256：`ec8a83af07742e3049a8318a2fd651ea50daee446ab43d3c2aa2f07c24962122`。
- prefetch / fswinpds256 SPIR-V SHA-256：`306ee32679939eee3c5286d52a03643dce09593a034f0d6f4af8c23394fab9f6`。
- prototype.patch SHA-256：`0fc6e255e7a3e002d28be05ebd9f52489b21d0baacfd4557d89bea1fff5ca6a1`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

源快照、原型、ISA、工具/产物哈希和全部私有结果在忽略的运行目录；本机定位见 local ledger。
只将本记录、索引、状态提交主仓；没有游戏、FSR4/FG、部署、安装、打包、发布、推送或外部消息。

下一步：研究 C256 contract 的不可变权重加载与 LDS 读取交错，避免继续扩大 bias 存活期。每个累加器保持 K 顺序，并从相同基线重新开始。
