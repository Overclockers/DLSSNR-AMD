# EXP-081：Swin 残差增益的 wave-uniform 读取

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261008-130553-swin9070xt` 第 5/10 个假设；1 版实现，0 次追加确认。
起始主仓 `7c760e5cd6aa5cdbcde9ecb2f3420b3e46158e29`；宿主 `8aacc8b2b88b0a39a952d56d047b6139762dbb8a`。

## 假设与实现

残差增益地址由 subgroup 内共同的通道基址与 low/high 半个 wave 的偏移构成，16 个 lane 读取同一对 FP32 权重。候选显式广播 off-rbase，再统一加载 lower/upper 两对权重，按 lane half 选择原值。四个源读取点的原 FP32 乘法、WMMA 初始化与累加顺序保持；集体读取集合不增、不改 packing/binding/模型。该机制是读取请求组织，不是 EXP-062 GEMM 预取或 EXP-045 控制记录向量化；对照继承 EXP-076。

525,312 个选中标量地址及集体读取集合检查完全一致；六个 disabled SPIR-V 还原基线，目标 FP32 和所有保存的 packed 输出精确一致。机制确实改变了指令：C256 DS 的 buffer_load_b128 896→856，但 s_buffer_load_b32 31→351，v_cndmask_b32_e32 57→154，v_readfirstlane 48→68；.text 224384→230272 B，静态行 31956→33226，VGPR 229→230。六模块机器码均增大；C64 UPS VGPR 虽 194→185，仍不能据此判断性能，所有模块零 scratch。采用 EXP-080 统一预热协议，ADL 0.5 秒采样包含启动/预热且未与正式帧同步。

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
| ab-uniform | 11.039958 | 11.092745 | -0.052788 | -0.045640 / -0.059422 / -0.053301 |
| control-uniform | 11.061661 | 11.061849 | -0.000188 | 0.003234 / -0.003611 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

完整 NR 三组均明确变慢，平均增加 0.052788 ms，分别增加 0.045640/0.059422/0.053301 ms；同路径控制最大绝对组差 0.003611 ms。关闭为 regressed，不保留、不追加确认。统一读取引入的大量标量加载和选择指令没有形成净收益。

保存 23 份宿主回读、4009 个測量帧；全部完整输出与独立基线逐字节比较。
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
- uniform / fswinpds64 SPIR-V：`076a047b5679893a4f28b0de8a0a6e203e9d930f6935b557115273f0bb132a56`。
- uniform / fswinpds128 SPIR-V：`752a0225915062be59753b11b55742aa3cf8773b9b27bc403129ccd446f7c4bc`。
- uniform / fswinpds256 SPIR-V：`85ce49a706e742cadfcaa3b21bb87eca68ff2e09a2d045fa76a54ef24cfd93ec`。
- uniform / fswinpup64 SPIR-V：`42e5d8850f8289ef44f635fcdeae2317beaf42a031c5d9e7c0458476c6ee7253`。
- uniform / fswinpup128 SPIR-V：`4d79c1a6c4b3d828fae5d3085ca2e9595a1e24072059e4666a284eddbc3c112f`。
- uniform / fswinpup256 SPIR-V：`96b340b3628b1dd83787b37e624dd4cb62385b2d61d6a3e5043d7d2cce4b2d5a`。
- prototype.patch：`a2a5175bf106f610a40a56010221ea0cb7b830723a9b6ee2b42cb307e60a36e3`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：测试输入投影权重在两 wave 之间的协作缓存，利用已有 feature barrier 发布，不增加 EXP-074 中那类额外同步。
