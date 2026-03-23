/**
 * DRM/KMS Performance Test
 *
 * 在 OpenHarmony 的 Linux 内核上直接通过 DRM dumb buffer 绘制 "aaaaa"
 * 记录三个时间戳：
 *   T1: 程序启动时刻（main 入口）
 *   T2: 开始绘制时刻
 *   T3: 绘制完成时刻
 *
 * 交叉编译（见 build.sh）
 *
 * 运行：
 *   hdc shell chmod 755 /data/local/tmp/fb_perf
 *   hdc shell /data/local/tmp/fb_perf
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

/* DRM headers - inline minimal definitions to avoid SDK dependency */
#include <drm/drm.h>
#include <drm/drm_mode.h>

/* Simple 8x8 bitmap font for 'a' */
static const unsigned char font_a[8] = {
    0x00, 0x00, 0x3C, 0x06, 0x3E, 0x66, 0x3E, 0x00,
};

static double get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static void draw_char_32bpp(unsigned int *fb, int stride_pixels,
                            int x, int y, const unsigned char *glyph,
                            unsigned int color) {
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (glyph[row] & (0x80 >> col)) {
                fb[(y + row) * stride_pixels + (x + col)] = color;
            }
        }
    }
}

int main(int argc, char *argv[]) {
    int force_show = (argc > 1 && strcmp(argv[1], "--show") == 0);
    double t1 = get_time_ms();

    /* Open DRM device */
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

    /* Get resources */
    struct drm_mode_card_res res = {0};
    if (ioctl(drm_fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
        perror("DRM_IOCTL_MODE_GETRESOURCES (1)");
        close(drm_fd);
        return 1;
    }

    unsigned int *connector_ids = calloc(res.count_connectors, sizeof(unsigned int));
    unsigned int *crtc_ids = calloc(res.count_crtcs, sizeof(unsigned int));
    res.connector_id_ptr = (unsigned long)connector_ids;
    res.crtc_id_ptr = (unsigned long)crtc_ids;

    /* Also need encoder and fb arrays */
    unsigned int *encoder_ids = calloc(res.count_encoders ? res.count_encoders : 1, sizeof(unsigned int));
    unsigned int *fb_ids = calloc(res.count_fbs ? res.count_fbs : 1, sizeof(unsigned int));
    res.encoder_id_ptr = (unsigned long)encoder_ids;
    res.fb_id_ptr = (unsigned long)fb_ids;

    if (ioctl(drm_fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
        perror("DRM_IOCTL_MODE_GETRESOURCES (2)");
        close(drm_fd);
        return 1;
    }

    printf("Connectors: %d, CRTCs: %d, Encoders: %d\n",
           res.count_connectors, res.count_crtcs, res.count_encoders);

    /* Find connected connector and its mode */
    unsigned int conn_id = 0;
    unsigned int crtc_id = 0;
    struct drm_mode_modeinfo mode = {0};
    int found = 0;

    for (unsigned int i = 0; i < res.count_connectors && !found; i++) {
        struct drm_mode_get_connector conn = {0};
        conn.connector_id = connector_ids[i];

        /* First call: get counts */
        if (ioctl(drm_fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) continue;

        printf("  Connector %u: connection=%u, modes=%u, encoder=%u\n",
               connector_ids[i], conn.connection, conn.count_modes, conn.encoder_id);
        if (conn.count_modes == 0) continue;

        /* Second call: get modes - must set all pointers AND preserve counts */
        unsigned int saved_encoder_id = conn.encoder_id;
        unsigned int n_modes = conn.count_modes;
        unsigned int n_encoders = conn.count_encoders;
        unsigned int n_props = conn.count_props;

        struct drm_mode_modeinfo *modes = calloc(n_modes, sizeof(struct drm_mode_modeinfo));
        unsigned int *enc_ids = calloc(n_encoders ? n_encoders : 1, sizeof(unsigned int));
        unsigned int *prop_ids = calloc(n_props ? n_props : 1, sizeof(unsigned int));
        unsigned long *prop_vals = calloc(n_props ? n_props : 1, sizeof(unsigned long));

        memset(&conn, 0, sizeof(conn));
        conn.connector_id = connector_ids[i];
        conn.count_modes = n_modes;
        conn.modes_ptr = (unsigned long)modes;
        conn.count_encoders = n_encoders;
        conn.encoders_ptr = (unsigned long)enc_ids;
        conn.count_props = n_props;
        conn.props_ptr = (unsigned long)prop_ids;
        conn.prop_values_ptr = (unsigned long)prop_vals;

        if (ioctl(drm_fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) {
            printf("  GET_CONNECTOR (2) failed: %s\n", strerror(errno));
            free(modes);
            free(enc_ids);
            free(prop_ids);
            free(prop_vals);
            continue;
        }
        printf("  Got %u modes, first: %ux%u@%u\n",
               conn.count_modes, modes[0].hdisplay, modes[0].vdisplay, modes[0].vrefresh);

        /* Use first mode (usually native/preferred) */
        mode = modes[0];
        conn_id = connector_ids[i];

        /* Find CRTC via encoder */
        if (conn.encoder_id) {
            struct drm_mode_get_encoder enc = {0};
            enc.encoder_id = conn.encoder_id;
            if (ioctl(drm_fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0) {
                crtc_id = enc.crtc_id;
            }
        }
        if (!crtc_id && res.count_crtcs > 0) {
            crtc_id = crtc_ids[0];
        }

        if (!conn.encoder_id) conn.encoder_id = saved_encoder_id;
        found = 1;
        free(modes);
        free(enc_ids);
        free(prop_ids);
        free(prop_vals);
    }

    free(connector_ids);
    free(crtc_ids);
    free(encoder_ids);
    free(fb_ids);

    if (!found) {
        fprintf(stderr, "No connected display found\n");
        close(drm_fd);
        return 1;
    }

    int width = mode.hdisplay;
    int height = mode.vdisplay;
    printf("Display: %dx%d @ %dHz, connector=%u, crtc=%u\n",
           width, height, mode.vrefresh, conn_id, crtc_id);

    /* Create dumb buffer */
    struct drm_mode_create_dumb create = {0};
    create.width = width;
    create.height = height;
    create.bpp = 32;
    if (ioctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
        perror("DRM_IOCTL_MODE_CREATE_DUMB");
        close(drm_fd);
        return 1;
    }

    /* Create framebuffer object */
    struct drm_mode_fb_cmd fb_cmd = {0};
    fb_cmd.width = width;
    fb_cmd.height = height;
    fb_cmd.pitch = create.pitch;
    fb_cmd.bpp = 32;
    fb_cmd.depth = 24;
    fb_cmd.handle = create.handle;
    if (ioctl(drm_fd, DRM_IOCTL_MODE_ADDFB, &fb_cmd) < 0) {
        perror("DRM_IOCTL_MODE_ADDFB");
        close(drm_fd);
        return 1;
    }

    /* Map dumb buffer */
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

    int stride_pixels = create.pitch / 4; /* 32bpp = 4 bytes per pixel */

    /* T2: Start drawing */
    double t2 = get_time_ms();

    /* Clear to black */
    memset(fb, 0, create.size);

    /* Draw "aaaaa" centered */
    int text_width = 5 * 10;
    int start_x = (width - text_width) / 2;
    int start_y = height / 3;
    unsigned int white = 0xFFFFFFFF;

    for (int i = 0; i < 5; i++) {
        draw_char_32bpp(fb, stride_pixels, start_x + i * 10, start_y, font_a, white);
    }

    /* Try to set CRTC - will fail if render_service holds DRM master */
    /* To show on screen: run with --show flag, which kills render_service first */
    int show_on_screen = 0;
    /* Check argv (passed via global) */

    if (force_show) {
        printf("--show: stopping render_service...\n");
        system("kill -STOP $(pidof render_service) 2>/dev/null");
        usleep(300000);
        ioctl(drm_fd, DRM_IOCTL_SET_MASTER, 0);
        show_on_screen = 1;
    } else if (ioctl(drm_fd, DRM_IOCTL_SET_MASTER, 0) == 0) {
        show_on_screen = 1;
    } else {
        printf("Not DRM master (render_service holds it). Timing-only mode.\n");
        printf("To show on screen: run 'fb_perf --show'\n");
    }

    if (show_on_screen) {
        struct drm_mode_crtc crtc = {0};
        crtc.crtc_id = crtc_id;
        crtc.fb_id = fb_cmd.fb_id;
        crtc.set_connectors_ptr = (unsigned long)&conn_id;
        crtc.count_connectors = 1;
        crtc.mode = mode;
        crtc.mode_valid = 1;
        if (ioctl(drm_fd, DRM_IOCTL_MODE_SETCRTC, &crtc) < 0) {
            perror("DRM_IOCTL_MODE_SETCRTC");
        } else {
            printf("SETCRTC OK - 'aaaaa' visible on screen!\n");
        }
    }

    /* T3: Draw complete */
    double t3 = get_time_ms();

    /* Write log */
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

    /* Print to stdout */
    printf("T1 (program start):  %.3f ms\n", t1);
    printf("T2 (draw start):     %.3f ms\n", t2);
    printf("T3 (draw complete):  %.3f ms\n", t3);
    printf("Init time (T2-T1):   %.3f ms\n", t2 - t1);
    printf("Draw time (T3-T2):   %.3f ms\n", t3 - t2);
    printf("Total time (T3-T1):  %.3f ms\n", t3 - t1);
    printf("Log: %s\n", log_path);

    if (show_on_screen) {
        printf("Displaying for 10 seconds...\n");
        sleep(10);
    }

    munmap(fb, create.size);

    /* Resume render_service if we stopped it */
    if (force_show) {
        printf("Resuming render_service...\n");
        ioctl(drm_fd, DRM_IOCTL_DROP_MASTER, 0);
        system("kill -CONT $(pidof render_service) 2>/dev/null");
    }

    close(drm_fd);
    return 0;
}
