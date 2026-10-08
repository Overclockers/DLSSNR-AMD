# EXP-085：Persistent 队列的显式原子加载

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261008-130553-swin9070xt` 第 9/10 个假设；2 版实现，1 次追加确认。
起始主仓 `70ce123af07ddf83eafbefb2e5489948fcf97ac1`；宿主 `8aacc8b2b88b0a39a952d56d047b6139762dbb8a`。

## 假设与实现

当前 Windows LLPC 将 nr_sync 的 atomicAdd(x,0) 编译为 returning buffer_atomic_or_b32，未实现源码 ACO 注释描述的普通加载。候选只替换 23 个零加读取点；coherent buffer、全部写入/非零增加/compare-swap、发布屏障、epoch、超时和回退逻辑保持。第一版使用 device/acquire atomicLoad；ISA 显示每个读点额外 global_inv，因此未进入完整宿主性能筛选。第二版以 device/relaxed atomicLoad 匹配原有排序要求。原版 SPIR-V 轮询为 QueueFamily scope 5、semantics 0；最终候选 Device scope 1、semantics 0，未削弱原有排序或作用域。它不同于 EXP-013 的工作组内 LDS 广播；EXP-026 的队列时间比例仅用于限制预期，不预测帧时收益。

六个 disabled SPIR-V 精确复现基线，两个实现的 FP32 全图均一致。最终六个模块的 ISA 均以 device-scope buffer_load_b32 替代 atomic OR，保留原有两个 global_inv；C256 DS 的循环加载在 _L464，两个回跳经 _L434 再执行该加载，未提升到循环外；C128 DS 的轮询/straggler 读取亦在 _L674/_L744 所在回跳路径内。其余四个同类轮询的设备作用域由保存 ISA 复核。C256 DS .text 224384 B、VGPR229/LDS32768 不变，静态指令 31956→31955；C128 DS .text 65408→65152 B、指令 9077→9041；六个模块均零 scratch。初始源码审计器误纳入宏定义已修正；relaxed 编译要求 storage semantics 为 None，修正该 API 参数后构建通过，失败构建未运行 GPU、不增加实现变体。没有用 acquire 单独替代 coherent 访问或发布屏障。ADL 0.5 秒采样包含启动/预热，未与正式帧对齐。

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
| ab-relaxed-fixed | 11.038904 | 11.033339 | 0.005565 | 0.012925 / -0.000483 / 0.004254 |
| ba | 11.089678 | 11.094656 | -0.004978 | -0.014783 / -0.000101 / -0.000049 |
| confirm-control | 11.091580 | 11.089904 | 0.001677 | 0.004423 / -0.001069 |
| control-relaxed-fixed | 11.057903 | 11.055607 | 0.002296 | 0.002023 / 0.002569 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

初筛平均节省 0.005565 ms，各组 0.012925/-0.000483/0.004254 ms，控制组差 0.002023/0.002569 ms。唯一反序确认平均增加 0.004978 ms，三组节省 -0.014783/-0.000101/-0.000049 ms，确认控制 0.004423/-0.001069 ms。两批方向相反，未建立可重复完整路径收益，关闭为 unproven；没有追加第三次测量或采用仅凭指令类型推断的收益。

保存 44 份宿主回读、8012 个測量帧；全部完整输出与独立基线逐字节比较。
保存 2 份整网 standalone 数值结果，其中 2 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / fswinpds64 SPIR-V：`bd003438c359949f70035c9e3c6b94c9dd4fcdc42fe5bd297800965378049f0a`。
- disabled / fswinpds128 SPIR-V：`76c494f152893b90cbcff8bf4641b2ea84531cd10e1cd15dce7dfb58e95b73ef`。
- disabled / fswinpds256 SPIR-V：`ec8a83af07742e3049a8318a2fd651ea50daee446ab43d3c2aa2f07c24962122`。
- disabled / fswinpup64 SPIR-V：`763a81fb44653494dbc59bb9501786f2b0f7582c40f527f35e167136ad47a6ba`。
- disabled / fswinpup128 SPIR-V：`7b7ea858828c7de2ba34c509fd8c1f9117c273b5978dfe1bbfb1025df504f8db`。
- disabled / fswinpup256 SPIR-V：`dbe2b443324d954ff60d9a24d79453ecc6c8fe9e29af9835f5772ba177e83e21`。
- loads / fswinpds64 SPIR-V：`f8ea2b12cbf91171eb37df0c5a93ea9a8c3cd55c74823394a558e0b9f45a6257`。
- loads / fswinpds128 SPIR-V：`25846db7084496a5aaecf0b08cf97ee019da78cc3e6fe387e19c3edc4e4d6a31`。
- loads / fswinpds256 SPIR-V：`080b3d73a381054e9bf196168f86760812bdd7251739d76d99def5223c3a0fa3`。
- loads / fswinpup64 SPIR-V：`70c615a8621ff5181fbb55f1b2b8d19032eec6251efb7beb75f155a94f22e0e0`。
- loads / fswinpup128 SPIR-V：`342583d155075ffc0441cb8599f1682820e2e51d9129cd23f25b3dbf1dab4b4e`。
- loads / fswinpup256 SPIR-V：`972f6d90b57ba332bd59c87ec2d13652236af4356ec3a25b029373e3506d59cb`。
- relaxed-fixed / fswinpds64 SPIR-V：`80b217e74e651af87a0693ed5e1063b5f9ced3e8b304af3e83e948e7600b7112`。
- relaxed-fixed / fswinpds128 SPIR-V：`de384fd25429630bb9c49eb0e421083bd27bd7d6151d00a6a9ca629f13815068`。
- relaxed-fixed / fswinpds256 SPIR-V：`99b1954ce81773456ed94958bf07953422c2a4809932ef2f157171e5855767aa`。
- relaxed-fixed / fswinpup64 SPIR-V：`662baeed5050c76ab9bb5d0a3cb9dcfdc4d445f375a5505f4c9b93400c6490b7`。
- relaxed-fixed / fswinpup128 SPIR-V：`84198f1d83c9d595e8490b5d28b736b1dedc41ca9aef3cb7f4a87e6f3281fc38`。
- relaxed-fixed / fswinpup256 SPIR-V：`b0ffa1ed37c28d634d0fd088e684836e5645a59591ed7c72586db20e813729c8`。
- prototype.patch：`2c95e809c4a52ef9916d291ecfb959c2d3530d947c1be29319dbc6a1fab51ab0`。
- relaxed-fixed.patch：`5bfcac9b05917b7df9608d9293e7fc349fb0d64e3474b446d87d36268d845d1a`。
- relaxed.patch：`e68e7ea3a1c69d8df7dbf9dc4272bbb7cb67ef54eee8447656bf09e5c22542b9`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：最后一项研究 ViT attention 的完整键块循环与尾块分离：当前 ISA 每轮分支合流有大量 context 寄存器复制。按原顺序执行所有完整 K64 块，再执行至多一个原尾块，保留原 half 归约、FP8 概率与全部 MMA。C256 单角落在目标每内核仅 8/1596 个窗口，未实施。
