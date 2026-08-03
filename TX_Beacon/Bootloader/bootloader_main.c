/*
 * TX_Beacon OTA Bootloader — STM32WB1M
 *
 * Responsibilities (ONLY these — keep this file small and correct forever):
 *   1. Read BootState from OTA metadata page
 *   2. If pending_slot: copy Slot B → Slot A page by page, update BootState
 *   3. Set VTOR to Slot A, load MSP+PC, jump
 *
 * Copy approach (not direct-jump):
 *   The app binary is always linked for Slot A address (0x08002800).
 *   Slot B is a download buffer only.  On update: bootloader copies B→A
 *   and boots A.  This avoids needing a position-independent binary and
 *   keeps the jump target address constant (0x08002800 always).
 *
 * Rollback safety:
 *   - Power loss BEFORE copy starts: Slot A has old (good) firmware → boots normally.
 *   - Power loss DURING copy: Slot A partially written.  On next boot,
 *     pending_slot is still B (boot_attempts ≤ MAX), so copy retries from B
 *     (B is intact).  Retry succeeds on next power cycle.
 *   - App crashes/IWDG reset after copy: boot_attempts incremented each time.
 *     After BOOT_MAX_ATTEMPTS unsuccessful boots (app never self-confirms),
 *     bootloader clears pending_slot and tries Slot A as-is.  If the image
 *     in Slot A is consistently bad, the device is NOT bricked — it hangs in
 *     the fault handler rather than bootlooping infinitely, and can be
 *     recovered via SWD.
 */

#include <stdint.h>
#include <string.h>

/* ── Layout constants (duplicated from ota_flash_map.h to keep bootloader
 *    self-contained — must be kept in sync manually) ──────────────────────── */
#define OTA_PAGE_SIZE         2048U
#define OTA_FLASH_BASE        0x08000000UL
#define OTA_SLOT_A_PAGE       5U
#define OTA_SLOT_A_ADDR       (OTA_FLASH_BASE + (uint32_t)OTA_SLOT_A_PAGE * OTA_PAGE_SIZE)
#define OTA_SLOT_B_PAGE       47U
#define OTA_SLOT_B_ADDR       (OTA_FLASH_BASE + (uint32_t)OTA_SLOT_B_PAGE * OTA_PAGE_SIZE)
#define OTA_SLOT_PAGE_COUNT   42U
#define OTA_SLOT_SIZE         ((uint32_t)OTA_SLOT_PAGE_COUNT * OTA_PAGE_SIZE)
#define OTA_META_PAGE         89U
#define OTA_META_ADDR         (OTA_FLASH_BASE + (uint32_t)OTA_META_PAGE * OTA_PAGE_SIZE)

#define OTA_IMAGE_MAGIC       0x4241544FUL
#define BOOT_SLOT_A           0U
#define BOOT_SLOT_B           1U
#define BOOT_NO_PENDING       0xFFU
#define BOOT_MAX_ATTEMPTS     3U

/* ── Types (duplicated from ota_types.h) ─────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t image_size;
    uint32_t crc32;
    uint32_t reserved[4];
} ImageHeader_t;

typedef struct __attribute__((packed)) {
    uint8_t  active_slot;
    uint8_t  pending_slot;
    uint8_t  boot_attempts;
    uint8_t  reserved;
    uint32_t crc32;
} BootState_t;

typedef struct __attribute__((packed)) {
    BootState_t   boot_state;
    ImageHeader_t slot_a_header;
    ImageHeader_t slot_b_header;
} OtaMetaPage_t;

/* ── HAL register definitions (minimal subset, no HAL headers needed) ──────── */
#define FLASH_BASE_REG   0x40022000UL

typedef struct {
    volatile uint32_t ACR;
    volatile uint32_t PDKEYR;
    volatile uint32_t KEYR;
    volatile uint32_t OPTKEYR;
    volatile uint32_t SR;
    volatile uint32_t CR;
    volatile uint32_t ECCR;
    volatile uint32_t RESERVED0;
    volatile uint32_t OPTR;
    volatile uint32_t PCROP1ASR;
    volatile uint32_t PCROP1AER;
    volatile uint32_t WRP1AR;
    volatile uint32_t WRP1BR;
    volatile uint32_t PCROP1BSR;
    volatile uint32_t PCROP1BER;
} FLASH_TypeDef;

