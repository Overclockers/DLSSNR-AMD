# EXP-070：Image post 原图纹理提前读取

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261008-115341-shapes9070xt` 第 5/10 个假设；1 版实现，1 次追加确认。
起始主仓 `d5c5ffd2b82787db629453d46c51572ebbce7083`；宿主 `9d1fa938563ce5cf7c06882d0134e6385f5e50eb`。

## 假设与实现

当前 image post 诊断约 0.925 ms，其合成读取的原图与 Swin 主体独立。候选在主体开始前按相同 token/pixel、texel-centre UV、textureLod、LOD 0 和输出边界预读 vec4，在合成处使用保存值。原 FP32 计算、RGB 投影累加序、滤波、格式、输出和 alpha 语义不改。区别于 EXP-042 的整数采样和 EXP-049 的权重面板。

禁用版 SPIR-V 与基线一致，目标 FP32 和全部宿主 packed 图逐字节一致。驱动文本中的 image_sample_lz 从第 3051 行移动至第 91 行，提前读取实际生效；VGPR 122→126、SGPR 66→50、LDS 4096 B、零 scratch，.text 24576→24832 B，静态指令 3312→3347。ADL 以 0.5 秒采样，包含启动与预热，非帧同步。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.862125 / 10.870288 / 10.903622 / 10.937338 ms，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-early | 11.007773 | 11.025712 | -0.017939 | -0.024198 / -0.013821 / -0.015798 |
| ba | 11.097982 | 11.097999 | -0.000017 | 0.013772 / -0.002306 / -0.011518 |
| confirm-control | 11.124772 | 11.122601 | 0.002171 | 0.002533 / 0.001810 |
| control-early | 11.082811 | 11.099484 | -0.016673 | -0.029980 / -0.003367 |

初测慢 0.017939 ms，但对应控制也慢 0.016673 ms，因此使用唯一反序确认。确认平均慢 0.000017 ms，各组为 +0.013772/-0.002306/-0.011518 ms；同次控制约 +0.002171 ms。没有稳定收益，关闭为 unproven。

保存 42 份宿主回读、8006 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / fswinimagepost32 SPIR-V：`5c45fc50685b8ee7c18ef1aae0ad12d8ac764dbceabeec569d07e3f812c5d76b`。
- early / fswinimagepost32 SPIR-V：`4e5acf478301b9a51f15ca2b5856676e5cd0a9e494e433530706ea8364d57adc`。
- prototype.patch：`f6b1e17a6e4350cf076f19e3a9128f336585f9925e586fe8f941747002c8f15c`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查 persistent Swin 残差缩放的相邻 FP32 权重能否用显式 float2 读取减少实际访存指令。
