# 当前状态

更新日期：2026-10-10（Asia/Hong_Kong）。
运行 `20261009-235203-ten9070xt`，上限10；已启动1，本提交关闭1，继续剩余9项。
[EXP-131](experiments/EXP-131-windows-ffwd-peeled-weight-lookahead.md) 保留：初测和反序确认均节省约0.0356 ms，登记0.035623 ms（非保证下界）。
两套配方的全部FFWD中间输出、各126对目标历史/格式、边界与生命周期精确验证通过。
宿主提交 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4` 保留本地customer-mochi-backed；主仓main-experiment为本提交，按技能推送。
下一项检查ViT尾块重复读取相同clamped V片段。

RX9070XT/26.8.1 LLPC，2258×1270→2368×1280，800tokens、123dispatches，100%尺度、1pass。
宿主完整计时R11G11B10/depth3；ADL只读采样含启动和预热，未与测量帧同步。
运行起点核验65宿主源、77产物；继承EXP-088，历史收益不重复计入。
EXP-050上游比较不混作当前完整宿主基线；SQTT只用EXP-028最终验证数据。
保留预先暂存.codegraph/.gitignore；模型、原型、ISA和回读留忽略目录。
严格无损、Windows-only、离线；无游戏、安装、部署或打包发布；宿主不推送。
恢复发布前仍需核对EXP-012/015 runtime shader分发/卸载闭包。
用户要求十次研究全部完成、保存并核验推送后执行shutdown -s -t 0；当前尚未安排关机，无后台续跑。
