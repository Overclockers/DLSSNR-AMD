# EXP-135：Decoder 成对 half 运算与原融合舍入定位

日期：2026-10-10（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261009-235203-ten9070xt` 第 5/10 个假设；2 版实现，1 次追加确认。
起始主仓 `b5afd2d3b4215ed117a802be55fb56aa3fcde55d`；宿主 `f526a6d7c8bd6e69c5bc15e6ef6bdba379515cf4`。

## 假设与实现

NR_DECUPS_VEC=16已有向量读取，但后续八个channel的half计算以标量书写。候选尝试将相邻两channel成对运算，保持地址、scalar FP8量化和partial-tile fallback；不采用EXP-103的跨lane共享。初始依据源码注释以为乘法与加法分开舍入，实际驱动证据否定了这个假定。

第一版precise half乘法再加法改变8,602,255个最终float，最大绝对差0.0462594628，立即拒绝、未做该版性能测量。原driver的向量路线实际为8个标量half FMA/FMAC，仅有一次融合舍入；修正版显式fma(gain,skip,projected)，生成4个v_pk_fma_f16，恢复整网FP32与全部packed精确一致。修正版.text均2304 B、静态行均327，VGPR21→17、SGPR24不变、零LDS/scratch，s_wait_loadcnt12→8；packing/permutation也增加。源码中的half临时变量并不能证明实际存在两次舍入，后续优化应以独立原版机器码和精确数值验证交叉核对。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.943105 / 10.917341 / 10.917457 / 10.944389 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-fused | 10.933416 | 10.929455 | 0.003961 | 0.006148 / -0.003556 / 0.009290 |
| ba | 10.972228 | 10.972041 | 0.000187 | -0.003247 / 0.000142 / 0.003666 |
| confirm-control | 10.961033 | 10.965262 | -0.004229 | -0.000882 / -0.007576 |
| control-fused | 10.942543 | 10.936809 | 0.005735 | 0.004596 / 0.006873 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

修正版初测节省 0.003961 ms，反序确认节省 0.000187 ms；未建立超过同路径波动的可重复收益，unproven，不保留。数值不等价的第一版只作为失败证据保存，不能作为提速路线。

ADL 合计 624 个有效样本，核心时钟 420–2989 MHz，edge 36–57°C，hotspot 37–82°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 44 份宿主回读、8012 个測量帧；全部完整输出与独立基线逐字节比较。
保存 2 份整网 standalone 数值结果，其中 1 份一致。
修正版未通过性能筛选，因此未做穷举half/FMA边界探针、其它格式/边界尺寸、目标运动历史、中间值、replay、fallback及双配方完整集成验收。第一版有明确数值差异，已拒绝；修正版静态一致性不替代这些未测项。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包或发布；核心实验记录按技能推送 main-experiment，宿主不推送。

## 身份与复现

- disabled / decups SPIR-V：`4c76d6b701942b709632dc35a671724a8ce0614e7cd442a4df06bfb4d63ea5c7`。
- fused / decups SPIR-V：`c158a25bd2fd3e39e9a22c77d4c6121c9b846b5d771f32f6bdd077423e7fd3a0`。
- paired / decups SPIR-V：`8063656b1573b85a33d0bf9d6e08e957a441dedacce2ab864ec35a83cd3504d9`。
- fused-repair.patch：`fa95b86f367fcca9eb956208abcfb91df2dedd128c4425c5a2d4a4bb8fee5176`。
- prototype.patch：`1e2fc5c3e7160bb0f75956a1bc9b44e9c645fb4a94c24d27d38ab552012dae5f`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查C512 attention在softmax分母归约前预读第一个V片段，保持每个context的原key累加顺序。
