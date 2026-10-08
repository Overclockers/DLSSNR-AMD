# EXP-078：输入特征的 subgroup 寄存器交换

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261008-130553-swin9070xt` 第 3/10 个假设；1 版实现，1 次追加确认。
起始主仓 `8c292bedbd8857d934b44393078eba322bc85fad`；宿主 `8aacc8b2b88b0a39a952d56d047b6139762dbb8a`。

## 假设与实现

两-wave 输入层的每个 wave 已拥有自己需要的两个 token tile，原实现仍将 1024 个 half 特征写入 LDS、barrier 后再读回矩阵片段。本候选在原 NR_F16 转换后逐对 packFloat2x16，将八个私有 word 按 token 对应的 raster lane 做 subgroupShuffle，再在交换之后选择 low/high feature half，直接构造原 B16 fragment。特征计算、half 舍入、输入投影与后续归约、权重和采样均保持，两-wave 形状不变。它取消特征 LDS 及其屏障，不重复 EXP-024/025 的 LDS 打包/布局实验；对照继承 EXP-076。

独立 GPU transfer 探针比较实际 cooperative B16 load 与寄存器交换：每个特征通道遍历全部 65,536 个 half 编码，共 1,048,576 个值，2,097,152 输出字节完全一致，含 NaN/Inf/正负零/subnormal。probe 源 SHA-256 3a49845c45993215d1e15b19966576ccbb3decfd38c6b2c01f0043eb94059161，输出 SHA-256 edee35c37f03dfc4b368c029cac43f39c9220632992c34ca9e351ecfc7c82af3。普通 pre 与宿主实际选中的 runtime_preencode 均已编译，两个禁用 SPIR-V 精确重现基线。native pre 的 .text 25856→25728 B、静态行 3509→3499；runtime_preencode 的 .text 26240 B 不变、静态行 3577→3567。两者 VGPR 124→122，LDS 6144→4096 B，零 scratch；宿主 FP32/packed 全图均精确一致。ADL 0.5 秒采样包含初始化/预热，非帧同步。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.840392 / 10.891116 / 10.921970 / 10.937182 ms，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-shuffle | 10.942044 | 10.943861 | -0.001816 | -0.009664 / -0.001449 / 0.005663 |
| ba | 11.007481 | 11.008894 | -0.001412 | 0.006078 / -0.003491 / -0.006824 |
| confirm-control | 11.048001 | 11.051468 | -0.003467 | -0.013623 / 0.006689 |
| control-shuffle | 11.020136 | 11.020773 | -0.000637 | 0.000072 / -0.001346 |

完整路径初测平均慢 0.001816 ms，唯一反序确认平均慢 0.001413 ms；确认各组节省 0.006078/-0.003491/-0.006824 ms。两批均值无收益、组间方向不一致，且确认控制已有约 0.013623 ms 的组差。关闭为 unproven，不保留；资源减少没有建立完整 NR 性能收益。

保存 42 份宿主回读、8006 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / fswinimagepreds32 SPIR-V：`6ec78cb87f11f452e7c8b36c70cd325f81033c541faac48259175fc48fa6813f`。
- disabled / runtime_preencode SPIR-V：`34a29bfd739eddb50f8cb546b6a42fdaa2d4ba728b8ff4054054ce1c345febdc`。
- shuffle / fswinimagepreds32 SPIR-V：`e975385d7a23d30b2804c57d28790d356404cbdc8a73d8883ce0e9a6102634ee`。
- shuffle / runtime_preencode SPIR-V：`13ab75960e8a9e02973ca7b7242e4eb1f70b56e202791ea058bb984e54540413`。
- prototype.patch：`1931a70e1b47bcba3a1c8aa730d3e7eb1d350c15973b542f9cff841252d21b2d`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：审计 EXP-061 后剩余的 lin/(2*gy)：对 power-of-two span 使用严格整数移位，非适用网格保留原除法，证明所有最终坐标和任务序不变。
