# EXP-116：ViT 完整键块的独立 logits 分批计算

日期：2026-10-09（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261009-005048-lossless9070xt` 第 10/10 个假设；1 版实现，1 次追加确认。
起始主仓 `0cc7c5f6b4312ff291c7f03dc520eaff226cf79a`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

完整 K64 块原源码逐 query 计算 logits，再做概率、分母和 context。候选仅在 full chunk、QB=2 时先形成两组独立 logit accumulator，再用原 b 次序消费；每个 accumulator 的 d=0→d=1 MMA、所有 half 舍入、K16/K64 累加、分母归约及 context 代码保留，原 K/V 读取与尾部路径不变。与 EXP-023 K/V 预取、EXP-086 改变尾块控制流不同。

BDA/nobda 的 disabled SPIR-V 均精确复现基线。独立 BDA、mode=4、finite FP8 attention 探针在16/32/48/63/64/65/80/96/127/128/129/160/256/768/800/832 tokens全部输出一致、保护区零改写；proof SHA256 d2ed599d62b62bcbe03a617da035a5e9018e0403e7dd9e43eab2988c5952de24。目标整网 FP32 与所有 packed 全图一致。driver .text14976→14848 B，静态指令2137→2134，VGPR132、SGPR33、零 LDS/scratch不变。首个 full K16 tile 的两组 QK 之间，静态间隔从62条指令缩至30条；基线驱动本来已经把第二组 QK 提到第一组 context 之前，候选只是进一步改变调度，并没有形成四条连续 WMMA。schedule proof SHA256 cd92099c995117348c42d03191801ab8200b94275c6e38abe7a6da3431384359。这是指令顺序证据，不是时长或可相加的节省；nobda 已构建但目标未执行。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.992494 / 10.960786 / 10.984746 / 10.959362 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-batched | 10.998863 | 10.998597 | 0.000266 | -0.002812 / 0.010209 / -0.006597 |
| ba | 11.030804 | 11.056286 | -0.025481 | -0.105975 / 0.020983 / 0.008547 |
| confirm-control | 11.017012 | 11.013161 | 0.003851 | 0.002472 / 0.005230 |
| control-batched | 10.997088 | 10.999133 | -0.002045 | -0.000053 / -0.004037 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

unproven，不保留。初测完整路径 10.998863→10.998597 ms，只省0.000266 ms，组差 -0.002812/+0.010209/-0.006597 ms。唯一反序确认 11.030804→11.056286 ms，慢0.025482 ms；首组候选出现耗时尖峰，三组差 -0.105975/+0.020983/+0.008547 ms。控制组亦有数微秒组差，未证实稳定收益；全部批次保留，不筛掉首组、不用后两组替代整批、不追加第三轮。

ADL 合计 615 个有效样本，核心时钟 86–2759 MHz，edge 38–57°C，hotspot 38–82°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 44 份宿主回读、8012 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
已做单 attention 的16种长度/保护区与目标整网精确检查；性能未通过，因此未扩大到其它中间值、另一配方、非有限 attention 输入、其它图像格式/边界、目标运动历史、replay、lifetime、fallback 与完整宿主集成验收。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- batched / vitattn SPIR-V：`46c0afc006f62af84940be2e629a4cfd33f037bc34d0e0dc2d837a98f3713811`。
- batched / vitattnnobda SPIR-V：`fbd1c0b783830d3052ef145b17a0d13701405f362fa0cbf287f379d535dd4044`。
- disabled / vitattn SPIR-V：`1aa2e32b083a5b776c92e751713250b80163897fcac3cbd170ccca4aa2b84570`。
- disabled / vitattnnobda SPIR-V：`3cdd38d4115abe4c9c063dd519cecd641ee9bc93956745163cf7f8e502d419ff`。
- prototype.patch：`53f3cec281f50cd99397f6447a690df36573b01b7ad035280a007573a8619b69`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：十次额度已用尽；核验全部提交、基线/候选产物、两仓分支与原暂存内容，保存最终汇总，然后执行用户已授权的 shutdown -s -t 0。新的实验运行需要新的用户额度。
