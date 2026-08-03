#pragma once
#include <stdint.h>

/*
 * OTA dual-slot flash layout — STM32WB1M (2048-byte pages, 160 total).
 *
 * PRE-REQUISITE — BLE light stack reflash (one-time, via STM32CubeProgrammer):
 *   1. FUS operations → Install BLE stack →
 *      stm32wb1x_BLE_Stack_light_fw.bin  (firstinstall=1)
 *   2. After FUS: read SFSA from Option bytes → confirm SFSA ≥ 99
 *   3. Update OTA_BLE_FIRST_PAGE below to match actual SFSA
 *   4. Run flash_test.c diagnostic to verify pages 68-98 are writable
 *   5. Build with -DOTA_LAYOUT, flash bootloader @ 0x08000000 + app @ 0x08002800
 *
 * Without OTA_LAYOUT (development, app @ 0x08000000):
 *   Config = page 60, Log = pages 61-67, BLE full stack @ page 68+
 *
 * With OTA_LAYOUT (production, bootloader present, BLE light stack):
 *   Layout below applies.
 *
 * Pages 0-4   Bootloader   (10 KB, 5 pages)  — flashed once via SWD, NEVER OTA
 * Pages 5-46  Slot A       (84 KB, 42 pages) — active app; app linked @ 0x08002800
 * Pages 47-88 Slot B       (84 KB, 42 pages) — OTA download buffer; copy→A on boot
 * Page  89    OTA metadata (2 KB,  1 page)   — BootState_t + ImageHeader_t × 2
 * Page  90    Config       (2 KB,  1 page)   — was page 60
 * Page  91    Log header   (2 KB,  1 page)   — was page 61
 * Pages 92-97 Log data     (12 KB, 6 pages)  — was pages 62-67 (same 768 entries)
 * Page  98    HW descriptor(2 KB,  1 page)   — was page 59 (inside Slot B; relocated)
 * Pages 99+   BLE light    (≥98 KB)          — SFSA; DO NOT WRITE
 */

#define OTA_PAGE_SIZE     2048U
#define OTA_FLASH_BASE    0x08000000UL

/* ── Bootloader ──────────────────────────────────────────────────────────── */
#define OTA_BOOT_START_PAGE   0U
#define OTA_BOOT_PAGE_COUNT   5U
#define OTA_BOOT_SIZE         ((uint32_t)OTA_BOOT_PAGE_COUNT * OTA_PAGE_SIZE)
#define OTA_BOOT_ADDR         OTA_FLASH_BASE

/* ── App slots ───────────────────────────────────────────────────────────── */
#define OTA_SLOT_PAGE_COUNT   42U
#define OTA_SLOT_SIZE         ((uint32_t)OTA_SLOT_PAGE_COUNT * OTA_PAGE_SIZE)

#define OTA_SLOT_A_PAGE       5U
#define OTA_SLOT_A_ADDR       (OTA_FLASH_BASE + (uint32_t)OTA_SLOT_A_PAGE * OTA_PAGE_SIZE)

#define OTA_SLOT_B_PAGE       47U
#define OTA_SLOT_B_ADDR       (OTA_FLASH_BASE + (uint32_t)OTA_SLOT_B_PAGE * OTA_PAGE_SIZE)

/* ── OTA metadata (BootState + ImageHeaders) ─────────────────────────────── */
#define OTA_META_PAGE         89U
#define OTA_META_ADDR         (OTA_FLASH_BASE + (uint32_t)OTA_META_PAGE * OTA_PAGE_SIZE)

/* ── Config (was page 60) ────────────────────────────────────────────────── */
#define OTA_CFG_PAGE          90U
#define OTA_CFG_ADDR          (OTA_FLASH_BASE + (uint32_t)OTA_CFG_PAGE * OTA_PAGE_SIZE)

/* ── Log header (was page 61) ────────────────────────────────────────────── */
#define OTA_LOG_HDR_PAGE      91U
#define OTA_LOG_HDR_ADDR      (OTA_FLASH_BASE + (uint32_t)OTA_LOG_HDR_PAGE * OTA_PAGE_SIZE)

/* ── Log data (same 6 pages = 768 entries as original layout) ───────────────
 * NOTE: page 59 (hw_desc) falls inside Slot B (47-88) — WOULD BE ERASED.
 * HW descriptor is relocated to page 98 in OTA_LAYOUT (see hw_desc.h).     */
#define OTA_LOG_DATA_START_PAGE  92U
#define OTA_LOG_DATA_END_PAGE    97U   /* 6 pages = 768 entries (same as original) */
#define OTA_LOG_DATA_PAGES       (OTA_LOG_DATA_END_PAGE - OTA_LOG_DATA_START_PAGE + 1U)
#define OTA_LOG_DATA_START_ADDR  (OTA_FLASH_BASE + (uint32_t)OTA_LOG_DATA_START_PAGE * OTA_PAGE_SIZE)
#define OTA_LOG_ENTRIES_MAX      (OTA_LOG_DATA_PAGES * OTA_PAGE_SIZE / 16U)  /* 768 */

/* ── HW descriptor (was page 59, relocated out of Slot B range) ──────────── */
#define OTA_HW_DESC_PAGE      98U
#define OTA_HW_DESC_ADDR      (OTA_FLASH_BASE + (uint32_t)OTA_HW_DESC_PAGE * OTA_PAGE_SIZE)

/* ── BLE light stack start (update after reflash — check SFSA option byte) ─ */
#define OTA_BLE_FIRST_PAGE    99U   /* UPDATE: set to actual SFSA value        */

/* ── Compile-time sanity ─────────────────────────────────────────────────── */
#if (OTA_SLOT_A_PAGE + OTA_SLOT_PAGE_COUNT) > OTA_SLOT_B_PAGE
#error "OTA: Slot A and Slot B overlap!"
#endif
#if (OTA_SLOT_B_PAGE + OTA_SLOT_PAGE_COUNT) > OTA_META_PAGE
#error "OTA: Slot B and metadata page overlap!"
#endif
#if OTA_LOG_DATA_END_PAGE >= OTA_BLE_FIRST_PAGE
#error "OTA: Log data overlaps BLE stack — update OTA_BLE_FIRST_PAGE after reflash"
#endif
