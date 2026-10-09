# EXP-126：C256 DS 边缘输入加载提前剔除

日期：2026-10-09（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261009-fresh-five9070xt` 第 1/5 个假设；1 版实现，0 次追加确认。
起始主仓 `4beffd37cded1ab69666451c8d8f319d4747f691`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

C256 DS 输入 staging 原先先读 activation 再将越界 tile 置零。候选在原加载前用同一个 wave-uniform 边界条件选择零片段，保留原 LDS store、零值、全部有效输入、算术和同步。与 EXP-008/076/118 的 attention/output 工作剔除不同，本项只尝试省去输入加载。

disabled SPIR-V 与基线完全一致；候选 FP32 11,470,640 个位模式及 packed 输出精确一致。driver .text 224512→233728 bytes，VGPR 229→234、SGPR 107、LDS 32768、scratch 0。分支改变伴随 v_perm 72→240、v_lshr 478→798 等转换/地址开销增长；静态全局 load 数量不变，不声称已测量动态事务减少。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：11.009683 / 10.973646 / 10.992009 / 10.992915 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-cull | 11.009669 | 11.021724 | -0.012055 | -0.011617 / -0.017499 / -0.007050 |
| control-cull | 11.015268 | 11.018013 | -0.002744 | -0.006138 / 0.000650 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

regressed，不保留。完整宿主初测 11.009669→11.021724 ms，平均慢 0.012055 ms，三组全部更慢；同路径控制均差 -0.002744 ms。未申请额外确认。

ADL 合计 331 个有效样本，核心时钟 60–2730 MHz，edge 42–56°C，hotspot 42–80°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 23 份宿主回读、4009 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- cull / fswinpds256 SPIR-V：`2de0c7b9fb47e8426f3bbd111c1f23d82978666036f5279434cfcfa69c39fcea`。
- disabled / fswinpds256 SPIR-V：`3dd150b0f770d67564aaa23199fd59dd53014eb748a5b53d16de43f4ad1c92ad`。
- prototype.patch：`4c48eb85fe1966bf5c3234e0a6e4b6f56ca5055739f687768de982a2679665fc`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：研究 C512 FFWD Q2 权重能否在当前 chunk 的激活计算前提前加载，避免继续边缘分支参数扫描。
