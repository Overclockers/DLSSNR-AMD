# EXP-021：ViT QKV 单 head 的 wave 分工及权重加载

日期：2026-10-07。结论：未保留；第 6 个假设，两版均三组退步，无需额外确认。
主仓 `d7f283a496f5b6445cdebfcf60c2ce848c6ae430`，宿主 `3a88c92d7d85933cf7c290cbb9fa25f9faa6d688`；当前算子表见 [EXP-016](EXP-016-windows-vit-expand-n-split.md)。

## 依据与两版原型

ViT QKV：M/N/K=800/3072/1024、每帧 8 次、156 工作组/次、4 waves/组，当前 167 VGPR。
比扩张层的 208 组更少，还包含 32 通道 head 的归一化；没有套用 EXP-016 的结论代替这个形状的实测。
原每 wave 输出 64×64，两个 heads；改成 64×32、一个 head，组内八 waves、工作组仍 64×256。
`WM=1 WN=8 MFRAG=4 NFRAG=2 KFRAG=2`，保留 64-token 范围、每输出 K 次序和 N-pair weights。
每个 head 仍由完整的一对 16-channel fragments 处理，半精度归一化的配对和蝶形次序不变。

版本 1 降至 **95 VGPR**，无 LDS/scratch；但 ISA 中一个 K step 的权重向量被拆成四条 b32 读取。
版本 2 添加现有 `NR_GLD_U2=1`，保留原字节及布局；确认变成两条 b128 权重读取，
整个内核 b32 静态读取 20→16（其余为 head scales），VGPR 仍为 95。没有改变精度或引入新权重格式。
这与历史 EXP-002 的同时拆 M/N 或只拆 M 不同，已有小 ViT 收缩收益不重复计入。

## 验证与性能

两版都先做 SPIR-V/寄存器诊断，并在独立完整图上与基线逐字节对照。
同一 session 只替换八个 QKV 步骤；各自 **8 份 live 中间数组**（每份 2457600 字节）、两份保存图像、24 批图像回读一致。
合计 16 个中间数组、48 批回读、4 份保存图像及两份统计构建图像一致。baseline shader 重编也完全相同。

RX 9070 XT / 26.8.1 (LLPC)、glslang 16.5.0 / MSVC 14.51.36231；2258×1270、工作 2368×1280、800 tokens。
FP32 照片、scale 1、单遍，无历史/SR/FG。**下表是 standalone 整网，不含完整宿主 NR 的搬运/转换。**
每版 ABBA×3 + 同路径 ABBA×3；每批预热 100、测量 200 帧，合计 9600 测量帧。

| 比较 | 基线 ms | 候选 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| one-head-study ab | 10.522089 | 10.546369 | -0.024279 | -0.040704 / -0.010113 / -0.022021 |
| one-head-study control | 10.630842 | 10.628117 | 0.002726 | 0.006183 / 0.001435 / 0.000559 |
| one-head-vector-study ab | 10.564247 | 10.589278 | -0.025030 | -0.039811 / -0.010173 / -0.025106 |
| one-head-vector-study control | 10.647847 | 10.652209 | -0.004362 | 0.002054 / -0.006122 / -0.009018 |

两版都是三组变慢，约增加 0.024–0.025 ms；减少 VGPR 和修复向量读取没有形成整网收益。
没有选择性删除批次，也没有继续盲扫 KFRAG/更多 waves。未采样时钟、温度及持续后台利用率。
因为筛选未通过，没有进入 host 完整区间、历史/重放/格式/边界尺寸等扩大验收。
生产源码及 Linux 未改；如果日后重启，需要为已验证 token 范围注册独立入口和配套 host/marker/packing。

- rebuilt SPIR-V SHA-256: `7e3251e0d1cddac21a28f55578c40be707e864327222d64d82dfca16ab9aa0eb`。
- one-head SPIR-V SHA-256: `0a8a73e44f9ad6abecc83cd205ce317b4eb9d40d756d5ce23a1eb7680b663317`。
- one-head-vector SPIR-V SHA-256: `a4a697cbd211dc9e9d14bece956d50670ba050d59f42cdbaeda74f7ff3e36988`。
- 工具二进制：`b495d060d79dd2034d8caadbeb1ab19403171125ceed5c096ca6faae49cf2b8d`；源码：`ed75a1a263383970abf16906cbcfde9f507592692ec62d4f86e1bf802b253c94`。

```text
shape_study 2258 1270 <plan> <prefix> CCVit1DQKV <candidate-shaders> --reuse --model-pack <private-model> --spv-dir <baseline-shaders> --pipeline-cache <cache> --in-image <photo>
```

只提交公开记录、索引、状态；原型、ISA、身份和输出留忽略目录。宿主无提交，没有游戏、部署、打包或推送。
下一步检查 C256 DS 既有 wave/head 所有权在地址生成中是否被标量化；不再缩小输出块。
