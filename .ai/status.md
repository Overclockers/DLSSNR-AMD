# 当前状态

更新日期：2026-10-07（Asia/Hong_Kong）。

## 当前工作

运行 `20261007-214238-codegen9070xt`，上限 10；已启动 6，本提交完成后关闭 6。
最近完成 [EXP-056](experiments/EXP-056-windows-vit-contract-fixed-dimensions.md)：保留 ViT 固定 K/N 特化。
完整宿主初测节省 0.046392 ms（同期控制较大）；唯一反序确认节省 0.037738 ms，三组均超过同期控制差。
126 对目标历史、6 对边界/回退、基本合约及两仓全量 shader 构建通过；宿主提交 `70484c408f717ba97da8636a2f0346a3e983fc40`。
下一步：在此基线上检查尾部整 wave 的无效计算，继续剩余 4 项。

## 本轮证据

RX 9070 XT / 26.8.1 (LLPC)，2258×1270→2368×1280，800 tokens、123 dispatches，scale 1/pass 1。
完整宿主 R11G11B10、depth 3。起点四批 10.640660 / 10.627673 / 10.667158 / 10.678415 ms，不与后续直接相减。
当前保留 1 项，保守确认收益 0.037738 ms；ADL 从 EXP-052 起采样，包含启动/预热，不与测量帧同步。

- EXP-051：unproven；主仓 a38c212a0deab9bd9a281e7d27747c59a01d03e8。
- EXP-052：regressed；主仓 d6a98bb77880dbd5bb8d4ee33d0d3f2ff77720ff。
- EXP-053：regressed；主仓 79790de40444f462834b9bb4642172838efb821e。
- EXP-054：unproven；主仓 642f2b12788676e18f3fa10066ebd0f57685c607。
- EXP-055：unproven；主仓 91988095f7273c103d52e05e1adb8a416ae1d95a。
- EXP-056：retained；主仓 本提交。

## 边界

EXP-044/048 为此前运行成果，EXP-050 是独立上游比较，不重复计入本轮。
EXP-028 只使用最终验证目录；C256 DS 样本时序有效，其余内核有残余时序问题。
所有原型/捕获/模型保留在忽略目录；`.codegraph/` 原有未跟踪内容未改。
Linux、游戏、SR/FG、安装、部署、打包、发布和推送均未进行；离线结果不代表游戏帧率。
发布前仍需 runtime shader 分发/卸载闭包等已有检查。
