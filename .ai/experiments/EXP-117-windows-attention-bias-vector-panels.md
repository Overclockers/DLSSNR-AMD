# EXP-117：C512 attention 偏置的显式向量面板读取

日期：2026-10-09（Asia/Hong_Kong）。结论：未保留（mechanism_not_realized）。
运行 `20261009-085523-newpaths9070xt` 第 1/5 个假设；1 版实现，0 次追加确认。
起始主仓 `9704b358213665b4821066c60c88ebb1fbc95629`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

将每个 query/key 偏置行的八个连续 FP32 值经同一 binding 的 vec4 视图读成两个面板，保留原 pair 指数调用、偏置加法位置、归约树和所有舍入。put_f32 经 16-byte 对齐，query 行和 key 起点均为 16 个 float 的倍数。区别于 EXP-029 的跨 MMA 预取和 EXP-087 的 FP8 权重宽读。

禁用 SPIR-V 精确复现。目标 FP32、packed 全图均逐字节一致。驱动静态 buffer_load_b128=8、buffer_load_b64=176 在两版完全相同：编译器已完成相同宽度的读合并，预期减少访存未实现。机器码26880→27008 B，静态指令3882→3890；候选185 VGPR、58 SGPR、9216 B LDS、零 scratch。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.978340 / 11.019070 / 11.008404 / 11.006539 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-panels | 11.031508 | 11.034201 | -0.002692 | -0.006935 / 0.006345 / -0.007487 |
| control-panels | 11.046449 | 11.053351 | -0.006902 | -0.011557 / -0.002247 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

mechanism_not_realized，不保留。完整路径11.031508→11.034201 ms，平均慢0.002692 ms，三组方向混合；同路径控制组差-0.011557/-0.002247 ms。没有访存指令减少，也无可分辨收益，因此在初测后关闭，不追加确认。

ADL 合计 307 个有效样本，核心时钟 78–2763 MHz，edge 37–55°C，hotspot 37–81°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 23 份宿主回读、4009 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / attn SPIR-V：`4bafe70c5fa393acc1b0d6c1f53715a48f1cc6b9adf137d42d23caabdf6c62dc`。
- panels / attn SPIR-V：`be90a40a9f5f756a3382285116e6be674d22867fbf96e4a40fe5c239e650bd5d`。
- prototype.patch：`0eb0f7e26484716d01767b76e1bd62496f1393095d4f6c337d1f33e3b53f1083`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查终端 image post 在共享 K/V 准备完成后的无输出 wave 退出；只删除没有消费者的后续计算。
