# EXP-066：ViT 扩张与 QKV 固定通道尺寸

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261008-115341-shapes9070xt` 第 1/10 个假设；1 版实现，0 次追加确认。
起始主仓 `dd48cc31aed2feaa9d56a131f0be9fabab5dd900`；宿主 `9d1fa938563ce5cf7c06882d0134e6385f5e50eb`。

## 假设与实现

将 gemm1x1 的 NR_FIXED_KN 推广为独立 NR_FIXED_K/NR_FIXED_N 参数，同一假设内在 gemmvact 启用 K=1024/N=4096、gemmvqkvnorm 启用 K=1024/N=3072。两者均由 nr_graph.cpp 的固定类型映射约束；原有收缩 K=4096/N=1024 保持，M 仍动态，入口核对尺寸。所有 WMMA、K 累加次序、精度、舍入、权重、packing、descriptor 和 dispatch 网格不变。与 EXP-056 不同的是扩张和 QKV 各自的加载/输出主体；两个模块未拆作两个额度。

禁用改动的三个模块逐字节重现当前基线 SPIR-V。候选扩张 driver .text 从 13312 缩小至 12544 B，QKV 从 20224 缩小至 19584 B；静态指令行分别 1720→1600、3041→2983。候选 VGPR 为 166/165，SGPR 均为 22，LDS/scratch 为 0。目标 FP32 全图及完整宿主 packed 输出精确一致。每批 ADL 以 0.5 秒间隔只读采样时钟、温度、活动率与功耗，含初始化和预热，非帧同步。预检的 nr_profile.hpp 只有 LF/CRLF 差异，规范化哈希匹配旧基线；没有实现差异。

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
| ab-fixed | 10.988305 | 11.012341 | -0.024036 | -0.030384 / -0.022040 / -0.019684 |
| control-fixed | 11.072344 | 11.070921 | 0.001423 | -0.000735 / 0.003581 |

三组均退步，完整 NR 平均增加 0.024036 ms；对应同路径控制最大绝对组差 0.003582 ms。更短机器码未形成更快执行，提前拒绝，不追加确认。候选只存在于忽略目录，两仓生产源均未改变。

保存 22 份宿主回读、4006 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / gemmvact SPIR-V：`d2f933764c49db726f35d45f7c8cff8cabbf75b9842d28d896e98499d910a9d2`。
- disabled / gemmvqkvnorm SPIR-V：`7e3251e0d1cddac21a28f55578c40be707e864327222d64d82dfca16ab9aa0eb`。
- disabled / gemmprojws SPIR-V：`63449ee64f848b4d78056350362052a10e279f795dc3788bfee7bc0cb118f636`。
- fixed / gemmvact SPIR-V：`9ce864d65a9deea225723cc90d9e0e9638519d976a00a3e249f1de31765f056e`。
- fixed / gemmvqkvnorm SPIR-V：`da329106416b7449c724d64db7de86b8fdf5d986e5acf21cfed8ea8862e4c2a8`。
- prototype.patch：`534c9ea304ee8530017d2afab9ebbe698420346d2a4a1e791bf990ae77212b55`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查 persistent Swin 任务编号到窗口坐标的动态除法，测试带严格整数修正与通用回退的有界商计算。
