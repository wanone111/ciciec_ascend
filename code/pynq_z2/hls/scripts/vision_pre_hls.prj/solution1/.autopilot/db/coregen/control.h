// 0x00 : Control signals
//        bit 0  - ap_start (Read/Write/COH)
//        bit 1  - ap_done (Read/COR)
//        bit 2  - ap_idle (Read)
//        bit 3  - ap_ready (Read/COR)
//        bit 7  - auto_restart (Read/Write)
//        bit 9  - interrupt (Read)
//        others - reserved
// 0x04 : Global Interrupt Enable Register
//        bit 0  - Global Interrupt Enable (Read/Write)
//        others - reserved
// 0x08 : IP Interrupt Enable Register (Read/Write)
//        bit 0 - enable ap_done interrupt (Read/Write)
//        bit 1 - enable ap_ready interrupt (Read/Write)
//        others - reserved
// 0x0c : IP Interrupt Status Register (Read/TOW)
//        bit 0 - ap_done (Read/TOW)
//        bit 1 - ap_ready (Read/TOW)
//        others - reserved
// 0x10 : Data signal of img_in
//        bit 31~0 - img_in[31:0] (Read/Write)
// 0x14 : Data signal of img_in
//        bit 31~0 - img_in[63:32] (Read/Write)
// 0x18 : reserved
// 0x1c : Data signal of img_out
//        bit 31~0 - img_out[31:0] (Read/Write)
// 0x20 : Data signal of img_out
//        bit 31~0 - img_out[63:32] (Read/Write)
// 0x24 : reserved
// 0x28 : Data signal of rows
//        bit 31~0 - rows[31:0] (Read/Write)
// 0x2c : reserved
// 0x30 : Data signal of cols
//        bit 31~0 - cols[31:0] (Read/Write)
// 0x34 : reserved
// 0x38 : Data signal of roi_x
//        bit 31~0 - roi_x[31:0] (Read/Write)
// 0x3c : reserved
// 0x40 : Data signal of roi_y
//        bit 31~0 - roi_y[31:0] (Read/Write)
// 0x44 : reserved
// 0x48 : Data signal of roi_w
//        bit 31~0 - roi_w[31:0] (Read/Write)
// 0x4c : reserved
// 0x50 : Data signal of roi_h
//        bit 31~0 - roi_h[31:0] (Read/Write)
// 0x54 : reserved
// 0x58 : Data signal of thresh_val
//        bit 7~0 - thresh_val[7:0] (Read/Write)
//        others  - reserved
// 0x5c : reserved
// (SC = Self Clear, COR = Clear on Read, TOW = Toggle on Write, COH = Clear on Handshake)

#define CONTROL_ADDR_AP_CTRL         0x00
#define CONTROL_ADDR_GIE             0x04
#define CONTROL_ADDR_IER             0x08
#define CONTROL_ADDR_ISR             0x0c
#define CONTROL_ADDR_IMG_IN_DATA     0x10
#define CONTROL_BITS_IMG_IN_DATA     64
#define CONTROL_ADDR_IMG_OUT_DATA    0x1c
#define CONTROL_BITS_IMG_OUT_DATA    64
#define CONTROL_ADDR_ROWS_DATA       0x28
#define CONTROL_BITS_ROWS_DATA       32
#define CONTROL_ADDR_COLS_DATA       0x30
#define CONTROL_BITS_COLS_DATA       32
#define CONTROL_ADDR_ROI_X_DATA      0x38
#define CONTROL_BITS_ROI_X_DATA      32
#define CONTROL_ADDR_ROI_Y_DATA      0x40
#define CONTROL_BITS_ROI_Y_DATA      32
#define CONTROL_ADDR_ROI_W_DATA      0x48
#define CONTROL_BITS_ROI_W_DATA      32
#define CONTROL_ADDR_ROI_H_DATA      0x50
#define CONTROL_BITS_ROI_H_DATA      32
#define CONTROL_ADDR_THRESH_VAL_DATA 0x58
#define CONTROL_BITS_THRESH_VAL_DATA 8
