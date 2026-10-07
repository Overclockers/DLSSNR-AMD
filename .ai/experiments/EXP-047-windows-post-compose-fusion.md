# EXP-047：Image post 与 packed compose 融合

日期：2026-10-07（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261007-152904-schedule9070xt` 第 18/20 个假设；1 版实现，1 次追加确认。
主仓基线 `d5909290266649a3a3a6cafe5c2b680f8a7c007b`；宿主 `137b3859a7572bee73c50a6975ab87f11adc4abf`。

## 假设与实现

Runtime alternate 使用原 fswinimagepost32 已完成 packing 的权重、原 128-byte push 及相同模型计算，把生成的 FP32 model pixel 直接送入原同尺寸 linear compose 算术，最后写 packed 输出。仅单遍、同尺寸、无 temporal/mask/preprocess、支持原 direct-packed 的 binding 启用；保留其它路由。没有新图节点或不同 weight layout。每帧参数用 GPU update-buffer 和 compute↔transfer 屏障排序；绑定跟随既有 InputBinding 生命周期，池/内核/buffer 在已完成尾部后销毁。

独立原核心 DLL、原 shader；禁用融合的 post SPIR-V 精确复现基线。once-per-context stderr 标记确认候选实际执行，全部目标 packed 回读相同。最初 nr::logf 被宿主日志设置抑制，仅修复路由标记的可见性，没有将无标记当成提速证据。独立匹配的 pipeline 探针只编译而不执行模型：原 post/融合均 122 VGPR、4096 B LDS、0 scratch，SGPR 66→68；这些资源数来自该探针，不混用旧 standalone 统计。

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
| ab-fused | 10.779554 | 10.773794 | 0.005760 | 0.002882 / 0.005174 / 0.009224 |
| ba | 10.802386 | 10.790545 | 0.011840 | 0.023768 / 0.008594 / 0.003159 |
| confirm-control | 10.857374 | 10.867769 | -0.010396 | -0.006708 / -0.014083 |
| control-fused | 10.850789 | 10.845996 | 0.004793 | 0.010610 / -0.001024 |

初测六批候选/六批基线平均省 0.005760 ms，唯一反序确认省 0.011840 ms，六组方向均正。但初测同路径控制已省 0.004793 ms、最大组差 0.010610 ms；确认控制均差 -0.010396 ms、最大绝对组差 0.014083 ms。较小组收益仅 0.002882/0.003159 ms，与控制明显重叠，仍不足以确认完整收益，判为 unproven，不保留。

另保存 43 份原独立宿主回读、8009 个测量帧；回读逐一与独立基线比较。
保存 0 次 standalone 结果（其中 0 份与基线完全一致）；具体失败见上文。
本项未扩大完整 NR 到中间值、其它格式/边缘尺寸、history/replay、fallback 及宿主集成验收，因为候选未通过前置筛选。
静态输出检查不代替这些未测项。生产和 Linux 源码未应用原型，宿主没有修改或提交。

## 身份与复核

- disabled / fswinimagepost32 SPIR-V SHA-256：`5c45fc50685b8ee7c18ef1aae0ad12d8ac764dbceabeec569d07e3f812c5d76b`。
- fused / runtime/postcompose SPIR-V SHA-256：`38ca61e6483a6a71bee9750bd7a800ec2c9340ce3df6259a91d3fd4ff4421332`。
- prototype.patch SHA-256：`35efb365c8b0ee81593bbe1a3bb2e4238470a200686f60f13d5b18d6c61888eb`。
- baseline DLL SHA-256：`27b1f918a30832409273bf10a5cc5b24b5e72bd9744568562d86b1b4cd77da7d`；源快照、原编译配方及重用 adapter object 哈希保存在 build-source.json。
- fused DLL SHA-256：`994da671f1b32783e022164f66ee24dc4c3d821fb5ad32c9eca0b3807270fb61`；源快照、原编译配方及重用 adapter object 哈希保存在 build-source.json。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

源快照、原型、ISA、工具/产物哈希和全部私有结果在忽略的运行目录；本机定位见 local ledger。
只将本记录、索引、状态提交主仓；没有游戏、FSR4/FG、部署、安装、打包、发布、推送或外部消息。

下一步：测试线性输入 encode 与首个 image pre 的融合；保留供 post/compose 使用的 encoded proxy，避免它的首次读回，严格检查采样、反射边界及编码舍入。
