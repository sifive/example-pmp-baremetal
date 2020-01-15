/* Copyright 2018 SiFive, Inc */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>
#include <stdlib.h>
#include <metal/machine.h>
#include <metal/machine/platform.h>
#include <metal/machine/inline.h>

/*
 * This test will enable PMP regions and then test access to a protected
 * RAM space, where we will enter and exception handler and then exit
 * the test with a code of 0x0, which signifies a passing scenario */

/* !!!!IMPORTANT!!!!
 * Make sure this definition points to a valid RAM location on your design, or
 * this test may not function as expected.  Also, note the regions where this
 * is used below, since they are all linked together to cover the ranges. */
#define RAM_LOCATION_FOR_TEST   0x80000100

/* Arty FPGA Board Example Test Range
 * Top of Range (A_TOR) Regions are defined by [ PMP(i - 1) > a > PMP(i) ],
 * Where 'a' is the address range.  This example uses Top of Range option
 * where each previous PMP address register defines the end of range from
 * the current PMP address register.  See the manual for your custom device
 * to see how many PMP regions exist.  This information also exists in the
 * design.dts file in the freedom-e-sdk/bsp path.
 *
 * For more information on addressing range options, refer to the
 * RISC-V Privileged Specification.
 *
 * Address regions are 4-byte aligned so we right shift by two.
 */
#define REGION0_ADDR         (0x40400000 >> 2)   /* Base for SPI Flash */
#define REGION1_ADDR         (RAM_LOCATION_FOR_TEST >> 2)   /* Base for DTIM0 (ram) */
#define REGION2_ADDR         (0x80008000 >> 2)   /* Base for DTIM1 (ram) */
#define REGION3_ADDR         (0x80010000 >> 2)   /* End of DTIM range */
#if __riscv_xlen == 64
#define REGION4_ADDR        /* Define as needed */
#define REGION5_ADDR        /* Define as needed */
#define REGION6_ADDR        /* Define as needed */
#define REGION7_ADDR        /* Define as needed */
#endif

/* Define PMP Configuration Fields */
#define R               (1 << 0)
#define W               (1 << 1)
#define X               (1 << 2)
#define A(x)            (x & 3) << 3
#define A_OFF           A(0)    /* Disabled */
#define A_TOR           A(1)    /* Top of Range */
#define A_NA4           A(2)    /* Naturally aligned four byte region */
#define A_NAPOT         A(3)    /* Naturally aligned power of two region, >= 8 bytes */
#define L               (1 << 7)  /* Lock bit - Behaviors apply to Machine Mode also, clear only on reset. */
#define OFF             0
#define RWX             (R|W|X)
#define RWXL            (RWX|L)
#define RW              (R|W)
#define RWL             (RW|L)
#define RX              (R|X)
#define RXL             (RX|L)
#define WX              (W|X)
#define WXL             (WX|L)
#define PMP0_CFG_SHIFT  0
#define PMP1_CFG_SHIFT  8
#define PMP2_CFG_SHIFT  16
#define PMP3_CFG_SHIFT  24
#if __riscv_xlen == 64
#define PMP4_CFG_SHIFT  32
#define PMP5_CFG_SHIFT  40
#define PMP6_CFG_SHIFT  48
#define PMP7_CFG_SHIFT  56
#endif

/* Summary of protection - PMP0 is used with REGION0 */
/* 0x00000000 - 0x40400000 RWX to support Debug region 0x0 - 0x1000 using PMP0_CFG */
/* 0x40400000 - 0x80000100 RX for Code in flash using PMP1_CFG */
/* 0x80000100 - 0x80008000 RX (Normally RWX, use RX for test) using PMP2_CFG */
/* 0x8008000 - 0x80010000 RWX using PMP3_CFG */
#define PMPCONFIG0      ( (((RWX|A_TOR|L)&0xFF) << PMP0_CFG_SHIFT) | \
                        (((RX|A_TOR|L)&0xFF) << PMP1_CFG_SHIFT)    | \
                        (((RX|A_TOR|L)&0xFF) << PMP2_CFG_SHIFT)    | \
                        (((RWX|A_TOR|L)&0xFF) << PMP3_CFG_SHIFT)   )
/* NOTE: 64-bit Architectures have pmp0cfg -> pmp7cfg in a single 64-bit CSR, so
 * if needed define additional bytes here for pmp4 - 7 config fields */

#if __riscv_xlen == 32
#define MCAUSE_INTR                         0x80000000UL
#define MCAUSE_CAUSE                        0x000003FFUL
#else
#define MCAUSE_INTR                         0x8000000000000000UL
#define MCAUSE_CAUSE                        0x00000000000003FFUL
#endif
#define MCAUSE_CODE(cause)                  (cause & MCAUSE_CAUSE)

/* Setup some data for testing */
#define DATA_VALUE_NO_PROTECTION            0xAAAAAAAA
#define DATA_VALUE_PMP_ENABLED              0xEEEEEEEE

