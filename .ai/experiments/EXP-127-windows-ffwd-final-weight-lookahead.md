# EXP-127：C512 FFWD final 权重提前读取

日期：2026-10-09（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261009-fresh-five9070xt` 第 2/5 个假设；1 版实现，0 次追加确认。
起始主仓 `2c316613f1278ac64d2d61cd41feaa81ea5fa2fc`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

ffwd3w 每个 middle chunk 的 half 激活与 FP8 量化独立于 Q2 权重读取。候选在激活前加载 Q2 第一 K 步的四个权重片段，后续仍按原 K 次序消费；不改变 chunk 边界、乘加、权重、舍入、store 或同步。区别于 EXP-073 的 middle 成对流式消费和 EXP-074 的 Q0 工作组共享缓存。

禁用 SPIR-V 精确复现基线，候选整网 FP32 11,470,640 位模式与所有 packed 输出精确一致。VGPR 182→167、SGPR 23、LDS/scratch 0；driver .text 31232→32512 bytes，buffer_load_b128 72→56、buffer_load_b32 32→96、s_wait_loadcnt 88→109。读取粒度变化反映调度尝试有实际代码代价；尚未证明等待被隐藏，不将寄存器下降解释为收益。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：11.009683 / 10.973646 / 10.992009 / 10.992915 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-lookahead | 11.017855 | 11.051254 | -0.033399 | -0.057143 / -0.021464 / -0.021590 |
| control-lookahead | 11.039676 | 11.029960 | 0.009717 | 0.013172 / 0.006261 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

regressed，不保留。完整宿主 11.017855→11.051254 ms，平均慢 0.033399 ms；三组分别慢 0.057143/0.021464/0.021590 ms。控制组最大绝对组差 0.013172 ms。未追加确认或其他变体。

ADL 合计 315 个有效样本，核心时钟 78–2915 MHz，edge 40–56°C，hotspot 41–81°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 23 份宿主回读、4009 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / ffwd3w SPIR-V：`3ad3da9ee684c7951e5d37a7bca62af1ca9e8357b9abbeabbd2cd662aeaaabdb`。
- lookahead / ffwd3w SPIR-V：`c20c8b0a4acc3b13ee98bd1104e275e10342aa84365ace1be6eecc7427f0aeef`。
- prototype.patch：`1474845c9d491939d0ac84a6b485e65a2f883e1d21dacaa8dff49b238f3bb804`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查 C512 attention 两个 context 输出片段的独立累加链，尝试按 key 片段交错更新以增加独立工作。
