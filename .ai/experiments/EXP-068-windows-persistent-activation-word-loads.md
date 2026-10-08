# EXP-068：Persistent 激活片段的 8 字节读取

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（mechanism_not_realized）。
运行 `20261008-115341-shapes9070xt` 第 3/10 个假设；1 版实现，0 次追加确认。
起始主仓 `6e509c544ec03d53ea957f07793b12a22a7e86f7`；宿主 `9d1fa938563ce5cf7c06882d0134e6385f5e50eb`。

## 假设与实现

六个 persistent DS/UPS 的 NR_LOAD_B_ACT 改为 coherent uvec2 视图，并使用现有 NR_FRAG_Q 按字节填充 FP8 fragment。原地址由 256 字节 tile、16 字节行与 lane 的 0/8 字节偏移组成，始终 8 字节对齐。binding、coherent、barrier、队列发布与数值顺序均保持；与 EXP-044 非 persistent attention 的读取不同，本项考察 Swin 的 coherent 读取。

六个禁用 SPIR-V 完全复现基线，候选目标 FP32 全图逐字节一致。完整 driver 指令串逐位置比较通过：DS64/128、UPS64/128 各 8 个立即数变化，DS256/UPS256 各 40 个，全部仅为 v_and_b32_e32 的 -4→-8。其余操作数均相同，六个 .text 大小不变且零 scratch；没有使用这次三帧诊断时间推导收益。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.862125 / 10.870288 / 10.903622 / 10.937338 ms，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

没有进入完整宿主性能验收；诊断时间不作为提速结果。

所需机制未实现：原路径已由驱动合并成相同宽度的 load。候选只把对齐掩码 -4 改为 -8，实际地址一致，指令顺序、数量、加载宽度及依赖均不变。提前关闭为 mechanism_not_realized，不进行完整宿主性能批次，不作零收益的测量结论。

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
- word / fswinpds64 SPIR-V：`d488996a0d1b7de5e2676a045350be1f53c8253c3266c15df1cf1a3caa49cc8f`。
- word / fswinpds128 SPIR-V：`b5c29f5affb402f5fb2d36fea3d52bd4a67c45d4a87877a7c276b3a41743043e`。
- word / fswinpds256 SPIR-V：`5e577b523808ad299854d8d791fec6b504e837dbcff21637969453f02412996e`。
- word / fswinpup64 SPIR-V：`656b862021ac92c7405576f9731fa8aa92ae927d728c75e75f5451aa4ef21bf5`。
- word / fswinpup128 SPIR-V：`416c4190eeebf3c5762b914a4d39e241a09ce7a5c80e1f6eadf13a8a98b9e492`。
- word / fswinpup256 SPIR-V：`9b365fbd7b01a841642896b89123a7467b2d0244cab43fd4f30ba5e40bf6dc63`。
- prototype.patch：`a49aec4f931c363a6fda5e31526eaa9d03940cb6d3a300ee15d346cb7316778b`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：沿这份新证据检查如何在保持地址完全一致时将 uniform 基址和 lane 偏移分别缩放，消除重复的向量对齐掩码。
