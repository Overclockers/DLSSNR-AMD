# EXP-052：ViT 扩张/收缩的分块依赖

日期：2026-10-07（Asia/Hong_Kong）。结论：未保留（regressed）。
运行 `20261007-214238-codegen9070xt` 第 2/10 个假设；2 版实现，1 次追加确认。
起始主仓 `a38c212a0deab9bd9a281e7d27747c59a01d03e8`；宿主 `1cc36f96205e361afe5242ea2c330f84b5485b2a`。

## 假设与实现

从真实图截取第一组 M=800、K/N=1024/4096 的 ViT 扩张与收缩，复用原打包权重及实际 FP8 输入/残差。使用四个独立、不别名的缓冲区范围；原输出先以 0xcd 填充。候选以 device-scope release/acquire 的完成计数器替代两 dispatch 间屏障，每次测量前在时间戳外清零计数器。每个消费工作组只等待自己的 16-token 块，最多轮询 2^20 次并写错误字。第一版按工作组发布；第二版改为 Linux 风格按 wave 发布，消除新增工作组 barrier，计数阈值相应从 16 改为 64。这区别于 EXP-005 的 barrier scope 调整。

禁用宏的两份 shader 与独立原版 SPIR-V 相同。四次调用中原版/控制/候选共 12 份输出及 12 份中间 FP8 张量精确一致，输入与 guard 没有改写，完成计数和 timeout 均正确。每调用先完整网络预热 100 帧，pair 再交替预热 128 次；12 blocks×4 positions×32 samples，其中八块为正反顺序 A/B、四块为同路径控制，共 6144 个局部计时样本。每对独立提交/等待，计数清零在计时外；不与全网每层或宿主时间混减。

| 版本/批次 | 原算子对 ms | 候选 ms | 节省 ms | 控制最大绝对组差 ms |
| --- | ---: | ---: | ---: | ---: |
| 工作组发布初测 | 0.128159 | 0.150326 | -0.022167 | 0.002872 |
| wave 发布初测 | 0.128638 | 0.145223 | -0.016585 | 0.002729 |
| 工作组发布确认 | 0.128702 | 0.151079 | -0.022377 | 0.004062 |
| wave 发布确认 | 0.126904 | 0.145989 | -0.019085 | 0.001367 |

ADL 只读每 0.5 秒采样，覆盖进程启动/编译/预热及测量；原始样本留私有，不能视作单帧同步遥测。GFX clock {'min': 41, 'max': 2822, 'samples': 84}；edge temperature {'min': 38, 'max': 43, 'samples': 84}。未改时钟、驱动或其它进程。

独立夹具构建补齐了既有 Windows sysfs guard 和宿主进度回调空实现；不改网络数学。原始夹具扩展 producer-units 时其 executable 被重建，首版源码已按原哈希还原；追加确认两份报告均核对为同一已保留程序。完整身份及一次换行哈希检查修复记录于 ledger。冻结程序 SHA-256：`839dd610461d5e0d74d35db00e5f27facac3e2a60a355055631203bbc9fe60cd`。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
standalone 为 FP32；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：10.640660 / 10.627673 / 10.667158 / 10.678415 ms，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

没有进入完整宿主性能验收；诊断时间不作为提速结果。

两版各八组局部 A/B 均变慢；唯一追加确认中，工作组发布平均慢 0.022377 ms，wave 发布平均慢 0.019085 ms。全部方向与控制差距支持本算子对原型退步；未提升到完整宿主。此结论只覆盖该 ViT 对与两种发布实现，不证明所有 Windows 分块依赖方案无效。

保存 0 份 standalone 数值结果，其中 0 份一致。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- disabled / gemmvact SPIR-V：`c48c1619dde7995664f7b0e6e34c4bd51abf0d6b20cac70b65daf186b01efbdb`。
- disabled / gemmprojws SPIR-V：`e0c3e40657cb29894195d127fccd9df6e5d03507881f216e804c31e96ae60143`。
- tile / gemmvact SPIR-V：`5992e9105a626e357674db72c49b7083193bf1fa40ddd26156fbc4a0dbb126fe`。
- tile / gemmprojws SPIR-V：`9ba9a3a22abe81de0c880560dec65fef15e959adabbd301dfe5522113aef561f`。
- tilewave / gemmvact SPIR-V：`560a5f4919aa97f4fa63ed80823292132f6a9a014e04cd85c57462e3e2aa17fe`。
- tilewave / gemmprojws SPIR-V：`9ba9a3a22abe81de0c880560dec65fef15e959adabbd301dfe5522113aef561f`。
- prototype.patch：`31443167c1a26fa0fb4fdc7683954e2c0ce349481491702dc8c095fdc6ed8b8b`。
- wave-variant.patch：`793df865dd0ed5c1741a8d33187b4d2c5eb5aef4c385b231b75c540a0da85ede`。

```text
nr_benchmark 2258 1270 <plan> --reuse --model-pack <private-model> --spv-dir <shaders> --in-image <photo> --out-image <rgba> --warmup 3 --repeats 3 --per-layer
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：验证保持原 shader 与逐层屏障的 token 分块执行：缩短 ViT 中间激活的生产到消费距离，避免本实验的原子轮询成本。
