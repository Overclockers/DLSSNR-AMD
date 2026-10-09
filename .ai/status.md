# 当前状态

更新日期：2026-10-10（Asia/Hong_Kong）。

## 当前工作

运行 `20261009-235203-ten9070xt`，上限 10；已启动 7，本提交完成后关闭 7。
最近完成 [EXP-137](experiments/EXP-137-windows-input-noise-load-lookahead.md)：预期的加载/纹理或编码重叠未实现，mechanism_not_realized，不保留。第一版已启动的完整ABBA结果节省 -0.008658 ms，方向混合且未建立可靠收益，全部结果保留。针对wait修正后真实代码不变，停止本假设，不消耗追加确认。
下一步：检查ViT概率片段直接转为B操作数时，能否在保留原key permutation的条件下减少FP8打包。

## 本轮证据

起点主仓 `879291d`、宿主 `6a8e322`。
RX 9070 XT / 26.8.1 (LLPC)，2258×1270、2368×1280 工作尺寸、800 tokens、123 dispatches。
100% 模型尺度、单次 NR、完整宿主 R11G11B10、depth=3；四批基线：10.943105 / 10.917341 / 10.917457 / 10.944389 ms。
本轮保留数量：1；历史收益不重复计入本轮。

- EXP-131：retained；提交 076637eb4cdfa80f6a508d1ffdd01fb4ba328177。
- EXP-132：unproven；提交 3c58a503d504d23ea536a431716d5f5838d8a938。
- EXP-133：unproven；提交 0b261a904c774c252d9432c178303db6b8632e1c。
- EXP-134：mechanism_not_realized；提交 b5afd2d3b4215ed117a802be55fb56aa3fcde55d。
- EXP-135：unproven；提交 3c937554b5ebc0bc71c46b0afccbd1f4830ce563。
- EXP-136：unproven；提交 e735b8181e98474c4bfeb1c3d329f70bf00295b3。
- EXP-137：mechanism_not_realized；提交 本提交。

## 继承状态与边界

此前 EXP-126–130 的五次运行已结束，未保留新代码；本轮继承 EXP-088 及更早保留版本。
本轮 EXP-131 已保留 FFWD 首步预取消费展开：初测省0.035623 ms、确认省0.035626 ms；后续候选使用已更新的独立基线。
历史收益不重复计入本轮；运行起点四批不与后来候选跨批相减。
EXP-050 已完成当前分支与指定上游的四分辨率组合验证，目标尺寸核心时间约省 3.106%；不混作本轮宿主完整区间基线。
EXP-028 只有最终 detailed decoder 结果可用于指令研究；C256 DS 样本时序通过，其他内核有 233 处负时长/倒序。
原型使用忽略目录的独立源码快照；保留预先暂存的 `.codegraph/.gitignore`，提交只包含本轮明确路径。
严格保持数值、权重、归约和行为；游戏测量继续暂缓，无安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。
恢复发布前仍需核对 EXP-012/015 的 runtime shader 分发/卸载闭包。
核心实验只使用 main-experiment，宿主修改只使用 customer-mochi-backed；本轮为十次无损实验；用户明确要求十次完成并保存后执行 shutdown -s -t 0；不启动后台自动续跑。
