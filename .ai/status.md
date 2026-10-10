# 当前状态

更新日期：2026-10-11（Asia/Hong_Kong）。

## 当前工作

运行 `20261011-001700-ten9070xt`，上限 10；已启动 9，本提交完成后关闭 9。
最近完成 [EXP-164](experiments/EXP-164-windows-decoder-aligned-gain-vector-load.md)：完整离线路径初筛10.917434→10.926385 ms，平均慢0.008950 ms；三组节省均为负（-0.013606/-0.007442/-0.005803 ms），同路径控制为+0.001103/-0.007635 ms。没有候选提速信号，按regressed关闭；不把该小差异当作稳定通用回退估计。没有追加确认，未保留生产改动，本轮新增收益仍为0 ms。
下一步：第十次测试ViT fused QKV同token边界guard在channel fragment之间共享；完成十次的保存、提交、推送和最终核验后执行用户指定关机。

## 本轮证据

起点主仓 `e2141ea`、宿主 `f526a6d`。
RX 9070 XT / 26.8.1 (LLPC)，2258×1270、2368×1280 工作尺寸、800 tokens、123 dispatches。
100% 模型尺度、单次 NR、完整宿主 R11G11B10、depth=3；四批基线：10.930771 / 10.922131 / 10.921556 / 10.919724 ms。
本轮保留数量：0；历史收益不重复计入本轮。

- EXP-156：mechanism_not_realized；提交 5b989885aa4a3e28c3d8f1e364bcbc27ce7f12fb。
- EXP-157：unproven；提交 a67a075221870ff7f390f8c469b3f395b8c34dce。
- EXP-158：correctness_failed；提交 d98b24cc9a9421e8c4eb5b224cf7e67386fe6c80。
- EXP-159：unproven；提交 201929eca4d62d159f68016489ee1241a3e618e4。
- EXP-160：mechanism_not_realized；提交 4b0c6879b19762495e25dfc645f0a1bb84ff6358。
- EXP-161：unproven；提交 24fe39e88c0448a10a82deee2f4ccf997b5aa0f6。
- EXP-162：no_codegen_change；提交 7f741298f48b2b87ac11f1b731876dca5ac33445。
- EXP-163：mechanism_not_realized；提交 43951c7b423c45e03f400a02b94beea7023df93c。
- EXP-164：regressed；提交 本提交。

## 继承状态与边界

此前 EXP-131–155 的运行已结束；本轮继承 EXP-131 及更早保留版本，提效工具提交 29f1b93。
EXP-131 的历史收益不计入本轮；本轮使用新的独立基线和逐候选同批对照。
历史收益不重复计入本轮；运行起点四批不与后来候选跨批相减。
EXP-050 已完成当前分支与指定上游的四分辨率组合验证，目标尺寸核心时间约省 3.106%；不混作本轮宿主完整区间基线。
EXP-028 只有最终 detailed decoder 结果可用于指令研究；C256 DS 样本时序通过，其他内核有 233 处负时长/倒序。
原型使用忽略目录的独立源码快照；保留预先暂存的 `.codegraph/.gitignore`，提交只包含本轮明确路径。
严格保持数值、权重、归约和行为；游戏测量继续暂缓，无安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。
恢复发布前仍需核对 EXP-012/015 的 runtime shader 分发/卸载闭包。
核心实验只使用 main-experiment，宿主修改只使用 customer-mochi-backed；本轮为十次无损实验；用户要求十次完成、保存和核验后执行 shutdown -s -t 0；不启动后台自动续跑。新性能批次先过精确输出/ISA门槛，按阶段保存壁钟。
