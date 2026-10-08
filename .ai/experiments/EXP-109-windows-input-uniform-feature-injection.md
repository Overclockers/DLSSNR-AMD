# EXP-109：输入层统一特征直接构造矩阵片段

日期：2026-10-09（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261009-005048-lossless9070xt` 第 3/10 个假设；2 版实现，1 次追加确认。
起始主仓 `16e056a67b60c6f059754ca0ebbbc834ca89c3aa`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

无 external control mask 时，输入特征 10..15 为每个像素相同的 style/tone/structure/skin/other/zero。两版均保留原 half 转换、前十个变化特征、barrier、矩阵布局与所有算术，只在消费 B16 fragment 时直接填入这些统一值。mask 路径维持原实现。第一版省略六个 half 的每像素 LDS 写入；第二版针对驱动读取变窄，将已转换的前十个 half 按 uint 成对暂存，精确解包。与 EXP-024/025/078 的全特征布局/交换不同。

两版独立 transport probe 都覆盖 65536 窗口、4194304 像素、每个非恒零特征的全部 65536 half 编码（含 NaN/Inf/±0/subnormal），各 134217728 字节输出与真实 cooperative B16 基线相同，SHA256 d427002129031fabe467d9254ee56ee3f70d6c49c69d13143e324010cc759cd3。探针测转换后的传递，未将其误称为新的浮点算术证明；实际 float→half 操作保持且完整 FP32/packed 图像一致。探针一次缺 int16 扩展的编译错误已修复，无 GPU 错误。disabled 普通 pre 与 runtime_preencode 精确复现。普通 pre 第一版静态 LDS 两次 b128 读取变成 20 次 u16，store_b16 16→10；第二版为四次双地址 b32 加两次 b32 读取，写入为两次双地址 b32 加一次 b32。第二版 .text 25856→25728 B、静态指令 3509→3470；两版均为 124 VGPR、46 SGPR、6144 B LDS、零 scratch。此 ISA 结论仅来自普通 pre；宿主实际 runtime_preencode 通过完整回读和计时，未另外采集该模块 ISA。

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
| ab-uniform-packed | 11.525387 | 11.487950 | 0.037437 | -0.750572 / 0.107562 / 0.755320 |
| ab-uniform | 11.024363 | 11.009044 | 0.015319 | 0.049174 / -0.039652 / 0.036436 |
| ba | 11.214902 | 11.342839 | -0.127937 | 0.048596 / -0.465906 / 0.033500 |
| confirm-control | 11.321192 | 11.479009 | -0.157817 | -0.427394 / 0.111760 |
| control-uniform-packed | 11.185819 | 11.045247 | 0.140572 | 0.367592 / -0.086447 |
| control-uniform | 11.042646 | 11.041236 | 0.001410 | 0.013567 / -0.010747 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

unproven，两版均不保留。第一版均值看似省 0.015319 ms，但三组正负混合；第二版初测省 0.037437 ms，小于同路径均差 0.140572 ms。第二版唯一反序确认反而慢 0.127937 ms，控制差 -0.157817 ms，仍有明显尖峰与正负组混合。全部批次保留，不以机器码缩减或最佳组宣称完整路径提速。

ADL 合计 992 个有效样本，核心时钟 6–3130 MHz，edge 35–55°C，hotspot 35–80°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 67 份宿主回读、12021 个測量帧；全部完整输出与独立基线逐字节比较。
保存 2 份整网 standalone 数值结果，其中 2 份一致。
未通过性能前置筛选，因此未扩大到实际输入特征中间值、另一套配方、temporal/mask 变体、其它格式、边界尺寸、目标历史、replay/lifetime/fallback 和完整集成验收。独立探针与静态图不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / fswinimagepreds32 SPIR-V：`6ec78cb87f11f452e7c8b36c70cd325f81033c541faac48259175fc48fa6813f`。
- disabled / runtime_preencode SPIR-V：`34a29bfd739eddb50f8cb546b6a42fdaa2d4ba728b8ff4054054ce1c345febdc`。
- uniform / fswinimagepreds32 SPIR-V：`3daf044422f17414f673225d0854238ebe2058ad653f906e9950910d2900b77a`。
- uniform / runtime_preencode SPIR-V：`61058ee5ccf58551acc1abd00071ea755d4e4f59d3fbcc10306de32f820915b5`。
- uniform-packed / fswinimagepreds32 SPIR-V：`ba4706b98b2ee12d8f54506895e1e55e72c1b4052ee9993c66a4e0f61737e11a`。
- uniform-packed / runtime_preencode SPIR-V：`bf61fc82bfc6cb6ec229c4c9327516b80a8c40d7b4d7de77198eb87e2873ecf8`。
- packed-variant.patch：`7d5646a31224a5c09631b643c5fde02fb4442444ed0009603497af0199eb9da3`。
- prototype.patch：`c33ac0bdcd99fe95cd78194434dbf44d20af53531c05b109bc3cdd79df10fa62`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查 C32 QKV 的小型权重片段能否在 query fragment 之间复用，保持每份累加器原 K 次序。
