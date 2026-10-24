// Железо платы, кроме датчиков I2C: питание и заряд, пищалка, вибромотор, микрофон,
// сторожевой таймер и выключение.
#pragma once

#include <Arduino.h>

namespace hal {

// Первый запуск: 3.3 В с регулятора REG0 модуля (по умолчанию он даёт 1.8 В) и
// DC/DC для ядра. Если 3.3 В ещё не записаны — записывает и перезагружает плату.
void power_init();
// Короткая перезагрузка (сторожевой таймер, программный сброс, зависание): часы шли и до неё.
// После полного выключения, подключения USB к спящей плате или первой подачи питания — false.
bool warm_reset();

// Напряжение питания системы (VDDH): без USB это ячейка, от USB — само USB.
float vsys_volts();
bool usb_present();
bool charging();  // зарядник заряжает (статус MCP73832)
float temperature_c();

// Сторожевой таймер: 8 с; считает и во сне процессора. Загрузчик тоже его кормит.
void watchdog_start();
void watchdog_feed();

// Выключение: всё гаснет, плата просыпается от кнопки или USB (с перезагрузкой).
[[noreturn]] void system_off();
// Перезагрузка в загрузчик для обновления прошивки по USB (диск KEYCHAIN и последовательный DFU).
[[noreturn]] void enter_bootloader();

// Пищалка: два вывода в противофазе через PWM3. hz = 0 — тишина; soft — тихо (один вывод,
// узкий импульс).
void buzzer_begin();
void buzzer(uint16_t hz, bool soft);

// Вибромотор: короткие узоры без задержек. Узор — длительности «вкл, выкл, вкл…» в мс.
void vibro_begin();
void vibrate(const uint16_t* pattern, int n);
void vibro_update(uint32_t now_ms);
bool vibro_busy();

// Микрофон PDM: уровень звука в дБ относительно полной шкалы по блокам ~32 мс.
bool mic_begin();
void mic_end();
bool mic_on();
// Новый уровень с прошлого вызова: true и значение в *dbfs.
bool mic_take(float* dbfs);

}  // namespace hal
