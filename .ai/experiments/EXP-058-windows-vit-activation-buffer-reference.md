# EXP-058：ViT 激活片段的 buffer-reference 读取

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261007-214238-codegen9070xt` 第 8/10 个假设；1 版实现，0 次追加确认。
起始主仓 `22d8b4b8800db20ce81c9fa65fe7afd9fb046a96`；宿主 `70484c408f717ba97da8636a2f0346a3e983fc40`。

## 假设与实现

仅在私有 gemmvact/gemmprojws 中，使用 PushGemm 已有 activation GPU 地址，通过 8 字节对齐的只读 uvec2 buffer-reference 读取 A 片段。按原字节顺序 unpack，原权重加载、MMA、归约及精度不变。此项改变激活读取的存储类别，区别于 EXP-019 的残差转置读取与 descriptor-only word loads。原型只测试实际已开启 BDA 的设备；未将非 BDA 能力回退集成进生产模块。

禁用时两模块逐字节复现当前 EXP-056 基线。native FP32 全图和宿主 packed 输出精确一致。driver ISA 确认 8/16 条 buffer_load_b64 分别换成 global_load_b64，但新增 64 位地址加法/位移：扩张 .text 13056→13312 字节、静态指令 1659→1709；收缩 4736→5632、680→818。候选 VGPR 158/91、SGPR 21/43，LDS/scratch 为 0。ADL 只读采样间隔 0.5 秒，包含启动/预热，不与测量帧同步。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.640660 / 10.627673 / 10.667158 / 10.678415 ms，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-bda | 10.725677 | 10.984511 | -0.258834 | -0.259664 / -0.258209 / -0.258628 |
| control-bda | 10.792632 | 10.788377 | 0.004255 | 0.003789 / 0.004721 |

完整宿主初测 10.725677→10.984511 ms，平均慢 0.258834 ms；三组分别慢 0.259664/0.258209/0.258628 ms，明显大于同路径控制最大绝对组差 0.004721 ms。判定本实现退步，不保留，也不消耗追加确认额度。全局地址寻址并未减少该驱动的净工作量；不能从 VMEM 指令名称推断更快。

保存 22 份宿主回读、4006 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- bda / gemmvact SPIR-V：`26814beab193b47e873115b797724feca6644340eac539691d93c14d37d4c61d`。
- bda / gemmprojws SPIR-V：`18b7beedf07252f197d95b65f0be9b4de54ebf0e695d3d64255d6b9cfdfbcbed`。
- disabled / gemmvact SPIR-V：`c48c1619dde7995664f7b0e6e34c4bd51abf0d6b20cac70b65daf186b01efbdb`。
- disabled / gemmprojws SPIR-V：`efdae1191942b8913e3b6411d8342225aa72c2904a819ece57fd1e24d7c0e2d4`。
- prototype.patch：`d8ac20fc209ecfaa0374e41305682fbcbca860255be3eeb69f0b7e50314f2a18`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：在独立原生设备上查询并启用 computeFullSubgroups，仅为 localSizeX 是 32 倍数的 GEMM pipeline 设置 REQUIRE_FULL_SUBGROUPS，比较实际机器码；保持宿主设备契约原样。
