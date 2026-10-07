# EXP-056：ViT 收缩固定通道尺寸的代码生成

日期：2026-10-07（Asia/Hong_Kong）。结论：**保留；完整离线路径反序确认节省 0.037738 ms。**
运行 `20261007-214238-codegen9070xt` 第 6/10 个假设，1 个优化实现，1 次性能追加确认。
主仓起点 `91988095f7273c103d52e05e1adb8a416ae1d95a`，宿主起点 `1cc36f96205e361afe5242ea2c330f84b5485b2a`；
宿主保留提交 `70484c408f717ba97da8636a2f0346a3e983fc40`。

## 假设与实现

Windows 的 gemmprojws 只由 CCVit1DFfnContract 选择，该层类型的 N=1024、K=4096 是图定义，
此前却作为 push constant 参与地址和循环计算。增加 NR_FIXED_KN 编译开关，仅在该 pipeline 启用，
将这两个不变量暴露为 uint 常量；M 仍动态。main 入口保留 K/N 一致性检查。
没有更改一次 WMMA 的输入、K 迭代顺序、精度、舍入、饱和、残差、权重或布局。

可达性由 nr_graph.cpp 的类型到形状映射、F_GEMM 分支对 sh.N/sh.K 的直接赋值、
以及 gemmprojws 唯一选择点共同约束。原有选择范围 768≤M≤1024、64×128 tile 不变。
--no-projws、--no-projw、小网格和大网格继续选择原通用模块；禁用编译开关重现原 SPIR-V。
不新增 kernel、binding、push 布局或 host 网格常量，原有 weight packing 和 shader-constants 不变。
只改两仓 Windows 的 gemm1x1.comp / pipelines.json，Linux 不变。

## 环境与性能

RX 9070 XT / AMD proprietary 26.8.1 (LLPC)，glslang 16.5.0、MSVC 14.51。
2258×1270 R11G11B10 线性照片，2368×1280 工作尺寸、800 tokens、123 dispatches，scale 1、单次 NR、depth 3。
验收为 D3D12 输入复制到输出复制的完整 NR GPU 区间，无历史/SR/FG 性能负载。
每批预热 100 帧、正式 200 帧，ABBA×3、BAAB×3，各附同路径控制×2；全部批次保留。
ADL 只读传感器每 0.5 秒采集 GFX/MEM 时钟、温度、活动率和功耗，覆盖启动/预热；
传感器并非与测量帧同步，未修改时钟、驱动或后台进程。

| 批次 | 基线 ms | 候选/控制 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-fixed | 10.758901 | 10.712509 | 0.046392 | 0.053688 / 0.048801 / 0.036685 |
| ba | 10.769130 | 10.731392 | 0.037738 | 0.046075 / 0.040411 / 0.026729 |
| confirm-control | 10.819057 | 10.826981 | -0.007924 | -0.017883 / 0.002035 |
| control-fixed | 10.817154 | 10.865018 | -0.047865 | -0.074694 / -0.021035 |

初测三组同向，但初测控制很不稳定，不能将其当作独立明确收益；这份控制未删除。
唯一反序确认的三组收益 0.046075/0.040411/0.026729 ms 均大于该次控制最大绝对组差 0.017883 ms，
且与初测方向一致。保守记录确认平均 0.037738 ms（约 0.350%），不把两次控制混入收益均值。
控制差只是本次噪声证据，不是普适误差界；不推算游戏 FPS 或 Linux 差距。

driver ELF .text 5504→4736 字节，静态指令行 770→680；VGPR 70→95、SGPR 24→30，LDS/scratch 为 0。
减少地址和循环指令并不等于增加驻留；完整路径实测是保留依据。

## 精确验证与适用边界

- 目标整网 FP32 照片及所有 42 份宿主性能批次末帧 packed 输出逐字节一致。
- 原有 GPU 合约夹具的 15 对回读、6 对梯度/随机棋盘/HDR 输入和 normal 多遍切换通过；
  包括 2258×1270、2257×1269、129×65、17×19、双队列、重放、延迟回收、DRS 和取消。
