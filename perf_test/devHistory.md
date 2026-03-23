# Framebuffer 性能测试 — 开发记录

## 背景

评估 OpenHarmony 硬件系统的性能：绕过 OH 的 App 框架（ArkUI / 图形合成器 / 窗口管理器），直接在 Linux 内核上通过 DRM/KMS 绘制，测量从程序启动到渲染完成的耗时。

## 2026-03-19 DRM 版本实现

### 思路

在屏幕上绘制 "aaaaa"，记录三个时间戳：
- **T1**：`main()` 入口（`clock_gettime(CLOCK_MONOTONIC)`）
- **T2**：开始绘制（DRM 初始化完成后）
- **T3**：绘制完成

### 第一次尝试：Framebuffer（fbdev）

用传统的 `/dev/fb0` 或 `/dev/graphics/fb0` → **设备不存在**。P7885 上没有 fbdev 设备。

```
open framebuffer: No such file or directory
```

### 第二次尝试：DRM/KMS

P7885 上的显示设备是 DRM：

```
/dev/dri/card0      ← DSI 显示器
/dev/dri/card1
/dev/dri/renderD128
```

Connector 信息：`card0-DSI-1`，状态 connected。

### 踩坑

| 坑 | 根因 | 解法 |
|---|---|---|
| `open framebuffer: No such file or directory` | P7885 不走 fbdev，走 DRM/KMS | 改用 DRM ioctl |
| `GET_CONNECTOR (2) failed: Bad address` (EFAULT) | 第二次 ioctl 前没清零 conn 结构体 + 没设 props_ptr/prop_values_ptr | 清零结构体，设置所有指针（modes、encoders、props、prop_values） |
| `DRM_IOCTL_MODE_SETCRTC: Permission denied` | OH 的 render_service 进程是 DRM master，普通进程无法切换显示 | 不调 SETCRTC，只做 buffer 创建 + 绘制 + 计时 |
| 设备重启 | SETCRTC 虽然报错但可能干扰了 render_service | 去掉 SETCRTC 调用 |

### DRM ioctl 流程

```
1. open("/dev/dri/card0")
2. DRM_IOCTL_MODE_GETRESOURCES → 拿 connector/crtc/encoder ID 列表
3. DRM_IOCTL_MODE_GETCONNECTOR (第1次) → 拿 count_modes 等
4. DRM_IOCTL_MODE_GETCONNECTOR (第2次) → 填 modes_ptr 拿具体模式
5. DRM_IOCTL_MODE_GETENCODER → 拿 crtc_id
6. DRM_IOCTL_MODE_CREATE_DUMB → 创建 dumb buffer（纯 CPU 可写的显存）
7. DRM_IOCTL_MODE_ADDFB → 创建 framebuffer 对象
8. DRM_IOCTL_MODE_MAP_DUMB → 拿 mmap offset
9. mmap → 映射到用户空间
10. memset + 画像素 → 绘制内容
11. (可选) DRM_IOCTL_MODE_SETCRTC → 切换显示到我们的 buffer
```

### P7885 测试结果

```
Screen: 1728x2368 @ 38Hz, 32bpp
T1 (program start):  491478829.418 ms
T2 (draw start):     491478831.668 ms
T3 (draw complete):  491478852.744 ms
Init time (T2-T1):   2.251 ms  (open DRM + get mode + create buffer + mmap)
Draw time (T3-T2):   21.076 ms (clear + draw 'aaaaa' + set CRTC)
Total time (T3-T1):  23.326 ms
```

注：Draw time 里大部分耗时是 `memset` 清屏（1728x2368x4 = 16MB），画 5 个 8x8 字符本身几乎不耗时。`SETCRTC` 报 Permission denied 但计时包含了这个失败的 ioctl 调用。

### 字体

使用最简单的 8x8 点阵字体，只定义了 'a' 字符（硬编码 8 字节 bitmap）。白色像素画在黑色背景上。

### 编译

```bash
./build.sh
# 内部调用：
# $OHOS_CLANG --target=aarch64-unknown-linux-ohos --sysroot=$SYSROOT -O2 -o fb_perf fb_perf.c
```

产物 20KB，纯 C，无外部依赖。

### 文件

| 文件 | 说明 |
|------|------|
| `fb_perf.c` | DRM 绘制 + 计时程序 |
| `build.sh` | 交叉编译脚本 |
| `devHistory.md` | 本文件 |

### 运行

```bash
# 推到板子
hdc file send fb_perf /data/local/tmp/fb_perf
hdc shell 'chmod 755 /data/local/tmp/fb_perf && /data/local/tmp/fb_perf'

# 查看日志
hdc shell 'cat /data/local/tmp/fb_perf.log'
```

### P7885 vs RK3568

| 指标 | P7885 (1728x2368) | RK3568 (720x1280) |
|------|-------------------|-------------------|
| Init (T2-T1) | 2.251 ms | 6.693 ms |
| Draw (T3-T2) | 21.076 ms | 1.581 ms |
| Total (T3-T1) | 23.326 ms | 8.274 ms |

P7885 的 Init 快但 Draw 慢——因为屏幕大（16MB vs 3.5MB 的 memset 清屏）。RK3568 的 Init 慢但 Draw 快——屏幕小。

---

## 2026-03-19 显示到屏幕

### 问题

render_service（OH 图形合成器）占着 DRM master，普通进程 `SETCRTC` 报 Permission denied。

### 尝试 1：kill render_service

```bash
system("kill $(pidof render_service)");
```

结果：**P7885 直接重启**——init 检测到核心服务挂了触发重启。

### 尝试 2：SIGSTOP/SIGCONT（成功）

用 `SIGSTOP` 暂停 render_service（不杀死），抢到 DRM master 后 `SETCRTC`，显示完用 `SIGCONT` 恢复。

```bash
kill -STOP $(pidof render_service)   # 暂停，不触发 init 重启
# ... SETCRTC 显示 ...
kill -CONT $(pidof render_service)   # 恢复
```

**RK3568 上验证通过，屏幕上显示了 "aaaaa"。**

### --show 模式

```bash
# 纯计时（不显示到屏幕）
/data/local/tmp/fb_perf

# 显示到屏幕（暂停 render_service，10 秒后恢复）
/data/local/tmp/fb_perf --show
```

### RK3568 --show 模式结果

```
--show: stopping render_service...
SETCRTC OK - 'aaaaa' visible on screen!
T1 (program start):  577654089.469 ms
T2 (draw start):     577654095.939 ms
T3 (draw complete):  577654450.648 ms
Init time (T2-T1):   6.470 ms
Draw time (T3-T2):   354.709 ms  ← 包含 SETCRTC 模式切换耗时
Total time (T3-T1):  361.179 ms
Displaying for 10 seconds...
Resuming render_service...
```

注：`--show` 模式的 Draw time（354ms）远大于纯计时模式（1.6ms），因为 `SETCRTC` 包含了 DRM 模式切换（重新配置 DSI 控制器 + 等待 vblank）。纯渲染性能看不带 `--show` 的数据。

### 踩坑补充

| 坑 | 根因 | 解法 |
|---|---|---|
| kill render_service → 设备重启 | init 检测到核心服务挂了触发重启 | 用 SIGSTOP 暂停而不是 kill |
| hdc 连接断开 | kill render_service 连带影响其他服务 | SIGSTOP 不影响进程存在，hdc 正常 |
