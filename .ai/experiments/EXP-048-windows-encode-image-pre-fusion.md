# EXP-048：线性编码与 image pre 融合

日期：2026-10-07（Asia/Hong_Kong）。结论：**保留；完整离线路径初测省 0.028813 ms，反序确认省 0.026916 ms。**
运行 `20261007-152904-schedule9070xt` 第 19/20 个假设；1 版实现、1 次追加确认。
基线已包含本轮保留的 EXP-044；本项收益是对此基线的增量。

## 问题与实现

原路径先由 runtime_encode_direct 写出 RGBA32F proxy，再由 fswinimagepreds32 采样做特征准备。
新增 runtime/runtime_preencode.spv，作为原 pre 的 runtime alternate：直接采样原 R11G11B10，
按完全相同的 FP32 白点、luma knee、sRGB 算术编码，再执行原特征准备和网络运算。
每个有效原图 texel 仍写出 FP32 proxy，供原 post/compose 使用；只省去独立 encode dispatch
及 pre 对 proxy 的首次读取。每个 8×8 组覆盖独立像素，padded 位置沿用原反射坐标和 nearest sampler，
重复编码其对应源像素但不写 proxy；proxy 每个有效位置恰好一个写入者。

它沿用 fswinimagepreds32 已打包的权重、noise table 和 124 字节 push，不添加图节点或改变 packing。
新增 16 字节 storage control buffer 通过 GPU update 写入，并保留前后 compute/transfer 依赖，
避免 host 更新与已录制或正在执行的帧竞争。proxy 的 write→read 布局转换移至首个融合 dispatch 后。
绑定由已有 LinearInputBinding 的 descriptor pool 持有，调用方完成 tail 后释放。

条件为单 pass、同尺寸、非 temporal/mask/preprocess 的直接 packed R11G11B10 路径；
其它格式、配置或缺少可选模块时沿用原 encode。没有新低精度边界、重排归约或改变模型参数。
性能原型的首次路径日志已从保留代码移除；清理后的新 SPIR-V 与实测版本逐字节相同，GPU 命令顺序未改。

主仓基线 `93214905e2e6339895020f8d0342cfcbf8483925`，宿主基线 `137b3859a7572bee73c50a6975ab87f11adc4abf`。
宿主保留提交：`1cc36f96205e361afe5242ea2c330f84b5485b2a`。两个核心文件独立应用补丁，保留宿主专有进度、取消和计时接口。

## 环境与性能

RX 9070 XT，AMD proprietary / 26.8.1 (LLPC)，glslang 16.5.0、MSVC 14.51。
2258×1270 线性 R11G11B10 照片，2368×1280 工作尺寸、800 tokens、123 个网络 dispatch；
scale 1、单次 NR、depth 3，无运动历史/SR/FG 性能负载。
计时覆盖 D3D12 输入复制至输出复制的完整 NR 区间，独立进程交错运行。
每批预热 100、测量 200 帧；ABBA×3、BAAB×3，各自同路径控制×2，所有批次保留。
时钟、温度和持续后台负载未采样；未修改时钟或驱动。

| 批次 | 基线 ms | 候选/控制 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-fused | 10.775862 | 10.747049 | 0.028813 | 0.030968 / 0.034919 / 0.020552 |
| control-fused | 10.841404 | 10.860029 | -0.018626 | -0.020362 / -0.016889 |
| ba | 10.789968 | 10.763051 | 0.026916 | 0.019989 / 0.024773 / 0.035987 |
| confirm-control | 10.859916 | 10.853463 | 0.006453 | 0.005174 / 0.007732 |

六个 A/B 组均为正。初测最小组收益 0.020552 ms，仅略高于该轮控制最大绝对组差 0.020362 ms，
因此追加唯一反序确认；确认最小组收益 0.019989 ms，控制最大绝对组差 0.007732 ms。
两轮 4800 个 A/B 测量帧的同口径均差为 0.027865 ms；控制另有 3200 帧。
这是本项的离线增量，不将其与 EXP-044 的独立会话均差相加宣称已直接测得组合收益。

匹配的仅编译 pipeline probe：原/融合 pre 均为 124 VGPR、6144 B LDS、零 scratch，SGPR 45→46。
这些值只适用于相同 probe 创建条件，不混用其它 standalone 编译统计。

## 精确验证和集成

