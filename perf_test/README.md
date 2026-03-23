# DRM 性能测试工具

绕过 OpenHarmony 的 App 框架（ArkUI / 图形合成器 / 窗口管理器），直接通过 Linux 内核的 DRM/KMS 接口在屏幕上绘制 "aaaaa"，测量从程序启动到渲染完成的耗时。

用于评估硬件系统的**纯内核级绘制性能**——不含 App 框架开销。

## 原理

```
正常 App 的显示链路（本工具跳过这些）：
  ArkTS UI → ArkUI 框架 → Render Service（图形合成器）→ DRM/KMS → 屏幕

本工具的链路：
  C 程序 → DRM ioctl → 直接写显存 → 屏幕
```

DRM（Direct Rendering Manager）是 Linux 内核的显示子系统接口。程序通过 ioctl 直接操作显示硬件：创建显存 buffer、写入像素、把 buffer 输出到屏幕。

## 测量指标

| 时间点 | 含义 |
|--------|------|
| T1 | 程序启动（main 入口） |
| T2 | 开始绘制（DRM 初始化完成：打开设备 + 查询模式 + 创建 buffer + mmap） |
| T3 | 绘制完成（清屏 + 画 "aaaaa" + SETCRTC） |

| 指标 | 计算 | 含义 |
|------|------|------|
| Init time | T2 - T1 | DRM 初始化耗时 |
| Draw time | T3 - T2 | 渲染耗时 |
| Total time | T3 - T1 | 总耗时 |

## 编译

需要 OpenHarmony 交叉编译工具链（Clang + musl sysroot）。

```bash
# 一键编译
./build.sh
```

产物：`fb_perf`（~20KB），纯 C，无外部依赖。

如果没有 `build.sh` 或需要手动编译：

```bash
/path/to/oh6/source/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang \
    --target=aarch64-unknown-linux-ohos \
    --sysroot=/path/to/oh6/source/prebuilts/ohos-sdk/linux/20/native/sysroot \
    -O2 -o fb_perf fb_perf.c
```

## 运行

### 推到板子

```bash
hdc file send fb_perf /data/local/tmp/fb_perf
hdc shell 'chmod 755 /data/local/tmp/fb_perf'
```

### 模式一：纯计时（不影响屏幕）

```bash
hdc shell '/data/local/tmp/fb_perf'
```

只做 DRM 初始化 + buffer 创建 + 写像素 + 计时，**不抢显示输出，屏幕上不会显示任何内容**，不影响系统 UI。结果只输出到终端和日志文件。

输出示例（RK3568）：

```
Display: 720x1280 @ 69Hz, connector=159, crtc=92
T1 (program start):  577643908.213 ms
T2 (draw start):     577643914.906 ms
T3 (draw complete):  577643916.487 ms
Init time (T2-T1):   6.693 ms
Draw time (T3-T2):   1.581 ms
Total time (T3-T1):  8.274 ms
```

### 模式二：显示到屏幕（`--show`）

```bash
hdc shell '/data/local/tmp/fb_perf --show'
```

先暂停 `render_service`（OH 图形合成器），抢 DRM master 权限，把 "aaaaa" 和计时结果显示到**物理屏幕**上，**保持 10 秒**，然后自动恢复 render_service。

**屏幕上会显示：**
- 白色 "aaaaa"
- 绿色 屏幕分辨率和刷新率
- 黄色 Init time / Draw time
- 白色 Total time

⚠️ **运行期间系统 UI 会消失（黑屏 + 测试画面），10 秒后自动恢复。**

⚠️ **想看到屏幕效果必须加 `--show`，不加的话屏幕上什么都没有。**

### 查看日志

```bash
hdc shell 'cat /data/local/tmp/fb_perf.log'
```

## 测试结果

| 设备 | 分辨率 | Init (T2-T1) | Draw (T3-T2) | Total (T3-T1) |
|------|--------|-------------|-------------|---------------|
| RK3568 | 720x1280 | 6.7 ms | 1.6 ms | 8.3 ms |
| P7885 | 1728x2368 | 2.3 ms | 21.1 ms | 23.3 ms |

- P7885 Init 快（芯片 IO 性能好）但 Draw 慢（屏幕大，清屏 memset 16MB）
- RK3568 Init 慢但 Draw 快（屏幕小，清屏 memset 3.5MB）
- `--show` 模式的 Draw time 会显著增大（包含 SETCRTC 模式切换耗时），纯渲染性能看不带 `--show` 的数据

## 注意事项

- **`--show` 模式会暂停系统 UI**：render_service 被 SIGSTOP 暂停，10 秒后 SIGCONT 恢复。不会导致设备重启（用 SIGSTOP 而不是 kill，不触发 init 守护进程的重启机制）
- **重要的设备慎用 `--show`**：某些设备（如 P7885）对核心服务异常敏感，kill render_service 会触发整机重启。SIGSTOP 理论上安全，但建议先在自己的开发板上测
- **时间戳是 CLOCK_MONOTONIC**：单调递增，不受系统时间修改影响，适合测量间隔。绝对值没有意义（是系统启动以来的毫秒数）
- **Draw time 包含清屏**：`memset` 清整个显存是 Draw time 的主要组成部分，画 5 个 8x8 字符本身几乎不耗时

## 文件

| 文件 | 说明 |
|------|------|
| `fb_perf.c` | 主程序，含详细中文注释 |
| `build.sh` | 交叉编译脚本 |
| `devHistory.md` | 开发记录（踩坑过程） |
| `README.md` | 本文件 |
