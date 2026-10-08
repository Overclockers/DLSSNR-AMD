# EXP-076：最终图像层的空窗口提前退出

日期：2026-10-08（Asia/Hong_Kong）。结论：保留。
运行 `20261008-130553-swin9070xt` 第 1/10 项；1 版实现，1 次反序确认。
起点主仓 `814e80820c961a431fdb6a94863e60cf3d529825`，宿主 `9d1fa938563ce5cf7c06882d0134e6385f5e50eb`，继承 EXP-056/061。
宿主提交 `8aacc8b2b88b0a39a952d56d047b6139762dbb8a`，分支 customer-mochi-backed。

## 假设与实现

新鲜 standalone 诊断的 image post 为 0.9297 ms，派发 297×161 个窗口，而实际输出为 2258×1270。
其原 epilogue 对越界像素直接跳过；完全没有输出像素的窗口仍执行整个 Swin body。
在 fswinimagepost32 及继承它的 temporal_post_fp32 中启用 NR_POST_WINDOW_CULL，
当 `[8*gx+4*shift, +7] × [8*gy+4*shift_y, +7]` 与输出图像不相交时整组返回。
检查使用 group-uniform 整数，在所有 LDS/barrier 前执行。有效窗口保持原路径。
它是终端输出窗口裁剪，不是省略注意力项、跳过有效图块或改变模型 passes。

启用配方非 persistent、无 dump/padding 诊断；body 仅产生私有 LDS/寄存器结果，
没有依赖队列、激活输出或跨组副作用。最终输出与历史写入都受原来的同一个像素边界谓词控制，
所以被裁掉的组在原实现中也没有全局写入。所有有效像素的权重、归约、精度、转换、采样与控制保持。
CPU 对 1,422 个窗口、每窗 64 个 token 验证矩形谓词与逐像素谓词等价，覆盖正负 shifts、极小和边界尺寸。

## 完整离线 NR 测量

RX 9070 XT / AMD proprietary 26.8.1 (LLPC)，glslang 16.5.0、MSVC 14.51。
2258×1270 输入，2368×1280 工作尺寸，800 tokens、123 dispatches，100% 模型尺度、1 pass。
宿主线性 R11G11B10、depth=3，静态照片、无历史/SR/FG 性能负载。
计时是 D3D12 输入复制至输出复制，包含 Vulkan 往返；每批预热 100、测量 200 帧，
ABBA×3、BAAB×3，各有同路径控制×2，共 8,000 正式性能帧。全部批次保留。
ADL 每 0.5 秒采样频率、温度、活动率和功耗，包括初始化/预热，未与正式帧同步；未调频或改驱动。

| 比较 | 基线 ms | 候选/控制 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-cull | 10.977227 | 10.930387 | 0.046840 | 0.041574 / 0.044661 / 0.054286 |
| ba | 11.073904 | 11.015391 | 0.058513 | 0.066590 / 0.050112 / 0.058836 |
| confirm-control | 11.098977 | 11.104947 | -0.005969 | -0.004027 / -0.007912 |
| control-cull | 11.063850 | 11.063638 | 0.000212 | 0.001724 / -0.001300 |

六组 A/B 全部同向，分别超过本次控制组的绝对差。初测省 0.046840 ms，反序确认省 0.058513 ms；
登记较小批次均值 0.046840 ms。这不是保证下界，也不与过去不同会话的收益相加声称实测组合收益。

driver .text 24576→24704 B，静态指令 3312→3333；122 VGPR、66 SGPR、4096 B LDS、零 scratch 不变。
2135 条 vector 指令数量保持，但完整操作数/顺序并非逐字相同，正确性以独立输出和源级证明为准。

## 精确验证与构建

- 目标 standalone FP32 与 42 份宿主 packed 性能/检查末帧逐字节一致。
- 主仓、宿主各自的改前/改后 post 配方独立验证：各 15 对宿主回读、6 对照片/随机/HDR/奇数/小尺寸输出，
  双队列、重放、动态尺寸、历史顺序、延迟回收、多遍切换通过。
- 两套配方各 126 对目标历史输出精确一致，覆盖 13 种底层 VkFormat、非零亚像素 motion、变化 alpha、
  连续/丢弃/reset/乱序/replay；DXGI 别名复用未变的宿主映射，并非每个别名单独测量。
- 两套配方各 4 对边界输出一致：原生 1920×1080、1920×1536、2368×1280、3840×2160。
  2257×1269、129×65、17×19 在前述宿主检查中覆盖。
