# Sm8450Ufs —— cupid（小米 12 / SM8450）存储组件

来源：`Chengdong-Yu/pong-ufs`（BSD-2-Clause-Patent），按 cupid 板级事实改造。目标只有一个：**让内核在关 8 不再报 `Failed to mount root device`**。

## 目录内容

| 文件 | 作用 |
|---|---|
| `Sm8450Ufs.cpp` | IOService 前端：匹配 `qcom,sm8450-ufshc`、校验 DT 里的板级事实、映射 GCC 页与 UFS host、读寄存器快照 |
| `Sm8450UfsTransport.inc` | 传输层：接管 ABL 已启动的控制器 → SMMU 接管（stream 0xe0）→ NOP / 电源模式 / TUR / READ CAPACITY → 解析 userdata GPT 与 SHNGROOT 清单 → 发布 `IOBlockStorageDevice` 窗口 |
| `CupidProbe.h` | 诊断桩（pong 那份本来就是空桩，只改名） |
| `RootLayout.h` `RootCommands.h` `RootQueue.h` `RootDiagnostics.h` `RootTransportLayout.h` `TransferPoll.h` `SmmuTakeover.h` | 原样沿用：GPT/清单解析、UTR D/UPIU/PRDT 命令、SMMU 规划 |

单编译单元设计：UniXNU 的 `scripts/build-arm-driver.py` 每个组件只编译一个 `.cpp`，所以传输层以 `.inc` 在命名空间 `sm8450_ufs_impl` 内被包含进来。

## 与 pong 的差异（只有三处）

| 项 | pong / SM8475 | 这里 / SM8450 | 依据 |
|---|---|---|---|
| `compatible` | `qcom,sm8475-ufshc` | `qcom,sm8450-ufshc` | 上游 `sm8450.dtsi` ufshc@1d84000 |
| DT 属性前缀 | `pong,` | `cupid,` | 本仓 `dts/cupid.dts` |
| userdata 4K LBA | `0x413248` / `0x75a40ea` | `0x306000` / `0x3AAD7FA`（3170304 / 61528058） | `board.toml [userdata]`，实机 2026-11-07 |

其余全部相同，且都有上游依据：

- UFS host `0x1d84000` 长 `0x3000`、中断 SPI 265 —— `sm8450.dtsi`
- apps SMMU `0x15000000` 长 `0x100000` —— `sm8450.dtsi`
- UFS 的 SMMU stream id `0xe0` —— `sm8450.dtsi` 的 `iommus = <&apps_smmu 0xe0 0x0>`
- GCC UFS 页 `0x187000`，分支 `0x87018…0x870d4`，GDSC `0x87004` —— `gcc-sm8450.c`（本仓 `package/Sm8450Ufs/Sm8450UfsRegs.h` 已录）

**注意**：本组件**不初始化 QMP PHY**（`0x1d87000`）。它假设 bootloader 已经把 UFS 跑起来（ABL 要从 UFS 读 boot，这是成立的），只做接管。所以 SM8450 与 SM8475 的 PHY 差异在这一步不构成阻塞。

## 接线（三处，已生成补丁）

1. `0001-cupid-storage-component.patch`（设备仓）
   - `board.toml`：`[components] include` 加 `Sm8450Ufs`；`[kernel] archives` 加 `component:Sm8450Ufs`
   - `dts/cupid.dts`：`&ufs { status = "okay"; cupid,* = … }`，把九个板级事实写进 ADT
   - `.github/workflows/macos.yml`：新增 "Stage the board storage component" 步骤，把 `components/Sm8450Ufs/*` 拷进 `UniXNU/drivers/Sm8450Ufs`
   - `components/Sm8450Ufs/`：本目录源码
2. `0002-unixnu-sm8450-ufs-component.patch`（UniXNU 框架）
   - `components.toml` 注册 `Sm8450Ufs`（`path = drivers/Sm8450Ufs`，本地目录优先于 git 源，所以不会真去 clone）
   - `scripts/build-arm-driver.py` 的存储 ABI 头路径条件里加上 `Sm8450Ufs`

## 上机前的已知缺口

1. **userdata 里必须真的存在 SHNGROOT 清单**。传输层要求 `OS` / `SystemOS` 两个窗口（`XNU_CUPID_UFS_DATA` 打开时再要 `Data`），清单缺失会打印 `invalid SHNGROOT manifest/hash/bounds` —— 这是“驱动对了但卷没准备好”的明确信号，不要误判成驱动失败。
2. **根卷来源**：iOS 27 的 OS/SystemOS 镜像要按 SHNGROOT 布局落到 userdata，这一步属于 profile 的 `[root]` 段，还没做。
3. **SMMU context-bank / match-group 计数**：pong 把它们当板级事实硬校验，这里已改成“读出来打日志”。第一次上机看 `CUPID_UFS TRANSPORT` 那行日志就能确认。
4. 编译未验证：需要 macOS runner（`macos-26`）跑 `unixnu build kernel`，本机/云主机没有 Xcode。
