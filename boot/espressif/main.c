/*
 * Copyright (c) 2021 Espressif Systems (Shanghai) Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <bootutil/bootutil.h>
#include <bootutil/image.h>

#include <mcuboot_config/mcuboot_logging.h>

#include <bootloader_init.h>
#include <esp_loader.h>

void do_boot(struct boot_rsp *rsp)
{
    MCUBOOT_LOG_INF("br_image_off = 0x%x", rsp->br_image_off);
    MCUBOOT_LOG_INF("ih_hdr_size = 0x%x", rsp->br_hdr->ih_hdr_size);
    esp_app_image_load(0, rsp->br_hdr->ih_hdr_size);
}

int main()
{
    bootloader_init();
    struct boot_rsp rsp;
    int rv = boot_go(&rsp);

    if (rv == 0) {
        do_boot(&rsp);
    } else {
        MCUBOOT_LOG_ERR("Image not bootable");
    }
    while(1);
}
