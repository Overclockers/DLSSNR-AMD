# EXP-074：C512 Q0 权重的工作组协作缓存

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261008-115341-shapes9070xt` 第 9/10 个假设；1 版实现，0 次追加确认。
起始主仓 `f2cc18878a3500fe68a227fb95397fb0c7738d43`；宿主 `9d1fa938563ce5cf7c06882d0134e6385f5e50eb`。

## 假设与实现

group-major ffwd3w 的四个 wave 对不同 token 重复读取相同 Q0 权重。候选协作加载原连续 4 KiB chunk 到 LDS，保持原 packed bytes、WMMA、激活/量化、K 顺序和输出布局。一个工作组统一的 full-wave 条件保证新增 barrier 只在全部 wave 有效时执行；部分尾部仍用原 global load 且不执行新增 barrier。写入后发布、所有读取结束后才允许下一 chunk 覆盖；不跨 dispatch 融合。

4096 个 packed word 地址及 17,408 个完整/尾部条件检查通过。禁用 SPIR-V 复现基线，目标 native FP32 和全部 packed 图逐字节一致。初次编译缺少 int8 类型扩展，在 GPU 运行前补充声明，属于一次构建修复而非新变体。driver .text 31232→37632 B，指令 4185→5036，VGPR 182→167、SGPR 23→24，LDS 0→4096 B，零 scratch。ADL 0.5 秒采样包含启动/预热，非帧同步。

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
| ab-lds-fixed | 11.005228 | 11.039888 | -0.034661 | -0.034748 / -0.035293 / -0.033941 |
| control-lds-fixed | 11.095664 | 11.102090 | -0.006426 | -0.005837 / -0.007015 |

完整 NR 三组稳定变慢，平均增加 0.034661 ms；各组慢 0.034748/0.035293/0.033941 ms，控制最大绝对组差 0.007016 ms。额外 LDS、同步和回退控制未形成净收益，关闭为 regressed，不追加确认。

保存 22 份宿主回读、4006 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / ffwd3w SPIR-V：`3ad3da9ee684c7951e5d37a7bca62af1ca9e8357b9abbeabbd2cd662aeaaabdb`。
- lds-fixed / ffwd3w SPIR-V：`fcd2227fe9c74fcc515771be12666c97e2fcb69381efd9c6224d9b3e20522527`。
- prototype.patch：`9d6a78a49a59ab4cff1f88dd2992995532ddc582189d7a096d83dfeb3ffa8547`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：最后一项根据实际 ISA 的逐分量 half 激活 clamp，测试无运行时分支的位域限幅；先穷举全部 half 值及 NaN/Inf/有符号零语义。
