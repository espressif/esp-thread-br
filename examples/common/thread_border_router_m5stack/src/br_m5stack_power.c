/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 */

#include "br_m5stack_power.h"
#include "br_m5stack_common.h"

#include "esp_check.h"
#include "esp_err.h"
#include "bsp/esp-bsp.h"

esp_err_t br_m5stack_enable_port_c_power(void)
{
    esp_err_t ret = ESP_OK;
    uint8_t data[2];
    uint8_t val = 0;

    data[0] = 0x02;
    ESP_GOTO_ON_ERROR(i2c_master_write_read_device(BSP_I2C_NUM, BSP_AW9523_ADDR, data, 1, &val, 1, pdMS_TO_TICKS(1000)),
                      exit, "", "");
    data[1] = val | CORE_S3_BUS_OUT_EN;
    ESP_GOTO_ON_ERROR(i2c_master_write_to_device(BSP_I2C_NUM, BSP_AW9523_ADDR, data, sizeof(data), pdMS_TO_TICKS(1000)),
                      exit, "", "");

    data[0] = 0x03;
    ESP_GOTO_ON_ERROR(i2c_master_write_read_device(BSP_I2C_NUM, BSP_AW9523_ADDR, data, 1, &val, 1, pdMS_TO_TICKS(1000)),
                      exit, "", "");
    data[1] = val | CORE_S3_BOOST_EN;
    ESP_GOTO_ON_ERROR(i2c_master_write_to_device(BSP_I2C_NUM, BSP_AW9523_ADDR, data, sizeof(data), pdMS_TO_TICKS(1000)),
                      exit, "", "");

    data[0] = 0x11;
    data[1] = 0x10;
    ret = i2c_master_write_to_device(BSP_I2C_NUM, BSP_AW9523_ADDR, data, sizeof(data), pdMS_TO_TICKS(1000));

exit:
    if (ret != ESP_OK) {
        ESP_LOGE(BR_M5STACK_TAG, "Failed to enable PORT.C for M5Stack.");
    }
    return ret;
}