/* Defines to access CSR registers within C code */
#define read_csr(reg) ({ unsigned long __tmp; \
  asm volatile ("csrr %0, " #reg : "=r"(__tmp)); \
  __tmp; })

#define write_csr(reg, val) ({ \
  asm volatile ("csrw " #reg ", %0" :: "rK"(val)); })

/* Globals */
int *memptr, return_code = 0;
void __attribute__((weak, interrupt)) __mtvec_clint_vector_table(void);
void __attribute__((weak)) default_vector_handler (void);
void __attribute__((weak)) default_exception_handler(void);

/* Main - Setup protection and test */
int main() {

    uintptr_t pmpconfig0_read;
    uintptr_t region0, region1, region2, region3;
    void *mtvec_base;

    /* First, setup mtvec to point to our exception handler table using mtvec.base,
     * and leave mtvec.mode = 0 for default CLINT direct (non-vectored) mode.
     * mtvec.mode is bit[0] for designs with CLINT, or [1:0] using CLIC */
    mtvec_base = (void *)&__mtvec_clint_vector_table;
    write_csr (mtvec, mtvec_base);

    /* setup return code to 1 for fail, this gets written
     * to 0x0 in exception handler for a passing scenario */
    return_code = 1;

    /* IMPORTANT:  Make sure this is a RAM location on your design!  If it
     * is not, change this to point to a writable location for this test */
    memptr = (int *)(RAM_LOCATION_FOR_TEST);

    /* Write the location where we will protect later to
     * test that the initial access passes
     */
    *memptr = DATA_VALUE_NO_PROTECTION;

    /* If the above write did not succeed, we will go to exception handler
     * and not continue here.
     */
    if (*memptr == DATA_VALUE_NO_PROTECTION) {
        printf ("Initial write to location 0x%08x passes!\n", memptr);
        /* continue */
    }
    else {
        printf ("Initial write did not succeed.  Check Setup...\n");
        return 0xFA;  /* Exit */
    }

    /* Write the base address CSR registers */
    write_csr (pmpaddr3, REGION3_ADDR);
    write_csr (pmpaddr2, REGION2_ADDR);
    write_csr (pmpaddr1, REGION1_ADDR);
    write_csr (pmpaddr0, REGION0_ADDR);

    /* Verify each CSR to make sure assignments are correct */
    region0 = read_csr (pmpaddr0);
    if (region0 != REGION0_ADDR) {
        printf ("region0 NOT OK! Written: 0x%08x, Exp: 0x%08x\n", REGION0_ADDR, region0);
    }
    region1 = read_csr (pmpaddr1);
    if (region1 != REGION1_ADDR) {
        printf ("region1 NOT OK! Written: 0x%08x, Exp: 0x%08x\n", REGION1_ADDR, region1);
    }
    region2 = read_csr(pmpaddr2);
    if (region2 != REGION2_ADDR) {
        printf ("region2 NOT OK! Written: 0x%08x, Exp: 0x%08x\n", REGION2_ADDR, region2);
    }
    region3 = read_csr (pmpaddr3);
    if (region3 != REGION3_ADDR) {
        printf ("region3 NOT OK! Written: %08x, Exp: %08x\n", REGION3_ADDR, region2);
    }

    /* Now, assign protection config for each region */
    write_csr (pmpcfg0, PMPCONFIG0);

    /* Check our write is correct */
    pmpconfig0_read = read_csr (pmpcfg0);
    if (pmpconfig0_read != PMPCONFIG0) {
        printf ("pmpconfig0 NOT OK! Written: 0x%08x, Exp: 0x%08x\n", PMPCONFIG0, pmpconfig0_read);
    }

    /* Test protection on Arty
     *
     * We will take away write permissions from region2 -> region1 range.
     * Otherwise this read will execute as expected.
     * When 'W' is removed, we will wind up in the exception handler.
     */
    *memptr = DATA_VALUE_PMP_ENABLED;

    /* If code execution resumes here, something is wrong with the config.
     * We are supposed to vector to default_exception_handler() because we have
     * now written the protected the region where *memptr exists */
    return_code = 0xFA17;
    printf ("PMP protection not correct - check config!  Test Failed!\n");
    return return_code;
}

void __attribute__((weak, interrupt)) default_vector_handler (void) {
    /* Add functionality if desired */
}
void __attribute__((weak)) default_exception_handler(void) {

    if (*memptr == DATA_VALUE_PMP_ENABLED) {
        /* This value should NOT get written, as we should be protected */
        return_code = 0xFF;
        printf ("Unexpected Exception Hit!  PMP not enabled correctly - check Setup...\n");
    }
    else {
        /* Return code of 0x0 implies a pass */
        return_code = 0;
        printf("Exception Hit as Expected! Exception Code: 0x%02x\n", MCAUSE_CODE(read_csr(mcause)));
    }

    printf("Now Exiting...\n");

    /* Exit here using the return_code */
    exit (return_code);
}
