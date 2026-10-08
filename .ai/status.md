# 当前状态

更新日期：2026-10-09（Asia/Hong_Kong）。

## 当前工作

运行 `20261008-193306-lossless9070xt`，上限 10；已启动 9，本提交完成后关闭 9。
最近完成 [EXP-105](experiments/EXP-105-windows-vit-expand-weight-outer-mma.md)：初测完整路径 11.359819→11.491254 ms，平均慢 0.131436 ms；三组方向混合，同路径控制存在 -0.594449 ms 的组差。唯一反序确认 11.089438→11.102141 ms，平均慢 0.012703 ms，三组节省 -0.051810/-0.011283/+0.024984 ms；确认控制组差 -0.022762/+0.377152 ms。全部含尖峰批次均保留，无法建立稳定收益，判为 unproven，不保留。
下一步：完成最后一次额度：研究 C32 mode-7 下采样池化将两个 y 邻居的 half 值合成一个 uint 交换，先验证舍入和机器码。

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
- EXP-105：unproven；提交 本提交。

## 继承状态与边界

上一轮 EXP-087–096 的 10/10 已结束，保留 EXP-088；本轮继承该产物以及更早的保留代码。
历史收益不重复计入本轮；运行起点四批不与后来候选跨批相减。
EXP-050 已完成当前分支与指定上游的四分辨率组合验证，目标尺寸核心时间约省 3.106%；不混作本轮宿主完整区间基线。
EXP-028 只有最终 detailed decoder 结果可用于指令研究；C256 DS 样本时序通过，其他内核有 233 处负时长/倒序。
原型使用忽略目录的独立源码快照；保留预先暂存的 `.codegraph/.gitignore`，提交只包含本轮明确路径。
严格保持数值、权重、归约和行为；游戏测量继续暂缓，无安装、部署、打包、发布或推送。
恢复发布前仍需核对 EXP-012/015 的 runtime shader 分发/卸载闭包。
核心实验只使用 main-experiment，宿主修改只使用 customer-mochi-backed；本轮没有关机或后台任务授权。
