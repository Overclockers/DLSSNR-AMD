# 当前状态

更新日期：2026-10-08（Asia/Hong_Kong）。

## 当前工作

运行 `20261008-115341-shapes9070xt`，上限 10；已启动 9，本提交完成后关闭 9。
最近完成 [EXP-074](experiments/EXP-074-windows-ffwd-cooperative-q0-weights.md)：完整 NR 三组稳定变慢，平均增加 0.034661 ms；各组慢 0.034748/0.035293/0.033941 ms，控制最大绝对组差 0.007016 ms。额外 LDS、同步和回退控制未形成净收益，关闭为 regressed，不追加确认。
下一步：最后一项根据实际 ISA 的逐分量 half 激活 clamp，测试无运行时分支的位域限幅；先穷举全部 half 值及 NaN/Inf/有符号零语义。

## 本轮证据

起点主仓 `dd48cc3`、宿主 `9d1fa93`。
RX 9070 XT / 26.8.1 (LLPC)，2258×1270、2368×1280 工作尺寸、800 tokens、123 dispatches。
100% 模型尺度、单次 NR、完整宿主 R11G11B10、depth=3；四批基线：10.862125 / 10.870288 / 10.903622 / 10.937338 ms。
本轮保留数量：0；历史收益不重复计入本轮。

- EXP-066：regressed；提交 8ca1b451ddc0433e98771f360c3bf06a34c48de4。
- EXP-067：unproven；提交 6e509c544ec03d53ea957f07793b12a22a7e86f7。
- EXP-068：mechanism_not_realized；提交 0d8bb5b2970ccdd692e92704ed621181a37c191f。
- EXP-069：regressed；提交 d5c5ffd2b82787db629453d46c51572ebbce7083。
- EXP-070：unproven；提交 ce870a06b97375159d58470ae6414fe09c351265。
- EXP-071：mechanism_not_realized；提交 d5335bb34fd5f11e5490cc0eb0d2f1626ae7ab20。
- EXP-072：unproven；提交 2b52db0a331a4648a1b68903f64956d803523b03。
- EXP-073：regressed；提交 f2cc18878a3500fe68a227fb95397fb0c7738d43。
- EXP-074：regressed；提交 本提交。

## 继承状态与边界

前两轮 10/10、5/5 已耗尽并保留 EXP-056/061；更早保留 EXP-044/048。历史收益只作为新基线，不计入本轮。
EXP-050 已完成当前分支与指定上游的四分辨率组合验证，目标尺寸核心时间约省 3.106%；不混作本轮宿主完整区间基线。
EXP-028 只有最终 detailed decoder 结果可用于指令研究；C256 DS 样本时序通过，其他内核有 233 处负时长/倒序。
原型使用忽略目录的独立源码快照；保留原有 `.codegraph/`；不清理或覆盖历史数据。
严格保持数值、权重、归约和行为；游戏测量继续暂缓，无安装、部署、打包、发布或推送。
恢复发布前仍需核对 EXP-012/015 的 runtime shader 分发/卸载闭包。
宿主修改只使用 customer-mochi-backed；本轮没有关机或后台任务授权。
