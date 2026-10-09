# EXP-125：C256 DS bias lane 连续面板

日期：2026-10-09。状态：**resource_gate_failed；不保留实现，无新增提速结论**。

用户授权执行 [既定计划](../research-plan-bias-lane-panels-20261009.md)。本次一项假设、一版候选，
运行 `20261009-bias-panel9070xt`；上限/启动/关闭为 1/1/1。没有恢复旧优化额度，没有追加布局变体。
本提交保存实验计划、结果及索引/状态；生产、外部宿主、Linux 和原有暂存内容均未修改。

## 假设与实验臂

把 `fswinpds256` 读取的既有 FP32 bias 位模式重排，使每条 b128 的 32 个 lane
访问连续 512 bytes，检验空间局部性是否值得进入完整宿主性能验收。
保持原消费位置、每个 logit 的源码 d 累加次序和已有精度；不新增 LDS、同步或跨 query 缓存。
与 EXP-123/124 的源码调度实验不同，本项改变真实物理地址。

- A0：独立保存并新鲜重编的当前路径，原 bias 布局和归约源码。
- A1：原 bias 复制到新 256-byte 对齐区域，显式表达既有驱动 half 归约链。
- B：与 A1 相同的 host 二进制、缓冲区长度和区域起点，副本改为计划中的 lane 面板及对应 helper。
- disabled：同一候选源码关闭 host/shader 实验宏，核验完整恢复。

原型只在忽略的独立源码快照中。新增 Windows helper 位于隔离的 `coopmm.glsl`，
按 `(tok0+16*m)/16` 计算 query fragment；目标 head-wave 路径的源码明确 `tok0=0`。
host 在 persistent records 序列化前，仅对最终选择 `fswinpds256` 的 run 替换所有层的 `b_off`。
原 bias 区保留，普通 C256/UPS 等不使用副本。实际目标 run 为 b015l0..b022l0，共八层。

## 环境与身份

- 核心：`main-experiment` / `9eb8985d9698ee6e1afa6c347d09c86bc4e51780`。
- 宿主：`customer-mochi-backed` / `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`，checkout 只读。
- 新查询：RX 9070 XT，AMD proprietary driver / `26.8.1 (LLPC)`；glslang 16.5.0。
- 输入：2258×1270 FP32 静态照片，工作尺寸 2368×1280、800 tokens、123 dispatches。
  100% 尺度、单次 NR、无历史/SR/FG；各臂预热 3、执行 3，GPU 调用全部串行。
- 使用宿主当前 shader 配方、math profile 3、baked exp bias；复制的是既有处理后的 FP32 位模式。
- standalone runner 从宿主核心快照构建，增加只读权重 dump；复用宿主已有 MSVC `dirent.h` 兼容头，
  runner 入口补充空的构建进度回调。各臂一致，不影响网络计算。

| 产物 | SHA-256 |
| --- | --- |
| A0 / disabled C256 DS SPIR-V | `3dd150b0f770d67564aaa23199fd59dd53014eb748a5b53d16de43f4ad1c92ad` |
| A1 C256 DS SPIR-V | `a6d2765de1ad88f14a2fcefc86f71fc2b56dbec5ed9a9d78877fbae4697e9908` |
| B C256 DS SPIR-V | `189fd6b3aa4be6540946812434142d43612d9b52edd108aa4dae7c73d7a034d0` |
| 四臂 FP32 输出 | `17fff022fac3014f6aca032b95105d0f378adc671003a52e440a332e0c89b7fe` |

## 映射、packing 与静态正确性

Windows GPU 标记探针覆盖八个 head、全部 query/key 片段：32,768 个唯一有限整数，
原 coopMatLoad 与面板 helper 的每个 lane/component 均精确对应，输出 guard 完整。
独立 CPU 索引检查为双射。实际八层共 262,144 个 uint32 bias 的控制副本与面板逆映射全部相等。

