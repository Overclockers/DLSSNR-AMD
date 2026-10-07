# EXP-046：Subgroup 合并就绪队列入队

日期：2026-10-07（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261007-152904-schedule9070xt` 第 17/20 个假设；1 版实现，1 次追加确认。
主仓基线 `2bd19c8551b0c9c3a894ce13c034510bdceb45c4`；宿主 `137b3859a7572bee73c50a6975ab87f11adc4abf`。

## 假设与实现

每个 producer 最多有四个消费者；原路径各自原子预留队列尾部。候选在现有 ready 和 normal/critical 分支中 ballot 当前 active lanes，由 elected lane 一次预留总数，再 broadcast 起点、按 exclusive bit count 分配互不重叠的槽。每个依赖/rank 原子、tag 写入、epoch、队列容量和工作组屏障保持。

按 [Khronos subgroup 规范](https://raw.githubusercontent.com/KhronosGroup/GLSL/main/extensions/khr/GL_KHR_shader_subgroup.txt)核对 elect 与 broadcast-first 均选择 active 集合的最低 lane，ballot/prefix 对应该集合。已有队列消费者等待 tag 发布，预留多个尚未发布的槽不改变该协议。六个禁用宏 shader 与基线相同，FP32 与全部 packed 回读一致；六个 VGPR 不变、无 scratch，额外 ballot/prefix 增加约 19 条静态指令，straggler 的 C128 DS 增加 71 条。没有采集动态原子计数，因此不把源级合并数当成实测减量。

## 环境与验证

RX 9070 XT，Vulkan 新查询为 AMD proprietary / 26.8.1 (LLPC)，glslang 16.5.0。
核对 65 个宿主源文件、76 个产物身份；MSVC 14.51 构建的现有离线程序与 production DLL 按身份复用。
2258×1270 输入，2368×1280 工作尺寸，800 ViT tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32，宿主为线性 R11G11B10、depth=3；静态照片、无运动历史/SR/FG。
本轮起点完整宿主基线四批：10.713152 / 10.765059 / 10.755895 / 10.752117 ms（各预热 100、测量 200 帧）。
分段诊断完整路径 10.774323 ms、算子 10.355108 ms；不能和普通性能区间混减。
时钟、温度及持续后台负载未采样；未改变时钟或驱动。

完整路径为 D3D12 输入复制至输出复制。每批预热 100、测量 200 帧；初测 ABBA×3、同路径×2，若有 BAAB 表则为唯一反序确认。所有批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-batched | 10.773555 | 10.779293 | -0.005738 | 0.013621 / -0.015496 / -0.015339 |
| ba | 10.821904 | 10.824907 | -0.003003 | 0.009037 / -0.010078 / -0.007968 |
| confirm-control | 10.871548 | 10.880217 | -0.008669 | -0.013980 / -0.003359 |
| control-batched | 10.845493 | 10.853899 | -0.008406 | -0.016867 / 0.000055 |

初测平均增加 0.005738 ms，唯一反序确认增加 0.003003 ms，均为一组快、两组慢；初测控制最大绝对组差 0.016867 ms，确认控制 0.013980 ms。未建立完整路径收益，判为 unproven，不保留。

另保存 42 份原独立宿主回读、8006 个测量帧；回读逐一与独立基线比较。
保存 1 次 standalone 结果（其中 1 份与基线完全一致）；具体失败见上文。
本项未扩大完整 NR 到中间值、其它格式/边缘尺寸、history/replay、fallback 及宿主集成验收，因为候选未通过前置筛选。
静态输出检查不代替这些未测项。生产和 Linux 源码未应用原型，宿主没有修改或提交。

## 身份与复核

- batched / fswinpds64 SPIR-V SHA-256：`948aa391016557f58a6b72a80527734944195da0c87578362c4c51d3c3dfffb4`。
- batched / fswinpds128 SPIR-V SHA-256：`cf534e87d75c7dde8657c726c4b4c694a0926c0031b5caf60b8839f4da065d2b`。
- batched / fswinpds256 SPIR-V SHA-256：`f8ef794e00ad1039fd6ca8dc84c075517bcc1b240b298d50001962b545ec61bb`。
- batched / fswinpup64 SPIR-V SHA-256：`07f5fe9d69ca120d08ba2684f0646cf0c8e2536b36dd71058cd562991160d538`。
- batched / fswinpup128 SPIR-V SHA-256：`a9a253f4ec65534dadf902e1d129919a0f343cc40f9e95f2fd22c7a3679e7967`。
- batched / fswinpup256 SPIR-V SHA-256：`dc4f0df20cbb57d72fafd1a489115887207cd5b3b8c0bb6c8e6f18fb594a3291`。
- disabled / fswinpds64 SPIR-V SHA-256：`bd003438c359949f70035c9e3c6b94c9dd4fcdc42fe5bd297800965378049f0a`。
- disabled / fswinpds128 SPIR-V SHA-256：`76c494f152893b90cbcff8bf4641b2ea84531cd10e1cd15dce7dfb58e95b73ef`。
- disabled / fswinpds256 SPIR-V SHA-256：`ec8a83af07742e3049a8318a2fd651ea50daee446ab43d3c2aa2f07c24962122`。
- disabled / fswinpup64 SPIR-V SHA-256：`763a81fb44653494dbc59bb9501786f2b0f7582c40f527f35e167136ad47a6ba`。
- disabled / fswinpup128 SPIR-V SHA-256：`7b7ea858828c7de2ba34c509fd8c1f9117c273b5978dfe1bbfb1025df504f8db`。
- disabled / fswinpup256 SPIR-V SHA-256：`dbe2b443324d954ff60d9a24d79453ecc6c8fe9e29af9835f5772ba177e83e21`。
- prototype.patch SHA-256：`1dcb01ff924b0aa5aeeb7f2d31ab8176057bc56d79b1b3d8ebf5e0592951d1c2`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

源快照、原型、ISA、工具/产物哈希和全部私有结果在忽略的运行目录；本机定位见 local ledger。
只将本记录、索引、状态提交主仓；没有游戏、FSR4/FG、部署、安装、打包、发布、推送或外部消息。

下一步：研究最后 image post 与 packed compose 融合，只针对单遍、同尺寸、无 history/mask/preprocess 的原合成路径；保留全部颜色算术及其余路由回退。
