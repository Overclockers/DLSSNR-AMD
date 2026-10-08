# EXP-095：融合输入后相邻 Vulkan 屏障调用合并

日期：2026-10-08（Asia/Hong_Kong）。结论：未保留（unproven）。
运行 `20261008-162203-lossless9070xt` 第 9/10 个假设；1 版实现，1 次追加确认。
起始主仓 `cf8f2bf646e800a82c94daa2c924602a6d4ea5de`；宿主 `6a8e322828d8d1f1ece67f87bd1bb1982d4464d1`。

## 假设与实现

融合输入第一个dispatch后，tex_in图像GENERAL→SHADER_READ_ONLY转换与compute内存屏障紧邻，源/目标均为COMPUTE，中间没有GPU命令。原型仅将两份原描述放入同一次vkCmdPipelineBarrier；所有access、layout、subresource、队列忽略标志和dispatch保留，exec-only仍不带内存屏障但保留图像屏障，最后一步仍为完整屏障。不同于 EXP-077 的shader任务循环LDS屏障删除。依据 [Vulkan屏障作用域规则](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdPipelineBarrier.html)，本例前后命令与阶段作用域相同，图像转换仍保留自身的可用性/可见性依赖。

单独诊断DLL记录目标7次录制：每次第一步合成1个memory和1个image barrier，memory access为0→96，image为64→32，layout为1→5，符合原invalidate-only路径；每帧只减少一条API调用。route证据 SHA256 `0df92c9594669fa907dc8bb62599a2fdca81ce291c7f5430a20772e03d06f0c2`。关闭构建恢复原调用，.text749260字节；候选749388字节。全部shader资产不变，所有完整宿主packed回读与独立继承基线逐字节相同。没有运行不覆盖Runtime路径的standalone，也未推测GPU内部实际flush次数。三种DLL均由固定源码完整重编4个对象。

## 环境与测量

RX 9070 XT，Vulkan 新查询 AMD proprietary / 26.8.1 (LLPC)，固定 glslang 16.5.0、MSVC 14.51。
运行起点核验 65 个宿主源、77 个构建产物；独立原版与候选资产。
2258×1270 输入、2368×1280 工作尺寸、800 tokens、123 dispatches，100% 尺度、单次 NR。
本项修改 Runtime 录制路径，未运行不覆盖该路径的 standalone；完整宿主为线性 R11G11B10、depth=3，静态照片、无运动历史/SR/FG。
新鲜起点宿主四批：11.019102 / 11.015484 / 11.018076 / 11.025782 ms，在基线 4000 帧统一预热后，各预热 100、测量 200 帧。
没有修改时钟或驱动；时钟、温度和持续后台负载的采集范围见本项记录，缺失时不作稳定时钟假定。

完整路径计时覆盖 D3D12 输入复制到输出复制，包含 Vulkan 往返。每批预热 100、测量 200 帧；ABBA×3、同路径×2，若有 BAAB 则为唯一反序确认。全部批次保留。

| 比较 | 基线 ms | 候选/对照 ms | 节省 ms | 各组节省 ms |
| --- | ---: | ---: | ---: | --- |
| ab-merged | 11.071547 | 11.069456 | 0.002091 | 0.003232 / -0.002860 / 0.005902 |
| ba | 11.126911 | 11.122638 | 0.004273 | 0.007253 / -0.002789 / 0.008356 |
| confirm-control | 11.111269 | 11.103675 | 0.007594 | 0.007041 / 0.008148 |
| control-merged | 11.077921 | 11.078498 | -0.000577 | 0.002489 / -0.003642 |

按 EXP-080 的固定协议，每次候选 A/B 或反序确认开始前先运行原基线 4000 帧预热，随后 3 帧仅作输出检查；该调用不进入 A/B 均值，同路径控制紧随其对应 A/B。未按温度选择性删样或修正时间。

初测11.071547→11.069456 ms，仅省0.002091 ms；唯一反序确认11.126911→11.122638 ms，省0.004273 ms。两轮均有一组变慢；初测控制最大绝对组差0.003642 ms，确认同路径均差0.007594 ms、最大组差0.008148 ms，均未建立与波动可分辨的收益。unproven，不保留。

ADL 合计 615 个有效样本，核心时钟 77–2802 MHz，edge 39–60°C，hotspot 39–86°C。范围包含进程启动、预热与计时，未与正式帧对齐；没有据此筛帧或校正，不能据此假定计时段固定时钟。

保存 46 份宿主回读、8018 个測量帧；全部完整输出与独立基线逐字节比较。
未通过前置筛选，因此未扩大到其它格式、边界尺寸、目标运动历史、中间值、replay、fallback 和完整宿主集成验收。已有静态一致性不替代这些检查。
生产和 Linux 源码未应用原型，宿主没有改动；没有游戏测试、安装、部署、打包、发布或推送。

## 身份与复现

- prototype.patch：`97386aa2e59e491888bfb5f7fead456ac43c73884e4faf10313cb139c9ea124b`。
- disabled DLL SHA256：`27ce2197faadac68290bc9e2f2388972dc3b7f77f96f94e59a59b74fc3e4ee84`；构建源/参数清单 SHA256：`c4a2e84fe5f6411e085958ffb20bf998f0575548e0b7c6ef41b8fe4000373c9d`。
- merged DLL SHA256：`accc7500316c3f6c73af205c30d49160757a246920d49bbdbcfee51b55f622bf`；构建源/参数清单 SHA256：`a2e72122d0883230f7f35338bb9636c6504a2158fb0b7fc16c25ccfac448fb68`。
- trace DLL SHA256：`186753d3846f89708fdefc0f65043ed01bf5eb6a38766b99f078f4bea6b41d51`；构建源/参数清单 SHA256：`16c70e7969000bf643fcd7af45a809ca8781cf5356b960034ad2dd8a6faf2d57`。

```text
profile_host.exe <production-dll> <assets> <csv> 3 0 200 100 2258 1270 <output> <photo>
```

原始数据、源码快照、原型、工具和捕获留忽略的研究目录，本机定位见 local ledger。

下一步：最后一项检查合成着色器完整8×8工作组能否跳过逐像素越界比较；保留边缘组原判定，验证有效像素集合和两种输出格式。
