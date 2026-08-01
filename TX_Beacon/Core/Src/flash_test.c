/**
 * flash_test.c — STM32WB1M Flash R/W/Erase diagnostic
 *
 * STM32WB1M (WB15): page size = 2048 bytes, 160 pages (0-159).
 * Page 55 @ 0x0801B800 — above firmware (<40KB), below secure area (>140 pages).
 */

#include "stm32wbxx_hal.h"
#include <stdio.h>
#include <string.h>

/* ── Config ────────────────────────────────────────────────── */
#define TEST_PAGE        55U                   /* 2KB page 55 = 0x0801B800 */
#define TEST_ADDR        (0x08000000U + TEST_PAGE * 2048U)  /* 0x0801B800 */
_Static_assert(TEST_ADDR == 0x0801B800U, "TEST_ADDR page size mismatch");
#define TEST_PATTERN_A   0xDEADBEEFUL
#define TEST_PATTERN_B   0xCAFEBABEUL
#define TEST_WORDS       8U                    /* 64-bit double-words  */

/* ── Helpers ───────────────────────────────────────────────── */
static void flash_print_sr(void)
{
    uint32_t sr = FLASH->SR;
    printf("  FLASH->SR = 0x%08lX", sr);
    if (sr & FLASH_SR_BSY)    printf(" BSY");
    if (sr & FLASH_SR_EOP)    printf(" EOP");
    if (sr & FLASH_SR_WRPERR) printf(" WRPERR(write-protected!)");
    if (sr & FLASH_SR_PGAERR) printf(" PGAERR(alignment!)");
    if (sr & FLASH_SR_SIZERR) printf(" SIZERR(size!)");
    if (sr & FLASH_SR_PGSERR) printf(" PGSERR(sequence!)");
    if (sr & FLASH_SR_MISERR) printf(" MISERR");
    if (sr & FLASH_SR_FASTERR)printf(" FASTERR");
    printf("\r\n");
}

static void flash_clear_errors(void)
{
    /* Write 1 to clear error flags */
    FLASH->SR = FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR |
                FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR |
                FLASH_SR_EOP;
}

/* ── Test 1: Erase ─────────────────────────────────────────── */
static int test_erase(void)
{
    printf("\r\n[TEST1] Erase page %u (0x%08lX)...\r\n",
           TEST_PAGE, (unsigned long)TEST_ADDR);

    HAL_StatusTypeDef st;
    uint32_t page_error = 0;

    FLASH_EraseInitTypeDef erase = {
        .TypeErase = FLASH_TYPEERASE_PAGES,
        .Page      = TEST_PAGE,
        .NbPages   = 1,
    };

    /* Unlock */
    st = HAL_FLASH_Unlock();
    if (st != HAL_OK) {
        printf("  FAIL: Unlock returned %d\r\n", st);
        flash_print_sr();
        return -1;
    }

    flash_clear_errors();

    /* Erase */
    st = HAL_FLASHEx_Erase(&erase, &page_error);

    if (st != HAL_OK) {
        printf("  FAIL: Erase returned %d, page_error=0x%08lX\r\n",
               st, (unsigned long)page_error);
        flash_print_sr();
        HAL_FLASH_Lock();
        return -2;
    }

    HAL_FLASH_Lock();

    /* Verify all 0xFF */
    int ok = 1;
    for (uint32_t i = 0; i < 2048U / 4U; i++) {  /* 2KB page = 512 words */
        uint32_t val = *(volatile uint32_t *)(TEST_ADDR + i * 4U);
        if (val != 0xFFFFFFFFUL) {
            printf("  FAIL: addr 0x%08lX = 0x%08lX (expected 0xFFFFFFFF)\r\n",
                   (unsigned long)(TEST_ADDR + i * 4U),
                   (unsigned long)val);
            ok = 0;
            break;
        }
    }

    if (ok) printf("  PASS: Page erased, all 0xFF\r\n");
    return ok ? 0 : -3;
}

