# 当前状态

更新日期：2026-10-08（Asia/Hong_Kong）。
运行 `20261008-162203-lossless9070xt`，上限10；已启动2，本提交关闭2，继续剩余8项。
[EXP-087](experiments/EXP-087-windows-swin-weight-pair-wide-load.md) 16字节成对权重读取回退：完整宿主慢0.280377 ms。
[EXP-088](experiments/EXP-088-windows-swin-projection-token-batches.md) 保留：初测省0.012494 ms、确认省0.007304 ms；登记较小均值0.007304 ms，非保证下界。
两套配方的池化中间值、各126对目标历史、边界与生命周期精确验证通过。
宿主提交 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`，customer-mochi-backed；主仓main-experiment为本提交。
下一项检查C32 Q/K规范化中重复的rsqrt工作。

RX 9070 XT/26.8.1 LLPC，2258×1270→2368×1280，800 tokens、123 dispatches，100%尺度、1pass。
宿主完整计时R11G11B10/depth3；ADL只读0.5秒采样含启动和预热，不与测量帧同步。
运行起点核验65宿主源、77产物；继承上一轮EXP-076，历史收益不计入本轮。
EXP-050上游比较不混作当前完整宿主基线；SQTT只用EXP-028最终已验证C256 DS数据。
保留预先暂存.codegraph/.gitignore；模型、原型、ISA和回读留忽略目录。
严格无损、Windows-only、离线；无游戏、安装、部署、打包发布、推送、关机或后台任务。
恢复发布前仍需核对EXP-012/015 runtime shader分发/卸载闭包。
