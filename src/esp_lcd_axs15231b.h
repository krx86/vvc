/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/**
 * @file
 * @brief ESP LCD & Touch: AXS15231B
 */

#pragma once

#include "hal/spi_ll.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_panel_vendor.h"
#include <stddef.h>

#define ESP_LCD_AXS15231B_VER_MAJOR    (1)
#define ESP_LCD_AXS15231B_VER_MINOR    (0)
#define ESP_LCD_AXS15231B_VER_PATCH    (0)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LCD panel initialization commands.
 *
 */
typedef struct {
    int cmd;                /*<! The specific LCD command */
    const void *data;       /*<! Buffer that holds the command specific data */
    size_t data_bytes;      /*<! Size of `data` in memory, in bytes */
    unsigned int delay_ms;  /*<! Delay in milliseconds after this command */
} axs15231b_lcd_init_cmd_t;

/**
 * @brief LCD panel vendor configuration.
 *
 * @note  This structure needs to be passed to the `vendor_config` field in `esp_lcd_panel_dev_config_t`.
 *
 */
typedef struct {
    const axs15231b_lcd_init_cmd_t *init_cmds;  /*!< Pointer to initialization commands array. Set to NULL if using default commands.
                                                 *   The array should be declared as `static const` and positioned outside the function.
                                                 *   Please refer to `vendor_specific_init_default` in source file.
                                                 */
    uint16_t init_cmds_size;                    /*<! Number of commands in above array */
    struct {
        unsigned int use_qspi_interface: 1;     /*<! Set to 1 if use QSPI interface, default is SPI interface */
    } flags;
} axs15231b_vendor_config_t;

/**
 * @brief Create LCD panel for model AXS15231B
 *
 * @note  Vendor specific initialization can be different between manufacturers, should consult the LCD supplier for initialization sequence code.
 *
 * @param[in] io LCD panel IO handle
 * @param[in] panel_dev_config general panel device configuration
 * @param[out] ret_panel Returned LCD panel handle
 * @return
 *          - ESP_ERR_INVALID_ARG   if parameter is invalid
 *          - ESP_ERR_NO_MEM        if out of memory
 *          - ESP_OK                on success
 */
esp_err_t esp_lcd_new_panel_axs15231b(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel);

/**
 * @brief LCD panel bus configuration structure
 *
 */
#define AXS15231B_PANEL_BUS_I80_CONFIG(dc, wr, clk, d0, d1, d2, d3, d4, d5, d6, d7, b_w, max_trans_sz) \
    {                                                          \
        .dc_gpio_num = dc,                                     \
        .wr_gpio_num = wr,                                     \
        .clk_src = clk,                                        \
        .data_gpio_nums = {                                    \
            d0,                                                \
            d1,                                                \
            d2,                                                \
            d3,                                                \
            d4,                                                \
            d5,                                                \
            d6,                                                \
            d7,                                                \
        },                                                     \
        .bus_width = b_w,                                      \
        .max_transfer_bytes = max_trans_sz,                    \
    }

#define AXS15231B_PANEL_BUS_SPI_CONFIG(sclk, mosi, max_trans_sz)  \
    {                                                           \
        .sclk_io_num = sclk,                                    \
        .mosi_io_num = mosi,                                    \
        .miso_io_num = -1,                                      \
        .quadhd_io_num = -1,                                    \
        .quadwp_io_num = -1,                                    \
        .max_transfer_sz = max_trans_sz,                        \
    }
#define AXS15231B_PANEL_BUS_QSPI_CONFIG(sclk, d0, d1, d2, d3, max_trans_sz) \
    {                                                           \
        .sclk_io_num = sclk,                                    \
        .data0_io_num = d0,                                     \
        .data1_io_num = d1,                                     \
        .data2_io_num = d2,                                     \
        .data3_io_num = d3,                                     \
        .max_transfer_sz = max_trans_sz,                        \
    }

/**
 * @brief LCD panel IO configuration structure
 *
 */
#define AXS15231B_PANEL_IO_I80_CONFIG(cs, dc, cb, cb_ctx)       \
    {                                                           \
        .cs_gpio_num = cs,                                      \
        .pclk_hz = 20 * 1000 * 1000,                            \
        .on_color_trans_done = cb,                              \
        .trans_queue_depth = 10,                                \
        .user_ctx = cb_ctx,                                     \
        .dc_levels = {                                          \
            .dc_idle_level = 0,                                 \
            .dc_cmd_level = 0,                                  \
            .dc_dummy_level = 0,                                \
            .dc_data_level = 1,                                 \
        },                                                      \
        .lcd_cmd_bits = 8,                                      \
        .lcd_param_bits = 8,                                    \
    }

#define AXS15231B_PANEL_IO_SPI_CONFIG(cs, dc, cb, cb_ctx)       \
    {                                                           \
        .cs_gpio_num = cs,                                      \
        .dc_gpio_num = dc,                                      \
        .spi_mode = 3,                                          \
        .pclk_hz = 40 * 1000 * 1000,                            \
        .trans_queue_depth = 10,                                \
        .on_color_trans_done = cb,                              \
        .user_ctx = cb_ctx,                                     \
        .lcd_cmd_bits = 8,                                      \
        .lcd_param_bits = 8,                                    \
    }

