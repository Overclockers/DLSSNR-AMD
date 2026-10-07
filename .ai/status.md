# 当前状态

更新日期：2026-10-07（Asia/Hong_Kong）。

## 当前工作

运行 `20261007-152904-schedule9070xt`，上限 20；已启动 19，本提交完成后关闭 19。
最近完成 [EXP-048](experiments/EXP-048-windows-encode-image-pre-fusion.md)：保留输入编码与 pre 融合；
完整 NR 初测省 0.028813 ms、反序确认省 0.026916 ms，同口径均差约 0.027865 ms。
宿主全量离线构建和 ABI、两仓 shader 配方、序列/边缘图、编码中间值、缺模块回退均精确通过。
宿主提交 `1cc36f96205e361afe5242ea2c330f84b5485b2a`。下一步：最后一个假设，image post 的 RGB 权重面板读取。

## 本轮证据

起点主仓 `10d56b9`、宿主 `3a88c92`。RX 9070 XT / 26.8.1 (LLPC)，2258×1270 输入、2368×1280 工作尺寸。
100% 尺度、单次 NR，完整宿主 R11G11B10、depth=3；800 tokens、123 个网络 dispatch。
保留 EXP-044（约 0.025710 ms）和 EXP-048（在 EXP-044 基线之上约 0.027865 ms）。
这两个增量分别测量，未直接测定二者相对本轮起点的组合收益。时钟、温度和持续后台负载未采样。

- EXP-029：correctness_failed；主仓提交 7e20a21441d63a88551344cda776f96c34da1ffc。
- EXP-030：unproven；主仓提交 754ddb5c6fb5c2c0bfa25ec415a292d8e0fbd228。
- EXP-031：regressed；主仓提交 3c4f0b739659e83ef69fc39d62717de53a483142。
- EXP-033：unproven；主仓提交 ca4d7cc5fbad6c29afff2ab11e93891fd8d4e130。
- EXP-034：regressed；主仓提交 8b97318a2634dd8a2ea39196c3346b9fecab98a6。
- EXP-035：unproven；主仓提交 57cc650b1d3e3c908fb475856b4636c538d24dcd。
- EXP-036：unproven；主仓提交 8593bfdaa02c18fe26cb3474526a0a51cfb91c10。
- EXP-037：unproven；主仓提交 5813758f6ea9bb4c738e81345833fbc4b4baa4b7。
- EXP-038：rejected；主仓提交 33343054ec7ee3524c3cb489020bf0e80ee8661e。
- EXP-039：unproven；主仓提交 5a7fd21e19620abd3af813a321143b47c116ae99。
- EXP-040：unproven；主仓提交 efcb9df2ab6e461bfec39388d4962e81db8a10a0。
- EXP-041：unproven；主仓提交 3034a326de024bd504b3013059c9367054823a2c。
- EXP-042：unproven；主仓提交 8b03c4839f2577dd6beb74ea4e59d6e8fb770935。
- EXP-043：regressed；主仓提交 5c7f001ec0594c363f86586e81b96f6488b48500。
- EXP-044：retained；主仓提交 9b0a9cd640a118c7afed71690c9582ed3e39ef99。
- EXP-045：regressed；主仓提交 2bd19c8551b0c9c3a894ce13c034510bdceb45c4。
- EXP-046：unproven；主仓提交 d5909290266649a3a3a6cafe5c2b680f8a7c007b。
- EXP-047：unproven；主仓提交 93214905e2e6339895020f8d0342cfcbf8483925。
- EXP-048：retained；主仓提交 本提交。

## 继承的研究与边界

EXP-032 为 discovery；双上下文夹具未证明消除控制噪声，不计尝试或收益。
EXP-028 的 C256 DS 详细 SQTT 时序样本可用，其余内核仍有 233 处负时长/倒序；只使用最终验证目录。
EXP-026/027 的计数器和驻留证据保留。旧 no_candidates 运行维持终止，本轮是新的 20 次调用。
失败原型只在忽略目录。worktree 创建被自动审批拒绝后，使用独立源快照；未重试或改权限。
原有 `.codegraph/` 未触碰。Linux 未改；没有游戏、安装、部署、打包、发布、推送或外部消息。
离线证据不代表游戏 FPS、FSR4/FG 或发布验收。恢复发布前仍需核对 EXP-012/015 的 runtime 分发/卸载闭包。
