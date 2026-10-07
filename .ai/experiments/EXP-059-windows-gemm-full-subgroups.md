# EXP-059：GEMM 的完整 subgroup 编译契约

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（no_codegen_change）。
运行 `20261007-214238-codegen9070xt` 第 9/10 个假设；1 版实现，0 次追加确认。
起始主仓 `612444f88d972ddd02dd41c7367f614821f8b903`；宿主 `70484c408f717ba97da8636a2f0346a3e983fc40`。

## 假设与实现

现有网络 pipeline 要求 wave32，但 stage.flags 没有 REQUIRE_FULL_SUBGROUPS。私有原生 benchmark 增加显式 computeFullSubgroups 查询/启用，只为 g_gemm 模块设置该 flag，其 localSizeX=32*NR_WAVES。原始和候选模式用同一冻结可执行切换；未修改 shader、工作组尺寸、MMA、算术、同步或宿主 adopted-device 能力契约。该优化假设在实验登记后才实现，计入第 9 次尝试。

两个模式各预热 3 帧、执行 3 帧，2258×1270 原生 FP32 全图 SHA-256 均为 `17fff022fac3014f6aca032b95105d0f378adc671003a52e440a332e0c89b7fe`，与独立保存基线逐字节相同。IR 捕获绕过 pipeline cache，文件按模式分开，保留所有产物；短期诊断时间不作为性能证据。只读 ADL 采样包含初始化/预热，没有更改 GPU 状态。

| 模块 | 两模式 .text 字节数 | 完整 driver ELF |
| --- | ---: | --- |
| g_gemmproj.spv | 5504 | 逐字节相同 |
| g_gemmpool.spv | 6656 | 逐字节相同 |
| g_gemmnores.spv | 5248 | 逐字节相同 |
| g_gemmvact.spv | 13056 | 逐字节相同 |
| g_gemmprojws.spv | 4736 | 逐字节相同 |
| g_gemmvqkvnorm.spv | 20224 | 逐字节相同 |
| g_gemmprojt.spv | 7296 | 逐字节相同 |
| g_gemmvqkvs.spv | 3328 | 逐字节相同 |

夹具二进制：`4e0d50685889114c3cdb14f68636554a2e3688cafc4b61aa50b21a6857e0020d`；源码清单：`a009cea3f42fee57fc95837b0bbc3cd65b3a7a8cd2ac2d9c8c8f7d038cc4b06b`；比较报告：`2a9bfb417cccaa26a728071f0a9af406eabcf1d4dd8204ef867f4fc096ff976b`。

合法性依据：[Khronos VkPipelineShaderStageCreateInfo](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineShaderStageCreateInfo.html)，VUID 02785 要求启用 computeFullSubgroups，VUID 02757 要求指定 subgroup size 时 localSizeX 是其倍数。本原型仅作用于满足要求的 GEMM。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.640660 / 10.627673 / 10.667158 / 10.678415 ms，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

没有进入完整宿主性能验收；诊断时间不作为提速结果。

本机报告 computeFullSubgroups=1；候选日志确认 8 个 GEMM 模块确实设置 flag=2。八份 .text 和完整 driver ELF（包括资源元数据）均逐字节相同，原模式 .text 也匹配 EXP-056 的当前基线。因此“减少编译器 EXEC/setup 工作”的机制在此配置未发生，判为 no_codegen_change，不保留。没有宣称测得零耗时差，也没有启动不必要的完整宿主性能批次；其它驱动或其它 pipeline 不在结论范围。

未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- prototype.patch：`ca0e745bcb632493919e188288996db89527c51e6aa9629d312c72a3ea2f6200`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：实现相邻 K16 激活 tile 的成对布局，先用原图真实权重/输入的隔离算子对检查逻辑中间值、最终输出、输入/guard 及局部控制，再决定是否进入完整宿主。
