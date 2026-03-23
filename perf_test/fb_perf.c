/**
 * DRM/KMS 性能测试
 *
 * 绕过 OpenHarmony 的 App 框架（ArkUI / 图形合成器），
 * 直接通过 Linux 内核的 DRM（Direct Rendering Manager）接口绘制 "aaaaa"。
 *
 * 记录三个时间戳：
 *   T1: 程序启动（main 入口）
 *   T2: 开始绘制（DRM 初始化完成后）
 *   T3: 绘制完成
 *
 * 两种运行模式：
 *   ./fb_perf          纯计时模式（不显示到屏幕，不影响系统 UI）
 *   ./fb_perf --show   显示模式（暂停 render_service，画面显示到屏幕，10 秒后恢复）
 *
 * 交叉编译见 build.sh
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

/*
 * DRM 头文件 —— 来自 Linux 内核的 userspace 接口
 * drm.h:      基础 ioctl 定义（DRM_IOCTL_SET_MASTER 等）
 * drm_mode.h: 显示模式相关结构体（connector、crtc、mode、dumb buffer）
 */
#include <drm/drm.h>
#include <drm/drm_mode.h>

/*
 * 8x8 点阵字体
 * 每行 8 bit，1 = 白色像素，0 = 透明
 * 支持：a-z 0-9 空格 . : ( ) = - / @ x %
 */
