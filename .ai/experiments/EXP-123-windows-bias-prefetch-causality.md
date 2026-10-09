# EXP-123：C256 DS 偏置预取的数值归因

日期：2026-10-09。状态：**归因完成；唯一修正版未保留（mechanism_not_realized）**。

用户授权执行 [单项实验设计](../research-plan-bias-prefetch-20261009.md)。
本项为定向诊断，不恢复已耗尽的 nr-optimize 运行。
主仓基线 `main-experiment` / `917c86d755102fb67046f301bdebb27fbf896ced`。

## 结论

EXP-029 预取候选改变了驱动生成的 **FP16 softmax 分母加法顺序**。
源码移动不可变 bias 加载，使后续未锁定的浮点归约以不同顺序执行；
不是仅凭寄存器数量或等待指令数量就能判断的等价调度。

首个 C256 持久化层（dispatch 标签 b015l0、内部 nr_layer=0）的窗口 `(0,0)`：
32,768 个 half 指数值逐位一致，1,024 个分母中 296 个不同。
使用相同指数值，在 CPU 上按原始 A/C 的实际机器码顺序逐次做 FP16 舍入，
两边各 1,024 个分母均与 GPU 回读精确一致。
首例 head=0、query fragment=0、lane=0：A 为 `0x2e20`，C 为 `0x2e1f`。
因而这里最早观察到的差异位于分母归约；无需假设 bias 读取损坏。
这建立具体编译产物的因果解释，不宣称驱动违反规范，也不证明 Linux/Windows 总差距的成因。

## 环境与身份

