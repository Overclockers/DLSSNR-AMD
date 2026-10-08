# EXP-069：Persistent 激活地址的提前缩放

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261008-115341-shapes9070xt` 第 4/10 个假设；1 版实现，0 次追加确认。
起始主仓 `0d8bb5b2970ccdd692e92704ed621181a37c191f`；宿主 `9d1fa938563ce5cf7c06882d0134e6385f5e50eb`。

## 假设与实现

EXP-068 完整 ISA 比较揭示每次 coherent 激活读取前的向量对齐掩码。本项保留原双 uint 加载及 fragment 构造，把除以四分配给已对齐的 uniform 基址、行步长和 lane 偏移；目标是地址 ALU，不再测试读取宽度。实际调用均为 256 字节 tile 基址、16 字节 stride；对原路径有效且不发生 uint32 地址回绕的范围，新旧地址代数相等。所有浮点、权重、布局、queue 和 barrier 不变。

524,544 组 CPU 地址对照覆盖 32 lanes、多种步长、低地址及接近 uint32 上限的有效地址，完全一致。六个禁用 SPIR-V 复现基线，目标 FP32 全图和全部 packed 输出一致。候选静态代码反增 18–118 条，.text 增加 128–512 B；DS256 VGPR 229→230，DS64 192→194，其余无 scratch。ADL 0.5 秒只读采样包含启动/预热，非帧同步。

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
| ab-scaled | 10.987306 | 11.015274 | -0.027968 | -0.038517 / -0.022292 / -0.023095 |
| control-scaled | 11.078252 | 11.077400 | 0.000852 | 0.000417 / 0.001286 |

完整 NR 三组均变慢，平均增加 0.027968 ms；对应控制最大绝对组差仅 0.001287 ms。地址代数化简未改善驱动调度，关闭为 regressed，未使用追加确认。

保存 22 份宿主回读、4006 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / fswinpds64 SPIR-V：`bd003438c359949f70035c9e3c6b94c9dd4fcdc42fe5bd297800965378049f0a`。
- disabled / fswinpds128 SPIR-V：`76c494f152893b90cbcff8bf4641b2ea84531cd10e1cd15dce7dfb58e95b73ef`。
- disabled / fswinpds256 SPIR-V：`ec8a83af07742e3049a8318a2fd651ea50daee446ab43d3c2aa2f07c24962122`。
- disabled / fswinpup64 SPIR-V：`763a81fb44653494dbc59bb9501786f2b0f7582c40f527f35e167136ad47a6ba`。
- disabled / fswinpup128 SPIR-V：`7b7ea858828c7de2ba34c509fd8c1f9117c273b5978dfe1bbfb1025df504f8db`。
- disabled / fswinpup256 SPIR-V：`dbe2b443324d954ff60d9a24d79453ecc6c8fe9e29af9835f5772ba177e83e21`。
- scaled / fswinpds64 SPIR-V：`75b7a73fe7f4fb4a08f9634e0cb554557168bc0ddf360289f003009467bf9a35`。
- scaled / fswinpds128 SPIR-V：`3129a66ca472846f6a8f31544d7e17e81e6f3fb90ff668b2ff52f1b4133faa09`。
- scaled / fswinpds256 SPIR-V：`2191552f89624610fd4a0bb2915fad9b889f6228fc26ba33404bae233194eadd`。
- scaled / fswinpup64 SPIR-V：`b7bbb0e013a2c54649aa162fcf167af8d922fdc72d6ace33de17ed43fa35efc8`。
- scaled / fswinpup128 SPIR-V：`355059c958e7e7466b4cf290f1492b674c92df920f9ab2aa461e2dbc89ec0b5b`。
- scaled / fswinpup256 SPIR-V：`75ed512d4bcc52de58fa29249f13d83705e857c14e93840b3421fc6196552bbd`。
- prototype.patch：`db41317d84c115428a20f69aa1177cd439d457c548488fcda05c4c032f7c22ef`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：转向 image post 的独立原图采样，测试提前发出纹理读取能否隐藏末段等待。
