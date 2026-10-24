// Датчики → события характера (Behavior::on_event). Здесь только обработка чисел:
// прошивка читает акселерометр, микрофон, датчик света, температуру и батарею и
// отдаёт значения сюда, а обратно получает события и наклон для взгляда. Всё без
// кучи и libm, поэтому проверяется тестами на ПК.
//
// Оси устройства: x — вправо, y — вверх по экрану, z — из экрана к зрителю. Ускорение
// в g; брелок висит ровно — (0, +1, 0).
#pragma once

#include <stdint.h>

#include "face/behavior.h"

namespace face {

// До скольких событий отдаёт один вызов feed.
constexpr int kSenseEvents = 4;

// Акселерометр: тряска, падение, удар, шаги, покой, «взяли в руки», вверх ногами,
// наклон для зрачков и рывок. Касания приходят отдельно — из аппаратного детектора
// акселерометра.
class MotionSensor {
 public:
  // Одна выборка; частота 20–100 Гц. Возвращает число событий в out.
  int feed(float ax, float ay, float az, uint32_t now_ms, Event out[kSenseEvents]);

  // Наклон для FaceEngine::set_tilt: −1…+1 по каждой оси.
  float tilt_x() const { return tilt_x_; }
  float tilt_y() const { return tilt_y_; }
  // Рывок с прошлого вызова: направление, куда «откатятся» зрачки (FaceEngine::jerk).
  bool take_jerk(float* x, float* y);
  // Шаги с прошлого вызова (для Behavior::add_steps).
  uint32_t take_steps();
  uint32_t steps_total() const { return steps_total_; }
  bool still() const { return still_; }
  bool walking() const { return gait_ != 0; }  // идёт или бежит

 private:
  bool init_ = false;
  float gx_ = 0, gy_ = 1, gz_ = 0;  // гравитация (медленный фильтр)
  float tilt_x_ = 0, tilt_y_ = 0;
  uint32_t last_ms_ = 0;
  // падение и удар
  uint32_t fall_since_ = 0;
  bool fall_timing_ = false;
  bool falling_ = false;
  uint32_t fall_at_ = 0;
  uint32_t impact_quiet_until_ = 0;
  // тряска: пики динамики со сменой знака
  uint32_t shake_t_[8] = {};
  int shake_n_ = 0;
  int last_peak_sign_ = 0;
  float shake_peak_ = 0;
  uint32_t shake_quiet_until_ = 0;
  // шаги
  bool step_high_ = false;
  uint32_t step_last_ = 0;
  uint32_t step_times_[8] = {};
  int step_n_ = 0;
  uint8_t steps_held_ = 0;  // шаги, ещё не засчитанные: ходьба пока не подтвердилась
  uint32_t steps_new_ = 0, steps_total_ = 0;
  uint8_t gait_ = 0;  // 0 — стоит, 1 — идёт, 2 — бежит
  uint32_t gait_said_ = 0;
  // покой и ориентация
  uint32_t calm_since_ = 0;
  bool still_ = false;
  uint32_t flip_since_ = 0;
  bool flip_timing_ = false;
  bool upside_ = false;
  // рывок
  bool jerk_ = false;
  float jerk_x_ = 0, jerk_y_ = 0;
  uint32_t jerk_quiet_until_ = 0;
};

// Микрофон: громкий хлопок, речь, музыка, тишина. На вход — уровень блока в дБ
// относительно полной шкалы (−90…0) примерно каждые 20–50 мс.
class SoundSensor {
 public:
  int feed(float dbfs, uint32_t now_ms, Event out[kSenseEvents]);
  float floor_db() const { return floor_; }

 private:
  bool init_ = false;
  float floor_ = -60.0f;  // фон: медленно поднимается, быстро опускается
  float prev_ = -90.0f, prev2_ = -90.0f;
  uint32_t last_ = 0;
  uint32_t loud_quiet_until_ = 0;
  // окно 4 с: доля блоков со звуком и число «слогов» — всплесков громкости
  uint32_t win_t0_ = 0;
  int win_blocks_ = 0, win_active_ = 0, win_onsets_ = 0;
  bool active_ = false;
  uint8_t heard_ = 0;  // 0 — тишина, 1 — речь, 2 — музыка
  uint32_t silent_since_ = 0;
};

// Освещённость в люксах: темно, светло, яркое солнце (с запасом против мигания).
class LightSensor {
 public:
  int feed(float lux, uint32_t now_ms, Event out[kSenseEvents]);

 private:
  uint8_t state_ = 255;  // 0 — темно, 1 — обычный свет, 2 — ярко; 255 — ещё не знаем
  uint8_t cand_ = 255;
  uint32_t cand_since_ = 0;
};

// Температура в °C: холодно, жарко, нормально.
class TempSensor {
 public:
  int feed(float celsius, uint32_t now_ms, Event out[kSenseEvents]);

 private:
  uint8_t state_ = 255;  // 0 — холодно, 1 — нормально, 2 — жарко
  uint8_t cand_ = 255;
  uint32_t cand_since_ = 0;
};

// Батарея: напряжение ячейки → проценты, события заряда и разряда.
class BatterySensor {
 public:
  // volts — напряжение питания системы: без USB это ячейка, от USB — само USB, и тогда
  // оно не учитывается. usb — есть питание USB; charging — зарядник заряжает.
  int feed(float volts, bool usb, bool charging, uint32_t now_ms, Event out[kSenseEvents]);
  // Заряд по последнему измерению без USB; −1 — ещё не измерен.
  int percent() const { return pct_; }
  float volts() const { return v_; }
  // Пора выключаться: ячейка почти пуста, USB нет.
  bool empty() const { return empty_; }
  static int percent_of(float volts);

 private:
  bool init_ = false, have_v_ = false;
  float v_ = 0;
  int pct_ = -1;
  bool usb_ = false, charging_ = false, was_charging_ = false;
  bool low_said_ = false, critical_said_ = false, empty_ = false;
  uint32_t empty_since_ = 0;
  bool empty_timing_ = false;
};

}  // namespace face
