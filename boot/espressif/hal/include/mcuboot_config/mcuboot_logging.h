/*
 * Copyright (c) 2021 Espressif Systems (Shanghai) Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

extern int ets_printf(const char *fmt, ...);

#define MCUBOOT_LOG_LEVEL_OFF      0
#define MCUBOOT_LOG_LEVEL_ERROR    1
#define MCUBOOT_LOG_LEVEL_WARNING  2
#define MCUBOOT_LOG_LEVEL_INFO     3
#define MCUBOOT_LOG_LEVEL_DEBUG    4

#ifndef MCUBOOT_LOG_LEVEL
#define MCUBOOT_LOG_LEVEL MCUBOOT_LOG_LEVEL_INFO
#endif

#if MCUBOOT_LOG_LEVEL >= MCUBOOT_LOG_LEVEL_ERROR
#define MCUBOOT_LOG_ERR(_fmt, ...)                                      \
    do {                                                                \
            ets_printf("[ERR] " _fmt "\n\r", ##__VA_ARGS__);         \
    } while (0)
#else
#define MCUBOOT_LOG_ERR(_fmt, ...)
#endif

#if MCUBOOT_LOG_LEVEL >= MCUBOOT_LOG_LEVEL_WARNING
#define MCUBOOT_LOG_WRN(_fmt, ...)                                      \
    do {                                                                \
            ets_printf("[WRN] " _fmt "\n\r", ##__VA_ARGS__);         \
    } while (0)
#else
#define MCUBOOT_LOG_WRN(_fmt, ...)
#endif

#if MCUBOOT_LOG_LEVEL >= MCUBOOT_LOG_LEVEL_INFO
#define MCUBOOT_LOG_INF(_fmt, ...)                                      \
    do {                                                                \
            ets_printf("[INF] " _fmt "\n\r", ##__VA_ARGS__);         \
    } while (0)
#else
#define MCUBOOT_LOG_INF(_fmt, ...)
#endif

#if MCUBOOT_LOG_LEVEL >= MCUBOOT_LOG_LEVEL_DEBUG
#define MCUBOOT_LOG_DBG(_fmt, ...)                                      \
    do {                                                                \
            ets_printf("[DBG] " _fmt "\n\r", ##__VA_ARGS__);         \
    } while (0)
#else
#define MCUBOOT_LOG_DBG(_fmt, ...)
#endif

#define MCUBOOT_LOG_MODULE_DECLARE(...)
