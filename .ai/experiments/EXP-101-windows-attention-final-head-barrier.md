# EXP-101：C512 attention 最后一个 head 的复用屏障

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261008-193306-lossless9070xt` 第 5/10 个假设；1 版实现，1 次追加确认。
起始主仓 `57f98b78f45cb9f4830cfef3341e0a3dff42f66a`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

C512 attention 的末尾 NR_QKVBAR 保护下一 head 对 Q/K/V 共享内存的覆盖。候选仅在 hh+1<NR_HPW 或 hg+gridZ<NR_HSPLIT 时保留它，最后一个 head 无后续共享内存写者时跳过。循环变量、workgroup ID 和 dispatch 维度在工作组内统一；支持原先任意 gridZ 的多 head 循环。QKV 生产者屏障、不同 lane 的独立输出、chain memoryBarrier/barrier/atomic release 和全部数值运算保持。区别于 EXP-077 的 persistent 任务队列屏障。

CPU 检查 11,648 次 head 迭代，后继谓词与实际循环迭代完全对应。disabled SPIR-V 精确复现基线；目标 FP32 与全部 packed 输出一致。驱动末尾 s_cmp_gt_u32 / s_cbranch_scc1 在下一个 hg>15 时绕过 store wait 和 barrier，较早 head 保留同步；ISA 证明 SHA256 `fd96bf4fb56a813277a8b2456df488a043d3fb51b85893d87c40ac53f6f98d37`。目标 gridZ=16，每组处理一个 head。静态 .text 26880→27008 B，指令 3882→3890；VGPR184、SGPR59、LDS9216 B 和零 scratch 不变。未测量 wave 驻留变化。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.999239 / 11.010161 / 11.007372 / 11.000571 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-last-head | 11.224489 | 11.061555 | 0.162933 | 0.015331 / -0.002382 / 0.475852 |
| ba | 11.192017 | 11.353617 | -0.161600 | -0.459422 / -0.013048 / -0.012329 |
| confirm-control | 11.203349 | 11.185627 | 0.017722 | 0.021371 / 0.014074 |
| control-last-head | 11.791232 | 11.328871 | 0.462361 | 0.929871 / -0.005148 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

初测表面节省 0.162933 ms，但末组参考批次出现 2.18 ms 帧间标准差，同路径组差达 0.929871 ms；唯一反序确认平均慢 0.161600 ms，其中候选首批同样出现约 2.21 ms 帧间标准差。确认控制组差 0.021371/0.014074 ms；确认另外两组也未快于基线。未建立可靠完整路径收益，unproven，不保留，不删除异常批次。

ADL 合计 655 个有效样本，核心时钟 112–3013 MHz，edge 40–57°C，hotspot 40–81°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 44 份宿主回读、8012 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / attn SPIR-V：`4bafe70c5fa393acc1b0d6c1f53715a48f1cc6b9adf137d42d23caabdf6c62dc`。
- last-head / attn SPIR-V：`dffbb19b52ee875ac54324cde1d4415935484fa31f1091e6147a343cd5378eda`。
- prototype.patch：`66058e5ec439d4039547e5a99092d6a96b0703d23cd2b6fbbd96116008e7f68c`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：继续剩余五次额度，优先测试 C32 概率产生后立即按原顺序更新分母部分和的调度；保留所有数值边界。