#define FLASH   ((FLASH_TypeDef *)FLASH_BASE_REG)

#define FLASH_KEY1   0x45670123UL
#define FLASH_KEY2   0xCDEF89ABUL

#define FLASH_SR_BSY1   (1U << 16)
#define FLASH_SR_PESD   (1U << 19)
#define FLASH_CR_PG     (1U << 0)
#define FLASH_CR_PER    (1U << 1)
#define FLASH_CR_STRT   (1U << 16)
#define FLASH_CR_LOCK   (1U << 31)
#define FLASH_CR_PNB_SHIFT 3U

/* ── CRC32 (zlib: poly 0xEDB88320, init 0xFFFFFFFF) ─────────────────────── */
static uint32_t _crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8U; b++)
            crc = (crc >> 1) ^ (0xEDB88320UL & -(crc & 1U));
    }
    return crc ^ 0xFFFFFFFFUL;
}

/* ── Flash helpers ────────────────────────────────────────────────────────── */

static void _flash_unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = FLASH_KEY1;
        FLASH->KEYR = FLASH_KEY2;
    }
}

static void _flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

static void _flash_wait(void)
{
    while (FLASH->SR & (FLASH_SR_BSY1 | FLASH_SR_PESD));
}

static int _flash_erase_page(uint32_t page)
{
    _flash_wait();
    FLASH->SR = 0xC3FBUL;     /* clear all error flags */
    FLASH->CR = FLASH_CR_PER | (page << FLASH_CR_PNB_SHIFT);
    FLASH->CR |= FLASH_CR_STRT;
    _flash_wait();
    FLASH->CR &= ~(FLASH_CR_PER);
    return ((FLASH->SR & 0xC3FAU) == 0U) ? 1 : 0;
}

static int _flash_write_dword(uint32_t addr, uint64_t dword)
{
    _flash_wait();
    FLASH->SR = 0xC3FBUL;
    FLASH->CR |= FLASH_CR_PG;
    *(volatile uint32_t *)addr         = (uint32_t)(dword);
    *(volatile uint32_t *)(addr + 4U)  = (uint32_t)(dword >> 32U);
    __asm__ volatile ("dsb sy" ::: "memory");
    _flash_wait();
    FLASH->CR &= ~FLASH_CR_PG;
    return ((FLASH->SR & 0xC3FAU) == 0U) ? 1 : 0;
}

/* Copy <bytes> (multiple of 8) from src flash to dst flash.
 * dst must be erased.  Returns 0 on any error. */
static int _flash_copy_block(uint32_t dst, uint32_t src, uint32_t bytes)
{
    for (uint32_t i = 0; i < bytes; i += 8U) {
        uint64_t dw;
        memcpy(&dw, (const void *)(src + i), 8U);
        if (!_flash_write_dword(dst + i, dw)) return 0;
    }
    return 1;
}

/* ── Boot state I/O ─────────────────────────────────────────────────────── */

static OtaMetaPage_t s_meta;   /* cached metadata page (RAM) */

static int _boot_state_valid(const BootState_t *bs)
{
    uint32_t calc = _crc32((const uint8_t *)bs, 4U);  /* first 4 bytes */
    return (calc == bs->crc32) ? 1 : 0;
}

static void _meta_read(void)
{
    memcpy(&s_meta, (const void *)OTA_META_ADDR, sizeof(OtaMetaPage_t));
}

static int _meta_write(void)
{
    _flash_unlock();
    if (!_flash_erase_page(OTA_META_PAGE)) { _flash_lock(); return 0; }

    /* Write 72 bytes (sizeof OtaMetaPage_t), padded to multiple of 8 = 80 bytes */
    uint8_t buf[80U];
    memcpy(buf, &s_meta, sizeof(s_meta));
    memset(buf + sizeof(s_meta), 0xFFU, sizeof(buf) - sizeof(s_meta));

    for (uint32_t i = 0U; i < sizeof(buf); i += 8U) {
        uint64_t dw;
        memcpy(&dw, buf + i, 8U);
        if (!_flash_write_dword(OTA_META_ADDR + i, dw)) {
            _flash_lock();
            return 0;
        }
    }
    _flash_lock();
    return 1;
}

/* ── Slot validation ────────────────────────────────────────────────────── */

