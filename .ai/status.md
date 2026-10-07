# 当前状态

更新日期：2026-10-08（Asia/Hong_Kong）。

## 当前工作

运行 `20261007-214238-codegen9070xt`，上限 10；已启动 10，本提交关闭第 10 项，达到 10/10。停止原因：budget_exhausted。
最近完成 [EXP-060](experiments/EXP-060-windows-vit-paired-k-activation-layout.md)：第一版在八个正反序 A/B 组全部变慢，局部平均增加 0.005371 ms；明确写入常量偏移的第二版仍八组全部变慢，平均增加 0.004860 ms。最小退步也大于各自控制最大绝对组差，判为 regressed，不保留。不把局部差值乘八当作完整 NR 变化。未启动完整宿主验收，也未消耗不需要的追加确认额度。
下一步：本次用户授权的 10 项已达上限；本提交后核对提交与基线身份，结束本轮，不自动开启新实验。

## 本轮证据

起点主仓 `d42d948`、宿主 `1cc36f9`。
RX 9070 XT / 26.8.1 (LLPC)，2258×1270、2368×1280 工作尺寸、800 tokens、123 dispatches。
100% 模型尺度、单次 NR、完整宿主 R11G11B10、depth=3；四批基线：10.640660 / 10.627673 / 10.667158 / 10.678415 ms。
本轮保留 1 项：[EXP-056](experiments/EXP-056-windows-vit-contract-fixed-dimensions.md)，确认节省 0.037738 ms（约 0.350%）。
主仓优化提交 `ee8868794e932b7904cc27e4894d00563102a1c7`；宿主 `70484c408f717ba97da8636a2f0346a3e983fc40`。
其余 9 项：4 项 unproven、4 项 regressed、1 项 no_codegen_change；全部原型留在忽略目录。
附带验证进展：EXP-056 补齐目标分辨率的 13 种底层格式、126 对动态历史输出和边界/回退，未另计尝试。
两仓没有待提交的本轮实现；主仓仅保留原有未跟踪 `.codegraph/`。历史收益不重复计入本轮。

- EXP-051：unproven；提交 a38c212a0deab9bd9a281e7d27747c59a01d03e8。
- EXP-052：regressed；提交 d6a98bb77880dbd5bb8d4ee33d0d3f2ff77720ff。
- EXP-053：regressed；提交 79790de40444f462834b9bb4642172838efb821e。
- EXP-054：unproven；提交 642f2b12788676e18f3fa10066ebd0f57685c607。
- EXP-055：unproven；提交 91988095f7273c103d52e05e1adb8a416ae1d95a。
- EXP-056：retained；提交 ee8868794e932b7904cc27e4894d00563102a1c7。
- EXP-057：unproven；提交 22d8b4b8800db20ce81c9fa65fe7afd9fb046a96。
- EXP-058：regressed；提交 612444f88d972ddd02dd41c7367f614821f8b903。
- EXP-059：no_codegen_change；提交 a78283b9651029077566c0a756a700401c5ff464。
- EXP-060：regressed；提交 本提交。

## 继承状态与边界

此前 20 次运行已耗尽，保留 EXP-044/048；此前三个 no_candidates 运行继续终止。
EXP-050 已完成当前分支与指定上游的四分辨率组合验证，目标尺寸核心时间约省 3.106%；不混作本轮宿主完整区间基线。
EXP-028 只有最终 detailed decoder 结果可用于指令研究；C256 DS 样本时序通过，其他内核有 233 处负时长/倒序。
原型使用忽略目录的独立源码快照；保留原有 `.codegraph/`；不清理或覆盖历史数据。
严格保持数值、权重、归约和行为；游戏测量继续暂缓，无安装、部署、打包、发布或推送。
恢复发布前仍需核对 EXP-012/015 的 runtime shader 分发/卸载闭包。
