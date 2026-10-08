# EXP-093：GEMM 已量化输出字节的 wave 转置宽写入

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261008-162203-lossless9070xt` 第 7/10 个假设；1 版实现，1 次追加确认。
起始主仓 `165196c0200de6ec71f73998967ef7e45557dd0e`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

gemmprojws 原本每个 lane 对 16 字节步长的八个位置写入单字节。保持 MMA、残差、half 舍入、FP8 量化原样，仅将最终八字节打包，再经过三次 XOR butterfly 转置，写回同一 16×16 tile 的连续八字节。仅启用普通无交换、无 pooling 的 tiled 输出；不同于 EXP-054 的矩阵乘法转置。

CPU 符号证明覆盖 tile 全部256字节；独立 GPU probe 覆盖2048 tiles和全部256种字节值，输出与哨兵逐字节相同。transpose-proof.json SHA256 `ff52f7d1ffec1306293b5e471bf964673b5175f6f5bd4ee76754ecd814edbb13`。关闭宏后 gemmprojws SPIR-V 与独立原版精确相同。目标整网 FP32及所有宿主 packed 回读相同。驱动 .text4992→6016B，静态行742→930；96VGPR/30SGPR/无LDS/零scratch不变。静态 buffer_store_b8 32→0，buffer_store_b64 0→4，但增加三轮交换指令。ADL只读采样含初始化/预热。已有函数/布局中间值 probe 不替代完整集成覆盖。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：11.019102 / 11.015484 / 11.018076 / 11.025782 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-transpose-fixed | 11.034605 | 11.041230 | -0.006625 | -0.002635 / -0.016546 / -0.000694 |
| ba | 11.101177 | 11.104216 | -0.003039 | -0.010043 / -0.001893 / 0.002819 |
| confirm-control | 11.091992 | 11.091146 | 0.000846 | -0.003842 / 0.005534 |
| control-transpose-fixed | 11.061620 | 11.054485 | 0.007135 | 0.009646 / 0.004623 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

初测11.034605→11.041230 ms，慢0.006625 ms；唯一反序确认11.101177→11.104216 ms，慢0.003039 ms。初测三组均无收益，反序三组两负一正；控制组差与候选效应重叠。两轮都未显示完整路径收益，unproven，恢复基线，不继续追加测量。

保存 44 份宿主回读、8012 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / gemmprojws SPIR-V：`63449ee64f848b4d78056350362052a10e279f795dc3788bfee7bc0cb118f636`。
- disabled-fixed / gemmprojws SPIR-V：`63449ee64f848b4d78056350362052a10e279f795dc3788bfee7bc0cb118f636`。
- transpose-fixed / gemmprojws SPIR-V：`1483990058b7c8b91cf5189249b971dccb528924a282cc25942eeb2abbc179ac`。
- prototype.patch：`40c41ed202ad328c4262401a1896be01e683d8267b9c5731b94f0952c8156e35`。
- repaired.patch：`f2077700c0591c6fef4b858d930e71253abd59aa974f20477f1e9fcb10a11efe`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查网络录制中重复的 push constant 数据；只在单次录制、兼容布局内缩小更新范围，验证命令语义与完整路径成本。
