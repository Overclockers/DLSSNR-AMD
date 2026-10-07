# 当前状态

更新日期：2026-10-07（Asia/Hong_Kong）。

## 当前工作

运行 `20261007-152904-schedule9070xt`，上限 20；已启动 13，本提交完成后关闭 13。
最近完成 [EXP-042](experiments/EXP-042-windows-integer-image-fetch.md)：初测三组均快，平均节省 0.012621 ms，但初测控制均差为 -0.020505 ms。唯一反序确认平均增加 0.010898 ms；确认控制 +0.042935 ms，第一组含 11.000194/10.966974 ms 批次，全部保留而未剔除。方向未复现且同路径噪声覆盖候选效应，unproven，不保留。
下一步：研究 persistent 层参数重复加载；先将固定 DS/UPS 参数移出 item 循环，再根据实际寄存器与代码评估层参数缓存。

## 本轮证据

起点主仓 `10d56b9`、宿主 `3a88c92`。65 个宿主源、76 个产物身份匹配。
RX 9070 XT / 26.8.1 (LLPC)，2258×1270 输入、2368×1280 工作尺寸，800 tokens、123 dispatches。
100% 尺度、单次 NR，完整宿主 R11G11B10、depth=3。新鲜四批基线：10.713152 / 10.765059 / 10.755895 / 10.752117 ms。
诊断完整路径 10.774323 ms、算子 10.355108 ms；不与低插桩验收混减。
本轮没有保留新的生产提速；时钟、温度和持续后台负载未采样。

- EXP-029：correctness_failed；提交 7e20a21441d63a88551344cda776f96c34da1ffc。
- EXP-030：unproven；提交 754ddb5c6fb5c2c0bfa25ec415a292d8e0fbd228。
- EXP-031：regressed；提交 3c4f0b739659e83ef69fc39d62717de53a483142。
- EXP-033：unproven；提交 ca4d7cc5fbad6c29afff2ab11e93891fd8d4e130。
- EXP-034：regressed；提交 8b97318a2634dd8a2ea39196c3346b9fecab98a6。
- EXP-035：unproven；提交 57cc650b1d3e3c908fb475856b4636c538d24dcd。
- EXP-036：unproven；提交 8593bfdaa02c18fe26cb3474526a0a51cfb91c10。
- EXP-037：unproven；提交 5813758f6ea9bb4c738e81345833fbc4b4baa4b7。
- EXP-038：rejected；提交 33343054ec7ee3524c3cb489020bf0e80ee8661e。
- EXP-039：unproven；提交 5a7fd21e19620abd3af813a321143b47c116ae99。
- EXP-040：unproven；提交 efcb9df2ab6e461bfec39388d4962e81db8a10a0。
- EXP-041：unproven；提交 3034a326de024bd504b3013059c9367054823a2c。
- EXP-042：unproven；提交 本提交。

## 继承的研究与边界

EXP-028 最终详细 SQTT 的地址/控制流通过；C256 DS 样本时序可用，其他内核仍有 233 处负时长/倒序。
只使用 `driver-final-results/timed-hardware-decode/`，不将跨 wave 时长和等待比例换算帧时间收益。
EXP-026/027 的计数器、驻留研究保留；旧公共 DS/expansion 变体未保留。详见实验索引。
之前三轮 no_candidates 运行保持终止状态；本轮为用户明确新调用 20 次。
历史保留优化不重复计入本轮收益；仅保留精度、舍入、归约、权重和完整行为一致的候选。
原型始终位于忽略目录。隔离 worktree 创建被自动审批拒绝后，使用独立源码快照；未重试或改权限。
当前主仓/宿主生产未改。原有 `.codegraph/` 未触碰。游戏测量继续暂缓。
没有安装、部署、打包、发布或推送；离线证据不代表游戏 FPS、FSR4/FG 或发布验收。
恢复发布前仍需核对 EXP-012/015 的 runtime shader 分发/卸载闭包。
