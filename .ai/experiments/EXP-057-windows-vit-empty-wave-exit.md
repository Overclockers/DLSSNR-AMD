# EXP-057：ViT 收缩尾部空 wave 提前退出

日期：2026-10-07（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261007-214238-codegen9070xt` 第 7/10 个假设；1 版实现，1 次追加确认。
起始主仓 `ee8868794e932b7904cc27e4894d00563102a1c7`；宿主 `70484c408f717ba97da8636a2f0346a3e983fc40`。

## 假设与实现

M=800、tile M=64 时最后一个 tile 只有前 32 行有效；后四个 wave 原本仍做完整 K=4096 计算，最终两个片段均由已有 epilogue 丢弃。原型在 remap 后、片段读取前以 mg0+mloc>=M 提前返回；同一 wave 的条件一致。编译期排除 LDS staging、跨 wave 链、非片段 epilogue 与占位 LDS 路径，不改变有效输出的运算或归约顺序。仅在私有 gemmprojws 上启用；与注意力 query masking、token 分块调度不同。基线包含已保留 EXP-056。

disabled SPIR-V 与 EXP-056 基线逐字节一致。启用后目标整网 FP32 和完整 packed 输出一致；ELF .text 4736→4608 字节，静态指令 680→660，VGPR 95 不变、SGPR 30→26，LDS/scratch 为 0。少量尾部无效工作消失并不保证完整路径收益。ADL 只读传感器 0.5 秒采样包含启动/预热，不与测量帧同步。

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
| ab-skip | 10.710067 | 10.708542 | 0.001525 | 0.000577 / 0.010375 / -0.006376 |
| ba | 10.760767 | 10.752312 | 0.008455 | 0.025052 / -0.005356 / 0.005669 |
| confirm-control | 10.796214 | 10.799484 | -0.003270 | -0.002370 / -0.004170 |
| control-skip | 10.776072 | 10.781930 | -0.005859 | -0.006762 / -0.004955 |

初测平均节省 0.001525 ms，各组 0.000577/0.010375/-0.006376 ms，处于控制波动内。唯一反序确认平均节省 0.008455 ms，但组差 0.025052/-0.005356/0.005669 ms，方向仍不稳定；控制最大绝对组差 0.004170 ms。无法确认可重复的完整路径收益，判为 unproven，不保留。

保存 42 份宿主回读、8006 个測量帧；全部完整输出与独立基线逐字节比较。
保存 1 份整网 standalone 数值结果，其中 1 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / gemmprojws SPIR-V：`efdae1191942b8913e3b6411d8342225aa72c2904a819ece57fd1e24d7c0e2d4`。
- skip / gemmprojws SPIR-V：`cdfbf445bc6f2cbaa88db3466d907e40163520a7d0e7237eb337ce9affca83d9`。
- prototype.patch：`a8f91c8296545201f44d83811dbecb02e5401dc83b444262787d62c0e7abf92d`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：检查 ViT 激活片段通过 buffer-reference 全局地址读取的代码生成，保持原始字节和矩阵次序；与既有 residual BDA 和描述符 word-load 实验区分。
