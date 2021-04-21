/*
 * Copyright (c) 2021 Espressif Systems (Shanghai) Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <soc/soc.h>
#include <soc/dport_reg.h>
#include <bootloader_flash.h>
#include <bootloader_flash_priv.h>
#include <bootloader_init.h>
#include <esp_image_format.h>

#include "esp32/rom/cache.h"
#include "esp32/rom/efuse.h"
#include "esp32/rom/ets_sys.h"
#include "esp32/rom/spi_flash.h"
#include "esp32/rom/crc.h"
#include "esp32/rom/rtc.h"
#include "esp32/rom/gpio.h"

#include <esp_loader.h>
#include <flash_map_backend/flash_map_backend.h>
#include <mcuboot_config/mcuboot_logging.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

static bool should_map(uint32_t load_addr)
{
    return (load_addr >= SOC_IROM_LOW && load_addr < SOC_IROM_HIGH)
           || (load_addr >= SOC_DROM_LOW && load_addr < SOC_DROM_HIGH);
}

static bool should_load(uint32_t load_addr)
{
    /* Reload the RTC memory segments whenever a non-deepsleep reset
       is occurring */
    //bool load_rtc_memory = rtc_get_reset_reason(0) != DEEPSLEEP_RESET;
    bool load_rtc_memory = false;

    if (should_map(load_addr)) {
        return false;
    }

    if (load_addr < 0x10000000) {
        // Reserved for non-loaded addresses.
        // Current reserved values are
        // 0x0 (padding block)
        // 0x4 (unused, but reserved for an MD5 block)
        return false;
    }

    if (!load_rtc_memory) {
        if (load_addr >= SOC_RTC_IRAM_LOW && load_addr < SOC_RTC_IRAM_HIGH) {
            return false;
        }
        if (load_addr >= SOC_RTC_DRAM_LOW && load_addr < SOC_RTC_DRAM_HIGH) {
            return false;
        }
        if (load_addr >= SOC_RTC_DATA_LOW && load_addr < SOC_RTC_DATA_HIGH) {
            return false;
        }
    }

    return true;
}

static esp_err_t verify_segment_header(esp_image_segment_header_t *segment, uint32_t segment_data_offs)
{
    if ((segment->data_len & 3) != 0
            || segment->data_len >= 0x1000000) {
        return ESP_ERR_IMAGE_INVALID;
    }

    uint32_t load_addr = segment->load_addr;
    bool map_segment = should_map(load_addr);

    /* Check that flash cache mapped segment aligns correctly from flash to its mapped address,
       relative to the 64KB page mapping size.
    */
    if (map_segment
            && ((segment_data_offs % SPI_FLASH_MMU_PAGE_SIZE) != (load_addr % SPI_FLASH_MMU_PAGE_SIZE))) {
        return ESP_ERR_IMAGE_INVALID;
    }

    return ESP_OK;
}

static int process_segment_data(intptr_t load_addr, uint32_t data_addr, uint32_t data_len, bool do_load)
{
    if (!do_load) {
        return ESP_OK;
    }
    const uint32_t *data = (const uint32_t *)bootloader_mmap(data_addr, data_len);
    if (!data) {
        MCUBOOT_LOG_ERR("%s: Bootloader nmap failed", __func__);
        return -1;
    }
    memcpy((void *)load_addr, data, data_len);
    bootloader_munmap(data);
    return 0;
}

static int process_segment(const struct flash_area *fap, unsigned int offset, esp_image_segment_header_t *header)
{
    int rc;

    rc = flash_area_read(fap, offset, header, sizeof(esp_image_segment_header_t));
    if (rc != 0) {
        MCUBOOT_LOG_ERR("%s: Error in flash read", __func__);
        return rc;
    }
    intptr_t load_addr = header->load_addr;
    uint32_t data_len = header->data_len;
    uint32_t data_addr = offset + sizeof(esp_image_segment_header_t);
    
    MCUBOOT_LOG_INF("%s: segment data length 0x%x data starts 0x%x load address %p", __func__, data_len, data_addr, load_addr);
    if (verify_segment_header(header, fap->fa_off + data_addr) != ESP_OK) {
        MCUBOOT_LOG_ERR("%s: Segment header verification failed", __func__);
        return -1;
    }

    bool do_load = should_load(load_addr);
    uint32_t free_page_count = bootloader_mmap_get_free_pages();
    uint32_t data_len_remain = data_len;
    while (data_len_remain > 0) {
        uint32_t offset_page = ((data_addr & MMAP_ALIGNED_MASK) != 0) ? 1 : 0;
        data_len = MIN(data_len_remain, ((free_page_count - offset_page) * SPI_FLASH_MMU_PAGE_SIZE));
        rc = process_segment_data(load_addr, fap->fa_off + data_addr, data_len, do_load);
        data_addr += data_len;
        data_len_remain -= data_len;
    }
    return 0;
}

