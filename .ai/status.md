# 当前状态

更新日期：2026-10-09（Asia/Hong_Kong）。

## 当前工作

运行 `20261008-193306-lossless9070xt`，上限 10；已启动 10，本提交完成后关闭 10。
最近完成 [EXP-106](experiments/EXP-106-windows-c32-pool-packed-y-exchange.md)：两版都未通过事先规定的严格位一致性门槛，判为 correctness_failed，不保留。没有把 NaN 位差异放宽为可接受误差，也没有运行完整 NR 性能筛选或追加确认；本项没有可报告的省时数字。
下一步：第十项提交完成后执行本轮最终身份、分支、提交和暂存区核验，以 budget_exhausted 结束本 run；新的实验需要新的明确预算。

## 本轮证据

起点主仓 `c7e0c9f`、宿主 `6a8e322`。
RX 9070 XT / 26.8.1 (LLPC)，2258×1270、2368×1280 工作尺寸、800 tokens、123 dispatches。
100% 模型尺度、单次 NR、完整宿主 R11G11B10、depth=3；四批基线：10.999239 / 11.010161 / 11.007372 / 11.000571 ms。
本轮保留数量：0；历史收益不重复计入本轮。

- EXP-097：regressed；提交 ed70f08f538dd40fd7c75637aaa1951fddb7f351。
- EXP-098：regressed；提交 7d789181cea395ad707388786c470434c143d224。
- EXP-099：unproven；提交 e56cd28c0abf493d4ac542ce5d2dcb35b9d34fe8。
- EXP-100：regressed；提交 57f98b78f45cb9f4830cfef3341e0a3dff42f66a。
- EXP-101：unproven；提交 159441cf8e145c6d01c6114d3fdc43195f261d5c。
- EXP-102：no_codegen_change；提交 2357a21c0fd3cbadd2f8d1ff4735ee170a7a1052。
- EXP-103：unproven；提交 e0156cdc19eeb943e55c018398cf82a7f8d7a443。
- EXP-104：unproven；提交 37aaf80b593fcbc9eb46aaac8277839bfd602432。
- EXP-105：unproven；提交 3c3cadccaaa2d5c3ea4341821fef4f73ac6787e4。
- EXP-106：correctness_failed；提交 本提交。

## 继承状态与边界

上一轮 EXP-087–096 的 10/10 已结束，保留 EXP-088；本轮继承该产物以及更早的保留代码。
历史收益不重复计入本轮；运行起点四批不与后来候选跨批相减。
EXP-050 已完成当前分支与指定上游的四分辨率组合验证，目标尺寸核心时间约省 3.106%；不混作本轮宿主完整区间基线。
EXP-028 只有最终 detailed decoder 结果可用于指令研究；C256 DS 样本时序通过，其他内核有 233 处负时长/倒序。
原型使用忽略目录的独立源码快照；保留预先暂存的 `.codegraph/.gitignore`，提交只包含本轮明确路径。
严格保持数值、权重、归约和行为；游戏测量继续暂缓，无安装、部署、打包、发布或推送。
恢复发布前仍需核对 EXP-012/015 的 runtime shader 分发/卸载闭包。
核心实验只使用 main-experiment，宿主修改只使用 customer-mochi-backed；本轮没有关机或后台任务授权。

## 本轮停止条件

本提交关闭第 10 项后，已启动/关闭/上限为 10/10/10，预算耗尽。
停止原因为 budget_exhausted；最终提交与产物核验写入本机 ledger。
本轮保留代码数量为 0；未保留原型均在忽略目录，无未提交实现改动或恢复需求。