static const unsigned char font_8x8[128][8] = {
    /* 默认全空（不可打印字符） */
    /* 空格 0x20 */
    [' '] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 符号 */
    ['!'] = {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00},
    ['%'] = {0x62,0x64,0x08,0x10,0x26,0x46,0x00,0x00},
    ['('] = {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},
    [')'] = {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},
    ['-'] = {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
    ['.'] = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    ['/'] = {0x02,0x04,0x08,0x10,0x20,0x40,0x00,0x00},
    [':'] = {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00},
    ['='] = {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00},
    ['@'] = {0x3C,0x42,0x5E,0x56,0x5E,0x40,0x3C,0x00},
    /* 数字 0-9 */
    ['0'] = {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00},
    ['1'] = {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},
    ['2'] = {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00},
    ['3'] = {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00},
    ['4'] = {0x0C,0x1C,0x2C,0x4C,0x7E,0x0C,0x0C,0x00},
    ['5'] = {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00},
    ['6'] = {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00},
    ['7'] = {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00},
    ['8'] = {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00},
    ['9'] = {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00},
    /* 大写字母 */
    ['A'] = {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00},
    ['B'] = {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00},
    ['C'] = {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00},
    ['D'] = {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00},
    ['H'] = {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00},
    ['I'] = {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    ['M'] = {0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00},
    ['T'] = {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
    /* 小写字母 */
    ['a'] = {0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00},
    ['b'] = {0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00},
    ['c'] = {0x00,0x00,0x3C,0x60,0x60,0x60,0x3C,0x00},
    ['d'] = {0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00},
    ['e'] = {0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00},
    ['f'] = {0x0E,0x18,0x3E,0x18,0x18,0x18,0x18,0x00},
    ['g'] = {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x3C},
    ['h'] = {0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00},
    ['i'] = {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00},
    ['k'] = {0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x00},
    ['l'] = {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    ['m'] = {0x00,0x00,0x6C,0xFE,0xD6,0xC6,0xC6,0x00},
    ['n'] = {0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00},
    ['o'] = {0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00},
    ['p'] = {0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60},
    ['r'] = {0x00,0x00,0x6E,0x70,0x60,0x60,0x60,0x00},
    ['s'] = {0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00},
    ['t'] = {0x18,0x18,0x7E,0x18,0x18,0x18,0x0E,0x00},
    ['u'] = {0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00},
    ['w'] = {0x00,0x00,0xC6,0xC6,0xD6,0xFE,0x6C,0x00},
    ['x'] = {0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x00},
    ['y'] = {0x00,0x00,0x66,0x66,0x66,0x3E,0x06,0x3C},
    ['z'] = {0x00,0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00},
};

/**
 * 获取当前时间（毫秒），使用 CLOCK_MONOTONIC（单调递增，不受系统时间修改影响）
 */
static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

/**
 * 在 32bpp 的 framebuffer 上画一个 8x8 字符
 *
 * @param fb            framebuffer 首地址（mmap 出来的）
 * @param stride_pixels 每行像素数（= pitch / 4，pitch 是每行字节数）
 * @param x, y          字符左上角坐标
 * @param ch            要画的字符
 * @param color         像素颜色（ARGB 格式，0xFFFFFFFF = 白色）
 */
static void draw_char_32bpp(unsigned int *fb, int stride_pixels,
                            int x, int y, char ch, unsigned int color) {
    const unsigned char *glyph = font_8x8[(unsigned char)ch];
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (glyph[row] & (0x80 >> col)) {
                fb[(y + row) * stride_pixels + (x + col)] = color;
            }
        }
    }
}

/**
 * 在 framebuffer 上画一行字符串
 *
 * @param fb            framebuffer 首地址
 * @param stride_pixels 每行像素数
 * @param x, y          起始坐标
 * @param str           要画的字符串
 * @param color         颜色
 */
static void draw_string(unsigned int *fb, int stride_pixels,
                        int x, int y, const char *str, unsigned int color) {
    for (int i = 0; str[i]; i++) {
        draw_char_32bpp(fb, stride_pixels, x + i * 8, y, str[i], color);
    }
}

int main(int argc, char *argv[]) {
    /* --show 参数：暂停 render_service，把画面显示到物理屏幕 */
    int force_show = (argc > 1 && strcmp(argv[1], "--show") == 0);

    /* ========== T1: 程序启动 ========== */
    double t1 = get_time_ms();

    /*
     * ==========================================
     * 第一步：打开 DRM 设备
     * ==========================================
     * /dev/dri/card0 是主显示设备（通常是 DSI 屏幕）
     * /dev/dri/card1 可能是第二个显示（HDMI 等）
     */
    const char *drm_paths[] = {"/dev/dri/card0", "/dev/dri/card1", NULL};
    int drm_fd = -1;
    for (int i = 0; drm_paths[i]; i++) {
        drm_fd = open(drm_paths[i], O_RDWR);
        if (drm_fd >= 0) {
            printf("Opened %s\n", drm_paths[i]);
            break;
        }
    }
    if (drm_fd < 0) {
        perror("open DRM device");
        return 1;
    }

    /*
     * ==========================================
     * 第二步：获取 DRM 资源列表
     * ==========================================
     * DRM 的显示管线：Connector → Encoder → CRTC → Framebuffer
     *
     * - Connector: 物理接口（DSI屏幕、HDMI口）
     * - Encoder:   信号编码器（把像素数据转成 DSI/HDMI 信号）
     * - CRTC:      显示控制器（负责扫描输出——从 framebuffer 读像素，按刷新率送到屏幕）
     *
     * 第一次 ioctl 只拿数量，第二次填指针拿具体 ID
     */
    struct drm_mode_card_res res = {0};
    if (ioctl(drm_fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
        perror("DRM_IOCTL_MODE_GETRESOURCES (1)");
        close(drm_fd);
        return 1;
    }

    /* 分配数组，存放各类资源的 ID */
    unsigned int *connector_ids = calloc(res.count_connectors, sizeof(unsigned int));
    unsigned int *crtc_ids = calloc(res.count_crtcs, sizeof(unsigned int));
    unsigned int *encoder_ids = calloc(res.count_encoders ? res.count_encoders : 1, sizeof(unsigned int));
    unsigned int *fb_ids = calloc(res.count_fbs ? res.count_fbs : 1, sizeof(unsigned int));

    /* 把指针填进结构体，第二次 ioctl 内核会填充具体 ID */
    res.connector_id_ptr = (unsigned long)connector_ids;
    res.crtc_id_ptr = (unsigned long)crtc_ids;
    res.encoder_id_ptr = (unsigned long)encoder_ids;
    res.fb_id_ptr = (unsigned long)fb_ids;

    if (ioctl(drm_fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
        perror("DRM_IOCTL_MODE_GETRESOURCES (2)");
        close(drm_fd);
        return 1;
    }

    printf("Connectors: %d, CRTCs: %d, Encoders: %d\n",
           res.count_connectors, res.count_crtcs, res.count_encoders);

    /*
     * ==========================================
     * 第三步：找到已连接的 Connector 和它的显示模式
     * ==========================================
     * 遍历所有 connector，找 connection==1（已连接）且有可用模式的那个。
     *
     * GET_CONNECTOR 也是两次 ioctl 模式：
     *   第一次：拿 count（modes 数量、encoder 数量、property 数量）
     *   第二次：填指针拿具体数据
     *
     * 坑：第二次调用前必须清零结构体并重新设置所有字段和指针，
     *     否则内核报 EFAULT（Bad address）。
     */
    unsigned int conn_id = 0;    /* 找到的 connector ID */
    unsigned int crtc_id = 0;    /* 对应的 CRTC ID */
    struct drm_mode_modeinfo mode = {0};  /* 显示模式（分辨率、刷新率） */
    int found = 0;

    for (unsigned int i = 0; i < res.count_connectors && !found; i++) {
        struct drm_mode_get_connector conn = {0};
        conn.connector_id = connector_ids[i];

        /* 第一次调用：拿数量 */
        if (ioctl(drm_fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) continue;

        printf("  Connector %u: connection=%u, modes=%u, encoder=%u\n",
               connector_ids[i], conn.connection, conn.count_modes, conn.encoder_id);
        if (conn.count_modes == 0) continue;

        /* 保存第一次拿到的值 */
        unsigned int saved_encoder_id = conn.encoder_id;
        unsigned int n_modes = conn.count_modes;
        unsigned int n_encoders = conn.count_encoders;
        unsigned int n_props = conn.count_props;

        /* 分配内存存具体数据 */
        struct drm_mode_modeinfo *modes = calloc(n_modes, sizeof(struct drm_mode_modeinfo));
        unsigned int *enc_ids = calloc(n_encoders ? n_encoders : 1, sizeof(unsigned int));
        unsigned int *prop_ids = calloc(n_props ? n_props : 1, sizeof(unsigned int));
        unsigned long *prop_vals = calloc(n_props ? n_props : 1, sizeof(unsigned long));

        /* 清零结构体，重新填所有字段（内核要求） */
        memset(&conn, 0, sizeof(conn));
        conn.connector_id = connector_ids[i];
        conn.count_modes = n_modes;
        conn.modes_ptr = (unsigned long)modes;
        conn.count_encoders = n_encoders;
        conn.encoders_ptr = (unsigned long)enc_ids;
        conn.count_props = n_props;
        conn.props_ptr = (unsigned long)prop_ids;
        conn.prop_values_ptr = (unsigned long)prop_vals;

        /* 第二次调用：拿具体模式数据 */
        if (ioctl(drm_fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) {
            printf("  GET_CONNECTOR (2) failed: %s\n", strerror(errno));
            free(modes); free(enc_ids); free(prop_ids); free(prop_vals);
            continue;
        }
        printf("  Got %u modes, first: %ux%u@%u\n",
               conn.count_modes, modes[0].hdisplay, modes[0].vdisplay, modes[0].vrefresh);

        /* 用第一个模式（通常是原生/首选分辨率） */
        mode = modes[0];
        conn_id = connector_ids[i];

        /*
         * 通过 encoder 找到对应的 CRTC
         * Connector → Encoder → CRTC 的链路
         */
        if (conn.encoder_id) {
            struct drm_mode_get_encoder enc = {0};
            enc.encoder_id = conn.encoder_id;
            if (ioctl(drm_fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0) {
                crtc_id = enc.crtc_id;
            }
        }
        /* 如果 encoder 没指向 CRTC，用第一个可用的 */
        if (!crtc_id && res.count_crtcs > 0) {
            crtc_id = crtc_ids[0];
        }

        if (!conn.encoder_id) conn.encoder_id = saved_encoder_id;
        found = 1;
        free(modes); free(enc_ids); free(prop_ids); free(prop_vals);
    }

    free(connector_ids); free(crtc_ids); free(encoder_ids); free(fb_ids);

    if (!found) {
        fprintf(stderr, "No connected display found\n");
        close(drm_fd);
        return 1;
    }

    int width = mode.hdisplay;
    int height = mode.vdisplay;
    printf("Display: %dx%d @ %dHz, connector=%u, crtc=%u\n",
           width, height, mode.vrefresh, conn_id, crtc_id);

    /*
     * ==========================================
     * 第四步：创建 Dumb Buffer
     * ==========================================
     * "Dumb Buffer" 是最简单的显存分配方式——纯 CPU 可写，不走 GPU。
     * 适合我们这种"往显存里写像素"的场景。
     *
     * 创建后得到 handle（显存句柄）、pitch（每行字节数）、size（总字节数）
     */
    struct drm_mode_create_dumb create = {0};
    create.width = width;
    create.height = height;
    create.bpp = 32;   /* 每像素 32 bit = 4 字节 (ARGB) */
    if (ioctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
        perror("DRM_IOCTL_MODE_CREATE_DUMB");
        close(drm_fd);
        return 1;
    }

    /*
     * ==========================================
     * 第五步：在 dumb buffer 上创建 Framebuffer 对象
     * ==========================================
     * DRM 需要一个 "framebuffer 对象" 才能把 buffer 送到 CRTC 输出。
     * ADDFB 把 dumb buffer 的 handle 包装成 fb_id。
     */
    struct drm_mode_fb_cmd fb_cmd = {0};
    fb_cmd.width = width;
    fb_cmd.height = height;
    fb_cmd.pitch = create.pitch;  /* 每行字节数（可能有 padding） */
    fb_cmd.bpp = 32;
    fb_cmd.depth = 24;            /* 颜色深度（不含 alpha） */
    fb_cmd.handle = create.handle;
    if (ioctl(drm_fd, DRM_IOCTL_MODE_ADDFB, &fb_cmd) < 0) {
        perror("DRM_IOCTL_MODE_ADDFB");
        close(drm_fd);
        return 1;
    }

    /*
     * ==========================================
     * 第六步：mmap 到用户空间
     * ==========================================
     * MAP_DUMB 拿到 offset，然后 mmap 映射——之后就能像写普通数组一样写显存了。
     */
    struct drm_mode_map_dumb map_req = {0};
    map_req.handle = create.handle;
    if (ioctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_req) < 0) {
        perror("DRM_IOCTL_MODE_MAP_DUMB");
        close(drm_fd);
        return 1;
    }

    unsigned int *fb = (unsigned int *)mmap(NULL, create.size,
                                            PROT_READ | PROT_WRITE,
                                            MAP_SHARED, drm_fd, map_req.offset);
    if (fb == MAP_FAILED) {
        perror("mmap");
        close(drm_fd);
        return 1;
    }

    int stride_pixels = create.pitch / 4; /* 32bpp → 每像素 4 字节 */

    /* ========== T2: 开始绘制 ========== */
    double t2 = get_time_ms();

    /* 清屏：全部填 0（黑色） */
    memset(fb, 0, create.size);

    /* 画 "aaaaa"：水平居中，垂直 1/3 处 */
    int start_x = (width - 5 * 8) / 2;
    int start_y = height / 3;
    unsigned int white = 0xFFFFFFFF;   /* ARGB 白色 */
    unsigned int green = 0xFF00FF00;   /* ARGB 绿色 */
    unsigned int yellow = 0xFFFFFF00;  /* ARGB 黄色 */

    draw_string(fb, stride_pixels, start_x, start_y, "aaaaa", white);

    /*
     * ==========================================
     * 第七步：把 buffer 输出到屏幕（SETCRTC）
     * ==========================================
     * SETCRTC 告诉 CRTC："从现在开始，扫描输出 fb_id 这个 buffer"。
     *
     * 问题：SETCRTC 需要 DRM master 权限。
     * OH 的 render_service（图形合成器）已经是 DRM master 了。
     *
     * 解法：
     * - 默认模式：不 SETCRTC，只做 buffer 创建 + 绘制 + 计时（不影响屏幕）
     * - --show 模式：先用 SIGSTOP 暂停 render_service，抢 master，SETCRTC 显示，
     *               10 秒后用 SIGCONT 恢复 render_service
     *
     * 为什么用 SIGSTOP 而不是 kill？
     * → kill 会触发 init 守护进程的自动重启机制，某些军工设备直接整机重启。
     * → SIGSTOP 只是暂停进程（冻结），不触发 init 重启，SIGCONT 可以恢复。
     */
    int show_on_screen = 0;

    if (force_show) {
        /* --show 模式：暂停 render_service，抢 DRM master */
        printf("--show: stopping render_service...\n");
        system("kill -STOP $(pidof render_service) 2>/dev/null");
        usleep(300000);  /* 等 300ms 让它真正暂停 */
        ioctl(drm_fd, DRM_IOCTL_SET_MASTER, 0);  /* 抢 master */
        show_on_screen = 1;
    } else if (ioctl(drm_fd, DRM_IOCTL_SET_MASTER, 0) == 0) {
        /* render_service 没跑（罕见情况），直接拿到了 master */
        show_on_screen = 1;
    } else {
        /* 正常情况：拿不到 master，纯计时模式 */
        printf("Not DRM master (render_service holds it). Timing-only mode.\n");
        printf("To show on screen: run 'fb_perf --show'\n");
    }

    if (show_on_screen) {
        /* 设置 CRTC：把我们的 buffer 输出到 connector 对应的屏幕 */
        struct drm_mode_crtc crtc = {0};
        crtc.crtc_id = crtc_id;
        crtc.fb_id = fb_cmd.fb_id;
        crtc.set_connectors_ptr = (unsigned long)&conn_id;
        crtc.count_connectors = 1;
        crtc.mode = mode;       /* 使用之前查到的显示模式 */
        crtc.mode_valid = 1;
        if (ioctl(drm_fd, DRM_IOCTL_MODE_SETCRTC, &crtc) < 0) {
            perror("DRM_IOCTL_MODE_SETCRTC");
        } else {
            printf("SETCRTC OK - 'aaaaa' visible on screen!\n");
        }
    }

    /* ========== T3: 绘制完成 ========== */
    double t3 = get_time_ms();

    /*
     * 把计时结果也画到屏幕上（在 "aaaaa" 下方）
     * 用 snprintf 格式化成字符串，然后逐行画
     */
    {
        char line[128];
        int ly = start_y + 30;   /* "aaaaa" 下方 30 像素 */
        int lx = 20;             /* 左边距 */
        int line_h = 14;         /* 行高 */

        snprintf(line, sizeof(line), "Display: %dx%d @ %dHz", width, height, mode.vrefresh);
        draw_string(fb, stride_pixels, lx, ly, line, green);
        ly += line_h;

        snprintf(line, sizeof(line), "Init time (T2-T1): %.3f ms", t2 - t1);
        draw_string(fb, stride_pixels, lx, ly, line, yellow);
        ly += line_h;

        snprintf(line, sizeof(line), "Draw time (T3-T2): %.3f ms", t3 - t2);
        draw_string(fb, stride_pixels, lx, ly, line, yellow);
        ly += line_h;

        snprintf(line, sizeof(line), "Total     (T3-T1): %.3f ms", t3 - t1);
        draw_string(fb, stride_pixels, lx, ly, line, white);
    }

    /* 如果已经 SETCRTC 了，需要重新 SETCRTC 让新画的内容生效 */
    if (show_on_screen) {
        struct drm_mode_crtc crtc2 = {0};
        crtc2.crtc_id = crtc_id;
        crtc2.fb_id = fb_cmd.fb_id;
        crtc2.set_connectors_ptr = (unsigned long)&conn_id;
        crtc2.count_connectors = 1;
        crtc2.mode = mode;
        crtc2.mode_valid = 1;
        ioctl(drm_fd, DRM_IOCTL_MODE_SETCRTC, &crtc2);
    }

    /* 写日志到文件 */
    const char *log_path = "/data/local/tmp/fb_perf.log";
    FILE *log_fp = fopen(log_path, "w");
    if (log_fp) {
        fprintf(log_fp, "=== DRM Performance Test ===\n");
        fprintf(log_fp, "Screen: %dx%d @ %dHz, 32bpp\n", width, height, mode.vrefresh);
        fprintf(log_fp, "T1 (program start):  %.3f ms\n", t1);
        fprintf(log_fp, "T2 (draw start):     %.3f ms\n", t2);
        fprintf(log_fp, "T3 (draw complete):  %.3f ms\n", t3);
        fprintf(log_fp, "---\n");
        fprintf(log_fp, "Init time (T2-T1):   %.3f ms  (open DRM + get mode + create buffer + mmap)\n", t2 - t1);
        fprintf(log_fp, "Draw time (T3-T2):   %.3f ms  (clear + draw 'aaaaa' + set CRTC)\n", t3 - t2);
        fprintf(log_fp, "Total time (T3-T1):  %.3f ms\n", t3 - t1);
        fclose(log_fp);
    }

    /* 打印到终端 */
    printf("T1 (program start):  %.3f ms\n", t1);
    printf("T2 (draw start):     %.3f ms\n", t2);
    printf("T3 (draw complete):  %.3f ms\n", t3);
    printf("Init time (T2-T1):   %.3f ms\n", t2 - t1);
    printf("Draw time (T3-T2):   %.3f ms\n", t3 - t2);
    printf("Total time (T3-T1):  %.3f ms\n", t3 - t1);
    printf("Log: %s\n", log_path);

    /* 如果显示到屏幕了，保持 10 秒让人看 */
    if (show_on_screen) {
        printf("Displaying for 10 seconds...\n");
        sleep(10);
    }

    /* 清理 */
    munmap(fb, create.size);

    /* 如果暂停了 render_service，恢复它 */
    if (force_show) {
        printf("Resuming render_service...\n");
        ioctl(drm_fd, DRM_IOCTL_DROP_MASTER, 0);  /* 释放 master */
        system("kill -CONT $(pidof render_service) 2>/dev/null");  /* 恢复 */
    }

    close(drm_fd);
    return 0;
}
