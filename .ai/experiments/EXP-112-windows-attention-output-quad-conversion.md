# EXP-112：Attention 与 QKV 最终输出的四分量量化

日期：2026-10-09（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261009-005048-lossless9070xt` 第 6/10 个假设；1 版实现，1 次追加确认。
起始主仓 `af5c4ae0642a661929ac3d978d95ed5958e3c096`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

ViT 最终输出原先逐分量调用 nr_quant_e4m3，fused QKV 归一化末端调用两次 nr_quant2_h，均未被通用 quad rewrite 覆盖。候选将已经按原顺序完成乘法、归一化和 half 舍入的四个独立值送入既有 nr_quant4_h，输出地址与分量映射不变；非向量/诊断输出仍用原路径。本项一次假设包含两个相同机制的输出站点，与 EXP-104 softmax 概率片段转换不同。

三个 disabled 模块精确复现基线 SPIR-V。独立三臂探针在四个分量位置覆盖所有 65,536 种 half 编码（含非有限符号和 payload），每臂 262,144 次转换，scalar/pair/quad 零差异；共同输出 SHA256 371f9679e2a4da4d12dbb661ea72d4ec5d94e9265f800055b39af6934552704f。转换探针源 SHA256 697813843d2b25e10e63b254cba5cc7749cb01f80db8da23e52414bca90bcf0a。另用独立 attention dispatch 检查 16/32/48/63/64/65/80/96/127/128/129/160/256/768/800/832 tokens，全部输出精确一致、保护区零改写；该探针仅 BDA、mode=4、finite FP8 输入，不代表完整历史覆盖。attention proof SHA256 7cecc11fe3bd36df777600b4a18eec5fef7649e3ccaa6a4ad60aa6b98f3eb9e0。目标整网 FP32 和 packed 全图一致。ViT .text14976→14080 B、静态指令2137→1995、FP8转换96→80、MODE写68→44；但64-bit写4→0、32-bit写0→8。QKV .text仍20224 B，指令3041→3056、MODE写64→32。ViT/QKV VGPR132/167、SGPR33/23，零 LDS/scratch，资源均不变；nobda 已构建但目标没有执行。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.992494 / 10.960786 / 10.984746 / 10.959362 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-quad | 10.994983 | 10.990512 | 0.004471 | 0.007522 / 0.007156 / -0.001264 |
| ba | 11.015499 | 11.009958 | 0.005540 | 0.002919 / 0.011975 / 0.001727 |
| confirm-control | 11.018158 | 11.006802 | 0.011356 | 0.022595 / 0.000118 |
| control-quad | 10.999614 | 11.000742 | -0.001127 | -0.003418 / 0.001163 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

unproven，不保留。初测完整路径省 0.004471 ms，组差 +0.007522/+0.007156/-0.001264 ms；唯一反序确认省 0.005540 ms。然而反序同路径控制自身出现 +0.022595 ms 组差，平均假收益 0.011356 ms，超过候选均值，候选的微小改善尚不能可靠区分于测量波动。指令减少确实发生，完整路径稳定提速未获证明；保留全部样本且不追加第三轮。

ADL 合计 619 个有效样本，核心时钟 41–2769 MHz，edge 38–56°C，hotspot 38–81°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 44 份宿主回读、8012 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
已做完整 half 编码与单 attention 的边界长度探针；性能未通过筛选，因此未扩大 QKV 中间值、另一套配方、其它输入格式/图像边界、目标运动历史、replay、lifetime、fallback 和完整宿主集成验收。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / gemmvqkvnorm SPIR-V：`7e3251e0d1cddac21a28f55578c40be707e864327222d64d82dfca16ab9aa0eb`。
- disabled / vitattn SPIR-V：`1aa2e32b083a5b776c92e751713250b80163897fcac3cbd170ccca4aa2b84570`。
- disabled / vitattnnobda SPIR-V：`3cdd38d4115abe4c9c063dd519cecd641ee9bc93956745163cf7f8e502d419ff`。
- quad / gemmvqkvnorm SPIR-V：`675bcd606a59e09c68c5cc1f709541e140c8903f9590ccdff6c3024fb527cd46`。
- quad / vitattn SPIR-V：`0907009d3f9b3e85d8b1310063ea0236b68bec770cc7e669f149ba9b8afdb13a`。
- quad / vitattnnobda SPIR-V：`650a2804848b3a55741c1c4de90595e1d6cf7fce5c6f3b201c4625f647df77ba`。
- prototype.patch：`5c97605c70b278fb706af9938570801ccc35831a9325a549e7f9b46ad97951e7`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：保持 C512 FFWD 每个 wave 的所有权与算术，只将相同权重组的相邻工作组成对排列，验证跨工作组局部性。