- 新夹具在目标尺寸、非零亚像素 motion 和逐帧平移图案/变化 alpha 下比较 126 对完整历史输出。
  覆盖宿主 ColourFormat 的全部 13 种底层 Vulkan 颜色格式（RGBA8 另含 sRGB 别名），
  包括 FP32/FP16、10/11 位浮点或整数、8/16 位 UNORM/SNORM 和 16 位 565/5551/4444。
  每种测试 9 次执行，涵盖连续帧、丢弃、ResetHistory、乱序、同 recording 重放；历史消费计数断言保留。
  其余 typeless/BGRX/sRGB 别名按未改的 ColourFormat 映射复用同一 Vulkan 字节格式；不声称每个 DXGI 别名均独立运行。
- 初版目标夹具在 R9 第 2 帧出现历史不消费，未通过。它在执行间转换、检查并写出大图，
  可越过 MochizukiRecording.inl 的 250 ms 连续性门限。新版预生成输入并在序列后写盘，
  不修改生产门限/历史断言；两臂重跑 36 对完全一致。旧源码、可执行、失败日志/回读全部保留。
- 另 6 对原生 FP32 全图逐字节一致：640、768、1024、1120 tokens，以及目标 --no-projws/--no-projw。
  dispatch 输出确认上下边界和通用 fallback 实际选择；768/1024 执行变化模块，640/1120 执行原模块。
- 两仓三套全量 Windows shader 构建各 64 个 SPIR-V；只有 g_gemmprojws.spv 改变，其他 63 个及 marker 不变。
  主仓与宿主该模块的前后产物分别逐字节一致，保留产物也与实测候选一致，因此复用相同产物的验证。
  C++ 和 ABI 源完全未变，复用已核验 production DLL；未重新编译或测试安装包。

数值证明为固定维度替换与原算术序列保持，实际编译路径以精确回读检查；没有用容差放行。
仍未进行游戏、发布、安装、部署或打包验证；全部证据是离线的。

## 身份与复现

- 原模块：`e0c3e40657cb29894195d127fccd9df6e5d03507881f216e804c31e96ae60143`。
- 保留模块：`efdae1191942b8913e3b6411d8342225aa72c2904a819ece57fd1e24d7c0e2d4`。
- 最终补丁：`b94dafd1e973487dbb9e5f39079ae6fc14ff1643cf3cc9bbbfdd98abd2d9cd0c`。
- 构建闭包：`2d775446fd2bb88f31b8a1d8507153533169e4a5544d061c380f01deb48d2c46`。
- 基本验证：`52df3d1252204858f388eb127568f0cba650d80fcdbc2b52743094518c389a39`。
- 目标历史验证：`7ffb49b2629ac5fd1cd25502b80ef8179895f0f6b0a024517cbd1626efde9e4d`。
- 扩展格式验证：`17f38c66be2771e93130b57d8365c68ef5b1e0eb8a7696346d710400fda3e158`。
- 边界/回退验证：`9443e672c04f5ffa352d7f99123e20eb2df90fc17868290f2b377d67bd6999ff`。
- 修正历史夹具源码/二进制：`e2ecf83d2e85ab739fc0cb447af428201e2da671a13adb9815338926663dc1cd` / `21656f80a2e0e36996e37f394c9918e2333794c7218482f4328a5b94391d4fc2`。
- 扩展格式夹具源码/二进制：`7e4df295cf85baca35bc78e6df34b4d6d364c8dfc742f2c3598cabae3ab9356d` / `7cc8d7556ddb6bdfcb529a4eae80ee7bec12840df577e15adc3f6e5b7b0c7434`。

```text
python -X utf8 windows/build/build_network.py rdna4 --out <shaders> --glslang <pinned-glslang>
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer --dispatch-grid
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
target_history_v2.exe <production-dll> <assets>
target_formats.exe <production-dll> <assets>
```

后两条为本机只读回放夹具，使用 NR_TEST_CAPTURE 指定私有回读文件；不作为宿主构建/安装入口。
源码快照、夹具、ISA、CSV、模型和原始回读留在忽略目录，local ledger 记录本机定位。
下一项在该新基线上检查尾部整 wave 的无效计算，保持同样的数值和控制要求。