static int _slot_b_valid(const ImageHeader_t *hdr)
{
    if (hdr->magic != OTA_IMAGE_MAGIC)             return 0;
    if (hdr->image_size == 0 ||
        hdr->image_size > OTA_SLOT_SIZE)           return 0;
    uint32_t calc = _crc32((const uint8_t *)OTA_SLOT_B_ADDR, hdr->image_size);
    return (calc == hdr->crc32) ? 1 : 0;
}

/* ── Copy Slot B → Slot A ─────────────────────────────────────────────── */

static int _copy_b_to_a(uint32_t image_size)
{
    uint32_t pages = (image_size + OTA_PAGE_SIZE - 1U) / OTA_PAGE_SIZE;
    if (pages > OTA_SLOT_PAGE_COUNT) return 0;

    _flash_unlock();

    for (uint32_t i = 0U; i < pages; i++) {
        /* Erase one Slot A page */
        if (!_flash_erase_page(OTA_SLOT_A_PAGE + i)) {
            _flash_lock();
            return 0;
        }
        /* Copy one page from Slot B */
        uint32_t src = OTA_SLOT_B_ADDR + i * OTA_PAGE_SIZE;
        uint32_t dst = OTA_SLOT_A_ADDR + i * OTA_PAGE_SIZE;
        uint32_t chunk = OTA_PAGE_SIZE;
        if (i == pages - 1U) {
            /* Last page: only copy actual image bytes (padded to 8) */
            uint32_t rem = image_size - i * OTA_PAGE_SIZE;
            chunk = (rem + 7U) & ~7U;
        }
        if (!_flash_copy_block(dst, src, chunk)) {
            _flash_lock();
            return 0;
        }
    }

    _flash_lock();
    return 1;
}

/* ── Jump to application ─────────────────────────────────────────────────
 * Standard Cortex-M4 boot-from-non-zero-address sequence:
 *   1. Disable all interrupts
 *   2. Set VTOR to app vector table
 *   3. Load MSP from vector[0]
 *   4. Load PC  from vector[1] (Reset_Handler)
 *   5. Branch to PC
 */
static void __attribute__((noreturn)) _boot_slot_a(void)
{
    uint32_t vtor = OTA_SLOT_A_ADDR;
    uint32_t msp  = *(volatile uint32_t *)vtor;
    uint32_t pc   = *(volatile uint32_t *)(vtor + 4U);

    /* Basic sanity: MSP must be in SRAM range */
    if ((msp < 0x20000000UL) || (msp > 0x20010000UL) ||
        (pc  < OTA_SLOT_A_ADDR) || (pc >= OTA_SLOT_A_ADDR + OTA_SLOT_SIZE)) {
        /* Bad MSP or PC: halt — recoverable via SWD */
        for (;;) { __asm__ volatile ("bkpt 0"); }
    }

    __asm__ volatile (
        "cpsid  i          \n"   /* mask all interrupts  */
        "ldr    r0, %0     \n"   /* r0 = VTOR address    */
        "str    r0, [%1]   \n"   /* SCB->VTOR = r0       */
        "msr    msp, %2    \n"   /* set MSP              */
        "bx     %3         \n"   /* jump to Reset_Handler*/
        :
        : "m"(vtor),
          "r"(0xE000ED08UL),     /* &SCB->VTOR */
          "r"(msp),
          "r"(pc)
        : "r0", "memory"
    );
    for (;;);   /* unreachable */
}

/* ── Main bootloader entry ─────────────────────────────────────────────── */

