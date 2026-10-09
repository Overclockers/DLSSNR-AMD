# EXP-134：ViT 扩张 bank 内权重优先加载

日期：2026-10-10（Asia/Hong_Kong）。结论：未保留（mechanism_not_realized）。
运行 `20261009-235203-ten9070xt` 第 4/10 个假设；1 版实现，0 次追加确认。
起始主仓 `0b261a904c774c252d9432c178303db6b8632e1c`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

ViT扩张每wave有128个FP32累加值，原prime依次发出activation与weight片段加载。候选仅交换NR_LOADS的两个独立宏组，保持PIPE=3、KFRAG=2、矩阵形状、bank索引和全部MMA顺序；不同于EXP-016的N/K分块及EXP-055的跨chunk携带。

disabled SPIR-V精确一致，目标FP32和所有packed末帧一致。driver .text均13312 B、静态行均1720，VGPR157/SGPR20/零LDS/scratch相同。ELF字节虽变化，但逐条检查仅14个位置不同：2个prologue描述符加载交换0x30/0x70偏移，12个数据加载的描述符寄存器随之改号。真正的输入/权重数据加载、WMMA及wait所在位置完全相同。证明SHA256 `abdd538b25cf2a043064c68c81cb16025c88d94fa5421e03746c5574d7855dd9`。SPIR-V或ELF hash变化不能证明目标调度机制发生。

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
| ab-weights-first | 10.957578 | 10.948730 | 0.008848 | 0.021826 / 0.001431 / 0.003287 |
| control-weights-first | 10.950327 | 10.954975 | -0.004648 | -0.001298 / -0.007998 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

预期权重数据提前加载未实现，mechanism_not_realized，不保留。查明此点时自动初筛已启动，保留其完整ABBA与控制结果：均差 0.008848 ms，首个baseline批次帧标准差0.269673 ms，其余多约0.04–0.06 ms，不删样或把表面均差登记为收益。因机制门槛已明确失败，不消耗追加确认。

ADL 合计 311 个有效样本，核心时钟 0–2729 MHz，edge 39–55°C，hotspot 40–80°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 23 份宿主回读、4009 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / gemmvact SPIR-V：`d2f933764c49db726f35d45f7c8cff8cabbf75b9842d28d896e98499d910a9d2`。
- weights-first / gemmvact SPIR-V：`ecc027be06aeff0d160796f3103701280d0421b1bdfa263c8672f8f670bc17c6`。
- prototype.patch：`cf640f7689b6fc54869de1e16d14f9f4d57d776262236c8099797dcb2609ff14`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查Decoder上采样已有向量数据的成对half乘法与加法，保持两次独立舍入，测试能否减少标量运算。
