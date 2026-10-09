# EXP-119：缓存噪声按输入窗口连续存储

日期：2026-10-09（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261009-085523-newpaths9070xt` 第 3/5 个假设；1 版实现，1 次追加确认。
起始主仓 `46ced6bc5a9f6803aab6de091f56751b21187618`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

噪声缓存由逐行改为8×8窗口连续布局，同时修改GPU生成器和普通/融合编码输入消费者的地址。每像素x/y/seed/noise计算、half值、随机流和特征运算均保留；缓存大小和权重内容不变。区别于EXP-065的half值传递、EXP-024的工作组LDS打包，本项改变global-memory空间局部性。宿主生成网格始终是pre派发尺寸×8。

8×8、16×24、136×72、2368×1280共3041280个缓存位置的CPU映射无重复、无越界、生产消费索引完全一致，proof SHA256 0e5288c9d012b16613a423d91cd7bae715bf5224fe22dc51598c5bb44eb0a7e3。三个disabled SPIR-V精确复现。目标FP32与全部packed回读一致。普通pre机器码大小25856 B不变，静态指令3509→3510、候选124 VGPR/45 SGPR/6144 B LDS/零scratch。noisefield机器码1152 B不变、静态指令126→131。没有采集cache命中计数，连续地址不是收益证明。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.978340 / 11.019070 / 11.008404 / 11.006539 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-tiled | 11.054422 | 11.052958 | 0.001464 | -0.001598 / 0.005547 / 0.000442 |
| ba | 11.086063 | 11.090065 | -0.004003 | -0.002841 / -0.001568 / -0.007599 |
| confirm-control | 11.085257 | 11.083571 | 0.001687 | -0.000975 / 0.004348 |
| control-tiled | 11.073027 | 11.066109 | 0.006918 | -0.004129 / 0.017965 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

unproven，不保留。初测11.054422→11.052958 ms，只省0.001464 ms，三组混合；唯一反序确认11.086063→11.090065 ms，三组均慢，平均慢0.004003 ms。控制波动覆盖这些小差异，未证实完整路径收益。

ADL 合计 593 个有效样本，核心时钟 77–2736 MHz，edge 39–58°C，hotspot 39–83°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 44 份宿主回读、8012 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
性能未通过，因此未扩大到生成噪声逐half的独立GPU探针、更多seed/gain、另一配方、mask/temporal消费者编译闭包、其它格式/边界、history/replay/lifetime/fallback与完整集成验收。私有候选仅供本项静态筛选，不能安装或用于temporal路线。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / noisefield SPIR-V：`fa081910e73356ed916ae30b059105e39aff5bb46e0468e0f910337e1544704a`。
- disabled / fswinimagepreds32 SPIR-V：`6ec78cb87f11f452e7c8b36c70cd325f81033c541faac48259175fc48fa6813f`。
- disabled / runtime_preencode SPIR-V：`34a29bfd739eddb50f8cb546b6a42fdaa2d4ba728b8ff4054054ce1c345febdc`。
- tiled / noisefield SPIR-V：`4c46479fee335ffd3b278550ed2c6c8e910e18626a1ba99ab9df5fbc894f767b`。
- tiled / fswinimagepreds32 SPIR-V：`dbf194bb3662f26085c3ea23778844e219a097ea74a49bcd891088c0ecda426a`。
- tiled / runtime_preencode SPIR-V：`03b0b5d293ed5812a0f05516028592114bd6f3273c85452d5e5afb0636832ffe`。
- prototype.patch：`b666f0eab59495efa6385defe9bb40f7752a7f4e51b4812cc0ee840b97b1ae63`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：测试仅对packed compose选择wave64；保持同一个8×8工作组与原SPIR-V，查询能力后决定，矩阵内核继续原wave32。
