# EXP-098：Swin 指数限幅的 NaN 保持整数变换

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261008-193306-lossless9070xt` 第 2/10 个假设；2 版实现，0 次追加确认。
起始主仓 `ed70f08f538dd40fd7c75637aaa1951fddb7f351`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

C32 指数构造在 half FMA 后逐半限幅。第一版用打包 i16 min/max，并显式将正 NaN 映回原 numeric clamp 的下界；第二版将 half 位模式模 16 位加 0x03ff，把正 NaN 转入负区间，以平移后的上下界限幅，最后把平移并入原有移位加法常量。所有负值、NaN、Inf、零和有限值均保留原限幅/指数结果。不同于 EXP-004 在有界区间插入位操作及 FP32 限幅，本次显式处理完整 half 域；不同于 EXP-063/075 的量化和对称激活限幅。未改 FMA、偏置或归约。一次 bvec 运算语法修复发生在有效构建前，不另计变体。

两版各穷举 65,536 个 half 编码，每个编码在配对两位置都覆盖，共 131,072 个 half 值，mismatch=0。第一版检查 clamp 位值，第二版检查完整指数位变换；探针 SHA256 `977bb40e4e0f2d1eb8cf3215eab70173b2e06e3e3229133c886b8488bee536b3` / `0393f9330e1af87e912fdb4f68b8add25834899210336d1dedf87b2b6e848fbe`。两版目标 FP32 以及所有 packed 回读精确一致。第一版 fswin32 增加 128 条比较和 128 条选择，静态指令4867→5377；平移版去掉这些判断，但仍4961条，.text38144→39296 B。两版均无scratch；没有从静态代码尺寸推算帧收益。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.999239 / 11.010161 / 11.007372 / 11.000571 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-biased | 11.129709 | 11.156936 | -0.027227 | -0.022611 / -0.028389 / -0.030681 |
| ab-packed-fixed | 11.098066 | 11.199198 | -0.101133 | -0.096028 / -0.107433 / -0.099936 |
| control-biased | 11.135991 | 11.134825 | 0.001167 | 0.000929 / 0.001404 |
| control-packed-fixed | 11.108904 | 11.122548 | -0.013644 | -0.027497 / 0.000209 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

第一版完整路径11.098066→11.199198 ms，慢0.101133 ms；平移版11.129709→11.156936 ms，慢0.027227 ms，三组分别慢0.022611/0.028389/0.030681 ms，均超过对应同路径最大组差0.001404 ms。两版都退步，regressed；不保留，不使用追加确认额度。

ADL 合计 606 个有效样本，核心时钟 115–2823 MHz，edge 40–59°C，hotspot 41–83°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 46 份宿主回读、8018 个測量帧；全部完整输出与独立基线逐字节比较。
保存 2 份整网 standalone 数值结果，其中 2 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- biased / fswin32 SPIR-V：`bd82989d02cf3d015b042ef639b36488655b92bb37ed00373745d1c2b9e80d14`。
- biased / fswindsp32 SPIR-V：`b0e4e12065168202db1c0c1ccd820f7ff4754b48f377fd381c595d3e70e2b9fc`。
- biased / fswinfusedup32 SPIR-V：`08ff4e1d787bd374c8711444eccf2eabcb3b7afdd78fa7735adb525a08659a52`。
- disabled / fswin32 SPIR-V：`572651948aaf9c0daf2a2ea2f0a3669065b4a6824d24c7d0827cbe290f8a426a`。
- disabled / fswindsp32 SPIR-V：`1287c97821396cc94440153887242fac8b9b236eae8c351962356cca56d20baf`。
- disabled / fswinfusedup32 SPIR-V：`a470ad407749a336b3ac7a721a23fc285769d51976424a9490784e535cfcd243`。
- packed-fixed / fswin32 SPIR-V：`e02e428f90985336a2c4bb3a193a9e17a57db024bcf81ebee57c1fd169674eda`。
- packed-fixed / fswindsp32 SPIR-V：`39067d4db91812b72ba157e59a59af9a23758edce56d03a6726f4ea1147d712d`。
- packed-fixed / fswinfusedup32 SPIR-V：`b3a40b1dbc267160defd24b5920ebc1310985b512e56acfbecfb5eb62030d015`。
- biased.patch：`07a64e5a8e5f36420c9876980d0b6fed094ea53f8de76e067c4f96156f60b395`。
- prototype.patch：`08b2156f44602271723ce081604fedae4c86f22481556cc0b594759c97189240`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：继续剩余额度的发现与筛选；复核 C32 中间量调度旧记录，优先寻找未试过的访存或录制成本。
