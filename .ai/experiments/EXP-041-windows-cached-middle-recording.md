# EXP-041：固定网络步骤的命令录制复用

日期：2026-10-07（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261007-152904-schedule9070xt` 第 12/20 个假设；1 版实现，1 次追加确认。
主仓基线 `efcb9df2ab6e461bfec39388d4962e81db8a10a0`；宿主 `3a88c92d7d85933cf7c290cbb9fa25f9faa6d688`。

## 假设与实现

在私有 Runtime::Impl 中缓存中间 121 个网络 dispatch 和各自原有 compute barrier，输入/输出步骤仍逐帧录制。仅单遍、无 mask/temporal、steps 与 disp 一一对应且中间无 controls 时使用；其它情况原路径回退。secondary command buffer 使用同一 queue family 和 SIMULTANEOUS_USE，完整绑定 pipeline/set/push；缓存建立失败释放临时池并回退，析构在 caller 已完成提交后先释放池。缓存的是命令，所有模型计算仍每帧执行。

源码检查确认 controls 只改 pre/post push，网络中间 descriptor、push 和调度尺寸在 session 内固定。[Vulkan 执行 secondary 的规则](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdExecuteCommands.html)要求匹配 queue family 与有效继承信息，多份在途 primary 引用使用 simultaneous-use；本原型按这些规则录制，timestamp 留在 primary。单独插桩诊断各 200 帧，CPU Vulkan record 从 0.064367 降至 0.041291 ms，但这两批不是交错性能验收，也未证明完整路径变快。使用独立原版核心 DLL、原版 shader；网络 SPIR-V 未修改。

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
| ab-cached | 10.813123 | 10.820375 | -0.007252 | -0.012385 / -0.013850 / 0.004478 |
| ba | 10.876695 | 10.885110 | -0.008415 | -0.003283 / -0.023496 / 0.001534 |
| confirm-control | 10.909770 | 10.904534 | 0.005237 | -0.000983 / 0.011456 |
| control-cached | 10.891693 | 10.889597 | 0.002097 | 0.000700 / 0.003493 |

原路径初测平均增加 0.007252 ms；唯一反序确认增加 0.008415 ms，两组慢、一组略快。确认控制均差为 +0.005237 ms，组差 -0.000983/+0.011456 ms。CPU 录制工作减少未转化为可重复的完整 NR 收益，判为 unproven，不保留。

另保存 44 份原独立宿主回读、8406 个测量帧；回读逐一与独立基线比较。
保存 0 次 standalone 结果（其中 0 份与基线完全一致）；具体失败见上文。
未扩大到中间值、其它格式/边缘尺寸、history/replay、fallback 及宿主集成验收，因为候选未通过前置筛选。
静态输出检查不代替这些未测项。生产和 Linux 源码未应用原型，宿主没有修改或提交。

## 身份与复核

- prototype.patch SHA-256：`ba62422241ffac59b6242b1d327a52074711a1d0acf1330567c641cfe4e774f5`。
- baseline DLL SHA-256：`c380d0095d8fad4ff6f71d992b1742863395fbb3ebcd1ac199dec2a636765c96`；源快照、原编译配方及重用 adapter object 哈希保存在 build-source.json。
- cached DLL SHA-256：`617c239d9e53429c1046e02fb0c26c7b92c49966437ae18affd64e4297e4494f`；源快照、原编译配方及重用 adapter object 哈希保存在 build-source.json。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

源快照、原型、ISA、工具/产物哈希和全部私有结果在忽略的运行目录；本机定位见 local ledger。
只将本记录、索引、状态提交主仓；没有游戏、FSR4/FG、部署、安装、打包、发布、推送或外部消息。

下一步：检查图像 pre/post 的中心点纹理采样，使用带尺寸/边界回退的整数 texelFetch，保持原采样值和格式精度。
