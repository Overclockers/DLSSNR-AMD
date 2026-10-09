# 下一项实验计划：C256 DS bias 的 lane 连续面板布局

日期：2026-10-09。状态：**已执行并关闭**，结果见
[EXP-125](experiments/EXP-125-windows-bias-lane-panels.md)：布局机制实现、静态输出精确，
VGPR 229→230 触发下述资源停止门槛；未保留，未进入完整宿主性能验收。
以下保留实验前的设计和判据；文末“本次设计边界”描述的是设计阶段。
设计基线：`main-experiment` / `9eb8985d9698ee6e1afa6c347d09c86bc4e51780`。
范围：一个布局假设、一版候选；不恢复此前已耗尽的优化运行。
执行编号已核对为 EXP-125；设计文档与实验结果分别保存。

## 问题与可证伪假设

把 `fswinpds256` 消费的既有 FP32 bias 按每个 wave 的两组四分量加载重排，
使单条加载的 lane 地址连续，能否在保持数值及归约链的前提下降低完整宿主耗时？
目标是改变实际访问地址分布；不要求驱动把相同语句调度得更早，也不预期减少 bias 的逻辑字节数。

当前源码在 `fswin_body.glsl` 的 head-wave logits 段用 `NR_LOAD_ACC_COL`，
按 `[head][query][key]`、stride=64 读取 16×16 bias 片段。
复核 EXP-123 保存的当前基线 ISA：片段读取已经是两条 `buffer_load_b128`，相差 16 字节；
地址构造的 lane 部分为 `4*((lane%16)*64 + 8*(lane/16))`。
因此再写一次 vec4 不会自动解决问题；本项同时改变物理 packing 与读取地址。
所读 ISA 来自 SPIR-V `3dd150b0f770d67564aaa23199fd59dd53014eb748a5b53d16de43f4ad1c92ad`。
这是已有产物的静态证据，不是本次新鲜 GPU 验证。

## 为何选择这一项

| 既有实验 | 对本计划的约束 |
| --- | --- |
| [EXP-123](experiments/EXP-123-windows-bias-prefetch-causality.md) | bias 提前加载未实现；必须检查真实 half 归约，不能仅比较算术计数 |
| [EXP-124](experiments/EXP-124-windows-qk-epilogue-streaming.md) | QK 提前消费未实现；不继续交换同一组语句 |
| [EXP-117](experiments/EXP-117-windows-attention-bias-vector-panels.md) | C512 的源码宽读被编译器归并回原加载；本项要改变存储，而非只改类型 |
| [EXP-010](experiments/EXP-010-windows-attention-bias-layout.md) | C512 完全转置/成对布局无可靠收益；这降低乐观预期，但未测 C256 的四元素 lane 面板 |
| [EXP-087](experiments/EXP-087-windows-swin-weight-pair-wide-load.md) | 更宽 FP8 权重读引入大量重排；必须检查额外 shuffle、permute 和地址指令 |
| [EXP-028](experiments/EXP-028-windows-sqtt-compatibility.md) | 旧 C256 DS 样本有加载等待线索，但没有证明 bias 是主要瓶颈，也不能换算预期毫秒收益 |

选择依据是已有可定位的分散地址和可逆的字节重排，不是已经证明 bias 带宽占主导。
预期收益可能很小或为零：bias 可能已命中缓存，原布局的相邻加载也可能复用相同缓存区域。

## 唯一候选的布局

`B[h,q,k]` 指**当前配方完成所有既有处理后实际存入权重缓冲区的 FP32 位模式**。
当前 `NR_BAKED_EXP_BIAS=1`，不能改用原始 FP16 bias、重新计算 affine，或缩小存储精度。
只复制 uint32 位模式。每个 head 仍为 4096 个元素，无压缩或去重。

对 `h=0..7, q,k=0..63` 定义：

```text
qf = q / 16; kf = k / 16
lane = (q % 16) + 16 * ((k % 16) / 8)
c = k % 8
tile = (h * 4 + qf) * 4 + kf
packed_index = tile * 256 + (c / 4) * 128 + lane * 4 + (c % 4)
P[packed_index] = bits(B[h,q,k])
```

