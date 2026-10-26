# FF7 Remake 帧率解锁（FF7RemakeFpsUnlock）

[English](README.en.md)

## 简介

本工具用于解除《最终幻想 VII 重制版 间奏》（FINAL FANTASY VII REMAKE
INTERGRADE，Unreal Engine 4.18）在 PC 平台的 120 FPS 帧率上限。

工具以代理 DLL 的形式在游戏进程内定位并修补限帧逻辑，不修改任何游戏文件。

## 特性

- 解除 120 FPS 上限，默认不限帧，也支持自定义上限
- 基于特征码扫描，不依赖固定地址
- 不修改游戏文件，删除 DLL 即恢复原状
- 手柄功能不受影响，XInput 调用完整转发至系统 DLL
- 提供运行日志，便于排查问题
- 支持 Steam 与 Epic 版本

## 系统要求

- Windows 10 / 11 64 位
- 游戏版本：Steam build 21888968（2026-10-15）已验证

## 安装

1. 将 `xinput1_3.dll` 与 `FF7RFpsUnlock.ini`（可选）复制到：

   ```
   ...\FINAL FANTASY VII REMAKE\End\Binaries\Win64\
   ```

   与 `ff7remake_.exe` 位于同一目录。
2. 启动游戏，在画面设置中将“帧率”设为 **120**，并关闭垂直同步。
3. 补丁在进程启动时自动应用，结果记录于同目录的 `FF7RFpsUnlock.log`。

## 配置

配置文件为 `FF7RFpsUnlock.ini`，与 DLL 放在同一目录，可省略（使用默认值）：

```ini
[Settings]
; 0 = 不限帧（默认）；正数 = 帧率上限，例如 144
FPS=0
; 关闭 UE4 平滑帧率夹取（超过 120 时启用）
DisableSmoothing=1
```

游戏内仍提供 30 / 60 / 90 / 120 四档帧率选项，本工具作用于“120”这一档。

## 日志

`FF7RFpsUnlock.log` 记录模块基址、特征码查找结果和补丁状态。
若游戏更新导致特征码不存在，日志会给出提示，游戏本身不受影响。

## 工作原理

游戏在应用“120”帧率选项时会写入两个控制台变量：

- `t.MaxFPS`：游戏线程限帧
- `RHI.TargetRefreshRate`：D3D11 RHI 的呈现限速

此外，UE4.18 还可能通过 `bSmoothFrameRate` / `SmoothedFrameRateRange`
进行二次限制。

工具在进程内扫描 `.text` 段中应用该选项的代码块，校验其引用的常量确为
`120.0` 后，将限帧值改为 0（不限帧）或自定义值（通过就近分配的跳转代码实现），
同时清零 `RHI.TargetRefreshRate`，并按配置跳过平滑帧率夹取。

`xinput1_3.dll` 是 Windows 自带的 XInput 1.3 手柄运行时。游戏可执行文件在导入表中
静态引用该 DLL，Windows 加载 DLL 时会优先搜索可执行文件所在目录。本工具的 DLL 即
通过该机制进入游戏进程，并将全部 XInput 导出函数转发至
`C:\Windows\System32\xinput1_3.dll`。

## 构建

要求：Visual Studio（C++ 桌面开发负载）、CMake 3.20 及以上。

```bat
build.bat
```

或：

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

产物：`build\Release\xinput1_3.dll`

## 卸载

删除游戏 `Win64` 目录中的 `xinput1_3.dll` 与 `FF7RFpsUnlock.ini`。

## 许可

MIT，详见 [LICENSE](LICENSE)。

本项目与 Square Enix 无关。FINAL FANTASY 是 Square Enix 的商标。
