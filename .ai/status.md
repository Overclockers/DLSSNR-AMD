# 当前状态

更新日期：2026-10-08（Asia/Hong_Kong）。

## 当前工作

运行 `20261007-214238-codegen9070xt`，上限 10；已启动 9，本提交完成后关闭 9。
最近完成 [EXP-059](experiments/EXP-059-windows-gemm-full-subgroups.md)：本机报告 computeFullSubgroups=1；候选日志确认 8 个 GEMM 模块确实设置 flag=2。八份 .text 和完整 driver ELF（包括资源元数据）均逐字节相同，原模式 .text 也匹配 EXP-056 的当前基线。因此“减少编译器 EXEC/setup 工作”的机制在此配置未发生，判为 no_codegen_change，不保留。没有宣称测得零耗时差，也没有启动不必要的完整宿主性能批次；其它驱动或其它 pipeline 不在结论范围。
下一步：实现相邻 K16 激活 tile 的成对布局，先用原图真实权重/输入的隔离算子对检查逻辑中间值、最终输出、输入/guard 及局部控制，再决定是否进入完整宿主。

## 本轮证据

起点主仓 `d42d948`、宿主 `1cc36f9`。
RX 9070 XT / 26.8.1 (LLPC)，2258×1270、2368×1280 工作尺寸、800 tokens、123 dispatches。
100% 模型尺度、单次 NR、完整宿主 R11G11B10、depth=3；四批基线：10.640660 / 10.627673 / 10.667158 / 10.678415 ms。
本轮保留数量：1；历史收益不重复计入本轮。

- EXP-051：unproven；提交 a38c212a0deab9bd9a281e7d27747c59a01d03e8。
- EXP-052：regressed；提交 d6a98bb77880dbd5bb8d4ee33d0d3f2ff77720ff。
- EXP-053：regressed；提交 79790de40444f462834b9bb4642172838efb821e。
- EXP-054：unproven；提交 642f2b12788676e18f3fa10066ebd0f57685c607。
- EXP-055：unproven；提交 91988095f7273c103d52e05e1adb8a416ae1d95a。
- EXP-056：retained；提交 ee8868794e932b7904cc27e4894d00563102a1c7。
- EXP-057：unproven；提交 22d8b4b8800db20ce81c9fa65fe7afd9fb046a96。
- EXP-058：regressed；提交 612444f88d972ddd02dd41c7367f614821f8b903。
- EXP-059：no_codegen_change；提交 本提交。

## 继承状态与边界

此前 20 次运行已耗尽，保留 EXP-044/048；此前三个 no_candidates 运行继续终止。
EXP-050 已完成当前分支与指定上游的四分辨率组合验证，目标尺寸核心时间约省 3.106%；不混作本轮宿主完整区间基线。
EXP-028 只有最终 detailed decoder 结果可用于指令研究；C256 DS 样本时序通过，其他内核有 233 处负时长/倒序。
原型使用忽略目录的独立源码快照；保留原有 `.codegraph/`；不清理或覆盖历史数据。
严格保持数值、权重、归约和行为；游戏测量继续暂缓，无安装、部署、打包、发布或推送。
恢复发布前仍需核对 EXP-012/015 的 runtime shader 分发/卸载闭包。
