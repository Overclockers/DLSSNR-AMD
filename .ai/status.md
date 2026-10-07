# 当前状态

更新日期：2026-10-08（Asia/Hong_Kong）。

## 当前工作

运行 `20261007-214238-codegen9070xt`，上限 10；已启动 8，本提交完成后关闭 8。
最近完成 [EXP-058](experiments/EXP-058-windows-vit-activation-buffer-reference.md)：完整宿主初测 10.725677→10.984511 ms，平均慢 0.258834 ms；三组分别慢 0.259664/0.258209/0.258628 ms，明显大于同路径控制最大绝对组差 0.004721 ms。判定本实现退步，不保留，也不消耗追加确认额度。全局地址寻址并未减少该驱动的净工作量；不能从 VMEM 指令名称推断更快。
下一步：在独立原生设备上查询并启用 computeFullSubgroups，仅为 localSizeX 是 32 倍数的 GEMM pipeline 设置 REQUIRE_FULL_SUBGROUPS，比较实际机器码；保持宿主设备契约原样。

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
- EXP-058：regressed；提交 本提交。

## 继承状态与边界

此前 20 次运行已耗尽，保留 EXP-044/048；此前三个 no_candidates 运行继续终止。
EXP-050 已完成当前分支与指定上游的四分辨率组合验证，目标尺寸核心时间约省 3.106%；不混作本轮宿主完整区间基线。
EXP-028 只有最终 detailed decoder 结果可用于指令研究；C256 DS 样本时序通过，其他内核有 233 处负时长/倒序。
原型使用忽略目录的独立源码快照；保留原有 `.codegraph/`；不清理或覆盖历史数据。
严格保持数值、权重、归约和行为；游戏测量继续暂缓，无安装、部署、打包、发布或推送。
恢复发布前仍需核对 EXP-012/015 的 runtime shader 分发/卸载闭包。
