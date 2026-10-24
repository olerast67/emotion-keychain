// Окружение для проверки журнала storage.cpp на ПК: три страницы флеша в памяти, операции
// SoftDevice асинхронные, как на плате (идут миллисекунды, пока идут — «занят»), а обрыв
// питания можно устроить посреди любой операции.
#pragma once

#include <stdint.h>
#include <string.h>

uintptr_t host_flash_base();

// Обрыв питания: через сколько слов записи или на каком стирании (−1 — не обрывать).
extern long host_cut_after_words;
extern long host_cut_on_erase;
struct HostPowerCut {};  // бросается вместо пропажи питания

uint32_t sd_flash_write(uint32_t* dst, const uint32_t* src, uint32_t words);
uint32_t sd_flash_page_erase(uint32_t page);
inline uint32_t sd_softdevice_is_enabled(uint8_t* en) {
  *en = 1;
  return 0;
}
uint32_t millis();
void delay(uint32_t ms);

constexpr uint32_t NRF_SUCCESS = 0;
constexpr uint32_t NRF_ERROR_BUSY = 17;
struct HostNvmc {
  uint32_t CONFIG, READY = 1, ERASEPAGE;
};
extern HostNvmc host_nvmc;
#define NRF_NVMC (&host_nvmc)
constexpr uint32_t NVMC_CONFIG_WEN_Een = 2, NVMC_CONFIG_WEN_Wen = 1, NVMC_CONFIG_WEN_Ren = 0;
