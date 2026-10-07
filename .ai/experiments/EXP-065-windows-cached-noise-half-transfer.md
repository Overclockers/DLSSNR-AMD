# EXP-065：缓存噪声的 half 直接传递与 NaN 语义

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261008-003019-five9070xt` 第 5/5 个假设；3 版实现，1 次追加确认。
起始主仓 `7f01208a6e0dc811c4f3ffdce10b05873f4c3048`；宿主 `9d1fa938563ce5cf7c06882d0134e6385f5e50eb`。

## 假设与实现

已存驱动 ISA 显示，图像输入缓存噪声的前两个 half 仍先转 float，合流后再转 half 写 LDS。尝试用 half 三分量直接承接缓存数据，生成噪声回退保留相同 float 乘法并在原有精度边界转 half；其余 13 个特征、LDS 排布、seed、barrier 不变。与 EXP-024/025 的 LDS 布局研究不同，本项只改变表达已舍入数值的方式。

第一版直接 unpackFloat2x16 在全部 65,536 个 half 编码、三个不同位置的 196,608 个探针值中出现 3,066 个 signaling NaN 差异：原 half→float→half 会设置 quiet bit，原始位传递不会。第二版尝试 precise half 加负零，GPU 结果仍有相同 3,066 个差异，两版均不进入整网/计时。第三版按 abs bits > 0x7c00 只为 NaN 设置 0x0200，其余 sign/payload/数值位不变；全部 196,608 个值，包括次正规数、正负零、Inf 和各种 NaN，逐位一致。该探针保留动态缓存/非缓存分支来检验合流，不能替代真实历史输入覆盖。

第三版重编普通 fswinimagepreds32 以及宿主实际使用的 runtime/runtime_preencode.spv；关闭宏两模块都与已保存基线逐字节一致。standalone FP32 照片、完整宿主 packed 照片回读精确一致。普通 pre 的驱动 .text 25,856→25,984 字节，v_cvt_f32_f16 与 v_cvt_f16_f32 各少 2 条，但各增加 3 条整数比较和选择，以及 mask 指令；VGPR 124、LDS 6,144 B、scratch 0。该 ISA 统计仅覆盖普通 pre；融合模块单独重建并参与完整宿主计时，未另采驱动 ISA。
只读 ADL 每 0.5 秒采集时钟、温度、活动率，包含初始化/预热，未与正式帧严格对齐。

- disabled / runtime_preencode SPIR-V：`34a29bfd739eddb50f8cb546b6a42fdaa2d4ba728b8ff4054054ce1c345febdc`。
- half-bits / runtime_preencode SPIR-V：`102cf0b004f27977be186615f954bfc5ae970c0127908a6c0793b0c41547b2bc`。
- noise-probe.comp SHA-256：`5c12b14d29b815e909d1c68b927157623365bbd082e047a146dec7b6349c0add`。
- noise-probe-quiet.comp SHA-256：`18dd47cc1912381503901299b173b59c1ee839e82c50ce7cef0370ceff2c3ee4`。
- noise-probe-bits.comp SHA-256：`b940cc246cbb0a7f940942302da7f3bc0f84e7156bc770a66aeae9f9ef2d5908`。
- half-transfer-proof.json SHA-256：`e7790cecd1fcea135d8f48377ae9ae7e97da8808f282fd8324c42f8173d4472c`。
- half-quiet-proof.json SHA-256：`04bda79f7ed719f7162b3693e0f37514b9d925fda984aaf96099b693f21d3c62`。
- half-bits-proof.json SHA-256：`398fade636a2f878a52b6333dabc97308133337de430ff0c71bd8aada0b5d981`。

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
| ab-half-bits | 10.695027 | 10.700307 | -0.005280 | -0.010576 / -0.003705 / -0.001558 |
| ba | 10.742455 | 10.738822 | 0.003633 | 0.009689 / 0.008434 / -0.007224 |
| confirm-control | 10.788915 | 10.784174 | 0.004741 | -0.000429 / 0.009910 |
| control-half-bits | 10.764198 | 10.764003 | 0.000195 | -0.002853 / 0.003243 |

初测节省 -0.005280 ms（负值表示变慢），三组全部更慢；唯一反序确认节省 0.003633 ms，组差 0.009689/0.008434/-0.007224 ms。初测/确认控制最大绝对组差 0.003243/0.009910 ms。没有形成稳定且超过控制波动的完整路径收益，判为 unproven，不保留。

保存 42 份宿主回读、8006 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / fswinimagepreds32 SPIR-V：`6ec78cb87f11f452e7c8b36c70cd325f81033c541faac48259175fc48fa6813f`。
- half-bits / fswinimagepreds32 SPIR-V：`04ca9e32cd53557bb8c5c07eab1a45371e5307bdbcef6ad6708c40f6db6cbadb`。
- prototype-v1.patch：`22aea89b411223f89999c3adde0c983d4085f06521ad43dd85fbe025c4eca137`。
- prototype-v2.patch：`2157d08b71a847cb21421c0e9a09ab92011b8f66dfde06cab4ce25a77240c8d5`。
- prototype-v3.patch：`8be23aaa40eeb0769bcbf9466ca1abc486a9c923bec22ceb9a4a4d0b5c60c23e`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：本轮 5/5 假设在此提交后全部关闭，停止原因 budget_exhausted；核验两仓提交、保留产物与源码、保存最终摘要后，执行用户明确授权的 shutdown -s -t 0。
