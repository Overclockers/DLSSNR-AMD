# EXP-077：Persistent 任务边界 LDS 屏障合并

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261008-130553-swin9070xt` 第 2/10 个假设；1 版实现，1 次追加确认。
起始主仓 `a5506eb6d51da060055d65f0527d1a45b2aec430`；宿主 `8aacc8b2b88b0a39a952d56d047b6139762dbb8a`。

## 假设与实现

在默认单任务、LDS claim、dependency queue 路径中，删去任务循环尾部的一组 barrier。原有写数据后的 memoryBarrierBuffer/barrier、consumer 发布原子和两组 claim/broadcast 屏障全部保持。前一 release barrier 已让所有 wave 完成该任务的 LDS 访问，随后只有同一首 wave 的前四个 lane 发布全局计数，且它们在首 wave 领取下一任务前完成；期间不再读写 LDS。它不改变任务、权重、数值表达式、归约或资源布局，区别于队列 batching、层查找和参数缓存。对照已继承本轮 EXP-076。

六个禁用 SPIR-V 复现当前基线。第一版安全守卫发现宿主 DS128 实际使用 NR_STRAG=2，在任何 GPU 运行前拒绝编译；一次构建修复让这些非适用配置保留原 barrier，不计新变体。五个实际变化模块各少一对 s_barrier_signal/wait；DS128 的完整 driver .text 不变。其余 VGPR/SGPR/LDS 和零 scratch 均保持，FP32 全图与全部保存的 packed 宿主回读精确一致。ADL 每 0.5 秒只读采集，含初始化与预热，并非帧同步。

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
| ab-compact-fixed | 10.913225 | 10.914895 | -0.001670 | -0.004881 / -0.007347 / 0.007218 |
| ba | 10.988292 | 10.969306 | 0.018986 | 0.040965 / 0.003535 / 0.012460 |
| confirm-control | 11.031650 | 11.033397 | -0.001747 | -0.005294 / 0.001801 |
| control-compact-fixed | 11.003200 | 11.002263 | 0.000937 | 0.002793 / -0.000919 |

初测平均慢 0.001670 ms，三组节省 -0.004881/-0.007347/0.007218 ms；唯一反序确认平均快 0.018986 ms，但组间幅度 0.040965/0.003535/0.012460 ms，确认控制最大绝对组差 0.005294 ms。初测方向未复现，确认第二组仍落在控制波动内，未建立可重复完整路径收益，关闭为 unproven，不保留原型。

保存 42 份宿主回读、8006 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- compact-fixed / fswinpds64 SPIR-V：`4818756fe14aa6f63e5dafa1a3caac4171ff84b0530bc852ffa69e23d604fd5c`。
- compact-fixed / fswinpds128 SPIR-V：`76c494f152893b90cbcff8bf4641b2ea84531cd10e1cd15dce7dfb58e95b73ef`。
- compact-fixed / fswinpds256 SPIR-V：`8997591277d4c4901923d49e5d6cdf0bc372fe11a339ecc60bc988c64e8cd2f5`。
- compact-fixed / fswinpup64 SPIR-V：`1a1c5497365588b254ecf4ca8b60e54c5b858ec118e2ca5c565e66fe51181776`。
- compact-fixed / fswinpup128 SPIR-V：`8e8b2e660ebf197bf2c511b91d492db18e0fb04baef32bcc93ce222bdbd38e7b`。
- compact-fixed / fswinpup256 SPIR-V：`0ff8a9597497ab2d3fc38fc806cef91c8a8866e47cf775bffff33da838cf24e6`。
- disabled / fswinpds64 SPIR-V：`bd003438c359949f70035c9e3c6b94c9dd4fcdc42fe5bd297800965378049f0a`。
- disabled / fswinpds128 SPIR-V：`76c494f152893b90cbcff8bf4641b2ea84531cd10e1cd15dce7dfb58e95b73ef`。
- disabled / fswinpds256 SPIR-V：`ec8a83af07742e3049a8318a2fd651ea50daee446ab43d3c2aa2f07c24962122`。
- disabled / fswinpup64 SPIR-V：`763a81fb44653494dbc59bb9501786f2b0f7582c40f527f35e167136ad47a6ba`。
- disabled / fswinpup128 SPIR-V：`7b7ea858828c7de2ba34c509fd8c1f9117c273b5978dfe1bbfb1025df504f8db`。
- disabled / fswinpup256 SPIR-V：`dbe2b443324d954ff60d9a24d79453ecc6c8fe9e29af9835f5772ba177e83e21`。
- prototype.patch：`a3d987a9e373f2c5dff4b8bf044a7d72b361d703e68119fbd70f15fd1c69da38`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查两-wave 输入层的像素与 token 所有权，测试用精确保留 half 位模式的 subgroup 交换替代输入特征 LDS 往返与其独立 barrier。
