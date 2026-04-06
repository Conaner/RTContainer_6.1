/* SPDX-License-Identifier: BSD-2-Clause */

/**
 * @file
 *
 * @ingroup RTEMSBSPsAArch64Phytium
 *
 * @brief BSP Shutdown and Reset
 */

/*
 * Copyright (C) 2025 Phytium Technology Co., Ltd.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <bsp.h>
#include <bsp/bootcard.h>
#include <dev/clock/arm-generic-timer.h>
#include <rtems/monitor.h>

#include <fwdt_hw.h>

RTEMS_NO_RETURN void bsp_reset(rtems_fatal_source source, rtems_fatal_code code)
{
    printk("System shutdown ...\n");
    while (true) {
      /* forever waiting here */
    }
}

#if defined(PHYTIUM_BSP_TYPE_E2000Q_DEMO) || defined(PHYTIUM_BSP_TYPE_E2000D_DEMO)
void rtems_monitor_reset_cmd(
  int argc,
  char **argv,
  const rtems_monitor_command_arg_t* command_arg,
  bool verbose
)
{
    uintptr_t watchdog_reg = FWDT_CONTROL_BASE_ADDR(FWDT0_REFRESH_BASE_ADDR);
    uint32_t frequency = 0;
    uint32_t irq = 0;

    arm_generic_timer_get_config(&frequency, &irq);
    (void)irq;

    printk("System reseting ...\n");
    FWDT_WRITE_REG32(watchdog_reg, FWDT_GWDT_WOR, 1 * frequency); /* timeout 1 seconds */
    FWDT_WRITE_REG32(watchdog_reg, FWDT_GWDT_WCS, FWDT_GWDT_WCS_WDT_EN);

    while (true) {
      /* wait for watchdog reset */
    }
}
#else
void rtems_monitor_reset_cmd(
  int argc,
  char **argv,
  const rtems_monitor_command_arg_t* command_arg,
  bool verbose
)
{
  (void) argc;
  (void) argv;
  (void) command_arg;
  (void) verbose;

  printk("System reseting ...\n");
  bsp_reset(RTEMS_FATAL_SOURCE_APPLICATION, 0);
}
#endif