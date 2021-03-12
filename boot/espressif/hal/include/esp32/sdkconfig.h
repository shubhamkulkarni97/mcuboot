/*
 * Copyright (c) 2021 Espressif Systems (Shanghai) Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define CONFIG_IDF_TARGET_ESP32 1
#define CONFIG_SPI_FLASH_ROM_DRIVER_PATCH 1
#define CONFIG_ESP32_XTAL_FREQ 40
#define CONFIG_MCUBOOT 1
#define NDEBUG 1
#define CONFIG_BOOTLOADER_WDT_TIME_MS 9000
#define CONFIG_ESP_CONSOLE_UART_BAUDRATE 115200