static void set_cache_and_start_app(
    uint32_t drom_addr,
    uint32_t drom_load_addr,
    uint32_t drom_size,
    uint32_t irom_addr,
    uint32_t irom_load_addr,
    uint32_t irom_size,
    uint32_t entry_addr)
{
    int rc;
    Cache_Read_Disable(0);
    Cache_Flush(0);

    /* Clear the MMU entries that are already set up,
       so the new app only has the mappings it creates.
    */
    for (int i = 0; i < DPORT_FLASH_MMU_TABLE_SIZE; i++) {
        DPORT_PRO_FLASH_MMU_TABLE[i] = DPORT_FLASH_MMU_TABLE_INVALID_VAL;
    }
    uint32_t drom_load_addr_aligned = drom_load_addr & MMU_FLASH_MASK;
    uint32_t drom_page_count = bootloader_cache_pages_to_map(drom_size, drom_load_addr);
    rc = cache_flash_mmu_set(0, 0, drom_load_addr_aligned, drom_addr & MMU_FLASH_MASK, 64, drom_page_count);
    rc |= cache_flash_mmu_set(1, 0, drom_load_addr_aligned, drom_addr & MMU_FLASH_MASK, 64, drom_page_count);
    uint32_t irom_load_addr_aligned = irom_load_addr & MMU_FLASH_MASK;
    uint32_t irom_page_count = bootloader_cache_pages_to_map(irom_size, irom_load_addr);
    rc |= cache_flash_mmu_set(0, 0, irom_load_addr_aligned, irom_addr & MMU_FLASH_MASK, 64, irom_page_count);
    rc |= cache_flash_mmu_set(1, 0, irom_load_addr_aligned, irom_addr & MMU_FLASH_MASK, 64, irom_page_count);
    DPORT_REG_CLR_BIT( DPORT_PRO_CACHE_CTRL1_REG,
                       (DPORT_PRO_CACHE_MASK_IRAM0) | (DPORT_PRO_CACHE_MASK_IRAM1 & 0) |
                       (DPORT_PRO_CACHE_MASK_IROM0 & 0) | DPORT_PRO_CACHE_MASK_DROM0 |
                       DPORT_PRO_CACHE_MASK_DRAM1 );
    DPORT_REG_CLR_BIT( DPORT_APP_CACHE_CTRL1_REG,
                       (DPORT_APP_CACHE_MASK_IRAM0) | (DPORT_APP_CACHE_MASK_IRAM1 & 0) |
                       (DPORT_APP_CACHE_MASK_IROM0 & 0) | DPORT_APP_CACHE_MASK_DROM0 |
                       DPORT_APP_CACHE_MASK_DRAM1 );
    Cache_Read_Enable(0);

    if (rc) {
        MCUBOOT_LOG_ERR("%s: Failed to start app (0x%x)", __func__, rc);
        return;
    }
    // Application will need to do Cache_Flush(1) and Cache_Read_Enable(1)

    typedef void (*entry_t)(void) __attribute__((noreturn));
    entry_t entry = ((entry_t) entry_addr);

    // TODO: we have used quite a bit of stack at this point.
    // use "movsp" instruction to reset stack back to where ROM stack starts.
    (*entry)();
}

static void unpack_load_app(const struct flash_area *fap, const esp_image_metadata_t *data)
{
    uint32_t drom_addr = 0;
    uint32_t drom_load_addr = 0;
    uint32_t drom_size = 0;
    uint32_t irom_addr = 0;
    uint32_t irom_load_addr = 0;
    uint32_t irom_size = 0;

    // Find DROM & IROM addresses, to configure cache mappings
    for (int i = 0; i < data->image.segment_count; i++) {
        const esp_image_segment_header_t *header = &data->segments[i];
        if (header->load_addr >= SOC_DROM_LOW && header->load_addr < SOC_DROM_HIGH) {
            if (drom_addr != 0) {
                MCUBOOT_LOG_ERR("%s: DROM Mapping failed", __func__);
            } else {
                MCUBOOT_LOG_INF("%s: Mapping segment %d as %s", __func__, i, "DROM");
            }
            drom_addr = data->segment_data[i] + fap->fa_off;
            drom_load_addr = header->load_addr;
            drom_size = header->data_len;
        }
        if (header->load_addr >= SOC_IROM_LOW && header->load_addr < SOC_IROM_HIGH) {
            if (irom_addr != 0) {
                MCUBOOT_LOG_ERR("%s: IROM Mapping failed", __func__);
            } else {
                MCUBOOT_LOG_INF("%s: Mapping segment %d as %s", __func__, i, "IROM");
            }
            irom_addr = data->segment_data[i] + fap->fa_off;
            irom_load_addr = header->load_addr;
            irom_size = header->data_len;
        }
    }

    set_cache_and_start_app(drom_addr,
                            drom_load_addr,
                            drom_size,
                            irom_addr,
                            irom_load_addr,
                            irom_size,
                            data->image.entry_addr);
}

void esp_app_image_load(int slot, unsigned int hdr_offset)
{
    const struct flash_area *fap;
    int area_id;
    int rc;

    area_id = flash_area_id_from_image_slot(slot);
    rc = flash_area_open(area_id, &fap);
    if (rc != 0) {
        MCUBOOT_LOG_ERR("%s: flash_area_open failed with %d", __func__, rc);
        goto done;
    }

    esp_image_metadata_t data;
    rc = flash_area_read(fap, hdr_offset, &data.image, sizeof(esp_image_header_t));
    if (rc != 0) {
        MCUBOOT_LOG_ERR("%s: flash_area_read failed with %d", __func__, rc);
        goto done;
    }
    MCUBOOT_LOG_INF("%s: image header: 0x%02x 0x%02x 0x%02x 0x%02x %08x",
            __func__,
            data.image.magic,
            data.image.segment_count,
            data.image.spi_mode,
            data.image.spi_size,
            data.image.entry_addr);

    uint32_t next_addr = hdr_offset + sizeof(esp_image_header_t);
    for (int i = 0; i < data.image.segment_count; i++) {
        esp_image_segment_header_t *header = &data.segments[i];
        process_segment(fap, next_addr, header);
        next_addr += sizeof(esp_image_segment_header_t);
        data.segment_data[i] = next_addr;
        next_addr += header->data_len;
    }
    flash_area_close(fap);
    unpack_load_app(fap, &data);
done:
    flash_area_close(fap);
}
