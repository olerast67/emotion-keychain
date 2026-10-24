#include "face/sensing.h"

#include "face/clock.h"
#include "face/fmath.h"

namespace face {

namespace {

constexpr float kFallG = 0.35f;       // |a| ниже — невесомость
constexpr uint32_t kFallMs = 80;      // столько подряд — падение
constexpr float kImpactG = 2.8f;      // удар
constexpr float kShakeG = 0.8f;       // пик динамики для тряски
constexpr float kShakeHardG = 1.8f;
constexpr uint32_t kShakeWindow = 1200;
constexpr float kStepHi = 0.18f, kStepLo = 0.05f;  // порог шага по |a| − 1 g с гистерезисом
constexpr uint32_t kStepMin = 250;                // не чаще 4 шагов в секунду
constexpr float kCalmG = 0.06f;
constexpr uint32_t kStillMs = 60000;

void push(Event* out, int& n, Event e) {
  if (n < kSenseEvents) out[n++] = e;
}

}  // namespace

// ---------------------------------------------------------------- движение

int MotionSensor::feed(float ax, float ay, float az, uint32_t now, Event out[kSenseEvents]) {
  int n = 0;
  if (!finitef(ax) || !finitef(ay) || !finitef(az)) return 0;
  if (!init_) {
    init_ = true;
    gx_ = ax, gy_ = ay, gz_ = az;
    last_ms_ = now;
    calm_since_ = now;
  }
  // Время не идёт назад: метки пачки FIFO могут чуть заходить за прошлую.
  if ((int32_t)(now - last_ms_) < 0) now = last_ms_;
  const uint32_t dt = now - last_ms_ > 1000u ? 1000u : now - last_ms_;
  last_ms_ = now;
  // Гравитация — медленный фильтр с постоянной ~0.4 с.
  const float k = clampf((float)dt / 400.0f, 0.02f, 1.0f);
  gx_ += (ax - gx_) * k;
  gy_ += (ay - gy_) * k;
  gz_ += (az - gz_) * k;
  const float dx = ax - gx_, dy = ay - gy_, dz = az - gz_;
  const float dyn = fsqrt(dx * dx + dy * dy + dz * dz);
  const float mag = fsqrt(ax * ax + ay * ay + az * az);

  // Наклон для взгляда: куда «скатываются» зрачки.
  tilt_x_ = clampf(gx_ / 0.7f, -1.0f, 1.0f);
  tilt_y_ = clampf(-gz_ / 0.7f, -1.0f, 1.0f);

  // Падение: невесомость дольше kFallMs, потом, скорее всего, удар.
  if (mag < kFallG) {
    if (!fall_timing_) fall_timing_ = true, fall_since_ = now;
    if (!falling_ && now - fall_since_ >= kFallMs) {
      falling_ = true;
      fall_at_ = now;
      push(out, n, Event::free_fall);
    }
  } else {
    fall_timing_ = false;
  }
  // Удар — после падения или вне тряски: сильная тряска тоже даёт пики больше 2.8 g.
  if (mag > kImpactG && (falling_ || shake_n_ < 2) && time_reached(now, impact_quiet_until_)) {
    impact_quiet_until_ = now + 1000;
    falling_ = false;
    push(out, n, Event::impact);
    shake_n_ = 0;  // удар — не тряска
    shake_peak_ = 0;
    shake_quiet_until_ = now + 1500;  // отскоки после удара — тоже не тряска
  } else if (falling_ && now - fall_at_ > 1500) {
    falling_ = false;  // поймали, удара не было
  }

  // Тряска: пики динамики со сменой направления.
  if (dyn > kShakeG) {
    const float dom = fabs_(dx) > fabs_(dy) ? (fabs_(dx) > fabs_(dz) ? dx : dz) : (fabs_(dy) > fabs_(dz) ? dy : dz);
    const int sign = dom >= 0 ? 1 : -1;
    if (sign != last_peak_sign_) {
      last_peak_sign_ = sign;
      if (shake_n_ < 8) shake_t_[shake_n_++] = now;
    }
    if (dyn > shake_peak_) shake_peak_ = dyn;  // сила — по всему размаху, не по моменту смены знака
  }
  while (shake_n_ && now - shake_t_[0] > kShakeWindow) {
    for (int i = 1; i < shake_n_; ++i) shake_t_[i - 1] = shake_t_[i];
    --shake_n_;
  }
  if (!shake_n_) shake_peak_ = 0;
  if (shake_n_ >= 4 && time_reached(now, shake_quiet_until_)) {
    push(out, n, shake_peak_ > kShakeHardG ? Event::shake_hard : Event::shake);
    shake_quiet_until_ = now + 1500;
    shake_n_ = 0;
    shake_peak_ = 0;
  }

  // Рывок: резкое движение без тряски — зрачки на миг отстают.
  if (dyn > 0.5f && dyn < kShakeG * 2.0f && time_reached(now, jerk_quiet_until_)) {
    jerk_ = true;
    jerk_x_ = clampf(-dx / dyn, -1.0f, 1.0f);
    jerk_y_ = clampf(-dy / dyn, -1.0f, 1.0f);
    jerk_quiet_until_ = now + 600;
  }

  // Шаги: всплески |a| − 1 g с гистерезисом и минимальным шагом по времени.
  const float vert = mag - 1.0f;
  const bool shaking = shake_n_ >= 2 || !time_reached(now, shake_quiet_until_);
  if (!step_high_ && vert > kStepHi && dyn < 1.5f && !shaking && now - step_last_ >= kStepMin) {
    step_high_ = true;
    step_last_ = now;
    if (step_n_ == 8) {
      for (int i = 1; i < 8; ++i) step_times_[i - 1] = step_times_[i];
      --step_n_;
    }
    step_times_[step_n_++] = now;
    // Каждый шаг засчитывается один раз: сразу, если ходьба уже идёт, иначе — когда подтвердится.
    if (gait_)
      ++steps_new_, ++steps_total_;
    else if (steps_held_ < 8)
      ++steps_held_;
  } else if (step_high_ && vert < kStepLo) {
    step_high_ = false;
  }
  while (step_n_ && now - step_times_[0] > 4000) {
    for (int i = 1; i < step_n_; ++i) step_times_[i - 1] = step_times_[i];
    --step_n_;
  }
  if (steps_held_ > step_n_) steps_held_ = (uint8_t)step_n_;  // старые толчки ушли из окна
  // Ходьба — когда за 4 с хотя бы пять шагов: так не считаются случайные толчки. Кончается,
  // когда шагов нет 2.5 с: медленная походка не «мигает».
  uint8_t gait = gait_;
  if (step_n_ >= 5) {
    const float span = (float)(step_times_[step_n_ - 1] - step_times_[0]) / 1000.0f;
    const float hz = span > 0.0f ? (float)(step_n_ - 1) / span : 0.0f;
    gait = hz > 2.6f ? 2 : 1;
  } else if (gait_ && now - step_last_ > 2500) {
    gait = 0;
  } else if (!gait_) {
    gait = 0;
  }
  if (gait != gait_) {
    if (gait && !gait_) {  // пошёл: засчитываем и первые шаги, по которым это стало ясно
      steps_new_ += steps_held_;
      steps_total_ += steps_held_;
      steps_held_ = 0;
    }
    if (gait && (gait > gait_ || now - gait_said_ > 30000)) push(out, n, gait == 2 ? Event::run : Event::walk);
    if (gait) gait_said_ = now;
    gait_ = gait;
  }

  // Покой и «взяли в руки».
  if (dyn > kCalmG) {
    if (still_ && dyn > 0.15f) {
      still_ = false;
      push(out, n, Event::pick_up);
    }
    if (!still_) calm_since_ = now;
  } else if (!still_ && now - calm_since_ >= kStillMs) {
    still_ = true;
    push(out, n, Event::still);
  }

  // Вверх ногами: гравитация смотрит в верх экрана дольше секунды.
  const bool flipped = gy_ < -0.6f, normal = gy_ > 0.6f;
  if (flipped != upside_ && (flipped || normal)) {
    if (!flip_timing_) flip_timing_ = true, flip_since_ = now;
    if (now - flip_since_ >= 1000) {
      upside_ = flipped;
      flip_timing_ = false;
      push(out, n, flipped ? Event::upside_down : Event::upright);
    }
  } else {
    flip_timing_ = false;
  }
  return n;
}

bool MotionSensor::take_jerk(float* x, float* y) {
  if (!jerk_) return false;
  jerk_ = false;
  *x = jerk_x_, *y = jerk_y_;
  return true;
}

uint32_t MotionSensor::take_steps() {
  const uint32_t s = steps_new_;
  steps_new_ = 0;
  return s;
}

// ---------------------------------------------------------------- звук

int SoundSensor::feed(float db, uint32_t now, Event out[kSenseEvents]) {
  int n = 0;
  if (!finitef(db)) return 0;
  db = clampf(db, -90.0f, 0.0f);
  if (!init_) {
    init_ = true;
    floor_ = db;
    prev_ = prev2_ = db;
    win_t0_ = now;
    silent_since_ = now;
    last_ = now;
  }
  // Микрофон был выключен (темнота, экорежим): старое окно и «тишина с тех пор» не в счёт.
  if (now - last_ > 1000) {
    win_t0_ = now;
    win_blocks_ = win_active_ = win_onsets_ = 0;
    silent_since_ = now;
    active_ = false;
    prev_ = prev2_ = db;
  }
  last_ = now;
  // Фон: быстро вниз, медленно вверх. Пока звук заметно громче фона, он растёт вдесятеро
  // медленнее (минуты): иначе ровная музыка через полминуты стала бы для брелока тишиной,
  // а постоянный шум всё же со временем становится фоном.
  const bool loud_now = db - floor_ > 10.0f;
  if (db < floor_)
    floor_ += (db - floor_) * 0.3f;
  else
    floor_ += (db - floor_) * (loud_now ? 0.0002f : 0.002f);
  const float above = db - floor_;

  // Хлопок или грохот: резкий скачок громкости над фоном (бывает, что он делится на два блока).
  const float before = prev_ < prev2_ ? prev_ : prev2_;
  if (db > -25.0f && db - before > 18.0f && above > 25.0f && time_reached(now, loud_quiet_until_)) {
    loud_quiet_until_ = now + 3000;
    push(out, n, Event::loud);
  }

  // Окно 4 с: сколько блоков со звуком и сколько «слогов» — входов в звук.
  const bool active = above > 10.0f;
  ++win_blocks_;
  win_active_ += active;
  if (active && !active_) ++win_onsets_;
  active_ = active;
  prev2_ = prev_;
  prev_ = db;
  if (active) silent_since_ = now;

  if (now - win_t0_ >= 4000) {
    const float duty = win_blocks_ ? (float)win_active_ / (float)win_blocks_ : 0.0f;
    const float onsets_hz = (float)win_onsets_ / ((float)(now - win_t0_) / 1000.0f);
    uint8_t heard = heard_;
    if (duty > 0.85f && onsets_hz < 1.5f)
      heard = 2;  // ровный звук почти без пауз — музыка
    else if (duty > 0.25f && onsets_hz >= 1.5f)
      heard = 1;  // частые всплески с паузами — речь
    if (heard != heard_ && heard) push(out, n, heard == 2 ? Event::music : Event::speech);
    heard_ = heard;
    win_t0_ = now;
    win_blocks_ = win_active_ = win_onsets_ = 0;
  }
  if (heard_ && now - silent_since_ > 10000) {
    heard_ = 0;
    push(out, n, Event::quiet);
  }
  return n;
}

// ---------------------------------------------------------------- свет и температура

int LightSensor::feed(float lux, uint32_t now, Event out[kSenseEvents]) {
  int n = 0;
  if (!finitef(lux) || lux < 0) return 0;
  // Гистерезис: в темноту ниже 3 лк, из неё — выше 15; солнце — выше 3000, обратно ниже 1500.
  uint8_t s = state_ == 255 ? (lux < 3 ? 0 : lux > 3000 ? 2 : 1) : state_;
  if (state_ != 255) {
    if (s == 0 && lux > 15) s = 1;
    if (s == 1 && lux < 3) s = 0;
    if (s == 1 && lux > 3000) s = 2;
    if (s == 2 && lux < 1500) s = 1;
  }
  if (s == state_) {
    cand_ = 255;
    return 0;
  }
  if (s != cand_) {
    cand_ = s;
    cand_since_ = now;
  }
  // Новое состояние держится 3 с: тень от руки — не темнота. Из темноты — за 1 с: брелок
  // достали из кармана, и лицо должно появиться сразу.
  const uint32_t hold = state_ == 0 ? 1000u : 3000u;
  if (state_ == 255 || now - cand_since_ >= hold) {
    state_ = s;
    cand_ = 255;
    push(out, n, s == 0 ? Event::dark : s == 2 ? Event::bright : Event::light);
  }
  return n;
}

int TempSensor::feed(float c, uint32_t now, Event out[kSenseEvents]) {
  int n = 0;
  if (!finitef(c)) return 0;
  uint8_t s = state_ == 255 ? (c < 5 ? 0 : c > 38 ? 2 : 1) : state_;
  if (state_ != 255) {
    if (s == 1 && c < 5) s = 0;
    if (s == 0 && c > 10) s = 1;
    if (s == 1 && c > 38) s = 2;
    if (s == 2 && c < 33) s = 1;
  }
  if (s == state_) {
    cand_ = 255;
    return 0;
  }
  if (s != cand_) {
    cand_ = s;
    cand_since_ = now;
  }
  if (state_ == 255 || now - cand_since_ >= 30000) {
    const bool first = state_ == 255;
    state_ = s;
    cand_ = 255;
    if (!(first && s == 1)) push(out, n, s == 0 ? Event::cold : s == 2 ? Event::hot : Event::temp_ok);
  }
  return n;
}

// ---------------------------------------------------------------- батарея

int BatterySensor::percent_of(float v) {
  // Разряд Li-Po малым током: напряжение → проценты.
  static const float kV[] = {3.30f, 3.50f, 3.60f, 3.68f, 3.74f, 3.79f, 3.85f, 3.92f, 4.00f, 4.08f, 4.18f};
  if (!(v > kV[0])) return 0;
  if (v >= kV[10]) return 100;
  for (int i = 1; i <= 10; ++i)
    if (v < kV[i]) return (int)((i - 1) * 10 + (v - kV[i - 1]) / (kV[i] - kV[i - 1]) * 10.0f + 0.5f);
  return 100;
}

int BatterySensor::feed(float volts, bool usb, bool charging, uint32_t now, Event out[kSenseEvents]) {
  int n = 0;
  if (!finitef(volts)) return 0;
  // От USB процессор видит напряжение USB, а не ячейки: заряд меряется только без USB.
  if (!usb) {
    if (usb_ || !have_v_) {
      v_ = volts;  // отключили USB или первое измерение: начинаем заново
      pct_ = -1;
      have_v_ = true;
    }
    v_ += (volts - v_) * 0.2f;  // моторчик и радио дают провалы напряжения
    const int pct = percent_of(v_);
    pct_ = pct_ < 0 || pct < pct_ ? pct : pct_;  // без USB заряд не растёт от шумов
  }
  if (!init_) {
    init_ = true;
    usb_ = usb, charging_ = charging, was_charging_ = charging;
    if (usb && charging) push(out, n, Event::charge_start);
    return n;
  }
  if (usb && charging && !(usb_ && charging_)) push(out, n, Event::charge_start);
  if (usb && !charging && charging_ && was_charging_) push(out, n, Event::charge_full);
  if (!usb && usb_) push(out, n, Event::charge_stop);
  if (charging) was_charging_ = true;
  if (!usb) was_charging_ = false;
  usb_ = usb, charging_ = charging;

  if (usb) {
    low_said_ = critical_said_ = false;
    empty_ = false;
    empty_timing_ = false;
    return n;
  }
  if (pct_ >= 0 && pct_ <= 15 && !low_said_) {
    low_said_ = true;
    push(out, n, Event::battery_low);
  }
  if (pct_ >= 0 && pct_ <= 5 && !critical_said_) {
    critical_said_ = true;
    push(out, n, Event::battery_critical);
  }
  // Пусто: ниже 3.35 В дольше минуты — пора спать, пока ячейка цела. Отсчёт сбрасывается,
  // только если напряжение поднялось выше 3.40 В: у почти пустой ячейки оно стоит у самого
  // порога, и шум в милливольты иначе обнулял бы минуту снова и снова.
  if (v_ < 3.35f) {
    if (!empty_timing_) empty_timing_ = true, empty_since_ = now;
    if (now - empty_since_ > 60000) empty_ = true;
  } else if (v_ > 3.40f) {
    empty_timing_ = false;
  }
  return n;
}

}  // namespace face
