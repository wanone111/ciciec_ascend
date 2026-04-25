#ifndef VIDEO_PARAMS_H_
#define VIDEO_PARAMS_H_

#include <ap_int.h>

static const int MAX_WIDTH  = 1280;
static const int MAX_HEIGHT = 720;

static const int STREAM_DEPTH_PIX = 16;

enum ProcMode : unsigned char {
    PROC_RGB    = 0,
    PROC_GRAY   = 1,
    PROC_THRESH = 2
};

enum RoiBgMode : unsigned char {
    ROI_BG_ZERO   = 0,
    ROI_BG_BYPASS = 1,
    ROI_BG_CROP_RESERVED = 2
};

#endif // VIDEO_PARAMS_H_
