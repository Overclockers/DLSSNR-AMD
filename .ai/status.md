# 当前状态

更新日期：2026-10-08（Asia/Hong_Kong）。

## 当前工作

运行 `20261008-193306-lossless9070xt`，上限 10；已启动 6，本提交完成后关闭 6。
最近完成 [EXP-102](experiments/EXP-102-windows-c32-probability-partial-scheduling.md)：编译器已把原来的部分和安排为相同机器指令，本原型没有改变目标执行代码，no_codegen_change，不保留。不从未完成的 A/B 批次提取收益，不追加变体或确认。
下一步：继续剩余四次额度，检查 decoder nearest upsample 的四个输出像素是否可以共享同一源投影读取。

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
- EXP-102：no_codegen_change；提交 本提交。

## 继承状态与边界

上一轮 EXP-087–096 的 10/10 已结束，保留 EXP-088；本轮继承该产物以及更早的保留代码。
历史收益不重复计入本轮；运行起点四批不与后来候选跨批相减。
EXP-050 已完成当前分支与指定上游的四分辨率组合验证，目标尺寸核心时间约省 3.106%；不混作本轮宿主完整区间基线。
EXP-028 只有最终 detailed decoder 结果可用于指令研究；C256 DS 样本时序通过，其他内核有 233 处负时长/倒序。
原型使用忽略目录的独立源码快照；保留预先暂存的 `.codegraph/.gitignore`，提交只包含本轮明确路径。
严格保持数值、权重、归约和行为；游戏测量继续暂缓，无安装、部署、打包、发布或推送。
恢复发布前仍需核对 EXP-012/015 的 runtime shader 分发/卸载闭包。
核心实验只使用 main-experiment，宿主修改只使用 customer-mochi-backed；本轮没有关机或后台任务授权。
