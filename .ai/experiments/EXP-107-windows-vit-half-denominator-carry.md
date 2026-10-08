# EXP-107：ViT attention 半精度分母的循环携带类型

日期：2026-10-09（Asia/Hong_Kong）。结论：未保留（no_codegen_change）。
运行 `20261009-005048-lossless9070xt` 第 1/10 个假设；1 版实现，0 次追加确认。
起始主仓 `5f11e792987aa89d915581f6d99c22c4a29509c5`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

原 transposed attention 每个 K64 小计均窄化为 half，随后以 float 数组跨循环保存。候选将该数组改为 half，保留每次 half 加法、padding 减法和最终 FP32 max/倒数；K64/K16 次序、概率和 context 运算不变。与 EXP-086 的尾块分离和 EXP-102 的部分和调度不同。两模块关闭开关后 SPIR-V 精确复现原基线。

目标 FP32 全输出逐字节相同。vitattn 实际 driver .text 为 14976 字节，基线/候选逐字节一致；2137 条静态指令、132 VGPR、33 SGPR、零 LDS/零 scratch 均不变。编译器已经消除所怀疑的转换，源码类型变化未产生优化机制。vitattnnobda 构建成功但未在目标 BDA 设备路线上执行。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.992494 / 10.960786 / 10.984746 / 10.959362 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

没有进入完整宿主性能验收；诊断时间不作为提速结果。

no_codegen_change：目标实际机器码完全相同，不保留本项，也不为相同代码执行性能验收。本项没有可报告的省时。

保存 1 份整网 standalone 数值结果，其中 1 份一致。
相同目标机器码使前置筛选结束，未运行完整宿主计时/packed 回读、其它配方、格式、历史、边界、中间值、replay/lifetime/fallback 扩展验收。现有 FP32 静态输出检查不是这些验收的替代。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / vitattn SPIR-V：`1aa2e32b083a5b776c92e751713250b80163897fcac3cbd170ccca4aa2b84570`。
- disabled / vitattnnobda SPIR-V：`3cdd38d4115abe4c9c063dd519cecd641ee9bc93956745163cf7f8e502d419ff`。
- half-carry / vitattn SPIR-V：`eae35ea324a50a7f6b8ee515263bd2b79a65df2b78cad7c7141f927beee15855`。
- half-carry / vitattnnobda SPIR-V：`24efa5f4025e2547e28a70205bee74fdf421fa49ea2402606f14a9c4efc6d20d`。
- prototype.patch：`f954b83eba97945514f608bbb5ffcb2383d9fea7d2fcff87dbd3c4cfb188726d`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查 persistent Q/K 联合投影按 query 对分批，降低阶段累加器峰值；不重做扩张分块或已保留的输出投影分块。
