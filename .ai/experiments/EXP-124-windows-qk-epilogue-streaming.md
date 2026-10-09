# EXP-124：C256 DS 的 QK 分数逐片段消费

日期：2026-10-09。状态：**mechanism_not_realized；不保留实现**。

本项来自侧会话中用户对无损 Windows 优化的自由裁量授权。代理将本次范围限制为一个独立假设，
不是恢复先前已耗尽的运行。参考外部 GPU skills 的 profiling / codegen / correctness 分层方法，
没有安装或执行外部 skills。侧会话独立保存本记录，未修改主线程已有 EXP-123、索引、状态和暂存内容，
也未修改生产或宿主源码。主线程于同日核对记录与最终 `codegen-gate.json` 后接纳，补充实验索引和状态；
本提交单独保存本项记录及索引/状态更新，原有暂存内容保留。

## 假设与区分

C256 DS 在一个 query 内先生成四个 key fragment 的 QK logits，再逐片段计算 bias/exponent。
候选把每个片段的既有 d 累加移到对应 bias/exponent 消费之前，尝试缩短 logits 的存活区间。
每个 logit 的 d 次序、输入、bias、指数公式、权重和量化不变；资源布局、mask、同步也不变。
与 EXP-029/123 提前加载 bias 不同，本项是改变独立 QK 结果的生成与消费排列。
与 EXP-108 的 Q/K 投影分批、EXP-116 的 ViT logits 分批也不同。

根据 EXP-123，候选用 precise 显式表达当前驱动既有的 half 分母加法链；
不直接锁定原 GLSL 的分组写法，因为那不是已有机器码的归约次序。
候选宏 `NR_QK_EPILOGUE_STREAM` 只作用于隔离构建的 `fswinpds256`。
禁用宏重编得到完全相同的当前基线 SPIR-V。

若完整输出不同、源码预期未体现为实际调度/存活期改善，或最终完整宿主无可靠收益，则拒绝。
本项一版实现，未追加变体或性能确认。

## 环境与身份

- 核心 `main-experiment` / `917c86d755102fb67046f301bdebb27fbf896ced`。
- 宿主只读，`customer-mochi-backed` / `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。
- 本次 Vulkan 查询：RX 9070 XT、AMD proprietary、26.8.1 (LLPC)。
- 使用固定 glslang 16.5.0；工具、输入和模型哈希对照现存上一轮清单验证。
- 核验保留生产构建的 65 个宿主源文件、77 个产物；源文件比较允许 CRLF/LF 规范化。
- 2258×1270 FP32 照片，工作尺寸沿用当前计划的 2368×1280，800 tokens、123 dispatches。
- 100% 模型尺度、单次 NR，静态无历史/SR/FG；基线与候选各预热 3、运行 3 次。
- 独立 shader 目录、全新 pipeline cache、串行 GPU 调用；重新导出驱动 ISA/ELF。
- 时钟、温度、后台负载未采样。本次不采用这些诊断运行的时间作性能证据。
- 准备脚本首次读取旧 JSON 因系统默认 GBK 编码退出，尚未创建 ledger 或运行 GPU；
  改用 Python UTF-8 模式后完成。没有删除或覆盖旧研究证据。

## 结果

| 检查 | 基线 | 候选 |
| --- | --- | --- |
| 静态整图 FP32 位模式 | 11,470,640 | 全部相同，差异 0 |
| VGPR / SGPR | 229 / 107 | 229 / 107 |
| LDS / scratch | 32 KiB / 0 | 32 KiB / 0 |
| ELF `.text` 大小 | 224,512 bytes | 224,512 bytes |

基线新生成的 ISA 与 EXP-123 当前基线完全相同。候选 SPIR-V 和机器码不同，
但指令助记符计数完全一致；全内核的 **5,632 个 buffer-load / WMMA / exponent-bit-generation
事件序列**在忽略 VGPR 编号后相同。没有出现预期的片段提前消费顺序，也没有寄存器数量下降。

12 个静态 softmax 区域的加法树形与后续累加项次序一致，唯一差别是最初二元加法的两个操作数互换：
基线 `p[0][1] + p[0][0]`，候选 `p[0][0] + p[0][1]`。这不是归约重新结合；
静态整图输出已精确验证。未进一步声称 NaN payload 或所有输入逐位等价。

因此以 **mechanism_not_realized** 关闭。事件序列与资源计数不能证明全部机器码或周期相同，
本记录也不声称候选更慢。由于拟验证的机制未实现，没有进入完整宿主性能 A/B。
本次保留优化数量 0，没有新增已确认毫秒收益。

## 未执行项与状态隔离

未做完整宿主 ABBA/同路径控制、反序确认、其它格式/边缘尺寸、历史/replay、生命周期、
主仓另一套构建配方、fallback 或游戏验收；静态 FP32 一致不能替代这些检查。
候选未进入生产，无回滚需求。Linux、宿主、驱动、时钟、安装目录和原有 Git 暂存内容均未改动。

运行标识 `20261009-side-stream9070xt`：范围上限 1，启动 1，实验关闭 1；
侧会话交接时按 nr-optimize 的“完成必须包含提交”口径，完成计数为 0；
主线程接纳并以本提交保存文档后，完成计数为 1，运行结束。
这是侧会话隔离要求下的独立结果，主线程已合并记录，不自动继续后台实验。

## 证据与复现

| 产物 | SHA-256 |
| --- | --- |
| 基线 SPIR-V | `3dd150b0f770d67564aaa23199fd59dd53014eb748a5b53d16de43f4ad1c92ad` |
| 候选 SPIR-V | `b83f4b5cabae872b0dbcd8e1f18562e5b951a18658c22a00ede090ebb8891875` |
| 两边完整 FP32 输出 | `17fff022fac3014f6aca032b95105d0f378adc671003a52e440a332e0c89b7fe` |
| 基线 ELF `.text` | `f776fec5e4b6bae79a1acb1dcc1f1484beac813eb0f40a619424375e6aecf0d6` |
| 候选 ELF `.text` | `be2d048cc3beed7f615bc17c615ab564a6b0376f31ae433392bc408de53783bd` |

私有证据在忽略的 `build/windows/side-stream-study-20261009/`：
`experiment.py`、独立源快照/补丁、编译清单、基线与候选输出/日志/ISA/ELF、
`screen.json`、`codegen-gate.json`、`close.py`。
脚本拒绝重跑已存在的输出；再次实验应使用新的隔离目录，不能删除证据来复用。

```text
python -X utf8 -B <study>/experiment.py prepare
python -X utf8 -B <study>/experiment.py build
python -X utf8 -B <study>/experiment.py screen
python -X utf8 -B <study>/close.py
```

下一项研究应先寻找能改变数据流/实际加载数量的机制，或针对当前驱动代码生成制作最小复现。
本结果不支持继续仅靠搬动同一组独立语句来预期调度改变，也不解释 Linux/Windows 的总性能差距。
