# EXP-042：图像 pre/post 整数取样

日期：2026-10-07（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261007-152904-schedule9070xt` 第 13/20 个假设；1 版实现，1 次追加确认。
主仓基线 `3034a326de024bd504b3013059c9367054823a2c`；宿主 `3a88c92d7d85933cf7c290cbb9fa25f9faa6d688`。

## 假设与实现

将 pre 的当前图像和 post 的 base colour 中心点读取改为有条件 texelFetch。pre 要求 textureSize 与源尺寸一致、反射后的坐标非负且在图内；post 已有负坐标/output bounds 检查，再检验 source bounds。条件不满足则保留原 textureLod，motion/history 采样不改。两者使用原 FP32 sample 和后续原有半精度/颜色计算。

禁用宏后，两份 SPIR-V 与独立基线逐字节相同。开启后，pre/post 机器码分别 25856→25984、24576→24704 字节，静态指令 3509→3536、3312→3334；候选 VGPR 124/122，均无 scratch。整数采样增加了边界回退控制，不是纯指令删除。standalone FP32 与全部完整宿主 packed 回读一致。

## 环境与验证

RX 9070 XT，Vulkan 新查询为 AMD proprietary / 26.8.1 (LLPC)，glslang 16.5.0。
核对 65 个宿主源文件、76 个产物身份；MSVC 14.51 构建的现有离线程序与 production DLL 按身份复用。
2258×1270 输入，2368×1280 工作尺寸，800 ViT tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32，宿主为线性 R11G11B10、depth=3；静态照片、无运动历史/SR/FG。
新鲜完整宿主基线四批：10.713152 / 10.765059 / 10.755895 / 10.752117 ms（各预热 100、测量 200 帧）。
分段诊断完整路径 10.774323 ms、算子 10.355108 ms；不能和普通性能区间混减。
时钟、温度及持续后台负载未采样；未改变时钟或驱动。

完整路径为 D3D12 输入复制至输出复制。每批预热 100、测量 200 帧；初测 ABBA×3、同路径×2，若有 BAAB 表则为唯一反序确认。所有批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-fetch | 10.817205 | 10.804584 | 0.012621 | 0.018121 / 0.011548 / 0.008194 |
| ba | 10.863496 | 10.874394 | -0.010898 | 0.007539 / -0.024794 / -0.015439 |
| confirm-control | 10.945379 | 10.902444 | 0.042935 | 0.084829 / 0.001042 |
| control-fetch | 10.877805 | 10.898310 | -0.020505 | -0.027490 / -0.013520 |

初测三组均快，平均节省 0.012621 ms，但初测控制均差为 -0.020505 ms。唯一反序确认平均增加 0.010898 ms；确认控制 +0.042935 ms，第一组含 11.000194/10.966974 ms 批次，全部保留而未剔除。方向未复现且同路径噪声覆盖候选效应，unproven，不保留。

另保存 42 份原独立宿主回读、8006 个测量帧；回读逐一与独立基线比较。
保存 1 次 standalone 结果（其中 1 份与基线完全一致）；具体失败见上文。
未扩大到中间值、其它格式/边缘尺寸、history/replay、fallback 及宿主集成验收，因为候选未通过前置筛选。
静态输出检查不代替这些未测项。生产和 Linux 源码未应用原型，宿主没有修改或提交。

## 身份与复核

- disabled-fixed / fswinimagepreds32 SPIR-V SHA-256：`6ec78cb87f11f452e7c8b36c70cd325f81033c541faac48259175fc48fa6813f`。
- disabled-fixed / fswinimagepost32 SPIR-V SHA-256：`5c45fc50685b8ee7c18ef1aae0ad12d8ac764dbceabeec569d07e3f812c5d76b`。
- fetch / fswinimagepreds32 SPIR-V SHA-256：`12f53f41139774e30fcca7b75427f775ca5a9ff6181bc714cc8b2bff914e9e27`。
- fetch / fswinimagepost32 SPIR-V SHA-256：`778c9b48b4d7a4d8d5c2343633702a95fef0d564d2dd20e921eb8c762926c338`。
- prototype.patch SHA-256：`e89e8c058a0d549b2fa13af58dcadb388d16e40338dc00e18d092f30d49bea0f`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

源快照、原型、ISA、工具/产物哈希和全部私有结果在忽略的运行目录；本机定位见 local ledger。
只将本记录、索引、状态提交主仓；没有游戏、FSR4/FG、部署、安装、打包、发布、推送或外部消息。

下一步：研究 persistent 层参数重复加载；先将固定 DS/UPS 参数移出 item 循环，再根据实际寄存器与代码评估层参数缓存。