/* ── Test 2: Write ─────────────────────────────────────────── */
static int test_write(uint64_t pattern)
{
    printf("\r\n[TEST2] Write pattern 0x%08lX%08lX...\r\n",
           (unsigned long)(pattern >> 32),
           (unsigned long)(pattern & 0xFFFFFFFF));

    HAL_StatusTypeDef st;
    st = HAL_FLASH_Unlock();
    if (st != HAL_OK) {
        printf("  FAIL: Unlock returned %d\r\n", st);
        return -1;
    }

    flash_clear_errors();

    int ok = 1;
    for (uint32_t i = 0; i < TEST_WORDS; i++) {
        uint32_t addr = TEST_ADDR + i * 8U; /* 8 bytes = 64-bit word */

        /* STM32WB writes 64-bit (double-word) at a time */
        st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                               addr, pattern);
        if (st != HAL_OK) {
            printf("  FAIL: Program at 0x%08lX returned %d\r\n",
                   (unsigned long)addr, st);
            flash_print_sr();
            ok = 0;
            break;
        }
    }

    HAL_FLASH_Lock();

    if (ok) printf("  PASS: %lu double-words written\r\n",
                   (unsigned long)TEST_WORDS);
    return ok ? 0 : -2;
}

/* ── Test 3: Read & Verify ─────────────────────────────────── */
static int test_read(uint64_t expected_pattern)
{
    printf("\r\n[TEST3] Read & verify pattern 0x%08lX%08lX...\r\n",
           (unsigned long)(expected_pattern >> 32),
           (unsigned long)(expected_pattern & 0xFFFFFFFF));

    int ok = 1;
    for (uint32_t i = 0; i < TEST_WORDS; i++) {
        uint32_t addr = TEST_ADDR + i * 8U;
        uint64_t val = *(volatile uint64_t *)addr;

        if (val != expected_pattern) {
            printf("  FAIL at 0x%08lX: got 0x%08lX%08lX expected 0x%08lX%08lX\r\n",
                   (unsigned long)addr,
                   (unsigned long)(val >> 32),
                   (unsigned long)(val & 0xFFFFFFFF),
                   (unsigned long)(expected_pattern >> 32),
                   (unsigned long)(expected_pattern & 0xFFFFFFFF));
            ok = 0;
        }
    }

    if (ok) printf("  PASS: All %lu words match\r\n", (unsigned long)TEST_WORDS);
    return ok ? 0 : -1;
}

/* ── Test 4: Write without prior erase (should fail / corrupt) */
static int test_write_no_erase(void)
{
    printf("\r\n[TEST4] Write WITHOUT erase (must erase first!)...\r\n");
    printf("  Attempting to overwrite 0xDEADBEEF with 0xCAFEBABE\r\n");

    HAL_StatusTypeDef st;
    st = HAL_FLASH_Unlock();
    flash_clear_errors();

    uint64_t pattern = ((uint64_t)TEST_PATTERN_B << 32) | TEST_PATTERN_B;
    st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                           TEST_ADDR, pattern);

    flash_print_sr();

    if (st != HAL_OK) {
        printf("  Expected FAIL: HAL returned %d (Flash won't write without erase)\r\n", st);
    } else {
        uint64_t val = *(volatile uint64_t *)TEST_ADDR;
        printf("  Result at 0x%08lX = 0x%08lX%08lX\r\n",
               (unsigned long)TEST_ADDR,
               (unsigned long)(val >> 32),
               (unsigned long)(val & 0xFFFFFFFF));
        printf("  NOTE: Flash can only set bits 1→0, not 0→1 without erase!\r\n");
    }

    HAL_FLASH_Lock();
    return 0; /* test is informational */
}

/* ── Test 5: Double-erase (already erased page) ────────────── */
static int test_double_erase(void)
{
    printf("\r\n[TEST5] Erase already-erased page (should succeed)...\r\n");
    /* After test1, page is already erased — erase again */
    return test_erase();
}

/* ── Test 6: Rapid erase-write-read cycle ──────────────────── */
static int test_cycle(uint32_t n)
{
    printf("\r\n[TEST6] Rapid cycle x%lu (erase→write→read→repeat)...\r\n",
           (unsigned long)n);

    int fails = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t pat = ((uint64_t)(i + 1) << 32) | (i + 1);

        if (test_erase() != 0)       { fails++; break; }
        if (test_write(pat) != 0)    { fails++; break; }
        if (test_read(pat) != 0)     { fails++; break; }

        /* Brief pause — let Flash cool down */
        HAL_Delay(10);

        if ((i % 5) == 4)
            printf("  Cycle %lu/%lu OK\r\n",
                   (unsigned long)(i + 1), (unsigned long)n);
    }

    if (fails == 0)
        printf("  PASS: All %lu cycles OK\r\n", (unsigned long)n);
    else
        printf("  FAIL: %d errors\r\n", fails);

    return fails ? -1 : 0;
}

