# HakiOS

[English](README.md) · 中文 · [Українська](README.uk.md)

HakiOS 在搭载高通 SoC 的 Android 手机上运行开源的 XNU 内核（xnu-8796.121.2），配上我自己写的引导
程序和驱动，并在其上启动原版 iOS 16.5 用户空间：launchd、backboardd、SpringBoard 和各个应用。同样
的东西也能在 Linux 电脑上的 QEMU（`virt`，aarch64）里运行。

不刷写任何东西。引导程序通过 `fastboot boot` 进入内存，系统在内存里运行一个会话，重启后手机回到
它自己的 Android。这是一个研究用的试验台，不是 ROM。

第一台手机是 **POCO X5 Pro 5G**（`redwood`，骁龙 778G）。内核事先对它一无所知：每个地址、中断、
电源轨和固件都来自手机自己的设备树和它的 Android，所以移植到另一台高通手机意味着在 `devices/`
下新建一个目录，而不是重写。

## 仓库里有什么

只有我的代码和我的数据：

- 内核：对 Apple 开源 XNU 的补丁（`xnu/hakios.patch`，APSL 2.0）和我的驱动文件（`xnu/files/`），
  由 `xnu/apply.sh` 叠加到一份新克隆上；
- 引导程序（`xnuloader/`）、我的程序和库（`userland/src/`）、工具（`tools/`）、构建和运行脚本；
- `prebuilt/`：用本仓库构建出的内核、引导程序和库；
- `devices/poco-x5-pro/`：一台手机所需的东西（UEFI 移植、设备树、固件脚本）。

永远不会有的东西：

- 任何 iOS 文件：没有二进制、镜像、缓存、字体、图片或截图。系统镜像由你在自己机器上用 iOS 16.5
  的 IPSW 构建，`sysimage/fetch.sh` 从 Apple 的公开服务器为你下载它，不解密、不绕过任何东西；
- 手机的固件（GPU、Wi-Fi、音频 DSP、调制解调器、振动、摄像头）：属于厂商，从你自己的手机取到
  `fw-*/`，git 忽略这些目录；
- 密钥、证书、激活，或任何让手机在 Apple 服务面前冒充 iPhone 的东西。这里无法登录 Apple 账户，
  这也不是目标。

## 现状

| 可用 | 尚未 |
|---|---|
| 锁屏、主屏、控制中心、手势、系统自带应用 | 蜂窝网络：调制解调器已启动、能看到网络、SIM 已绑定；注册和状态栏信号格正在进行（`docs/cellular.md`） |
| 触摸屏、多点触控、电源和音量键 | 蓝牙 |
| 全部 8 个 CPU 核心，大核 2.4 GHz | 本内核下的充电 |
| Adreno GPU 渲染（`gpu=1`，经 Mesa 的 OpenGL ES） | Metal（有着色器编译器，没有 Metal） |
| 扬声器和麦克风（`adsp=1 sound=4 mic=4`） | Apple 账户：没有 Secure Enclave |
| 界面里的电池电量（经音频 DSP，`adsp=1`） | |
| 振动（`haptic=5`） | |
| Wi-Fi：射频、驱动、联网（`wifi=1`） | |
| 相机应用里的摄像头（`cam=5`） | |
| `/private/var` 跨重启保留（`var_store=1 ufs_write=1`） | |
| Sileo、Procursus 软件包、SSH 登录手机 | |

## QEMU 快速开始

Linux x86-64，约 12 GB 磁盘，7-Zip 23 或更新，`lz4`，`qemu-system-aarch64`，`edk2-aarch64`。

```sh
git clone https://github.com/oosexgang/hackios-xnu-kernel hakios
cd hakios
make fetch        # 从 Apple 服务器取 iOS 16.5 IPSW，解包所需部分（7.4 GB）
make images       # APFS 镜像：userland/root.apfs 和 userland/system.apfs
make qemu         # 20 分钟的会话
```

两三分钟后出现锁屏。按住鼠标键就是手指；`H` 是主屏键，`L` 是电源键，`↑`/`↓` 是音量。

## 在手机上

一台有移植的手机（`devices/poco-x5-pro/`，POCO X5 Pro 5G），已解锁引导程序，电脑上有 `adb` 和
`fastboot`，上一节的镜像，以及手机自己的固件：

```sh
make firmware                       # GPU、Wi-Fi、音频/电池、振动、调制解调器、摄像头 -> fw-*/
make phone                          # 10 分钟会话，全部硬件开启，然后回到 Android
make phone ARGS="modem=1" SECS=300  # 或自定义内核参数
```

主要内核参数（完整列表见 `docs/ru/notes.md`）：

| 参数 | 作用 |
|---|---|
| `cpus=8 bootcpu=7 cpufreq=1` | 全部 CPU 核心及其频率 |
| `touchscreen=1` | 触摸屏 |
| `gpu=1` | Adreno 渲染（镜像需以 `GL=1` 构建，固件在 `fw-gpu/`） |
| `adsp=1 sound=4 mic=4` | 音频 DSP：扬声器、麦克风；电池与充电也经由它 |
| `haptic=5` | 振动马达 |
| `wifi=1` | Wi-Fi（网络来自 `fw-wifi/network`） |
| `cam=5` | 摄像头 |
| `modem=1` | 与系统并行启动调制解调器：固件、文件系统、IPA、SIM、NAS |
| `var_store=1 ufs_write=1` | 跨重启保留 `/private/var` |

## 从源码构建

```sh
make kernel       # 从 Apple 的 GitHub 取 XNU 源码 + 我的修改 -> xnu-src/，依赖，构建
make loader       # 引导程序；identity.h 在构建时由你的 IPSW 生成
make images       # 用户空间和镜像
```

详情见 `xnu/README.md` 和 `docs/ru/notes.md`。

## 目录结构

| 目录 | 内容 |
|---|---|
| `xnu/` | 内核补丁和文件；`apply.sh`、`export.sh`；内核 README |
| `xnu/files/bsd/dev/hakios_*.c` | 我的驱动：显示、触摸、UFS、GPU、Wi-Fi、音频、调制解调器、SIM/NAS、振动、摄像头 |
| `xnuloader/` | 引导程序（UEFI 应用） |
| `userland/` | 我的程序和库（`src/`）、镜像构建、launchd 任务 |
| `sysimage/` | 从 IPSW 取得并解包系统文件的脚本（不含文件本身） |
| `devices/poco-x5-pro/` | 一台手机：UEFI 移植、设备树、预构建 UEFI 镜像、固件脚本 |
| `tools/` | APFS、固件、QMI 测试、运行脚本 |
| `qemu/` | 在 QEMU 中运行 |
| `docs/` | `cellular.md`：蜂窝网络（英文）；`ru/`：详细的俄文开发日志 |
| `prebuilt/` | 构建好的内核、引导程序、库 |

## 许可与法律

我的代码遵循它所修改文件的许可：XNU 补丁为 Apple Public Source License 2.0（全文在
`LICENSE.APSL`；条款已满足：修改以源码形式发布，保留了声明），其余为 MIT（`LICENSE`）。iOS、
其镜像、Apple 商标和手机固件属于各自的所有者，不在本仓库中，也不由我分发；每个人从自己的手机和
自己的 IPSW 取得它们。第三方组件列于 `NOTICE`。若你认为这里的某个文件侵犯了任何人的权利，请开一
个 issue，我会删除它。