#define AXS15231B_PANEL_IO_QSPI_CONFIG(cs, cb, cb_ctx)          \
    {                                                           \
        .cs_gpio_num = cs,                                      \
        .dc_gpio_num = -1,                                      \
        .spi_mode = 3,                                          \
        .pclk_hz = 40 * 1000 * 1000,                            \
        .trans_queue_depth = 10,                                \
        .on_color_trans_done = cb,                              \
        .user_ctx = cb_ctx,                                     \
        .lcd_cmd_bits = 32,                                     \
        .lcd_param_bits = 8,                                    \
        .flags = {                                              \
            .quad_mode = true,                                  \
        },                                                      \
    }

/**
 * @brief Create a new AXS15231B1B touch driver
 *
 * @note  The I2C communication should be initialized before use this function.
 *
 * @param io LCD panel IO handle, it should be created by `esp_lcd_new_panel_io_i2c()`
 * @param config Touch panel configuration
 * @param tp Touch panel handle
 * @return
 *      - ESP_OK: on success
 */
esp_err_t esp_lcd_touch_new_i2c_axs15231b(const esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *tp);

/**
 * @brief I2C address of the AXS15231B controller
 *
 */
#define ESP_LCD_TOUCH_IO_I2C_AXS15231B_ADDRESS    (0x3B)

/**
 * @brief Touch IO configuration structure
 *
 */
#define ESP_LCD_TOUCH_IO_I2C_AXS15231B_CONFIG()             \
    {                                                       \
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_AXS15231B_ADDRESS, \
        .control_phase_bytes = 1,                           \
        .dc_bit_offset = 0,                                 \
        .lcd_cmd_bits = 8,                                  \
        .flags =                                            \
        {                                                   \
            .disable_control_phase = 1,                     \
        }                                                   \
    }

