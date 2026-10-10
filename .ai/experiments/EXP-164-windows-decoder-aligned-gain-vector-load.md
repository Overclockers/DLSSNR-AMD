# EXP-164：decoder 对齐增益向量读取

日期：2026-10-11（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261011-001700-ten9070xt` 第 9/10 个假设；1 版实现，0 次追加确认。
起始主仓 `43951c7b423c45e03f400a02b94beea7023df93c`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

decoder 宽路径原有八次连续 FP16 gain 读取，尝试在原偏移满足16字节对齐时用 uvec4 读取原16字节并 unpackFloat2x16；未对齐与部分tile继续原标量路径。仅改变增益交付，保留逐元素 skip 转换、残差乘法、half 加法、FP8转换与存储；不同于EXP-135成对算术和EXP-103 projected-source共享。

disabled SPIR-V逐字节重现独立基线；目标静态整网FP32与完整宿主packed输出逐字节一致。driver已实现gain buffer_load_b128（ISA文本行142，gain描述符s[8:11]），未对齐回退u16读取受predicate控制；VGPR21、SGPR24、LDS/scratch0不变，.text2304 B不变，静态指令327→335、wait_loadcnt12→5。新增alignment与fallback控制可能抵消宽读取收益；ISA位置不是周期。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.930771 / 10.922131 / 10.921556 / 10.919724 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-vector | 10.917434 | 10.926385 | -0.008950 | -0.013606 / -0.007442 / -0.005803 |
| control-vector | 10.937661 | 10.940927 | -0.003266 | 0.001103 / -0.007635 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

完整离线路径初筛10.917434→10.926385 ms，平均慢0.008950 ms；三组节省均为负（-0.013606/-0.007442/-0.005803 ms），同路径控制为+0.001103/-0.007635 ms。没有候选提速信号，按regressed关闭；不把该小差异当作稳定通用回退估计。没有追加确认，未保留生产改动，本轮新增收益仍为0 ms。

ADL 合计 299 个有效样本，核心时钟 0–2831 MHz，edge 33–53°C，hotspot 34–79°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 23 份宿主回读、4009 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
性能筛选已否决；未运行反序确认，也未扩大到穷举half域、其它格式、边界尺寸、运动历史、中间值、replay、fallback和两套无缓存集成验收。已有静态一致性不证明所有输入等价。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / decups SPIR-V：`4c76d6b701942b709632dc35a671724a8ce0614e7cd442a4df06bfb4d63ea5c7`。
- vector / decups SPIR-V：`d3a2693a383d2864802f4d0fee6b2b40a6dad979adff898643587239ab259e2c`。
- prototype.patch：`b950888be18215f2d9f29d90f5b066871cb1e9d933c488128601a54a1f1bae00`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：第十次测试ViT fused QKV同token边界guard在channel fragment之间共享；完成十次的保存、提交、推送和最终核验后执行用户指定关机。
