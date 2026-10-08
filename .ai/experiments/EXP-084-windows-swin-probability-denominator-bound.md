# EXP-084：Swin 概率分母的严格范围恒等式

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261008-130553-swin9070xt` 第 8/10 个假设；1 版实现，1 次追加确认。
起始主仓 `db6a1027544578740d3faf97d424aea5d46d2c0f`；宿主 `8aacc8b2b88b0a39a952d56d047b6139762dbb8a`。

## 假设与实现

现有 baked exponential 将 half 中间值 clamp 后按位构造正 half 概率。候选仅在 NR_BAKED_EXP_BIAS 且未启用 integer clamp 或 ablation 时移除 max(sum, NR_SUM_FLOOR)，其它编译路线保留原式。指数、全部 64 项、half 加法树和归约顺序、倒数、half 舍入以及 FP8 转换均不变。该机制是对可达范围的恒等式消除，不改变近似指数或精度边界。

GPU 穷举 65,536 个 half 编码并比较 65,792 个分母/倒数，字节完全一致、范围失败为零。概率范围 [2^-14,9.75]；原 half 树的单调舍入使每个部分和落在全最小与全最大树之间，最终端点 [1/256,624] 精确可表示，严格高于 floor 2^-14，无下溢和溢出。探针源 SHA-256 66321bd1efdfbbd21980487ca77df06cd412b90ec14217265834b9b003b30c92，输出 761713257aca4e06e26b9871e06a66cfd216cd11b4dd45c6b3839fdd294e251b。11 个普通模块及实际 runtime_preencode 的 disabled SPIR-V 还原基线，FP32 和 packed 全图一致。C256 DS 静态指令 31956→31936、.text 224384→224256 B、VGPR229 不变；post 指令 3333→3343、VGPR122→123；runtime_preencode 指令 3577→3573、VGPR124/LDS6144 不变；零 scratch。ADL 0.5 秒采样包含启动/预热，未与正式帧对齐。未扩大 temporal 变体编译。

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
| ab-bound | 11.039952 | 11.042883 | -0.002931 | -0.001990 / -0.003294 / -0.003508 |
| ba | 11.052859 | 11.051053 | 0.001806 | 0.000715 / -0.000760 / 0.005465 |
| confirm-control | 11.070777 | 11.070373 | 0.000404 | -0.000664 / 0.001472 |
| control-bound | 11.064019 | 11.059713 | 0.004306 | 0.002086 / 0.006525 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

初筛平均慢 0.002931 ms，三组均慢，处于同路径控制最大绝对组差 0.006525 ms 内。唯一反序确认平均快 0.001806 ms，各组节省 0.000715/-0.000760/0.005465 ms，方向混合，确认控制组差 -0.000664/0.001472 ms。未建立可重复的完整路径收益，关闭为 unproven。范围证明和静态指令减少不构成保留依据。

保存 44 份宿主回读、8012 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- bound / fswin32 SPIR-V：`7f5436c30cb7086db8e3b4ddee82cfaa0c3bd9676a093de1112c288279d5e9c1`。
- bound / fswindsp32 SPIR-V：`dfe27cd4a9b130417bf62fd581edb30d6884261b91bcf30bac1a06d853531e1d`。
- bound / fswinfusedup32 SPIR-V：`b8adb26834b196e3bb9a9f15265a43f63e94b0b7ede85d47761a87334edcee91`。
- bound / fswinimagepreds32 SPIR-V：`33705f8d4a87aad08293336af8813d2accdb24d3e6fabf99e6528ca7536a19b0`。
- bound / fswinimagepost32 SPIR-V：`c59147bfab7e7795e6f1831b01120e4b5c1f8b906c15a26a114a37372a1c8f0a`。
- bound / fswinpds64 SPIR-V：`fc1087466fa0b497890c761b1ecd375ae53a059255420bc5fc9721ce99e61d27`。
- bound / fswinpds128 SPIR-V：`c3a0c9b6e65e8af249b76ae9a245b541dadce511ac8e538db808a6bb4d7e9ce9`。
- bound / fswinpds256 SPIR-V：`e215cc0fd33be0a6b90573fd31d6b33291e3628782a773a17479f7e1302bc6ad`。
- bound / fswinpup64 SPIR-V：`edb8abeb057f3376026f878099167a12b73cbe4557ada87ac6f164e3c8acc8f4`。
- bound / fswinpup128 SPIR-V：`ddf012f1671a986e33f0b4345ce84bb59ef517ed9abc49605ee302f0b83efa93`。
- bound / fswinpup256 SPIR-V：`cf39c0b4e0ae19aee4fbd1c79d3c47a7305e60847bcd66a69278b42945508cfe`。
- bound / runtime_preencode SPIR-V：`83f3e5d8232f4c141654f742cf7828f229c82c978fb7c1adea6d0c77ce148d58`。
- disabled / fswin32 SPIR-V：`572651948aaf9c0daf2a2ea2f0a3669065b4a6824d24c7d0827cbe290f8a426a`。
- disabled / fswindsp32 SPIR-V：`1287c97821396cc94440153887242fac8b9b236eae8c351962356cca56d20baf`。
- disabled / fswinfusedup32 SPIR-V：`a470ad407749a336b3ac7a721a23fc285769d51976424a9490784e535cfcd243`。
- disabled / fswinimagepreds32 SPIR-V：`6ec78cb87f11f452e7c8b36c70cd325f81033c541faac48259175fc48fa6813f`。
- disabled / fswinimagepost32 SPIR-V：`49ca36fb4685261eda156c5a3358ff63426c28c270a80a0a17cb809826cf65d2`。
- disabled / fswinpds64 SPIR-V：`bd003438c359949f70035c9e3c6b94c9dd4fcdc42fe5bd297800965378049f0a`。
- disabled / fswinpds128 SPIR-V：`76c494f152893b90cbcff8bf4641b2ea84531cd10e1cd15dce7dfb58e95b73ef`。
- disabled / fswinpds256 SPIR-V：`ec8a83af07742e3049a8318a2fd651ea50daee446ab43d3c2aa2f07c24962122`。
- disabled / fswinpup64 SPIR-V：`763a81fb44653494dbc59bb9501786f2b0f7582c40f527f35e167136ad47a6ba`。
- disabled / fswinpup128 SPIR-V：`7b7ea858828c7de2ba34c509fd8c1f9117c273b5978dfe1bbfb1025df504f8db`。
- disabled / fswinpup256 SPIR-V：`dbe2b443324d954ff60d9a24d79453ecc6c8fe9e29af9835f5772ba177e83e21`。
- disabled / runtime_preencode SPIR-V：`34a29bfd739eddb50f8cb546b6a42fdaa2d4ba728b8ff4054054ce1c345febdc`。
- prototype.patch：`8543c262d10f768b5a0de329ac60d1161de1aaa3de1507f6dbdbf2a262d8ebe3`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查六个 persistent Swin 内核的零加原子轮询：Windows 当前 ISA 使用 device-scope atomic OR；测试显式 device/acquire atomicLoad，保留写入、发布屏障、队列和 epoch，并检查循环内的真实加载及可见性。
