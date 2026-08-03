#pragma once
#include <stdint.h>

/*
 * OTA types shared between the app, bootloader, and drv_ota.c.
 *
 * Memory map of the OTA metadata page (OTA_META_PAGE):
 *   [0..7]    BootState_t     — active/pending slot and boot-attempt counter
 *   [8..39]   ImageHeader_t   — header for Slot A image (magic, version, CRC)
 *   [40..71]  ImageHeader_t   — header for Slot B image
 *   [72..]    0xFF (unused)
 */

/* ── ImageHeader_t ───────────────────────────────────────────────────────── */
#define OTA_IMAGE_MAGIC   0x4241544FUL   /* "OTAB" little-endian               */

typedef struct __attribute__((packed)) {
    uint32_t magic;        /* OTA_IMAGE_MAGIC                                  */
    uint32_t version;      /* (major<<16)|(minor<<8)|patch                     */
    uint32_t image_size;   /* bytes of app binary in the slot                  */
    uint32_t crc32;        /* CRC32 (zlib poly) over image_size bytes @ slot   */
    uint32_t reserved[4];  /* always 0                                         */
} ImageHeader_t;           /* 32 bytes                                         */
_Static_assert(sizeof(ImageHeader_t) == 32U, "ImageHeader_t must be 32 bytes");

/* ── BootState_t ─────────────────────────────────────────────────────────── */
#define BOOT_SLOT_A         0U
#define BOOT_SLOT_B         1U
#define BOOT_NO_PENDING     0xFFU
#define BOOT_MAX_ATTEMPTS   3U    /* copies attempted before giving up pending  */

typedef struct __attribute__((packed)) {
    uint8_t  active_slot;     /* BOOT_SLOT_A or BOOT_SLOT_B                    */
    uint8_t  pending_slot;    /* BOOT_NO_PENDING (0xFF) or slot to copy+boot   */
    uint8_t  boot_attempts;   /* incremented by bootloader each copy attempt   */
    uint8_t  reserved;        /* always 0                                       */
    uint32_t crc32;           /* CRC32 of bytes [0..3] above                   */
} BootState_t;                /* 8 bytes                                        */
_Static_assert(sizeof(BootState_t) == 8U, "BootState_t must be 8 bytes");

/* ── Metadata page in-memory layout ─────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    BootState_t   boot_state;     /* [0..7]   */
    ImageHeader_t slot_a_header;  /* [8..39]  */
    ImageHeader_t slot_b_header;  /* [40..71] */
} OtaMetaPage_t;                  /* 72 bytes — rest of 2KB page is 0xFF       */
_Static_assert(sizeof(OtaMetaPage_t) == 72U, "OtaMetaPage_t must be 72 bytes");