void Bootloader_Main(void)
{
    _meta_read();

    BootState_t *bs = &s_meta.boot_state;
    int bs_valid = _boot_state_valid(bs);

    /* Default: boot Slot A directly (fresh device or confirmed firmware) */
    if (!bs_valid || bs->pending_slot == BOOT_NO_PENDING) {
        goto do_boot;
    }

    /* We have a pending update (pending_slot = B after OTA_FINISH). */
    if (bs->pending_slot != BOOT_SLOT_B) {
        /* Unexpected pending slot value — clear and boot current A */
        bs->pending_slot  = BOOT_NO_PENDING;
        bs->boot_attempts = 0U;
        bs->reserved      = 0U;
        bs->crc32 = _crc32((const uint8_t *)bs, 4U);
        _meta_write();
        goto do_boot;
    }

    /* Validate Slot B header + CRC before touching Slot A */
    if (!_slot_b_valid(&s_meta.slot_b_header)) {
        /* Corrupted image — abort update, keep Slot A unchanged */
        bs->pending_slot  = BOOT_NO_PENDING;
        bs->boot_attempts = 0U;
        bs->reserved      = 0U;
        bs->crc32 = _crc32((const uint8_t *)bs, 4U);
        _meta_write();
        goto do_boot;
    }

    /* Slot B looks good. Increment attempts BEFORE copying so that
     * a power loss during copy still counts as an attempt on next boot. */
    if (bs->boot_attempts >= BOOT_MAX_ATTEMPTS) {
        /* Too many failed boots: give up on this update, run whatever is in A */
        bs->pending_slot  = BOOT_NO_PENDING;
        bs->boot_attempts = 0U;
        bs->reserved      = 0U;
        bs->crc32 = _crc32((const uint8_t *)bs, 4U);
        _meta_write();
        goto do_boot;
    }

    bs->boot_attempts++;
    bs->crc32 = _crc32((const uint8_t *)bs, 4U);
    _meta_write();

    /* Copy Slot B → Slot A */
    uint32_t img_size = s_meta.slot_b_header.image_size;
    if (!_copy_b_to_a(img_size)) {
        /* Copy failed — next boot will retry (pending_slot still B, B intact) */
        goto do_boot;
    }

    /* Copy succeeded: update Slot A header in metadata */
    s_meta.slot_a_header = s_meta.slot_b_header;
    /* Leave pending_slot set — app self-confirm will clear it */
    bs->crc32 = _crc32((const uint8_t *)bs, 4U);
    _meta_write();

    /* Verify the copy */
    uint32_t calc = _crc32((const uint8_t *)OTA_SLOT_A_ADDR, img_size);
    if (calc != s_meta.slot_b_header.crc32) {
        /* Copy verify failed — pending_slot still B, will retry next boot */
        goto do_boot;
    }

do_boot:
    _boot_slot_a();
}

/* ── Reset_Handler — minimal startup before Bootloader_Main ─────────────
 * Initialises .data and .bss, then calls Bootloader_Main.
 * No HAL, no RTOS, no C library init beyond memcpy/memset.                */

extern uint32_t _sidata, _sdata, _edata;
extern uint32_t _sbss,   _ebss;

void __attribute__((noreturn)) Bootloader_Reset_Handler(void)
{
    /* Copy .data from flash to RAM */
    uint32_t *src = &_sidata;
    for (uint32_t *dst = &_sdata; dst < &_edata; )
        *dst++ = *src++;

    /* Zero .bss */
    for (uint32_t *dst = &_sbss; dst < &_ebss; )
        *dst++ = 0U;

    Bootloader_Main();
    for (;;);
}

/* ── Minimal vector table ─────────────────────────────────────────────────
 * Only MSP and Reset_Handler are needed; all other vectors point to
 * a fault handler that halts (recoverable via SWD debugger).             */

static void __attribute__((noreturn)) _fault_handler(void)
{
    for (;;) { __asm__ volatile ("bkpt 0"); }
}

__attribute__((section(".isr_vector")))
const uint32_t g_bootloader_vectors[] = {
    /* [0] Initial MSP — end of RAM1 (0x20000008 + 0x2FF8 = 0x20003000) */
    0x20003000UL,
    /* [1] Reset_Handler */
    (uint32_t)Bootloader_Reset_Handler + 1U,   /* +1 for Thumb bit */
    /* [2..15] Core exceptions → fault handler */
    (uint32_t)_fault_handler + 1U,
    (uint32_t)_fault_handler + 1U,
    (uint32_t)_fault_handler + 1U,
    (uint32_t)_fault_handler + 1U,
    (uint32_t)_fault_handler + 1U,
    (uint32_t)_fault_handler + 1U,
    (uint32_t)_fault_handler + 1U,
    (uint32_t)_fault_handler + 1U,
    (uint32_t)_fault_handler + 1U,
    (uint32_t)_fault_handler + 1U,
    (uint32_t)_fault_handler + 1U,
    (uint32_t)_fault_handler + 1U,
    (uint32_t)_fault_handler + 1U,
    (uint32_t)_fault_handler + 1U,
};
