# EXP-062：GEMM 尾段残差增益提前加载

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261008-003019-five9070xt` 第 2/5 个假设；1 版实现，1 次追加确认。
起始主仓 `f5627689b2ce1879455a05e7f72c4f6232532ef6`；宿主 `9d1fa938563ce5cf7c06882d0134e6385f5e50eb`。

## 假设与实现

在 ViT 收缩的 K 归约之前读取每 lane 两个已有 FP16 residual gain，片段 epilogue 使用相同权重位模式。没有预取残差、重新排列乘法/归约、改变精度或权重布局。与 EXP-055 的 K-chunk 操作数预取、EXP-029 的 Swin FP32 bias 提前读取不同，目标为尾段重复的 gain 加载及等待。只修改私有 gemmprojws，基线包含 EXP-061。

disabled SPIR-V 逐字节复现基线。native FP32 和全部 packed 回读精确一致。driver .text 4992 B、742 条指令、96 VGPR/30 SGPR、零 LDS/scratch 均保持；buffer_load_u16 从 4 减到 2，wait_loadcnt 从 51 减到 48，但增加动态选择与转换指令。ADL 0.5 秒只读采集包含启动/预热，不与测量帧同步。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.625679 / 10.650643 / 10.664368 / 10.666902 ms，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-gain | 10.689997 | 10.693987 | -0.003990 | -0.013081 / 0.001521 / -0.000410 |
| ba | 10.725086 | 10.725332 | -0.000246 | 0.017691 / 0.002980 / -0.021410 |
| confirm-control | 10.771851 | 10.775856 | -0.004005 | 0.004631 / -0.012641 |
| control-gain | 10.757643 | 10.761677 | -0.004034 | -0.004265 / -0.003802 |

初测 10.689997→10.693987 ms，平均慢 0.003990 ms；唯一反序确认 10.725086→10.725332 ms，平均慢 0.000246 ms，组差 0.017691/0.002980/-0.021410 ms。确认控制最大绝对组差 0.012641 ms。收益方向不稳定且均值接近控制波动，判为 unproven，不保留。

保存 42 份宿主回读、8006 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / gemmprojws SPIR-V：`63449ee64f848b4d78056350362052a10e279f795dc3788bfee7bc0cb118f636`。
- gain / gemmprojws SPIR-V：`18782dde37225a524435088cb51b12f4c5c6d60271dd29860f6c609941e86a80`。
- prototype.patch：`f99f703c82127ffca6d574dafcf3000d421ad1bc9a0b976f7ecc80d175b0ccf3`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：为已舍入 FP16 的量化范围检查建立整数位域实现，保留原来的非有限值传播；先对全部 half 编码精确验证，再考虑完整网络。
