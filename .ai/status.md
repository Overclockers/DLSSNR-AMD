# 当前状态

更新日期：2026-10-09（Asia/Hong_Kong）。

## 当前工作

运行 `20261009-005048-lossless9070xt`，上限 10；已启动 9，本提交完成后关闭 9。
最近完成 [EXP-115](experiments/EXP-115-windows-packed-compose-memory-dependency.md)：unproven，不保留。首轮完整路径 10.980515→10.969839 ms，表面省 0.010676 ms，但第三组反向。唯一反序确认 11.008103→11.007965 ms，仅省 0.000138 ms，组差 -0.006776/+0.010741/-0.003551 ms，首轮收益未复现。保留全部样本、关闭该假设，不根据单个有利组保留，也不追加第三轮。
下一步：最后一次尝试：在 ViT 完整键块中先计算两组独立 logits，再按原顺序执行各自的概率、分母和 context，尾块仍用原路径。

## 本轮证据

起点主仓 `5f11e79`、宿主 `6a8e322`。
RX 9070 XT / 26.8.1 (LLPC)，2258×1270、2368×1280 工作尺寸、800 tokens、123 dispatches。
100% 模型尺度、单次 NR、完整宿主 R11G11B10、depth=3；四批基线：10.992494 / 10.960786 / 10.984746 / 10.959362 ms。
本轮保留数量：0；历史收益不重复计入本轮。

- EXP-107：no_codegen_change；提交 875f2c28789687b7c835fd2b63a8f885333a3045。
- EXP-108：unproven；提交 16e056a67b60c6f059754ca0ebbbc834ca89c3aa。
- EXP-109：unproven；提交 5a2f791b7bdad8ec225bf5ee147eedcfd419c047。
- EXP-110：mechanism_not_realized；提交 138077435197a33fdaa2653c2da81090be729d65。
- EXP-111：unproven；提交 af5c4ae0642a661929ac3d978d95ed5958e3c096。
- EXP-112：unproven；提交 7d98351cc59508b79b43f6a5aa86e394fa6a7718。
- EXP-113：regressed；提交 313d157abd830a84e4b71009141b427710c0a5a8。
- EXP-114：unproven；提交 eec66af199b11ab3377cf5a2b0ac2dae008ae2ca。
- EXP-115：unproven；提交 本提交。

## 继承状态与边界

上一轮 EXP-097–106 的 10/10 已结束，未保留新代码；本轮继承 EXP-088 及更早保留版本。
历史收益不重复计入本轮；运行起点四批不与后来候选跨批相减。
EXP-050 已完成当前分支与指定上游的四分辨率组合验证，目标尺寸核心时间约省 3.106%；不混作本轮宿主完整区间基线。
EXP-028 只有最终 detailed decoder 结果可用于指令研究；C256 DS 样本时序通过，其他内核有 233 处负时长/倒序。
原型使用忽略目录的独立源码快照；保留预先暂存的 `.codegraph/.gitignore`，提交只包含本轮明确路径。
严格保持数值、权重、归约和行为；游戏测量继续暂缓，无安装、部署、打包、发布或推送。
恢复发布前仍需核对 EXP-012/015 的 runtime shader 分发/卸载闭包。
核心实验只使用 main-experiment，宿主修改只使用 customer-mochi-backed；用户授权本轮 10 次尝试完成、记录保存后执行 shutdown -s -t 0；未授权后台自动重启实验。
