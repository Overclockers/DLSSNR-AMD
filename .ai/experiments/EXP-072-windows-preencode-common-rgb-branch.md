# EXP-072：融合编码的 RGB 共同分支

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261008-115341-shapes9070xt` 第 7/10 个假设；1 版实现，1 次追加确认。
起始主仓 `d5335bb34fd5f11e5490cc0eb0d2f1626ae7ab20`；宿主 `9d1fa938563ce5cf7c06882d0134e6385f5e50eb`。

## 假设与实现

在 runtime_preencode 的色调压缩与钳制之后，若三个颜色分量都超过原 sRGB 阈值，则共同执行原非线性表达式；其它组合保留原逐分量分支。白点、色调、pow 指数、运算精度、舍入、采样和 alpha 均保持。它测试分支组织，不是近似 gamma、查表或改变色彩计算。

GPU 函数探针比较 328,194 个 RGBA 记录、1,312,776 个 FP32 分量，零位差：全部 half 编码在白点 0.01/1/2/100 下、阈值相邻 FP32、随机 bit patterns，包含 NaN/Inf/负值和混合 RGB 分支。禁用 runtime SPIR-V 复现基线；普通 pre 在两臂都逐字节不变，实际变化的是 runtime_preencode。匹配 runtime 创建条件的 .text 26240→26496 B，指令 3577→3612，124 VGPR/46 SGPR、6144 B LDS、零 scratch 不变。宿主所有末帧 packed 输出精确一致；ADL 每 0.5 秒采样，含启动/预热且非帧同步。

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
| ab-common | 10.992003 | 10.995374 | -0.003371 | -0.012112 / 0.000998 / 0.001001 |
| ba | 11.064670 | 11.058280 | 0.006390 | 0.011194 / 0.001537 / 0.006439 |
| confirm-control | 11.123132 | 11.112393 | 0.010738 | 0.014217 / 0.007259 |
| control-common | 11.087447 | 11.091737 | -0.004291 | -0.013614 / 0.005033 |

初测慢 0.003371 ms，反序确认快 0.006390 ms；确认的同路径控制却快 0.010738 ms，两个控制组为 0.014217/0.007259 ms。方向变化且收益幅度无法与控制区分，关闭为 unproven，不保留。

保存 42 份宿主回读、8006 个測量帧；全部完整输出与独立基线逐字节比较。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- common / fswinimagepreds32 SPIR-V：`6ec78cb87f11f452e7c8b36c70cd325f81033c541faac48259175fc48fa6813f`。
- common / runtime_preencode SPIR-V：`0813418b7b943597ad113010a1b305d6a44f0427aef22806878aa3d24b52f113`。
- disabled / fswinimagepreds32 SPIR-V：`6ec78cb87f11f452e7c8b36c70cd325f81033c541faac48259175fc48fa6813f`。
- disabled / runtime_preencode SPIR-V：`34a29bfd739eddb50f8cb546b6a42fdaa2d4ba728b8ff4054054ce1c345febdc`。
- prototype.patch：`692b05537e8d7090d3598463c6d326734fa64aa370403749228a788efc0bcba2`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：测试 C512 前馈第二级分片立即供第三级消费，减少同时存活的 FP32 中间累加器，同时保持每个输出的 K 顺序。