/* ── Test 7: Check write protection ───────────────────────────*/
static void test_write_protection(void)
{
    printf("\r\n[TEST7] Write protection check...\r\n");

    /* Read Option Bytes */
    uint32_t wrp1a = FLASH->WRP1AR; /* pages 0-31 */
    uint32_t wrp1b = FLASH->WRP1BR; /* pages 0-31 alt */

    printf("  WRP1AR = 0x%08lX\r\n", (unsigned long)wrp1a);
    printf("  WRP1BR = 0x%08lX\r\n", (unsigned long)wrp1b);

    /* If bits set — those pages are write-protected */
    uint32_t start_a = wrp1a & 0xFF;
    uint32_t end_a   = (wrp1a >> 16) & 0xFF;

    if (start_a <= end_a && end_a != 0) {
        printf("  WARNING: Pages %lu-%lu are WRITE PROTECTED!\r\n",
               (unsigned long)start_a, (unsigned long)end_a);
        if (TEST_PAGE >= start_a && TEST_PAGE <= end_a)
            printf("  ERROR: Test page %u is PROTECTED — this causes WRPERR!\r\n",
                   TEST_PAGE);
    } else {
        printf("  OK: No write protection active on WRP1AR\r\n");
    }

    /* Check RDP level */
    uint32_t optr = FLASH->OPTR;
    uint8_t rdp = optr & 0xFF;
    printf("  RDP level = 0x%02X (%s)\r\n", rdp,
           rdp == 0xAA ? "Level 0 (no protection)" :
           rdp == 0xBB ? "Level 1 (read protected)" :
           rdp == 0xCC ? "Level 2 (PERMANENT LOCK)" : "Unknown");

    /* Check nSWboot0 etc */
    printf("  OPTR = 0x%08lX\r\n", (unsigned long)optr);
    printf("  nBOOT0=%lu nBOOT1=%lu nSWBOOT0=%lu\r\n",
           (unsigned long)((optr >> 27) & 1),
           (unsigned long)((optr >> 23) & 1),
           (unsigned long)((optr >> 26) & 1));
}

/* ── Main test runner ──────────────────────────────────────── */
void Flash_RunTests(void)
{
    printf("\r\n");
    printf("========================================\r\n");
    printf("  STM32WB1M Flash Diagnostic\r\n");
    printf("  Test page: %u @ 0x%08lX\r\n",
           TEST_PAGE, (unsigned long)TEST_ADDR);
    printf("========================================\r\n");

    /* Always check protection first */
    test_write_protection();

    int r;

    /* Basic erase */
    r = test_erase();
    if (r != 0) { printf("\r\nFATAL: Erase failed, stopping.\r\n"); return; }

    /* Write pattern A */
    uint64_t patA = ((uint64_t)TEST_PATTERN_A << 32) | TEST_PATTERN_A;
    r = test_write(patA);
    if (r != 0) { printf("\r\nFATAL: Write failed, stopping.\r\n"); return; }

    /* Read back pattern A */
    r = test_read(patA);
    if (r != 0) printf("  NOTE: Read verify failed\r\n");

    /* Try to overwrite without erase */
    test_write_no_erase();

    /* Erase again */
    r = test_erase();
    if (r != 0) { printf("\r\nFATAL: Second erase failed!\r\n"); return; }

    /* Write pattern B */
    uint64_t patB = ((uint64_t)TEST_PATTERN_B << 32) | TEST_PATTERN_B;
    r = test_write(patB);
    if (r != 0) { printf("\r\nFATAL: Second write failed!\r\n"); return; }

    r = test_read(patB);

    /* Double erase */
    test_double_erase();

    /* 10 rapid cycles */
    test_cycle(10);

    printf("\r\n========================================\r\n");
    printf("  Flash diagnostic complete.\r\n");
    printf("  If all PASS: Flash hardware is OK.\r\n");
    printf("  If WRPERR:   Check write protection.\r\n");
    printf("  If PGSERR:   Sequence error in code.\r\n");
    printf("  If PGAERR:   Alignment error (need 8-byte aligned addr).\r\n");
    printf("========================================\r\n");
}
