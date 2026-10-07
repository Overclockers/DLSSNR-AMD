# EXP-061：GEMM 两个 M tile 映射的整数除法

日期：2026-10-08（Asia/Hong_Kong）。结论：**保留；完整离线路径初测节省 0.014475 ms，确认节省 0.021672 ms。**
运行 `20261008-003019-five9070xt` 第 1/5 项，1 版实现、1 次追加确认。
基线主仓 `06d960eece2ba8cf64dbaf5ed6b3037d187fbe3e`、宿主 `70484c408f717ba97da8636a2f0346a3e983fc40`，包括 EXP-056。
本项宿主提交 `9d1fa938563ce5cf7c06882d0134e6385f5e50eb`。

## 实现与不变量

gemmvact/gemmprojws 的 pc.remap=2 时，旧通用公式仍计算动态 G 和 gt 的除法/余数。
候选保留 lin/(2*gy)，将完整两列 tile 的余数/商改为 r&1 / r>>1，奇数 gx 最后的单列改为 bm/r。
G!=2 保留原公式；G=0 仍绕过映射。所有工作组最终的 (M,N) 坐标和任务顺序完全不变。
仅启用这两个模块的 NR_REMAP_PAIR=1，未更改网格、PC ABI、descriptor、packing、权重、精度、归约或舍入。
这是整数映射算法化简，区别于 EXP-056 的通道维度编译不变量和更改任务顺序的实验。

CPU 检查 gx=1..65、gy=1..32 的全部工作组，包含偶数/奇数/单列；所有新旧坐标一致。
代数上，gt 只有 1 或 2，两种分支分别等于原 r%gt 与 r/gt；实际支持尺寸的整数乘积不溢出。

## 环境与性能

RX 9070 XT，新查询驱动 26.8.1 (LLPC)，glslang 16.5.0、MSVC 14.51。
2258×1270→2368×1280，800 tokens、123 dispatches，100% 模型尺度、单次 NR，R11G11B10、depth 3。
计时为 D3D12 输入复制至输出复制的完整 NR GPU 区间；无历史/SR/FG 性能负载。
每批预热 100、测量 200；ABBA×3、BAAB×3，各有同路径控制×2，共 8000 个正式性能帧。
新运行起点四批 10.625679 / 10.650643 / 10.664368 / 10.666902 ms，不与随后候选直接相减。
ADL 每 0.5 秒只读采集时钟、温度、活动率和功耗，包含启动/预热，并非按测量帧同步；没有调频或改驱动。

| 批次 | 基线 ms | 候选/控制 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-pair | 10.714552 | 10.700077 | 0.014475 | 0.018633 / 0.012652 / 0.012140 |
| ba | 10.725828 | 10.704156 | 0.021672 | 0.043339 / 0.018505 / 0.003171 |
| confirm-control | 10.783623 | 10.789430 | -0.005807 | -0.011257 / -0.000357 |
| control-pair | 10.772535 | 10.776009 | -0.003474 | -0.004428 / -0.002520 |

六个 A/B 组均同向。初测最小组收益 0.012140 ms，高于该次控制最大绝对组差 0.004428 ms；
确认第三组仅 0.003171 ms，小于确认控制最大绝对组差 0.011257 ms，单独不能认定收益。
保留所有批次，以两个批次均值同向及初测三组超过控制作为整体证据；保守登记较小批次均值 0.014475 ms。
这不是置信区间或保证下限；该幅度只适用于此离线配置，未外推游戏 FPS。

代码总量增加：扩张 .text 13056→13312 B、1659→1720 条；收缩 4736→4992 B、680→742 条。
原通用分支仍存在，静态代码大小不代表实际 G=2 的执行成本。VGPR 157/96、SGPR 20/30，零 LDS/scratch。

## 精确验证与构建

- 禁用宏时两个 SPIR-V 字节复现基线；目标 native FP32 和 42 份宿主 packed 批次末帧精确一致。
- 基本宿主夹具 15 对回读、6 对目标/奇数/小尺寸梯度/随机/HDR、多遍切换通过，含双队列、重放、DRS、延迟回收。
- 重用身份固定的 EXP-056 夹具，重新执行基线与候选：13 种底层 Vulkan 格式的 126 对目标动态历史输出逐字节一致。
  非零亚像素 motion、逐帧平移图案、变化 alpha、连续/丢弃/reset/乱序/replay 的历史消费计数通过。
  其余 DXGI typeless/sRGB/BGRX 别名按未改的宿主映射复用同一底层格式，并非每个别名独立执行。
- 原生边界/回退 7 对全图一致：640、768、1024、1120 tokens，以及目标 --no-projws、--no-projw、--no-gemm-remap。
- 宿主、主仓改前/改后三套全量 shader 构建各 64 模块；仅两个 GEMM 改变，其余 62 和 markers 保持。
  两个变化模块的主仓/宿主前后字节各自相同，最终保留版本等于实测候选。C++/ABI 未变，复用核验后的 production DLL。

数值证据没有使用容差；没有安装、部署、游戏、Linux、发布包或推送操作。

## 身份与复现

- baseline/gemmvact：`c48c1619dde7995664f7b0e6e34c4bd51abf0d6b20cac70b65daf186b01efbdb`。
- baseline/gemmprojws：`efdae1191942b8913e3b6411d8342225aa72c2904a819ece57fd1e24d7c0e2d4`。
- pair/gemmvact：`d2f933764c49db726f35d45f7c8cff8cabbf75b9842d28d896e98499d910a9d2`。
- pair/gemmprojws：`63449ee64f848b4d78056350362052a10e279f795dc3788bfee7bc0cb118f636`。
- mapping-proof.json：`2369c70465e381bed7b9a133cd4d04491e2cfda2de4b78b2111e31fd4a710ca2`。
- build-closure.json：`df33c44a4edc26bd2e280f6f8b57a60fcc2b23d5ab38bbe001dd7bdb9a436055`。
- validation.json：`a7ebd3722274683d579117ea28be59b16f36b49562303988b50a40ec58321978`。
- target-validation-pair.json：`43fbc324d6ec5c2b775f6c12a769cd9ab0e19ad770b5dc13f8f651e96ed9e269`。
- format-validation-pair.json：`da4cfeb573904c6eb479fc496d92b1f7bfe207ff4de8e0b41868eb3fad4520aa`。
- edges-validation.json：`b554f9f17e1c3cbea6dd3437cac72835b1ab33ef2a98ab2bfd8c1b4588e3ca81`。
- retained.patch：`941b76bdf7a237a18955ea80347300ce55d3d84eeece29e87e0b4a3fbce49122`。

```text
python -X utf8 windows/build/build_network.py rdna4 --out <shaders> --glslang <pinned-glslang>
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer --dispatch-grid
```

原始 CSV、回读、源快照、ISA 和模型留在忽略目录；本机位置与完整身份见 local ledger。
下一项检查 GEMM 尾段残差增益加载的等待，不改变数学操作。
