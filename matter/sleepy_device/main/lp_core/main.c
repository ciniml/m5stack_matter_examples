/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include "ulp_lp_core_utils.h"
#include "ulp_lp_core_gpio.h"

#define WAKEUP_PIN LP_IO_NUM_2

bool gpio_level_prev = false;

int main (void)
{
    bool gpio_level = (bool)ulp_lp_core_gpio_get_level(WAKEUP_PIN);
    if( gpio_level != gpio_level_prev ) {
        gpio_level_prev = gpio_level;
        ulp_lp_core_wakeup_main_processor();
    }
    return 0;
}