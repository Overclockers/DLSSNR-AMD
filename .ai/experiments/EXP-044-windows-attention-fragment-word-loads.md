# EXP-044：Attention 片段按对齐 8 字节读取

日期：2026-10-07（Asia/Hong_Kong）。结论：**保留；完整离线路径初测省 0.021130 ms，反序确认省 0.030290 ms。**
运行 `20261007-152904-schedule9070xt` 第 15/20 个假设；1 版实现、1 次追加确认。

## 问题与实现

C512 attention 的 QKV 输入及权重从两个 fe4m3vec4 构造片段；实际 LLPC ISA 在 8 字节读取后
仍留下 176 条 `v_perm_b32 ..., 0x3020104` 恒等重排。改用原 binding 的 uvec2 视图一次取出
8 字节，再按原顺序 unpack8。构造后的 A/B 片段、每个 MMA 的输入与 K 归约顺序均不变。
地址来自 256 字节 tile、16 字节行与 8 字节半 lane 偏移，保证原始访问为 8 字节对齐；
没有多读相邻元素、修改权重布局、引入精度/舍入边界或增加 descriptor binding。

只改 Windows 的 attn.comp，并同步宿主副本。没有新 kernel 名称或 host define；不需要新增 packing。
ViT 的同类线索未实施，不另计一次尝试，也不计入本项收益。
初次构建补上 unpack8 临时值需要的 int8 GLSL extension 后成功；属于同一实现的构建修复。

主仓基线 `5c7f001ec0594c363f86586e81b96f6488b48500`，宿主基线 `3a88c92d7d85933cf7c290cbb9fa25f9faa6d688`。
宿主保留提交：`137b3859a7572bee73c50a6975ab87f11adc4abf`。

## 证据与性能

RX 9070 XT，AMD proprietary / 26.8.1 (LLPC)，glslang 16.5.0；已核对的 MSVC 14.51 production DLL 未变。
2258×1270 线性 R11G11B10 照片，2368×1280 工作尺寸，800 tokens、123 dispatches，scale 1、单次 NR、depth 3。
静态输入，无运动历史/SR/FG 性能负载。时钟、温度及持续后台负载未采样；未改变时钟或驱动。
计时是 D3D12 输入复制至输出复制的完整 NR 区间，非 standalone 算子时间或游戏帧时间。
每批预热 100、测量 200 帧；ABBA×3、BAAB×3，以及各自同路径控制×2；保留所有批次。

| 批次 | 基线 ms | 候选/控制 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-words-fixed | 10.802137 | 10.781007 | 0.021130 | 0.023933 / 0.019751 / 0.019706 |
| ba | 10.837822 | 10.807532 | 0.030290 | 0.046969 / 0.029697 / 0.014204 |
| confirm-control | 10.890991 | 10.887918 | 0.003073 | 0.011640 / -0.005494 |
| control-words-fixed | 10.869300 | 10.876532 | -0.007231 | -0.006617 / -0.007846 |

六个 A/B 组都更快；初测最小组收益 0.019706 ms，大于对应控制最大绝对组差 0.007846 ms；
确认最小组收益 0.014204 ms，对应控制最大绝对组差 0.011640 ms。控制不是普适误差上界。
两组实验方向一致，合并相同口径的 4800 个 A/B 测量帧平均节省 0.025710 ms，约 0.238%。
控制另有 3200 个测量帧，不混入收益均值。

driver ELF 的 .text 从 29056 降至 26880 字节，静态指令 4080→3882；176 条恒等重排全部消失。
VGPR 182→184，SGPR 59、LDS 9216 B 保持，scratch 为 0。
这些结构证据解释候选机制，不能单独代替完整路径测量。

## 精确验证与构建

- 开关禁用时重编译 attn SPIR-V 逐字节复现独立基线；清理后的保留 shader 与实测候选逐字节相同。
- standalone FP32 全图一致；42 份宿主 packed 回读一致，包含 A/B/control 全部批次末帧。
- 宿主原配方和候选分别重新生成/执行，15 对序列输出逐字节相同；覆盖 RGBA16F、R11G11B10、
  RGBA8 sRGB，14 次 replay、双队列、尺寸保留、延迟 collection、history 顺序、DRS、取消及已完成尾部销毁。
- 另 6 对完整输入一致：2258×1270、2257×1269、129×65、17×19 的梯度，以及目标尺寸随机棋盘、HDR。
  原版和候选均通过 normal 多遍切换。
- 三套完整 Windows shader 构建各 64 个模块：宿主清理后、主仓原版、主仓改后。
  各仓库只有 attn 模块改变，另外 63 个模块及 marker 不变。
- 主仓与宿主已有构建脚本差异，其 attn SPIR-V 不同。因此将主仓前后各自的 attn 放入匹配宿主路线，
  再独立比较上述 15 对序列、6 对输入和多遍切换，全部通过；没有用宿主产物冒充主仓原生包验证。

数值论证是原始字节与算术序列保持，精确回读用于检验实际编译路径。没有使用容差或图像相似度放行。
未进行游戏、FSR4/FG 或发布包验证；没有安装、部署、打包、推送或外部消息。Linux 未改。

## 身份与复现

- 原宿主 attn SPIR-V：`40419b2374b7d74f142c0c14063a2ff5cd47b0927ff768aa4e84864f9c4ad1d0`。
- 保留宿主 attn SPIR-V：`4bafe70c5fa393acc1b0d6c1f53715a48f1cc6b9adf137d42d23caabdf6c62dc`。
- 保留主仓 attn SPIR-V：`8f38a694ce610129fcf8b33878ecca8e4ffc8477555eaa8eda3474bc4ad4de78`。
- 保留源码：`d7b1a10a8dbfe7fb57550aec876c8b595c84e5cf05ed5e21c4f86659cf0d1d88`；补丁：`407fc4227cc4d90b4ffaa05f9b12d5d5483d174ead1a88d780de05e417fa65d2`。
- 扩展验证报告：`caa488980ef933e02c971e6bb352a387a0ec6cbb0d2763825e446a876cf26e63`；主仓验证：`3e8a3879ba594d59dcb1a77669c0c8b4d4d632440aaa8d1232616bf88bb48e12`。
- 构建闭包清单：`b5226a2d2fc672969d4b2da0a6d083df8d8da29207742716d024eed978616b87`。

```text
python -X utf8 windows/build/build_network.py rdna4 --out <private-shaders> --glslang <pinned-glslang>
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
host_contract.exe <production-dll> <assets>
host_contract.exe <production-dll> <assets> --pass-switch normal
```

源快照、原型、driver ISA、完整 CSV、精确回读、捕获夹具及身份留在忽略目录，本机位置见 local ledger。
下一步：在这项新基线上研究 persistent 参数记录的 16 字节向量读取；只改变读取指令，不延长跨 item 存活期。
