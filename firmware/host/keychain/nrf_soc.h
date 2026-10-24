// SoftDevice S140: питание, тактирование и флеш — как видит их прошивка.
#pragma once

#include <stdint.h>

#define NRF_SUCCESS (0)
#define NRF_ERROR_BUSY (17)

uint32_t sd_power_usbregstatus_get(uint32_t* st);
uint32_t sd_clock_hfclk_request();
[[noreturn]] uint32_t sd_power_system_off();
uint32_t sd_clock_hfclk_release();
// Флеш при SoftDevice — асинхронно: операция идёт в его окне, итог — событием.
uint32_t sd_flash_write(uint32_t* dst, const uint32_t* src, uint32_t words);
uint32_t sd_flash_page_erase(uint32_t page);