除法均为非负整数除法。shader 在原消费位置读取两个 uvec4，分别对应 `c=0..3`、`c=4..7`，
以 bitcast 恢复原 FP32 fragment 元素；不引入新的浮点计算、LDS、同步或跨 query 缓存。
每个片段是两个 512-byte 面板，片段共 1024 bytes；面板基址按 256 bytes 对齐。
实际 query fragment 必须用 `(tok0 + 16*m)/16`，不能未经核验就删去 tok0。
lane/component 映射封装在 Windows 的目标专用 helper 中，不把这个实现定义的映射扩散到 Linux。

设计阶段已用纯 CPU 检查 32,768 个索引覆盖恰好一次，且 32,768 个 lane/component 对应全部一致。
假定每条指令每个活动 lane 读取连续 16 bytes，完整 wave 的地址覆盖模型如下：

| 假想对齐区域大小 | 原布局覆盖数（base 为 16-byte 对齐，枚举模 128 偏移） | 候选覆盖数（256-byte 对齐） |
| --- | ---: | ---: |
| 32 bytes | 32 | 16 |
| 64 bytes | 16–32 | 8 |
| 128 bytes | 16–32 | 4 |

这些是**每条指令的地址集合计数，不是实测缓存事务**；没有假定某一级缓存的 line/sector 大小。
同一片段的两次读取和不同片段之间可能复用缓存，不能累加此表来预测外部带宽或耗时。
执行时按真实 ISA 的发射宽度、地址、活动 mask 再计算；完整 wave 与连续半 wave 的模型都已检查。
尚未运行新的 Windows fragment 映射探针；仅有 CPU 双射不足以证明 shader 读对元素。

## 三个固定实验臂

| 实验臂 | bias 与 shader | 用途 |
| --- | --- | --- |
| A0 | 独立保存的当前生产构建、原布局和原归约源码 | 正确性与最终提速基准 |
| A1 | 复制原布局 bias 到新对齐区域，锁定既有机器码 half 加法链 | 控制复制区域、对齐、host 改动及 precise 的影响 |
| B | 相同大小/位置的新区域改为 lane 面板，使用同一锁序函数 | 唯一布局候选 |

A1/B 的权重缓冲区长度、所有区域起点、参数表结构、其他 shader 和 host 构建必须相同，
仅目标 bias 内容及其读取 helper 不同。原布局副本仍保留供其他路线使用。
禁用整个实验后，不增加副本、marker 要求或锁序改动；SPIR-V 与 plan/权重数据恢复 A0。
构建时间戳等非确定性元数据单独说明，不据此要求 DLL 文件逐字节一致。

锁序沿用 EXP-123 验证的当前 ISA 链：扁平化项 `1,0,2,3,...,15`，
再做原 lane XOR16 合并及 low/high half 合并；保持每一步 half 舍入。
只在目标 shader 的实验路径使用。A1 本身必须先通过与 A0 的数值和机器码归约检查。
不直接给原 GLSL 的另一棵加法树加 precise，也不把 A1 的潜在提速归给布局。

## Host 与 shader 契约

原型放新的忽略目录，使用独立源码快照和资产；主仓分支为 `main-experiment`，
涉及宿主写入时先验证 `customer-mochi-backed`。

`nr_graph.cpp` 在 persistent merge 处构造 `PersistRec`。只有最终选择 `fswinpds256` 的 run，
才给其**所有层记录**的 `p.b_off` 指向匹配副本，不能只改最后一个 DS 层。
以旧 offset 为键建立副本映射，在序列化参数表前替换副本中的 offset；原 push/原权重区保持。
最终 kernel 未匹配时维持旧 offset，涵盖 no-persist、普通 C256、UPS、其他宽度和 fallback。
额外 bias 存储按每层 8×4096×4 = 128 KiB 记账，汇总实际层数与初始化开销。

实验构建需明确的 host/shader 布局契约，例如 `c256-ds-bias-layout`：
区分原路径、搬移控制、面板候选，并在 dispatch 前拒绝错配，不能仅靠人工选对目录。
从隔离原型开始核对两种方向的混用拒绝；若最终保留，再纳入 shader constants、manifest 和缓存身份。
主仓 `windows/build/arch/rdna4.sh`、`pipelines.json`、build marker 与宿主副本必须一致。
不修改模型分发格式；所有原始 bias、权重包、DLL 和输出只保存在忽略目录。

## 顺序门槛与停止规则

1. **身份与映射**：重新查询 GPU/驱动，核对 glslang、两仓版本、基线产物与资产哈希。
   用不含模型数据的唯一整数标记矩阵，经 A0 的 coopMatLoad 和候选 helper 分别回读每个 lane/component；
   覆盖所有 head/query/key 片段及有效 tok0。验证真实 packed bias 每个 uint32 的可逆对应。
   映射不同于上述固定设计时关闭为前提不成立，不现场追加布局搜索。