#ifdef ESP_LCD_AXS15231B_INIT_CMDS_IMPLEMENTATION
const axs15231b_lcd_init_cmd_t axs15231b_lcd_init_cmds[] = {
    {0xBB, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5A, 0xA5}, 8, 0},
    {0xA0,
     (uint8_t[]){0xC0, 0x10, 0x00, 0x02, 0x00, 0x00, 0x04, 0x3F, 0x20, 0x05,
                 0x3F, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00},
     17, 0},
    {0xA2, (uint8_t[]){0x30, 0x3C, 0x24, 0x14, 0xD0, 0x20, 0xFF, 0xE0,
                       0x40, 0x19, 0x80, 0x80, 0x80, 0x20, 0xF9, 0x10,
                       0x02, 0xFF, 0xFF, 0xF0, 0x90, 0x01, 0x32, 0xA0,
                       0x91, 0xE0, 0x20, 0x7F, 0xFF, 0x00, 0x5A},
     31, 0},
    {0xD0,
     (uint8_t[]){0xE0, 0x40, 0x51, 0x24, 0x08, 0x05, 0x10, 0x01, 0x20, 0x15,
                 0x42, 0xC2, 0x22, 0x22, 0xAA, 0x03, 0x10, 0x12, 0x60, 0x14,
                 0x1E, 0x51, 0x15, 0x00, 0x8A, 0x20, 0x00, 0x03, 0x3A, 0x12},
     30, 0},
    {0xA3, (uint8_t[]){0xA0, 0x06, 0xAA, 0x00, 0x08, 0x02, 0x0A, 0x04,
                       0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
                       0x04, 0x04, 0x04, 0x00, 0x55, 0x55},
     22, 0},
    {0xC1,
     (uint8_t[]){0x31, 0x04, 0x02, 0x02, 0x71, 0x05, 0x24, 0x55, 0x02, 0x00,
                 0x41, 0x00, 0x53, 0xFF, 0xFF, 0xFF, 0x4F, 0x52, 0x00, 0x4F,
                 0x52, 0x00, 0x45, 0x3B, 0x0B, 0x02, 0x0D, 0x00, 0xFF, 0x40},
     30, 0},
    {0xC3,
     (uint8_t[]){0x00, 0x00, 0x00, 0x50, 0x03, 0x00, 0x00, 0x00, 0x01, 0x80,
                 0x01},
     11, 0},
    {0xC4,
     (uint8_t[]){0x00, 0x24, 0x33, 0x80, 0x00, 0xEA, 0x64, 0x32, 0xC8, 0x64,
                 0xC8, 0x32, 0x90, 0x90, 0x11, 0x06, 0xDC, 0xFA, 0x00, 0x00,
                 0x80, 0xFE, 0x10, 0x10, 0x00, 0x0A, 0x0A, 0x44, 0x50},
     29, 0},
    {0xC5, (uint8_t[]){0x18, 0x00, 0x00, 0x03, 0xFE, 0x3A, 0x4A, 0x20,
                       0x30, 0x10, 0x88, 0xDE, 0x0D, 0x08, 0x0F, 0x0F,
                       0x01, 0x3A, 0x4A, 0x20, 0x10, 0x10, 0x00},
     23, 0},
    {0xC6,
     (uint8_t[]){0x05, 0x0A, 0x05, 0x0A, 0x00, 0xE0, 0x2E, 0x0B, 0x12, 0x22,
                 0x12, 0x22, 0x01, 0x03, 0x00, 0x3F, 0x6A, 0x18, 0xC8, 0x22},
     20, 0},
    {0xC7,
     (uint8_t[]){0x50, 0x32, 0x28, 0x00, 0xA2, 0x80, 0x8F, 0x00, 0x80, 0xFF,
                 0x07, 0x11, 0x9C, 0x67, 0xFF, 0x24, 0x0C, 0x0D, 0x0E, 0x0F},
     20, 0},
    {0xC9, (uint8_t[]){0x33, 0x44, 0x44, 0x01}, 4, 0},
    {0xCF, (uint8_t[]){0x2C, 0x1E, 0x88, 0x58, 0x13, 0x18, 0x56, 0x18, 0x1E,
                       0x68, 0x88, 0x00, 0x65, 0x09, 0x22, 0xC4, 0x0C, 0x77,
                       0x22, 0x44, 0xAA, 0x55, 0x08, 0x08, 0x12, 0xA0, 0x08},
     27, 0},
    {0xD5,
     (uint8_t[]){0x40, 0x8E, 0x8D, 0x01, 0x35, 0x04, 0x92, 0x74, 0x04, 0x92,
                 0x74, 0x04, 0x08, 0x6A, 0x04, 0x46, 0x03, 0x03, 0x03, 0x03,
                 0x82, 0x01, 0x03, 0x00, 0xE0, 0x51, 0xA1, 0x00, 0x00, 0x00},
     30, 0},
    {0xD6,
     (uint8_t[]){0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE, 0x93, 0x00,
                 0x01, 0x83, 0x07, 0x07, 0x00, 0x07, 0x07, 0x00, 0x03, 0x03,
                 0x03, 0x03, 0x03, 0x03, 0x00, 0x84, 0x00, 0x20, 0x01, 0x00},
     30, 0},
    {0xD7,
     (uint8_t[]){0x03, 0x01, 0x0B, 0x09, 0x0F, 0x0D, 0x1E, 0x1F, 0x18, 0x1D,
                 0x1F, 0x19, 0x40, 0x8E, 0x04, 0x00, 0x20, 0xA0, 0x1F},
     19, 0},
    {0xD8,
     (uint8_t[]){0x02, 0x00, 0x0A, 0x08, 0x0E, 0x0C, 0x1E, 0x1F, 0x18, 0x1D,
                 0x1F, 0x19},
     12, 0},
    {0xD9,
     (uint8_t[]){0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
                 0x1F, 0x1F},
     12, 0},
    {0xDD,
     (uint8_t[]){0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
                 0x1F, 0x1F},
     12, 0},
    {0xDF, (uint8_t[]){0x44, 0x73, 0x4B, 0x69, 0x00, 0x0A, 0x02, 0x90}, 8, 0},
    {0xE0,
     (uint8_t[]){0x3B, 0x28, 0x10, 0x16, 0x0C, 0x06, 0x11, 0x28, 0x5C, 0x21,
                 0x0D, 0x35, 0x13, 0x2C, 0x33, 0x28, 0x0D},
     17, 0},
    {0xE1,
     (uint8_t[]){0x37, 0x28, 0x10, 0x16, 0x0B, 0x06, 0x11, 0x28, 0x5C, 0x21,
                 0x0D, 0x35, 0x14, 0x2C, 0x33, 0x28, 0x0F},
     17, 0},
    {0xE2,
     (uint8_t[]){0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44, 0x32,
                 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D},
     17, 0},
    {0xE3,
     (uint8_t[]){0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44, 0x32,
                 0x0C, 0x14, 0x14, 0x36, 0x32, 0x2F, 0x0F},
     17, 0},
    {0xE4,
     (uint8_t[]){0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44, 0x2E,
                 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D},
     17, 0},
    {0xE5,
     (uint8_t[]){0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44, 0x2E,
                 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0F},
     17, 0},
    {0xA4,
     (uint8_t[]){0x85, 0x85, 0x95, 0x82, 0xAF, 0xAA, 0xAA, 0x80, 0x10, 0x30,
                 0x40, 0x40, 0x20, 0xFF, 0x60, 0x30},
     16, 0},
    {0xA4, (uint8_t[]){0x85, 0x85, 0x95, 0x85}, 4, 0},
    {0xBB, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 8, 0},
    {0x13, (uint8_t[]){0x00}, 0, 0},
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x2C, (uint8_t[]){0x00, 0x00, 0x00, 0x00}, 4, 0},
};
const size_t axs15231b_lcd_init_cmds_size =
    sizeof(axs15231b_lcd_init_cmds) / sizeof(axs15231b_lcd_init_cmds[0]);
#else
extern const axs15231b_lcd_init_cmd_t axs15231b_lcd_init_cmds[];
extern const size_t axs15231b_lcd_init_cmds_size;
#endif

#ifdef __cplusplus
}
#endif
