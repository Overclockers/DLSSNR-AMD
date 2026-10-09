# EXP-121：Activation槽位增加256-byte地址间隔

日期：2026-10-09（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261009-085523-newpaths9070xt` 第 5/5 个假设；1 版实现，1 次追加确认。
起始主仓 `a2e8241e8f3bdf589050cd45c691c272fffe57d3`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

为非空且原reservation小于8 MiB−256 B的activation槽位增加256 B尾部间隔，让并发数据流的低地址位不再全部相同。保持原精度、权重、shader、归约、量化、alias和生命周期；不改变cold-size分类。不同于EXP-005的cold4/cold4tail，本项不提高空闲块的复用距离或改变尾部保留策略，测试的是地址空间分布假设。只在忽略的Windows源码快照启用NR_ARENA_SKEW=1，关闭路径保留原reservation值。

历史 EXP-005 的布局显示184个activation槽位均为4 KB整倍数；当前宿主分配器主体与历史源码相同。新建CPU探针再次确认当前目标布局仍是184/184个offset % 4096 == 0。这是地址分布证据，没有采集cache/partition冲突计数，不能直接归因于硬件冲突。

| CPU布局尺寸 | 槽位/dispatch | 移动槽位 | arena额外字节 | 活跃范围重叠 |
| --- | ---: | ---: | ---: | ---: |
| 2258×1270 | 184 / 123 | 127 | 1536 | 0 |
| 1920×1080 | 184 / 123 | 162 | 1792 | 0 |
| 129×65 | 184 / 123 | 161 | 3328 | 0 |

目标arena为566067200→566068736字节，低12位由1种分散为13种，其中57个offset仍为4 KB整倍数。三种尺寸的payload大小、dispatch生命期、alias的owner/delta和256-byte对齐全部保留；每个reservation仍覆盖原payload及overread要求。只修改reservation大小，排序、free-list的cold-age阈值及尾部回收规则未变。

实际复用距离另作审计：目标与1080p两版均无小槽位gap≤1的复用。129×65两版各有一处原有top-tail例外允许的gap=1复用，槽位对分别为502→534和534→536；不存在活跃内存重叠。这项发现不等于正确性失败，也不能将“cold规则未改”写成所有实际复用距离相同。新增间隔会同时改变first-fit布局，不能将任何时间差完全归于cache颜色。

原版、disabled和skew各重编译四个DLL对象；CPU布局探针第一次因漏掉已有Windows dirent兼容头文件目录而编译失败，补齐相同生产include后成功，失败日志保留，不另算变体。布局打印只编入CPU工具，没有进入计时DLL。70份shader资产不变，唯一core源码差异为nr_graph.cpp。原版/disabled的.text相对继承production分别有21/23字节差异；不据此声称二进制等价。正式测量采用独立保存的production DLL，选择发生在任何GPU计时之前，没有删除测量结果。原版重编译、disabled、候选及正式基线的目标packed输出均与独立保存全图逐字节一致。

证据SHA256：

- layout-validation.json：`21fd63a845b6b9b2da8795803685b17c985615a1c21d3dd4bc9557b72d366257`。
- cold-age-proof.json：`07966afc7b562bac0e3bffd8621f2bfbab67b06e53c1f52a4a3d55652ec147b1`。
- source-audit.json：`143cf8007b51d129ae1ac2b532ac4af859c137d256f02361ff935da5cec8eb27`。
- baseline-selection.json：`18c19a0499943ecc1b0d344b05b379cf070ace815e3567da11a0cce7091053e1`。
- 历史布局CSV：`b237ba6274d2f5046e9cff63f70902d0a8665437619a52ee27190fb61db00691`。
- 正式production基线DLL：`7f8ae886f19c9f6d74ef23128cef190804d75fde9b7d8e2b06ab3d013f0caad6`。
- layout0-fixed工具：`20e141c0254f0125f4389250caa5d6b8b649a6e7d312cd2a4e4c87096f19f2d6`；构建清单：`482ea596c67a1718c023195916cae50f2571e27aac051c0cc59ecdbf0000e50f`。
- layout1-fixed工具：`fe4fdc5a78f2272185cd29f23a1ec3f2220df7e2d7b18ba27c13b8d0eb846615`；构建清单：`43a68e87484a75826ac9260986079129d5c1eb47473febb3512394d1c9b90abe`。

CPU布局复现：按宿主rdna4.sh的相同defines构建layout工具，两版分别使用NR_ARENA_SKEW=0/1、NR_LAYOUT_TRACE=1，包含nr_native_plan.cpp和已有Windows兼容目录；工具在arena_probe返回后结束，不创建Vulkan device。

```text
layout <width> <height> <plan> --reuse --model-pack <private-model> --spv-dir <saved-production-shaders>
```


## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
本项使用独立重编译 runtime DLL；没有运行 standalone GPU 基准；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.978340 / 11.019070 / 11.008404 / 11.006539 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-skew | 11.040329 | 11.037140 | 0.003189 | 0.015571 / -0.007292 / 0.001289 |
| ba | 11.078860 | 11.093992 | -0.015131 | -0.012718 / -0.011669 / -0.021008 |
| confirm-control | 11.085836 | 11.082139 | 0.003697 | 0.002534 / 0.004860 |
| control-skew | 11.045171 | 11.050640 | -0.005468 | 0.002081 / -0.013018 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

unproven，不保留。初测11.040329→11.037140 ms，平均省0.003189 ms，三组方向混合。唯一反序确认11.078860→11.093992 ms，三组全部更慢，平均慢0.015131 ms；确认同路径控制平均差0.003697 ms。地址低位分散已实现，但完整路径收益未复现，不追加第三轮。

ADL 合计 598 个有效样本，核心时钟 78–2812 MHz，edge 36–59°C，hotspot 37–83°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 46 份宿主回读、8018 个測量帧；全部完整输出与独立基线逐字节比较。
已完成三种尺寸的CPU布局/alias/lifetime审计与目标静态packed全图检查。每次GPU调用末尾回读一次，没有逐帧保存全部计时帧。性能未通过，因此未扩大到GPU其它尺寸/格式、中间张量、运动/历史、replay、多队列、释放重建、fallback、第二源码树和完整宿主集成验收；CPU布局检查不替代shader动态越界验证。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- prototype.patch：`b92f93c085c90704e7adea11f762f9e7c3eb4ee0657fdde51f317593c028601a`。
- disabled DLL SHA256：`88bfd232cd8d5c275076a4551bf67f6da92f44ca1e75051839aba8f044c0c528`；构建源/参数清单 SHA256：`55afccd7f18db087ba1e1bf61d8c2423a52536cf36684055a344baf44cc4957f`。
- original DLL SHA256：`174aa2b40bb65924bf5ee0024f4f4c6d3a5bdc82e8bb31b3960b8a22d5a39dcb`；构建源/参数清单 SHA256：`51a03e14f04989781a45bec2cc674882f86750193a4a76ad9230e503f811b39c`。
- skew DLL SHA256：`d10713639fa8807365ecf907421023bff1d061a1a7552c1ce6201e356aed4697`；构建源/参数清单 SHA256：`4c88debc4de12304530738e588d36a8aaa5bc445ce33c17ad84b2f73aa9620f1`。

```text
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：本轮5次预算耗尽，结束运行。后续新预算应先将EXP-028已验证的C256 DS加载等待细分到当前产物的具体load与消费者依赖，再选择独立调度或局部性实验；这只是后续研究建议，尚无新增收益证据。避免继续扫描本轮已否定的面板、wave大小或spacing参数。
