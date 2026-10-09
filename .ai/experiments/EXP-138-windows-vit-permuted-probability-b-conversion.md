# EXP-138：ViT 概率直接转换 B 片段并保持 key permutation

日期：2026-10-10（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261009-235203-ten9070xt` 第 8/10 个假设；1 版实现，1 次追加确认。
起始主仓 `d9cfe80e0725150477620c342a950ebc397b47f2`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

以半精度 B 片段承载原概率，直接转换 FP8 B，再施加原 VKPERM 字排列。指数、半精度分母、context MMA 顺序与 padding 不变；不是简单启用不兼容的 VPB16+VKPERM。

BDA/non-BDA disabled SPIR-V 均与独立基线一致；FP32 全网和 packed 回读一致。实际 .text 14976 B、2137 条指令、132 VGPR/33 SGPR、零 LDS/scratch 不变。v_bfi_b32 32→16，但 v_perm_b32 28→44，是指令替换而非总量减少。non-BDA 已编译，目标运行未执行。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.943105 / 10.917341 / 10.917457 / 10.944389 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-direct | 10.924133 | 10.920724 | 0.003409 | 0.002039 / 0.014494 / -0.006306 |
| ba | 10.943075 | 10.944143 | -0.001068 | -0.002223 / 0.001195 / -0.002177 |
| confirm-control | 10.951350 | 10.947963 | 0.003387 | 0.004494 / 0.002281 |
| control-direct | 10.931459 | 10.934315 | -0.002856 | -0.003888 / -0.001823 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

初测节省 0.003409 ms，组间正负混合；唯一反序确认节省 -0.001068 ms，控制最大绝对组差 0.004494 ms。未建立超出波动的重复收益，判为 unproven，不保留。

ADL 合计 615 个有效样本，核心时钟 0–2721 MHz，edge 35–55°C，hotspot 36–80°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 44 份宿主回读、8012 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- direct / vitattn SPIR-V：`ca4e7db977c06f823703015f454ee5a19823d2c12e305546b93a84acc3503088`。
- direct / vitattnnobda SPIR-V：`4950078509d97913940ec98e8f83e2c932b9b9772fb6e1e02ba8ea4b6b22a0c1`。
- disabled / vitattn SPIR-V：`1aa2e32b083a5b776c92e751713250b80163897fcac3cbd170ccca4aa2b84570`。
- disabled / vitattnnobda SPIR-V：`3cdd38d4115abe4c9c063dd519cecd641ee9bc93956745163cf7f8e502d419ff`。
- prototype.patch：`b6280e1ca2f1fdcc7c2711d5f552c7c51a001a8920d8a3c6f09e4197c5550d12`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查 GEMM 通道优先 epilogue 能否复用增益并消除 EXP-062 的动态选择开销。
