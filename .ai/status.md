# 当前状态

更新日期：2026-10-09（Asia/Hong_Kong）。

## 当前工作（已完成）

用户授权执行 [C256 DS 偏置预取设计](research-plan-bias-prefetch-20261009.md)，结果见
[EXP-123](experiments/EXP-123-windows-bias-prefetch-causality.md)：旧 A/B 精确一致，C 精确复现历史失败。
根因是最终机器码的 FP16 softmax 分母归约重排；首层窗口指数 32,768 half 全相同，296/1,024 分母不同。
按原始 ISA 顺序做 CPU half 舍入回放，两边各 1,024 分母均精确重现；探针保留原完整输出。
当前基线上的唯一修正版锁定既有归约顺序后，静态 FP32 整网输出精确一致、229 VGPR、零 scratch。
但 12 处 bias 加载与 QK WMMA 相对顺序未提前，按 mechanism_not_realized 关闭，未进入完整宿主性能 A/B。
生产与宿主未改，无新收益；原始证据在忽略的 build/windows/bias-prefetch-study-20261009/。
这是已结束的单项定向实验，没有恢复旧 nr-optimize 额度；后续应先核对真实机器码归约和加载机制。

## 前项定向研究（已结束）

[EXP-122](experiments/EXP-122-windows-dx12-fp8-feasibility.md)：用户确认尝试 DX12 FP8 GEMM，
通过后继续 C256 下采样热点内核。起点 main-experiment / 4500c2b；这是新的定向研究。
官方便携 Agility SDK 1.721.3-preview、DXC 1.10.2605.37-preview 已准备，签名与哈希已验证。
DX12 标量控制、Vulkan 原生 FP8 对照各完成 64×128×256 矩阵测试，8192 输出逐位一致，guard 完整。
两 API 使用同一张 RX 9070 XT；Vulkan 本次报告 26.8.1 (LLPC)。DX12 调试层检查 0 个错误。
HLSL FP8 shader 已编译。用户授权后临时开启开发者模式，预览 runtime 和实验功能成功加载。
当前驱动最高报告 SM 6.9、LinAlg Tier 0；FP8/FP16 wave MMA 均 flags=0、shape_count=0。
沙箱外复查一致；实际 FP8 PSO 创建返回 E_INVALIDARG，调试层明确拒绝 cs_6_10。
实验门槛检查已结束：当前驱动不支持这条标准 DX12 LinAlg 路径，尚无有效性能 A/B。
原生 DX12 FP8 未 dispatch，生产 GEMM/C256 DS 移植未开始，不能由此推断 DX12 性能。
开发者模式已恢复原状态并独立核验；未更换驱动或修改其他系统设置。
后续需支持 SM 6.10/LinAlg 的驱动；AMD 26.10.07.02 开发者预览版有官方支持说明，本次未安装。
驱动条件改变后先重跑门槛与数值验证，再推进生产 GEMM/C256 DS。此试验没有新增生产优化。
证据入口为忽略的 `build/windows/dx12-probe-20261009/README.md`，汇总为 `summary.json`。
合成矩阵控制只验证框架，不等于模型正确性或 API 性能结论。

## 上一轮工作（已结束）

运行 `20261009-085523-newpaths9070xt`，上限 5；已启动 5，本提交完成后关闭 5。
最近完成 [EXP-121](experiments/EXP-121-windows-activation-slot-address-skew.md)：unproven，不保留。初测11.040329→11.037140 ms，平均省0.003189 ms，三组方向混合。唯一反序确认11.078860→11.093992 ms，三组全部更慢，平均慢0.015131 ms；确认同路径控制平均差0.003697 ms。地址低位分散已实现，但完整路径收益未复现，不追加第三轮。
下一步：本轮5次预算耗尽，结束运行。后续新预算应先将EXP-028已验证的C256 DS加载等待细分到当前产物的具体load与消费者依赖，再选择独立调度或局部性实验；这只是后续研究建议，尚无新增收益证据。避免继续扫描本轮已否定的面板、wave大小或spacing参数。

## 上一轮证据

起点主仓 `9704b35`、宿主 `6a8e322`。
RX 9070 XT / 26.8.1 (LLPC)，2258×1270、2368×1280 工作尺寸、800 tokens、123 dispatches。
100% 模型尺度、单次 NR、完整宿主 R11G11B10、depth=3；四批基线：10.978340 / 11.019070 / 11.008404 / 11.006539 ms。
本轮保留数量：0；历史收益不重复计入本轮。

- EXP-117：mechanism_not_realized；提交 989e388890cd63aa8b293678aa85c6e1defffb78。
- EXP-118：unproven；提交 46ced6bc5a9f6803aab6de091f56751b21187618。
- EXP-119：unproven；提交 86e233c86d3df92ad67f1d7eba153f4573f6668c。
- EXP-120：unproven；提交 a2e8241e8f3bdf589050cd45c691c272fffe57d3。
- EXP-121：unproven；提交 本提交。

## 继承状态与边界

上一轮 EXP-107–116 的 10/10 已结束，未保留新代码；本轮继承 EXP-088 及更早保留版本。
历史收益不重复计入本轮；运行起点四批不与后来候选跨批相减。
EXP-050 已完成当前分支与指定上游的四分辨率组合验证，目标尺寸核心时间约省 3.106%；不混作本轮宿主完整区间基线。
EXP-028 只有最终 detailed decoder 结果可用于指令研究；C256 DS 样本时序通过，其他内核有 233 处负时长/倒序。
原型使用忽略目录的独立源码快照；保留预先暂存的 `.codegraph/.gitignore`，提交只包含本轮明确路径。
严格保持数值、权重、归约和行为；游戏测量继续暂缓，无安装、部署、打包、发布或推送。
恢复发布前仍需核对 EXP-012/015 的 runtime shader 分发/卸载闭包。
核心实验只使用 main-experiment，宿主修改只使用 customer-mochi-backed；本轮为五次新方向实验，未授权关机或后台自动继续。

## 上一轮停止条件

本提交关闭第 5 项后，已启动/关闭/上限为 5/5/5，预算耗尽。
停止原因为 budget_exhausted；最终提交与产物核验写入本机 ledger。
本轮保留代码数量为 0；未保留原型均在忽略目录，无未提交实现改动或恢复需求。