A0/A1/B/disabled 各自最终回读的 **11,470,640 个 FP32 位模式全部一致**，也等于独立历史基线。
disabled 的 SPIR-V、plan 和完整权重缓冲区与 A0 相同。
A1/B 除新复制的 bias 内容外，完整权重缓冲区逐字节一致；其他 shader 未改变。

额外 bias 共 1,048,576 bytes（八层，每层 128 KiB）；包括对齐，权重缓冲区从
178,234,628 增至 179,283,284 bytes，净增 1,048,656 bytes。未单独测量初始化耗时。
隔离 host/shader 以 `c256-ds-bias-layout.txt` 区分控制与面板；两个方向的错配均在 dispatch 前拒绝。
此为原型契约检查，没有完成生产 manifest/cache 集成。

## 机器码与数值检查

| 新鲜 driver 资源 | A0 | A1 | B |
| --- | ---: | ---: | ---: |
| VGPR | 229 | 229 | **230** |
| SGPR | 107 | 107 | 107 |
| LDS bytes | 32,768 | 32,768 | 32,768 |
| Scratch bytes | 0 | 0 | 0 |
| driver ISA 注释中的编码字节合计 | 224,108 | 224,108 | 224,576 |

12 个静态 query 区域仍各有八条 b128，即每片段两条。对全部 96 条 bias 加载做受限整数地址回放：
候选每条指令的 lane 地址步长为 16 bytes，每组覆盖连续 512 bytes；两个面板相距 512 bytes。
部分区域把第二面板地址放进另一个寄存器，而不是使用 `offset:512`，分析包含这些地址 ALU。
head 与 bias base 是统一平移；实际复制区均满足 256-byte 对齐。
这证明物理加载地址改变，**不证明实测缓存事务减少**。

全 shader 的各类 buffer load、WMMA、`v_perm_b32`、`v_permlanex16_b32` 静态数量均不变。
地址相关变化包括 `v_add_lshl_u32` +30、`v_add_nc_u32_e32` +25、`s_lshr_b32` +5，
以及部分旧地址指令减少；这些计数不能直接换算周期。编码字节合计增加 468 bytes。

受限符号追踪核对了 12 处局部分母加法树：B 与 A0 相同；A1 只把最初两个项的操作数互换，
后续加法链没有重新结合。lane XOR16 合并、low/high half 合并、下限与倒数输入的链一致。
这不是所有输入的数值等价证明，尤其没有证明 NaN payload 语义。
触发资源门槛后，未继续逐项完成全 shader 的 QK/FMA/转换数据流验收；源码次序和总指令计数
不能代替该检查。没有用静态照片一致放宽保留标准。

## 停止判定与未测项

计划预先规定：VGPR 超过新鲜 A0/A1 较大值即停止，不用性能测量挽救资源回退。
B 的 230 超过 229，因此按 **resource_gate_failed** 关闭。加载地址机制已经实现，
不归类为 mechanism_not_realized；也没有完整宿主测量支持 regressed 或提速结论。
单个 VGPR 增加本身不证明 occupancy 降低或实际耗时变慢。

未执行完整宿主 ABBA/BAAB、同期性能控制、扩大格式/边缘/HDR/history/replay/生命周期/
pass 切换/fallback/两套配方验收，也未新采 SQTT。静态检查日志中的三次诊断时间不用于提速结论。
未保留候选，无需恢复生产实现；仅提交文档，无安装、部署、推送或后台续跑。

后续若另立实验，应先解释当前面板寻址为何增加地址 ALU/寄存器，并重新制定资源与性能判据；
本记录不授权自动重开本项或继续参数扫描。

## 私有证据

入口：忽略的 `build/windows/bias-panel-study-20261009/README.md`。
核心报告：`identity.json`、`static-correctness.json`、`packing-audit.json`、`panel-addresses.json`、
`sum-traces.json`、`codegen-gate.json`；源码快照、编译命令、原始 driver ISA、模型相关数据均留在忽略目录。
`B-logical-offsets.analysis.txt` 仅供逻辑分量追踪，修改了文本 offset，**不是原始 driver ISA**。
