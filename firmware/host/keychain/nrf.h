// Регистры nRF52840, которые трогает прошивка платы, — с поведением настоящих.
#pragma once

#include <stdint.h>

#include "sim.h"

namespace sim {

// Регистр с действием при записи.
struct WReg {
  uint32_t v = 0;
  void (*on_write)(uint32_t) = nullptr;
  WReg& operator=(uint32_t x) {
    v = x;
    if (on_write) on_write(x);
    return *this;
  }
  operator uint32_t() const { return v; }
};

struct UicrReg {
  UicrReg& operator=(uint32_t x);  // флеш: только сброс битов и только при WEN
  operator uint32_t() const { return status.uicr_regout0; }
};
struct Uicr {
  UicrReg REGOUT0;
};
struct Nvmc {
  WReg CONFIG, ERASEPAGE;
  uint32_t READY = 1;
};
struct UsbRegStatus {
  operator uint32_t() const { return world.vbus ? 1u : 0u; }
};
struct PowerRegs {
  WReg DCDCEN, RESETREAS, SYSTEMOFF;
  UsbRegStatus USBREGSTATUS;
};
struct Wdt {
  WReg CONFIG, CRV, RREN, TASKS_START;
  WReg RR[8];
};
struct Pwm {
  struct {
    WReg OUT[4];
  } PSEL;
  WReg MODE, PRESCALER, DECODER, LOOP, COUNTERTOP, ENABLE, TASKS_STOP;
  WReg TASKS_SEQSTART[2];
  struct {
    WReg PTR, CNT, REFRESH, ENDDELAY;
  } SEQ[2];
};
struct Twim {
  WReg FREQUENCY;
};
struct Ficr {
  uint32_t DEVICEID[2];
};

extern Uicr uicr;
extern Nvmc nvmc;
extern PowerRegs power_regs;
extern Wdt wdt;
extern Pwm pwm3, pwm2;
extern Ficr ficr;
extern Twim twim0, twim1;

}  // namespace sim

#define NRF_UICR (&sim::uicr)
#define NRF_NVMC (&sim::nvmc)
#define NRF_POWER (&sim::power_regs)
#define NRF_WDT (&sim::wdt)
#define NRF_PWM3 (&sim::pwm3)
#define NRF_PWM2 (&sim::pwm2)
#define NRF_FICR (&sim::ficr)
#define NRF_TWIM0 (&sim::twim0)
#define NRF_TWIM1 (&sim::twim1)

#define UICR_REGOUT0_VOUT_Pos (0UL)
#define UICR_REGOUT0_VOUT_Msk (0x7UL << UICR_REGOUT0_VOUT_Pos)
#define UICR_REGOUT0_VOUT_3V3 (5UL)
#define UICR_REGOUT0_VOUT_DEFAULT (7UL)
#define NVMC_CONFIG_WEN_Pos (0UL)
#define NVMC_CONFIG_WEN_Ren (0UL)
#define NVMC_CONFIG_WEN_Wen (1UL)
#define NVMC_CONFIG_WEN_Een (2UL)
#define POWER_USBREGSTATUS_VBUSDETECT_Msk (1UL)
#define POWER_RESETREAS_RESETPIN_Msk (1UL << 0)
#define POWER_RESETREAS_DOG_Msk (1UL << 1)
#define POWER_RESETREAS_SREQ_Msk (1UL << 2)
#define POWER_RESETREAS_LOCKUP_Msk (1UL << 3)
#define POWER_RESETREAS_OFF_Msk (1UL << 16)
#define POWER_RESETREAS_LPCOMP_Msk (1UL << 17)
#define POWER_RESETREAS_DIF_Msk (1UL << 18)
#define POWER_RESETREAS_VBUS_Msk (1UL << 20)
#define WDT_CONFIG_SLEEP_Pos (0UL)
#define WDT_CONFIG_SLEEP_Run (1UL)
#define WDT_RREN_RR0_Msk (1UL)
#define WDT_RR_RR_Reload (0x6E524635UL)
#define PWM_MODE_UPDOWN_Up (0UL)
#define PWM_PRESCALER_PRESCALER_DIV_4 (2UL)
#define PWM_PRESCALER_PRESCALER_DIV_1 (0UL)
#define PWM_DECODER_LOAD_Common (0UL)
#define PWM_DECODER_LOAD_Pos (0UL)
#define PWM_DECODER_LOAD_Individual (2UL)
#define PWM_DECODER_MODE_Pos (8UL)
#define PWM_DECODER_MODE_RefreshCount (0UL)

inline void __DMB() { __sync_synchronize(); }
[[noreturn]] void NVIC_SystemReset();
