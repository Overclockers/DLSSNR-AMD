# EXP-064：相邻不同 kernel 的兼容 buffer 绑定复用

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261008-003019-five9070xt` 第 4/5 个假设；1 版实现，1 次追加确认。
起始主仓 `5a27e97680ff51007a85f5bcb2bf913ab3642fe5`；宿主 `9d1fa938563ce5cf7c06882d0134e6385f5e50eb`。

## 假设与实现

在 Kernel 创建时保存纯 buffer 的有序 VkBuffer handles 和 push range 大小。每遍网络循环以空缓存起步，只有前后均为纯 buffer、资源与范围相同、没有自定义 set 时省略 descriptor bind；image/alternate set 清空缓存。pipeline bind、push、dispatch、每个 barrier 均保留，各 set 的原有池和生命周期不变。EXP-005 只针对同一 kernel 的 4 处重复，本项跨不同 kernel；EXP-041 的 secondary command buffer 不在本项范围。

[Vulkan 规范的布局兼容规则](https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html#descriptorsets-compatibility)要求 set 0 定义、independent-sets 属性和 push constant ranges 一致。这里 create 固定 flags=0、compute 阶段、连续 binding、单个 storage buffer、offset=0/range=WHOLE_SIZE；签名还比较相同 device 和实际 buffer 顺序。只读审计没有发现这些纯 buffer set 在创建后被重写。

独立未改源码重建原版核心 DLL，与候选、带计数的诊断 DLL 各保存完整 build-source 身份；复用已核验且不依赖 Kernel 布局的 ABI/adapter objects。未修改 shader；全部 65 个 SPIR-V 与当前基线完全一致。诊断共 7 次网络录制，每次 123 个 dispatch 均跳过 34 个 descriptor bind，保留 89 个。三种 DLL 的完整静态照片 packed 输出均与独立已存基线逐字节一致。
计数 DLL 只用于机制检查，正式计时不带计数打印。只读 ADL 每 0.5 秒采集时钟/温度/活动率，覆盖初始化和预热，未与正式帧严格对齐。

- baseline DLL SHA-256：`8af22bf74215307dbd85d99e19431ef82d4a81260c6b8eb7425230e15695476c`。
- reuse DLL SHA-256：`27a43c43985de10571fdbb3563d274d05ea757a33afee07a76109af4dee21ab4`。
- diagnostic DLL SHA-256：`6a17131a4f85036c3b89174940ba86215ffc66d20e52f41d00556ad035fad0b1`。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
本项不运行 standalone（改动仅在 Runtime 录制路径）；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.625679 / 10.650643 / 10.664368 / 10.666902 ms，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-reuse | 10.690588 | 10.688915 | 0.001673 | -0.005840 / 0.004446 / 0.006413 |
| ba | 10.740106 | 10.730057 | 0.010050 | 0.012709 / 0.019549 / -0.002109 |
| confirm-control | 10.788723 | 10.787529 | 0.001194 | 0.000212 / 0.002176 |
| control-reuse | 10.749757 | 10.767201 | -0.017444 | -0.019462 / -0.015425 |

初测节省 0.001673 ms，各组方向不一致；唯一反序确认节省 0.010050 ms，其中一组反而慢。初测与确认同路径控制最大绝对组差分别 0.019462 / 0.002176 ms。实际减少绑定尚未形成稳定且可分辨的完整路径收益，判为 unproven，不保留。

保存 43 份宿主回读、8009 个測量帧；全部完整输出与独立基线逐字节比较。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- prototype.patch：`85ef75a36ca445c507b9adb3c9568220d76dfba1db9c80a9c65a48ad31ce9c53`。

```text
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：针对缓存 noise 的 half→float→half 往返，先以全部 half 编码和实际驱动 ISA 验证直接 half 传递，再测试图像输入路径。