- 本次 Vulkan 查询：RX 9070 XT / AMD proprietary / `26.8.1 (LLPC)`；glslang 16.5.0。
- 主仓 `917c86d755102fb67046f301bdebb27fbf896ced`，分支 `main-experiment`。
  外部宿主只读，`customer-mochi-backed` / `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。
- 2258×1270 FP32 照片输入，2368×1280 工作尺寸，800 tokens、123 dispatches；
  100% 尺度、单次 NR、静态无历史/SR/FG。没有完整宿主计时或游戏测试。
- 对照原型全部使用独立源码及 shader 目录；旧资产只读，缓存和 driver ISA 重新生成。
  每次数值运行预热 3、执行 3 帧，均为诊断，不作性能统计。
  探针另执行一次清空回读区后的完整网络。
- 本次记录模型、照片、程序和工具哈希，并与现存最新 ledger 核验；
  当前宿主 65 个源文件及保留构建 77 个产物身份通过。
  旧运行 ledger 已不在本机；不声称其原始资产哈希清单仍完整。
  旧 A/C 的重建 SPIR-V、重新生成 ISA 和完整输出分别精确复现保存值。
- 时钟、温度、后台负载未采样，未更改驱动/时钟。初次 B 与 C 诊断调用存在重叠；
  随后独立串行重跑 A/B/C，全部复现同一输出。探针及当前候选随后串行运行。

## 三个固定对照

| 控制 | 改动 | 与保存 A 的 FP32 位模式差异 | driver ISA |
| --- | --- | ---: | --- |
| A | 原加载位置及消费 | 0 / 11,470,640 | 与保存 A 完全一致 |
| B | 原位置写入/读取片段数组，立即消费 | 0 / 11,470,640 | 与 A 完全一致 |
| C | 四个片段提前到当前 query 的 QK WMMA 前 | 8,602,291 / 11,470,640 | 与保存 C 完全一致 |

B 的数组控制被编译器消除；数组表示本身没有触发此次差异。
A/B/C 的 SPIR-V `nr_swin_probability_sum` 函数体完全相同（205 条 SPIR-V 指令，
21 条 OpFAdd），最终 driver ISA 却改变了 C 的 half 加法链。
归因不再停留于“算术助记符数量相同”。

将 `p[j][c]` 扁平化为 `4*j+c`，完整窗口体的 16 项 packed-half 归约是：

```text
A/B: 1,0,2,3,4,5,6,7,8,9,10,11,12,13,14,15
C:   2,3,0,1,7,6,4,5,11,10,8,9,15,14,12,13
```

第一步加前两项，随后依序累加；再与 lane XOR 16 的结果相加，最后合并 low/high half。
每步保持 half 舍入。旧 A 和当前基线的全部 12 个静态 query 区域均使用上述 A 顺序；
旧 C 的边缘体还有其他重排，不能把完整窗口顺序套到所有边缘体。

## 真实输入回读与扰动控制

在首层窗口 `(0,0)` 写出四个 query、八个 head、32 lanes 的指数和分母，
使用已有诊断 arena 的独立尾部区域，共 17,408 个 uint32。
没有改变生产 runtime、绑定或权重。

- 探针 A 的整网输出与未插桩 A 完全相同；探针 C 与未插桩 C 完全相同。
  此次故障没有因插桩消失。
- 探针资源从 229 VGPR 分别变为 232 / 255，均零 scratch；仅用于数值归因，不能计时比较。
- 16,384 个 packed 指数值（32,768 half）精确一致，296/1,024 个分母不同。
- CPU 最近偶数 half 加法回放分别 0/1,024 不匹配，精确重现两边 GPU 分母。
  这是原始机器码顺序、相同实测输入与 GPU 数值的交叉证据。
- 未另行回读 bias 原始值、QK accumulator 和融合值；指数输入已一致，差异定位在其后。
  未用这一窗口声称整个网络每个中间值都已逐项验证。

## 当前基线上的唯一修正版

继承 EXP-088 的当前宿主配方重新构建独立 A，SPIR-V 与保留版本一致。
候选仅对 C256 DS 提前读取 bias，并用 `precise` 锁定 **现有基线机器码** 的加法链。
不能直接给原函数加 precise：原 GLSL 的分组形式与现有机器码顺序本就不同。
禁用候选恢复独立基线；候选的 11,470,640 个静态输出位模式全部与当前 A 一致。

但未实现预取机制：

- 全内核 buffer_load/WMMA 指令序列忽略 VGPR 分配后相同；
  各 12 处 query 的 bias 加载与 QK WMMA 相对位置均没有提前。
- 全部指令助记符数量相同；两边 229 VGPR / 107 SGPR / 32 KiB LDS / 零 scratch。
- 12 处归约顺序恢复一致，仍有其他调度和寄存器分配变化。
  不由此断言周期完全相同，但没有实现拟验证的加载提前机制。

按预定门槛以 `mechanism_not_realized` 关闭，不做完整宿主 ABBA/反序确认，
不追加新预取变体。没有已确认毫秒收益。
未继续格式、边缘输入、历史/replay、fallback、主仓独立配方及宿主集成验收；
静态 FP32 通过不代表这些路径已通过，候选不进入生产。

## 产物与复现

| 产物 | SHA-256 |
| --- | --- |
| 旧 A SPIR-V | `ec8a83af07742e3049a8318a2fd651ea50daee446ab43d3c2aa2f07c24962122` |
| 原位置数组 B SPIR-V | `2890ff1e82aad4157b9bbe4f703522d0a35fac944c8c6e05f2510d6086d23230` |
| 旧 C SPIR-V | `306ee32679939eee3c5286d52a03643dce09593a034f0d6f4af8c23394fab9f6` |
| 当前 A / disabled SPIR-V | `3dd150b0f770d67564aaa23199fd59dd53014eb748a5b53d16de43f4ad1c92ad` |
| 当前唯一修正版 SPIR-V | `44a20158ca7bbb5545625e06d0f1b7db95e1b35417b6700a834a8ace53ac7c2c` |
| A/B/当前修正版输出 | `17fff022fac3014f6aca032b95105d0f378adc671003a52e440a332e0c89b7fe` |
| 旧 C 输出 | `43dfca88efbdc9039fd7461aa99cc70e736b0dd2f7c7cea1706de1819d104f3b` |

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --pipeline-cache <fresh-cache> --in-image <photo> --out-image <output> --warmup 3 --repeats 3 --per-layer --dispatch-grid
python <study>/analyze.py
```

隔离目录含 `study.py`（对照/探针）、`current.py`（唯一修正版）、`trace_sum.py`（受限 ISA 数据流追踪）、
`analyze.py`（CPU 数值回放）、`causal-report.json`、`spirv-sum-audit.json`、
`current-codegen-gate.json` 及全部源快照、预处理结果、补丁、ISA、运行命令与输出。
追踪器不是通用 ISA 模拟器；遇到未支持运算会拒绝该归约区域，不作硬件时序模拟。

隔离脚本、源码、预处理结果和新 GPU 输出在忽略的
`build/windows/bias-prefetch-study-20261009/`；旧证据只读。
生产、Linux 和宿主源码均未修改；未安装、部署、打包或推送。本提交仅保存实验设计、记录及索引/状态。
后续调度实验应检查真实归约数据流，不能只核对源级表达式或 opcode 数量。
本项已关闭，不自动开始新的优化运行。