2. **禁用与静态正确性**：disabled 恢复 A0；A0/A1/B 使用独立缓存、串行 GPU 调用。
   2258×1270 FP32 照片、2368×1280 工作尺寸、800 tokens、123 dispatches，100%、单次 NR、无历史/SR/FG。
   各预热 3、执行 3 次只检输出，不作性能结论；全部 11,470,640 位模式必须一致。
3. **数值数据流**：核对 12 个静态区域的 QK 每个 d 累加次序、bias 位模式、既有 baked FMA/转换、
   指数及分母实际加法链。NaN payload、signed zero 等不能仅凭静态照片宣布等价；
   如发生操作数交换，明确限定并检查相关值域，无法支持数值保持则停止。
4. **机器码机制**：逐片段核验真实 lane 地址变为两个连续面板，并重算地址集合。
   目标仍为每片段两条 b128、无新增跨 lane 重排/额外内存读取；记录地址 ALU、指令大小和资源。
   新增 scratch 或 VGPR 超出本次新鲜 A0/A1 基线的较大值即停止，不用性能测量挽救寄存器回退。
   若驱动拆读/重排导致既定加载形式未实现，记 mechanism_not_realized，不追加变体。
5. **完整宿主性能**：前四关通过后才执行下面协议。若数值失败记 correctness_failed；
   可靠退步记 regressed，方向不稳/与控制无法区分记 unproven；均不保留。
6. **扩大验收**：只有性能通过才覆盖两套配方的中间值、RGBA16F/R11G11B10/RGBA8、
   非整齐/小尺寸、HDR/棋盘、history/replay、生命周期、pass 切换、fallback，以及布局混用拒绝。
   每条路线与自己的独立 A0 比较；无精度容差放行。游戏继续暂缓。

探针只用于诊断，性能使用无探针构建。已有探针/追踪脚本按新输出目录复用，不能覆盖 EXP-123/124。
没有要求新 SQTT 才能继续：若另取计数器或 trace，先遵守 EXP-028，重新验证捕获路由/ELF/时序，
并与无插桩计时分开；未测到请求下降时最多声称地址布局改变，不能写成硬件带宽机制已证实。

## 性能协议与保留判据

RX 9070 XT、实际查询的驱动版本，目标输入 2258×1270，100%、单次 NR，
完整宿主线性 R11G11B10、depth=3，静态无历史/SR/FG；不换驱动、不固定新时钟。
覆盖 D3D12 输入复制至输出复制的完整 NR GPU 区间；逐内核数据仅辅助。

- 先测 A1/B，区分布局与共同控制；通过初测后再测 A0/B，确认对当前生产路径的收益。
- 每个比较采用 ABBA×3、紧随同路径控制×2。初测通过的比较最多各一次 BAAB×3 反序确认和对应控制。
- 每轮 A/B 及反序确认开始前，用 A0 统一预热 4000 帧；后续每次调用预热 100、测量 200 帧。
  所有 GPU 调用串行，保存全部批次/输出；记录遥测覆盖范围，不删尖峰、不靠重复测量挑结果。
- 两个比较都须初测/反序方向一致，收益可区别于同期控制波动；不把同路径控制当普适误差上界。
  若只胜 A1、不胜 A0，或者只胜 A0、不胜 A1，本布局假设均不保留。
- 分开报告 A1−B、A0−B；不同批次不能相减推导 A0−A1。没有预设最低百分比或毫秒承诺。

## 交付与本次设计边界

执行后交付：身份清单、packing 双射和 GPU 映射、三臂精确结果、归约数据流、
真实访存地址/指令/资源、性能数据或未进入原因、额外内存成本及未验收项。
无论成功与否保留失败证据；一项假设结束后停止，不自动进入布局参数扫描。

本次仅复盘文档、源码及保存的 driver ISA，并执行无模型输入的 CPU 地址审计。
审计脚本/报告在忽略的 `build/windows/bias-panel-plan-20261009/`；命令：
`python -X utf8 -B <new-study>/address_audit.py`，脚本拒绝覆盖已有报告。
没有构建候选、运行 GPU、改生产/宿主/Linux、安装 WSL/工具/驱动或提交/推送。
此计划不提供 Linux/Windows 总性能差距的归因结论。
