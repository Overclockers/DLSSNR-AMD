# 当前状态

更新日期：2026-10-08（Asia/Hong_Kong）。

## 当前工作

运行 `20261008-130553-swin9070xt`，上限 10；已启动 8，本提交完成后关闭 8。
最近完成 [EXP-084](experiments/EXP-084-windows-swin-probability-denominator-bound.md)：初筛平均慢 0.002931 ms，三组均慢，处于同路径控制最大绝对组差 0.006525 ms 内。唯一反序确认平均快 0.001806 ms，各组节省 0.000715/-0.000760/0.005465 ms，方向混合，确认控制组差 -0.000664/0.001472 ms。未建立可重复的完整路径收益，关闭为 unproven。范围证明和静态指令减少不构成保留依据。
下一步：检查六个 persistent Swin 内核的零加原子轮询：Windows 当前 ISA 使用 device-scope atomic OR；测试显式 device/acquire atomicLoad，保留写入、发布屏障、队列和 epoch，并检查循环内的真实加载及可见性。

## 本轮证据

起点主仓 `814e808`、宿主 `9d1fa93`。
RX 9070 XT / 26.8.1 (LLPC)，2258×1270、2368×1280 工作尺寸、800 tokens、123 dispatches。
100% 模型尺度、单次 NR、完整宿主 R11G11B10、depth=3；四批基线：10.840392 / 10.891116 / 10.921970 / 10.937182 ms。
本轮保留数量：1；历史收益不重复计入本轮。

- EXP-076：retained；提交 a5506eb6d51da060055d65f0527d1a45b2aec430。
- EXP-077：unproven；提交 8c292bedbd8857d934b44393078eba322bc85fad。
- EXP-078：unproven；提交 7aa5dd6b5d44a54b0b0d14695be214cfec11da3d。
- EXP-079：unproven；提交 29f2a2fc9cb8930d0380392d2472b6efe6d7168e。
- EXP-081：regressed；提交 14f72701d0f542586582924202b9df6a9b8cf352。
- EXP-082：regressed；提交 124e3fc76898cb090bc5b97fb975e2048a81ac57。
- EXP-083：regressed；提交 db6a1027544578740d3faf97d424aea5d46d2c0f。
- EXP-084：unproven；提交 本提交。

## 继承状态与边界

EXP-066–075 的上一轮 10/10 已结束且无保留；再前两轮保留 EXP-056/061，更早保留 EXP-044/048。
本轮 EXP-076 已保留并纳入后续候选对照；历史收益不重复计入本轮。运行起点四批不与后来候选跨批相减。
EXP-050 已完成当前分支与指定上游的四分辨率组合验证，目标尺寸核心时间约省 3.106%；不混作本轮宿主完整区间基线。
EXP-028 只有最终 detailed decoder 结果可用于指令研究；C256 DS 样本时序通过，其他内核有 233 处负时长/倒序。
原型使用忽略目录的独立源码快照；保留原有 `.codegraph/`；不清理或覆盖历史数据。
严格保持数值、权重、归约和行为；游戏测量继续暂缓，无安装、部署、打包、发布或推送。
恢复发布前仍需核对 EXP-012/015 的 runtime shader 分发/卸载闭包。
宿主修改只使用 customer-mochi-backed；本轮没有关机或后台任务授权。
