# 当前状态

更新日期：2026-10-11（Asia/Hong_Kong）。

## 当前工作

运行 `20261010-224000-ten9070xt`，上限 10；已启动 10，本提交完成后关闭 10。
最近完成 [EXP-155](experiments/EXP-155-windows-vit-probability-partial-immediate.md)：两个完成的概率 pair 在基线和候选中仍都先于任一对应 partial 更新；候选没有实现 c4 更新先于 c6 概率生成。局部 c4 间距5→4、c6间距3不变，不等于源码计划的立即消费/消除跨 c4 更新的第二 pair 生存区间，也没有降低整体寄存器或代码尺寸。按 mechanism_not_realized 提前关闭，不进入完整宿主性能计时；没有声明提速或退化。
下一步：本轮十次额度耗尽；核验所有记录、提交、核心远端、资产和预先暂存内容，保存终态并停止，不自动启动新实验。

## 本轮证据

起点主仓 `2130829`、宿主 `f526a6d`。
RX 9070 XT / 26.8.1 (LLPC)，2258×1270、2368×1280 工作尺寸、800 tokens、123 dispatches。
100% 模型尺度、单次 NR、完整宿主 R11G11B10、depth=3；四批基线：10.963855 / 10.966668 / 10.968056 / 10.955780 ms。
本轮保留数量：0；历史收益不重复计入本轮。

- EXP-146：mechanism_not_realized；提交 a5eb50b96a8e538ed4a77c918f6394de532f7e23。
- EXP-147：mechanism_not_realized；提交 deab61bce3497f898ad3df36cc75fd3a38630187。
- EXP-148：no_codegen_change；提交 65f420cb87cbabc362638990d5a12498a10063b6。
- EXP-149：unproven；提交 579f9544cdb1fb5f1231c35f5725066fe8a280a1。
- EXP-150：regressed；提交 ea438b8f497e9e81ccf0d506f6d11561c16f8097。
- EXP-151：regressed；提交 743f0bdcd372090216278bfe994876cd534b6f23。
- EXP-152：no_codegen_change；提交 128c625847db87306727be0914f61945b5bd24b5。
- EXP-153：unproven；提交 6527a2b3721df45f9d9f2231272606a0fe222cb7。
- EXP-154：regressed；提交 0bb73ea1f28902e76e1125abe06bc51d94bebb50。
- EXP-155：mechanism_not_realized；提交 本提交。

## 继承状态与边界

此前 EXP-131–140 与 EXP-141–145 的运行已结束；本轮继承 EXP-131 及更早保留版本，提效工具提交 29f1b93。
EXP-131 的历史收益不计入本轮；本轮使用新的独立基线和逐候选同批对照。
历史收益不重复计入本轮；运行起点四批不与后来候选跨批相减。
EXP-050 已完成当前分支与指定上游的四分辨率组合验证，目标尺寸核心时间约省 3.106%；不混作本轮宿主完整区间基线。
EXP-028 只有最终 detailed decoder 结果可用于指令研究；C256 DS 样本时序通过，其他内核有 233 处负时长/倒序。
原型使用忽略目录的独立源码快照；保留预先暂存的 `.codegraph/.gitignore`，提交只包含本轮明确路径。
严格保持数值、权重、归约和行为；游戏测量继续暂缓，无安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。
恢复发布前仍需核对 EXP-012/015 的 runtime shader 分发/卸载闭包。
核心实验只使用 main-experiment，宿主修改只使用 customer-mochi-backed；本轮为十次无损实验，不关机、不启动后台自动续跑。新性能批次先过精确输出/ISA门槛，按阶段保存壁钟。

## 本轮停止条件

本提交关闭第 10 项后，已启动/关闭/上限为 10/10/10，预算耗尽。
停止原因为 budget_exhausted；最终提交与产物核验写入本机 ledger。
本轮保留代码数量为 0；未保留原型均在忽略目录，无未提交实现改动或恢复需求。
