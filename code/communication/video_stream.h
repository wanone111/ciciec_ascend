#ifndef VIDEO_STREAM_H
#define VIDEO_STREAM_H

#include <stdint.h>

#define VS_MAGIC 0x56534950u /* "VSIP" */
#define VS_VERSION 1u
#define VS_MAX_FRAME_BYTES (2u * 1024u * 1024u)

typedef enum {
    VS_FMT_YUV420P8 = 1,
} vs_pixel_format_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t header_bytes;
    uint32_t frame_id;
    uint64_t timestamp_ns;
    uint16_t width;
    uint16_t height;
    uint16_t roi_x_permille;
    uint16_t roi_y_permille;
    uint16_t roi_w_permille;
    uint16_t roi_h_permille;
    uint16_t pixel_format;
    uint16_t white_balance_mode;
    uint16_t denoise_mode;
    uint16_t gamma_permille;
    uint32_t payload_bytes;
    uint32_t crc32;
} vs_frame_header_t;

#endif