- standalone 1×1 的原基线选择了未随当前资产提供的 g_imgin16fast，尚未运行候选即失败；
  原日志保留。生产宿主 1×1 原基线随后在启动阶段停滞，限定完整命令路径停止了本轮这个测试进程；
  未运行 1×1 候选，也未声称 1×1/7×9/8×8 GPU 验证通过。小尺寸依赖已通过的 17×19/129×65 路由，
  极小窗口谓词另有 CPU 穷举；这些不可用的原基线路由不在本项性能/正确性结论内。
- 三套全量 shader 构建各 65 个模块，仅普通/temporal post 两个改变，其余模块和 markers 在各自配方内相同。
  主仓旧循环展开工具与宿主已有差异，post SPIR-V 跨仓不相同，因此将两套 post 前后分别放入相同已验证网络环境进行比较。
  主仓/宿主普通与 temporal 的四个关闭开关构建都逐字节还原各自基线；宿主最终普通模块等于实测版本。
  C++、ABI、packing 与资源/同步接口未改变，复用身份核验后的 production DLL；没有发布构建或游戏验收。

## 身份与复现

- host-before/g_fswinimagepost32.spv：`5c45fc50685b8ee7c18ef1aae0ad12d8ac764dbceabeec569d07e3f812c5d76b`。
- host-before/temporal/temporal_post_fp32.spv：`569a1024c1fbcf14afd52204bec391baeb57f244c98fa63f31085b5b24e40d4b`。
- host-after/g_fswinimagepost32.spv：`49ca36fb4685261eda156c5a3358ff63426c28c270a80a0a17cb809826cf65d2`。
- host-after/temporal/temporal_post_fp32.spv：`ca245fed475aa2db4256a0c9011f4f983bb20956c56f89804adaaf4238aa8b62`。
- main-before/g_fswinimagepost32.spv：`5367f4d55f2e72713d164a351db2fe5828d05b161e767ee0de6af010f4010f6d`。
- main-before/temporal/temporal_post_fp32.spv：`170945f64e05dc9924781742bd9354a54ad3ed845b75a2903608360a5f0e83b8`。
- main-after/g_fswinimagepost32.spv：`358fdbfd34ede2d90c985c21f5e76164504132186614a6b83261a7d9008d1e0f`。
- main-after/temporal/temporal_post_fp32.spv：`620063d838b669abaeebc11fb25cb462da3d84e02dc831410691a19f142feb14`。
- rectangle-proof.json：`eaf930cb19ffd9fd904f8db1c358d7bfe909650f0d47cb31a9b89b9120833b0f`。
- scope-proof.json：`a971d60a19350bd760b57750b6d05e286edbee36e1a68bd943c3be0af0848825`。
- build-closure.json：`8a123de0c2e95b3beb4fcc5e526fa3da586a3ade77c2d13f85fefe92d6f217ee`。
- validation.json：`d95f7be7b8f29a5b6957fffc9f3ade564220016d872de86dcc8dd9243e6d9cd9`。
- validation-main-after.json：`90fa4ffb0a5797ec1c3884607816bbc5778aa19d4665e0024294b162a9dbecaf`。
- target-validation-cull.json：`e00e7d63279a57f6bb46d6dd306baad569b9f4871e4b07adaf01cb92088babbd`。
- format-validation-cull.json：`e212b5f9251d8091080cc99d3ed6b2f1c08a91e09fe36c468deaa384cead4c5b`。
- target-validation-main-after.json：`f6dd6cfbdca3f34d04ed71d955e897c2aae635be3274621437b2dd55ed8531fb`。
- format-validation-main-after.json：`4572766d1cc88bf8ddd50f4b9502d66fe86861b1dd13aab18d5eafd6b923a6eb`。
- edges-validation.json：`e4086762b056af890f89b4d15129897828fb2838f32b8318aff5c73d73025151`。
- edges-validation-main.json：`64e45ce36049beb1df9b4e929c448c4dd98a80ddbab19f466c8bccece7a86aa6`。
- retained.patch：`393eb43f912b305fed21e1f78a384fcdc0fa2eab0e1ce47ca3c7c3327b014348`。

```text
python -X utf8 windows/build/build_network.py rdna4 --out <shaders> --glslang <pinned-glslang>
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
target_history_v2.exe <production-dll> <assets>
target_formats.exe <production-dll> <assets>
```

源码快照、原始 CSV、回读、模型、ISA 与完整命令留在忽略目录；本机路径见 local ledger。
下一项检查 persistent 任务边界的 LDS 同步，不修改数值计算。
