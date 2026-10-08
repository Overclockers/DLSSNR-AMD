# EXP-079：GEMM 映射的 power-of-two span 整数除法

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261008-130553-swin9070xt` 第 4/10 个假设；1 版实现，1 次追加确认。
起始主仓 `7aa5dd6b5d44a54b0b0d14695be214cfec11da3d`；宿主 `8aacc8b2b88b0a39a952d56d047b6139762dbb8a`。

## 假设与实现

在 EXP-061 的两列映射内，span=2*gy 若为非零 power of two，就以 lin>>findMSB(span) 计算原商；否则保留原 uint 除法。余数、奇数末列和 G!=2 路径均保持。gemmvact/gemmprojws 的目标 gy 为 16/8，两个模块作为同一个地址计算假设；不改 EXP-066 所试的 K/N 矩阵主体常量化、动态 M、权重、WMMA、精度或归约。对照继承 EXP-076。

1,432,560 组工作组坐标全部一致，其中 135,195 组进入 power-of-two 路径、1,297,365 组为除法回退；包含小网格穷举、奇数末列、65535 单轴上界和确定性随机位置。disabled 两个 SPIR-V 精确复现基线。首次编译缺少 dont_flatten 对应的 GL_EXT_control_flow_attributes，在任何 GPU 执行前补充条件扩展声明，一次构建修复。实际 driver 快分支含 scalar shift，通用除法仍保留；两模块 .text 各增加 128 B、静态行各增加 13，VGPR 157/96、SGPR 20/30，零 LDS/scratch 不变。FP32 及全部保存的 packed 输出精确一致。ADL 0.5 秒采样包含初始化/预热；首末 A/B 进程最后四个样本的 GFX 时钟约 2709–2717/2660–2672 MHz、hotspot 71–73/79°C，但不是正式帧对齐数据，不据此修正时间或认定唯一因果。

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
| ab-shift-fixed | 10.939160 | 10.949322 | -0.010162 | -0.020412 / -0.008414 / -0.001659 |
| ba | 11.013562 | 11.002019 | 0.011543 | 0.020279 / 0.011319 / 0.003032 |
| confirm-control | 11.055103 | 11.054126 | 0.000977 | 0.000056 / 0.001898 |
| control-shift-fixed | 11.022484 | 11.025416 | -0.002932 | 0.002070 / -0.007934 |

初测三组均慢，平均增加 0.010162 ms；唯一反序确认三组变快，平均节省 0.011543 ms。方向随顺序翻转，无法建立可重复完整 NR 收益，关闭为 unproven，不保留，也不追加候选测量。

保存 42 份宿主回读、8006 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / gemmvact SPIR-V：`d2f933764c49db726f35d45f7c8cff8cabbf75b9842d28d896e98499d910a9d2`。
- disabled / gemmprojws SPIR-V：`63449ee64f848b4d78056350362052a10e279f795dc3788bfee7bc0cb118f636`。
- shift-fixed / gemmvact SPIR-V：`d0014f5bb13632831509dc146768effc0bde47467aa5dbe3d6aa1bc5dda0fe3b`。
- shift-fixed / gemmprojws SPIR-V：`a1ad8b3dfbc4b6ebeae72ca0ca8730cf72de86fb626568d4b31712451abc8549`。
- prototype.patch：`e77570e9bf9c569d6d415083a84b629d8606b95bb3a60142e1f8cb220f4037af`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：使用完全相同的保留基线检查更充分的统一预热及同路径控制，作为不占额度的测量方法 discovery；随后继续 Swin 残差增益读取等新假设。
