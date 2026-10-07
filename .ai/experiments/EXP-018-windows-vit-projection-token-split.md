# EXP-018：800-token ViT 投影的 wave / 工作组分工

日期：2026-10-07。结论：未保留；第 3 个假设，两版实现及一次确认均未建立可靠收益。
主仓 `6115e9856335c327cc121dd7d5203a8a45ae299b`，宿主 `3a88c92d7d85933cf7c290cbb9fa25f9faa6d688`。

## 形状依据与实现

见 [本轮新基线](EXP-016-windows-vit-expand-n-split.md)。ViT 800×1024×1024 投影每帧 8 次，
原 200 工作组×4 waves，每 wave 32×32、75 VGPR。共用 `gemmproj` 的 C512 则是 380 组，不能合并调参。

- 版本 1：`WM=2 MFRAG=1 WN=4 NFRAG=2`，每 wave 16×32，工作组保持 32×128，200 组×8 waves，VGPR **51**。
- 版本 2：保持相同每-wave 输出，`WM=1 MFRAG=1`，工作组改为 16×128，400 组×4 waves。
  私有工具按 16-token tile 同步 gx；不是只改 shader 后沿用旧网格。此版 VGPR 未单独采集，不沿用版本 1 数字作实测值。
- 保留 `KFRAG=16`、每输出沿 K=0,16,… 的 MMA 次序、FP32 累加/FP16/FP8 舍入、skip 权重及 N-pair 布局。
- 同时生成普通和末层 `NR_OUT_PERM=1` 版本。私有工具只替换八个 ViT 投影步骤；C512 继续用原 kernel。

## 工具与正确性

私有 `shape_study` 直接使用现有 NrSession、原图权重打包和 runner；建立同一 device/arena 的独立 candidate pipelines。
完整网每批 A/B 后回读 FP32 图；另外分别执行到每个目标层的 prefix，在中间值仍存活时下载并逐字节比较。
三个 study 的 **24 份中间数组、6 份保存图像及 72 批回读** 一致；每份目标中间数组 819200 字节。
第一版独立普通图输出及寄存器诊断输出也一致。工具打印实际 PushGemm M/N/K 和网格，补充核对 EXP-016 的形状表。
同路径 control 复用原步骤检查测量漂移；两套候选不修改 production 源码或宿主。

- 工具二进制 SHA-256：`0fbd2772dfd0f074c8f77feb52f29e1cae07afbeb59cfff4b76dc3363c8486a5`。
- 工具源码 SHA-256：`7dc0bb00307004fcdaf9eb64d97a621201b2570d38ccf056a588945e083bc294`。
- rebuilt/gemmproj: `77ef66c73f2c9b25576d4bfd4d753b341a188882d1004fd70ff1c15d26bdaa08`。
- rebuilt/gemmprojt: `1df50bed87ddea923ba6eea27876d9039bf367b656aa863b1e19aaad399ac68c`。
- token-split/gemmproj: `3ae77162f26e729e1d40ce34fbe19d8a2a8100b8fb1eda36af316cc39735b7dd`。
- token-split/gemmprojt: `a0825197290d77f88037d564a6c7132741806d3e6f46c9ccec766e1d44e726c7`。
- more-groups/gemmproj: `f9c07df262b2517652dc0171b37579698afd2fa62aac87d3f8b52b6254cc7ff3`。
- more-groups/gemmprojt: `944f04aff5f4459218fd357fa3e308b4530d3b3b2f0501e6d6bbbb9d1154daa5`。

## 测量及决定

RX 9070 XT / 26.8.1 (LLPC)、MSVC 14.51.36231、glslang 16.5.0。
2258×1270→工作 2368×1280、800 tokens；FP32 照片、100% scale、单遍、无历史/SR/FG。
**此处计时是 standalone 整网，不包含宿主 D3D12/Vulkan 搬运或编码/合成。**
每批预热 100、测量 200 帧；每 study ABBA×3 + 同路径 ABBA×3，共 14400 个测量帧。
统计构建时间排除在外；未采样时钟/温度，批次有缓慢漂移，完整数据保留。

| 比较 | 基线 ms | 候选 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| token-split-study ab | 10.517337 | 10.523669 | -0.006332 | -0.019407 / -0.002283 / 0.002694 |
| token-split-study control | 10.626711 | 10.624098 | 0.002613 | 0.001867 / 0.009170 / -0.003198 |
| more-groups-study ab | 10.582832 | 10.592376 | -0.009544 | -0.017488 / -0.010854 / -0.000289 |
| more-groups-study control | 10.647892 | 10.648940 | -0.001048 | -0.006102 / 0.003791 / -0.000831 |
| confirmation-study ab | 10.622704 | 10.632348 | -0.009645 | -0.024769 / -0.001352 / -0.002813 |
| confirmation-study control | 10.672297 | 10.672052 | 0.000244 | 0.000409 / 0.004140 / -0.003816 |

版本 1 与控制波动重叠，版本 2 三组稍慢。唯一确认后仍无可重复的净收益，因此不推进宿主完整区间测试/集成。
不把这些结果当作宿主 FPS、最终画质或所有尺寸的结论；没有扩大边界、格式、历史/重放验收。

```text
shape_study 2258 1270 <plan> <output-prefix> CCVit1DProjection <candidate-shaders> --reuse --model-pack <private-model> --spv-dir <baseline-shaders> --pipeline-cache <cache> --in-image <photo>
```

版本 2 另加 `--candidate-mt 16`。若日后成立，必须新增按形状选择的生产入口，同步 host 常量、marker 和权重打包注册。
本次只提交实验记录/索引/状态；原型与工具在忽略目录，无宿主修改、Linux 修改、部署、发布包或推送。
下一步从残差读取的字节指令入手，避免继续对这两种 token 拆分盲扫。
