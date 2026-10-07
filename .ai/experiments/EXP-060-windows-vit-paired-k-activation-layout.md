# EXP-060：ViT 相邻 K16 激活 tile 的成对布局

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261007-214238-codegen9070xt` 第 10/10 个假设；2 版实现，0 次追加确认。
起始主仓 `a78283b9651029077566c0a756a700401c5ff464`；宿主 `70484c408f717ba97da8636a2f0346a3e983fc40`。

## 假设与实现

原布局相邻 K16 tile 相隔 256 字节。原型在每一对 512 字节内作可逆排列：旧地址为 tile*256+row*16+col，新地址为 row*32+(col/8)*16+tile*8+col%8；tile=0/1。512 个目标索引经独立枚举无重复且无缺口。producer 写入已量化 FP8 字节，consumer 用对齐 uvec4 读取并按原 K16 顺序选 xy/zw；每次 MMA、归约、残差、量化、权重、dispatch 和原 barrier 保持。没有启用错误数值的 NR_KPAIR_ABL，也没有引入低精度近似。先在独立真实 expand/contract 算子对内测试，未把只有按 M 判定的耦合布局推广到生产图。

初版禁用路径逐字节复现当前基线。隔离缓冲的真实原图第一个 ViT 算子对，分别验证原版/同路径控制/候选：6 份最终 FP8 和 6 份恢复逻辑布局的中间 FP8 完全一致，输入/guard 保持，退出正常。每版整网预热 100 次、pair 预热 128 次，12 blocks×4 positions×32 samples（八组 A/B、四组控制），共 3072 个局部计时样本。两个 frozen shader 源文件与各自构建时哈希一致，夹具二进制在测量前后哈希不变。

| 实现 | 原算子对 ms | 候选 ms | 增加 ms | 控制最大绝对组差 ms |
| --- | ---: | ---: | ---: | ---: |
| paired | 0.114920 | 0.120291 | 0.005371 | 0.002333 |
| paired-unrolled | 0.114665 | 0.119525 | 0.004860 | 0.002471 |

第一版 producer .text 13056→13824 B、指令 1659→1810，v_add_nc_u32 从 6→137。依据这条新证据，第二版把 row 基址前提并展开八个存储，代码降为 13184 B/1663 条；仍用同一布局和算术。consumer 的 16 条 buffer_load_b64 变为额外 8 条 buffer_load_b128，权重的原有 8 条 b128 保留，证明宽读取确实实现，但未换来净收益。候选 producer VGPR 163，consumer 94，两者 scratch/LDS 为 0。

为核对实际读取指令，另做两版 target native driver ISA 捕获；两份完整 FP32 图与独立保存基线逐字节一致。短期诊断时间不用于验收。ADL 0.5 秒只读传感器包含初始化/预热，不与测量帧同步。

夹具源码/二进制：`7bb90d838fdf4b7b84a89b8f13736db2803194734c17fa182a94a98f81e80353` / `46a8e6047d10783e157ffc194f170930d202f5922eef2560d2de995be29c8bc1`；512-byte 布局证明：`762fa0c5faa15bd5a32177589a4a2123d553b946d5e47b526243a4ca81fcf7b2`。第二版着色器源码：`8bd832b76b2edf943f66f942d62f25dca6a7673d60cdd5f0623beea67f5d3e82`。其它层、合成随机/极值、格式/历史、fallback 和构建分发没有扩展测试，因为局部性能已失败；保留全部已采集数据。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.640660 / 10.627673 / 10.667158 / 10.678415 ms，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

没有进入完整宿主性能验收；诊断时间不作为提速结果。

第一版在八个正反序 A/B 组全部变慢，局部平均增加 0.005371 ms；明确写入常量偏移的第二版仍八组全部变慢，平均增加 0.004860 ms。最小退步也大于各自控制最大绝对组差，判为 regressed，不保留。不把局部差值乘八当作完整 NR 变化。未启动完整宿主验收，也未消耗不需要的追加确认额度。

保存 2 份整网 standalone 数值结果，其中 2 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、其它层/输入的中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / gemmvact SPIR-V：`c48c1619dde7995664f7b0e6e34c4bd51abf0d6b20cac70b65daf186b01efbdb`。
- disabled / gemmprojws SPIR-V：`efdae1191942b8913e3b6411d8342225aa72c2904a819ece57fd1e24d7c0e2d4`。
- paired / gemmvact SPIR-V：`f5e07eeb1e29dadc7d806621dd5604d9483ec910097fa4ba73ebad311167a3a3`。
- paired / gemmprojws SPIR-V：`a8a869271e8df14449f1e98e2185c2263ba42a02917ac9db7a2f0a7ec1d4da9a`。
- paired-unrolled / gemmvact SPIR-V：`68e93863fb87c6e7bfdb08a841d9f84d82ccc189d94788b82a203db596be9de0`。
- paired-unrolled / gemmprojws SPIR-V：`a8a869271e8df14449f1e98e2185c2263ba42a02917ac9db7a2f0a7ec1d4da9a`。
- fixture.patch：`5d24860c370374af23775b0aac6aa67c3383b79be655a549bc0e18d75f409d3a`。
- prototype.patch：`98831c969f4f56a6c0e8195d0a67cbd143acaaa226b68146bc6fb2de892ec5f7`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
pair.exe 2258 1270 <plan> <output-prefix> --reuse --model-pack <private-model> --spv-dir <original-shaders> --candidate-dir <paired-shaders> --pipeline-cache <cache> --in-image <photo> --pair-index 0 --pair-data 0 --blocks 12 --samples 32
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：本次用户授权的 10 项已达上限；本提交后核对提交与基线身份，结束本轮，不自动开启新实验。
