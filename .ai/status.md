# 当前状态

更新日期：2026-10-08（Asia/Hong_Kong）。

## 当前工作

运行 `20261008-130553-swin9070xt` 已完成：已启动/关闭/上限 10/10/10，exhausted / budget_exhausted。
最近完成 [EXP-086](experiments/EXP-086-windows-vit-attention-tail-epilogue.md)：三个实现均未通过精确正确性，关闭为 correctness_failed。没有完整宿主 A/B、同路径控制或性能确认，不从三帧诊断时间、寄存器下降或代码缩小宣称提速。全部失败源、SPIR-V、ISA、输出和隔离探针保留，生产源码未应用这些改动。
下一步：本轮第10项已关闭，按 budget_exhausted 停止。最终核验本轮唯一保留 EXP-076 及两仓提交/产物；新一轮若重访 attention 循环，应先逐操作对照既有机器码的 half 结合顺序和 precise 传播。EXP-080 的统一预热是测量发现，另行记录，不计入优化收益。

## 本轮证据

起点主仓 `814e808`、宿主 `9d1fa93`。
RX 9070 XT / 26.8.1 (LLPC)，2258×1270、2368×1280 工作尺寸、800 tokens、123 dispatches。
100% 模型尺度、单次 NR、完整宿主 R11G11B10、depth=3；四批基线：10.840392 / 10.891116 / 10.921970 / 10.937182 ms。
本轮保留数量：1；EXP-076 登记新增节省 0.046840 ms（反序确认 0.058513 ms），采用较小批次均值，非保证下界。
结果分布：1 retained、5 unproven、3 regressed、1 correctness_failed；历史收益不重复计入本轮。
独立发现 [EXP-080](experiments/EXP-080-windows-reference-conditioning-control.md)：统一参考预热改善冷启动控制；不消耗假设、不计入提速。

- EXP-076：retained；提交 a5506eb6d51da060055d65f0527d1a45b2aec430。
- EXP-077：unproven；提交 8c292bedbd8857d934b44393078eba322bc85fad。
- EXP-078：unproven；提交 7aa5dd6b5d44a54b0b0d14695be214cfec11da3d。
- EXP-079：unproven；提交 29f2a2fc9cb8930d0380392d2472b6efe6d7168e。
- EXP-081：regressed；提交 14f72701d0f542586582924202b9df6a9b8cf352。
- EXP-082：regressed；提交 124e3fc76898cb090bc5b97fb975e2048a81ac57。
- EXP-083：regressed；提交 db6a1027544578740d3faf97d424aea5d46d2c0f。
- EXP-084：unproven；提交 70ce123af07ddf83eafbefb2e5489948fcf97ac1。
- EXP-085：unproven；提交 88b9385f07d13a6f40c3be39510807a1c016fc26。
- EXP-086：correctness_failed；提交 本提交。

## 继承状态与边界

EXP-066–075 的上一轮 10/10 已结束且无保留；再前两轮保留 EXP-056/061，更早保留 EXP-044/048。
本轮 EXP-076 已保留并纳入后续候选对照；历史收益不重复计入本轮。运行起点四批不与后来候选跨批相减。
EXP-050 已完成当前分支与指定上游的四分辨率组合验证，目标尺寸核心时间约省 3.106%；不混作本轮宿主完整区间基线。
EXP-028 只有最终 detailed decoder 结果可用于指令研究；C256 DS 样本时序通过，其他内核有 233 处负时长/倒序。
原型使用忽略目录的独立源码快照；保留原有 `.codegraph/`；不清理或覆盖历史数据。
严格保持数值、权重、归约和行为；游戏测量继续暂缓，无安装、部署、打包、发布或推送。
恢复发布前仍需核对 EXP-012/015 的 runtime shader 分发/卸载闭包。
宿主修改只使用 customer-mochi-backed；本轮没有关机或后台任务授权。

## 本轮停止条件

已启动/关闭/上限为 10/10/10，预算耗尽。
停止原因为 budget_exhausted；最终提交与产物核验写入本机 ledger。
本轮保留代码数量为 1；未保留原型均在忽略目录，无未提交实现改动或恢复需求。
