# 当前状态

更新日期：2026-10-07（Asia/Hong_Kong）。

## 当前工作

运行 `20261007-152904-schedule9070xt`，上限 20；已启动 15，本提交完成后关闭 15。
最近完成 [EXP-044](experiments/EXP-044-windows-attention-fragment-word-loads.md)：保留对齐 8 字节片段读取，
完整 NR 初测省 0.021130 ms、反序确认省 0.030290 ms；合并平均省 0.025710 ms。
两仓配方各 15 对序列、6 对完整输入及多遍切换精确通过；Windows shader 已同步。
宿主提交 `137b3859a7572bee73c50a6975ab87f11adc4abf`。本轮目前保留 1 项优化，后续以它为基线。
下一步：研究 persistent 参数记录的 16 字节向量读取，保持原加载位置与 item 生命周期。

## 本轮证据

起点主仓 `10d56b9`、宿主 `3a88c92`，初始 65 个宿主源与 76 个产物身份通过。
RX 9070 XT / 26.8.1 (LLPC)，2258×1270 输入、2368×1280 工作尺寸，800 tokens、123 dispatches。
100% 尺度、单次 NR，完整宿主 R11G11B10、depth=3。起点四批基线约 10.713–10.765 ms；
不与其它时间口径或后续批次直接相减。时钟、温度和持续后台负载未采样。

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
- EXP-044：retained；主仓提交 本提交。

## 继承的研究与边界

EXP-032 为独立 discovery，完整宿主双上下文夹具没有证明消除控制噪声。
EXP-028 最终 SQTT 的 C256 DS 时序样本可用，其余内核仍有 233 处负时长/倒序；只使用最终验证目录。
EXP-026/027 的计数器、驻留研究保留。旧 no_candidates 运行维持终止，本轮是用户新调用 20 次。
未保留的原型只在忽略目录。隔离 worktree 被自动审批拒绝后，使用独立源码快照；未重试或改权限。
原有 `.codegraph/` 未触碰。Linux 未改；没有游戏、安装、部署、打包、发布、推送或外部消息。
离线证据不代表游戏 FPS、FSR4/FG 或发布验收；恢复发布前仍需核对 runtime shader 分发/卸载闭包。