- 原型的禁用宏编译与原 pre 字节相同；真实融合路径由私有首次日志确认，目标 packed 输出一致。
- 原核心与候选分别运行 15 对序列输出、6 对完整输入与 normal pass 切换，逐字节一致。
  序列覆盖 RGBA16F、R11G11B10、RGBA8 sRGB，replay、双队列、resize/保留、延迟 collection、
  history 顺序、DRS、取消及已完成 tail 的销毁；额外输入为 2258×1270、2257×1269、129×65、17×19 梯度，
  以及目标尺寸随机棋盘和 HDR。
- 129×65 的有限 R11/G11/B10 边界模式及包含 Inf/NaN 的模式，各测白点 0.01、1、2、100：
  8 对 FP32 proxy（每图 33540 个 32 位值）和最终 packed 图均逐字节一致。
  读回在已完成 tail 后进行，诊断副本没有用于计时或进入生产源码。
- 最终宿主源码通过标准 profile_host.py --production 全量离线构建和 ABI 检查。
  同一最终 DLL 带/不带可选模块再独立比较上述 15 对序列、6 对输入及 pass 切换；全部一致。
  缺少模块的输出也与保存的原核心独立基线逐字节一致。
- 主仓 C++17 独立编译通过。主仓前/后与宿主的完整 shader 构建分别为 64/65/65 个模块；
  原有 64 个模块及 marker 在各自配方内不变，宿主新模块与实测模块相同。
  两仓生成的新模块不同，因此将主仓原 pre 与主仓融合 pre 分别放入匹配宿主路线，
  再比较 15 对序列、6 对输入、pass 切换及全部 8 对 FP32 proxy/packed 边界模式，全部一致。
  这不是主仓独立 Windows 安装包或游戏验证。
- 分发只作源码和构建清单核查：正常 builder 生成新模块；主仓 assemble_product_data 的 runtime 通配复制、
  宿主递归资产复制/安装以及 manifest 枚举均覆盖新模块；宿主显式卸载列表已补项，PowerShell 语法解析通过。
  没有执行安装、卸载或发布打包。EXP-012/015 旧 runtime 分发/卸载审计仍需另行完成。

严格一致性来自保留算术和数据流的论证及实际精确回读；未用误差容差或图像相似度放行。
Linux 未改；无游戏、FSR4/FG、发布包、部署、推送或外部消息，不能宣称游戏 FPS 收益。

## 身份与复现

- 原型补丁：`8c3027b3458dc9b5d2cbd8a4056d162258b1ef796c52793a9253f2458d8f9bf8`。
- 最终宿主核心补丁：`30fdc508dddab2f9eebd52f0e681a7cd83f0a8e02514ff8e37232590dc682530`；主仓补丁：`140dd6433da243901190b4d6b22d87e8196485a39aeed1517fb9e2e225ee87aa`。
- 宿主融合模块：`34a29bfd739eddb50f8cb546b6a42fdaa2d4ba728b8ff4054054ce1c345febdc`。
- 主仓融合模块：`8db34b9186526e4131ced5d5fa4a17d1b3658fedcf1cf942ee16aaea4cb7dab6`。
- 最终 DLL：`7f8ae886f19c9f6d74ef23128cef190804d75fde9b7d8e2b06ab3d013f0caad6`。
- 全量离线构建清单：`204460484d24b4f61e4074752eefd602f8d6c4958a25954c8ee7997c7eba2c64`。
- 原型扩展对照：`23c49bac97dcf15b49df9a3696fbf3801bc752d27b1bb15512d2eba8b6a75806`；最终对照：`2d75bb37ad4956c1760baaf82517db98b0facd78ea6001e61b67f93c7e38e7fb`。
- 主仓独立对照：`0b86d55878ddc278d994523b364672ef9a2948bfeae6c78d4f091216c9ed1769`。
- 宿主/主仓 proxy 报告：`b52d4340efe1d3e27ea6a18394fb8eb2aeaf9c10bfaefd961c3b5c43cbe6c163` / `d9e4d4a2216a36ae608768029e5c442a4137d153e411ad1f372a39c6e4dc6aa7`。

```text
python -X utf8 windows/build/profile_host.py --host <host-checkout> --out <private-offline-build> --production
python -X utf8 windows/build/build_network.py rdna4 --out <private-shaders> --glslang <pinned-glslang>
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
host_contract.exe <production-dll>
host_contract.exe <production-dll> <assets>
host_contract.exe <production-dll> <assets> --pass-switch normal
```

原始日志、控制白点/FP32 proxy 夹具、源快照、CSV、DLL 和回读均在忽略目录，本机位置见 local ledger。
下一步：以 EXP-044/048 为新基线，测试 image post 的有限 RGB 权重面板读取，保持原通道归约顺序。
