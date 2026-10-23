// Прошивка стенда на ПК: настоящий main.cpp и драйвер экрана, вместо железа — заглушки.
// Эмулятор повторяет адресацию SSD1306/SH1106, поэтому тест ловит ошибки в окнах,
// порциях I2C и смещении столбцов: после каждого прохода loop() память «экрана»
// должна совпадать с кадром движка.
#include <string.h>

#include <set>

#include "../src/main.cpp"

namespace host {
uint64_t now_us = 0;
int pins[64] = {};
int ledc_pin = -1;
uint32_t ledc_hz = 0, ledc_duty = 0, ledc_tones = 0;
}  // namespace host

HostSerial Serial;
TwoWire Wire;
EspClass ESP;

// ---------------------------------------------------------------- эмулятор контроллера

void OledEmu::command(uint8_t c) {
  if (pend_need) {
    pend[pend_n++] = c;
    if (pend_n < pend_need) return;
    switch (pend[0]) {
      case 0x20: mode = pend[1] & 3; break;
      case 0x21: col0 = col = pend[1] & 127, col1 = pend[2] & 127; break;
      case 0x22: page0 = page = pend[1] & 7, page1 = pend[2] & 7; break;
      case 0x81: contrast = pend[1]; break;
      default: break;
    }
    pend_need = pend_n = 0;
    return;
  }
  switch (c) {
    case 0x21:
    case 0x22:
      if (sh1106) ++stray;  // у SH1106 нет окон, такие команды он поймёт иначе
      pend[0] = c, pend_n = 1, pend_need = 3;
      return;
    case 0x20:
      if (sh1106) ++stray;
      [[fallthrough]];
    case 0x81:
    case 0x8D:
    case 0xA8:
    case 0xAD:
    case 0xD3:
    case 0xD5:
    case 0xD9:
    case 0xDA:
    case 0xDB: pend[0] = c, pend_n = 1, pend_need = 2; return;
    case 0xAE: on = false; return;
    case 0xAF: on = true; return;
    default: break;
  }
  if (c <= 0x0F)
    col = (col & 0xF0) | c;
  else if (c <= 0x1F)
    col = (col & 0x0F) | (c & 0x0F) << 4;
  else if (c >= 0xB0 && c <= 0xB7)
    page = c & 7;
}

void OledEmu::data(uint8_t d) {
  const int width = sh1106 ? 132 : 128;
  if (col < width) ram[page][col] = d;
  if (!sh1106 && mode == 0) {
    if (++col > col1) {
      col = col0;
      if (++page > page1) page = page0;
    }
  } else if (col < width - 1) {
    ++col;
  }
}

uint8_t TwoWire::endTransmission(bool) {
  // 9 бит на байт плюс адрес, старт и стоп.
  host::now_us += (uint64_t)((n_ + 1) * 9 + 2) * 1000000ull / hz_;
  if (!emu.present || addr_ != emu.addr) return 2;  // NACK
  if (emu.fail_next) return --emu.fail_next, 4;     // помеха: транзакция не дошла
  ++emu.transactions;
  if (n_ == 0) return 0;  // проверка адреса
  emu.bytes += (uint32_t)n_;
  if (buf_[0] == 0x00) {
    for (size_t i = 1; i < n_; ++i) emu.command(buf_[i]);
  } else if (buf_[0] == 0x40) {
    for (size_t i = 1; i < n_; ++i) emu.data(buf_[i]);
  } else {
    ++emu.stray;
  }
  return 0;
}

// ---------------------------------------------------------------- тест

static int g_fail = 0, g_checks = 0;

#define CHECK(cond, ...)                                      \
  do {                                                        \
    ++g_checks;                                               \
    if (!(cond)) {                                            \
      ++g_fail;                                               \
      printf("FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond); \
      printf(__VA_ARGS__);                                    \
      printf("\n");                                           \
    }                                                         \
  } while (0)

static bool screen_matches() {
  const OledEmu& e = Wire.emu;
  const int off = e.sh1106 ? 2 : 0;
  for (int p = 0; p < kPages; ++p)
    if (memcmp(&e.ram[p][off], &eng.frame().data[p * kWidth], kWidth) != 0) return false;
  return true;
}

static uint32_t g_mismatch = 0;
static uint64_t g_skipped_us = 0;  // перескоки часов к переполнению: в среднюю нагрузку не входят
static std::set<int> g_seen;

static void run_ms(uint32_t ms) {
  const uint64_t end = host::now_us + (uint64_t)ms * 1000;
  while (host::now_us < end) {
    loop();
    if (have_oled && !screen_matches()) ++g_mismatch;
    g_seen.insert((int)eng.expression());
  }
}

static void type(const char* s) {
  Serial.in += s;
  Serial.in += '\n';
  run_ms(20);
}

static bool said(const char* s) { return Serial.out.find(s) != std::string::npos; }

static void touch(uint32_t ms) {
  host::pins[PIN_BUTTON] = HIGH;
  run_ms(ms);
  host::pins[PIN_BUTTON] = LOW;
}

static bool reason_has(const char* s) { return strstr(bh.last_reason(), s) != nullptr; }

int main(int argc, char** argv) {
  const bool verbose = argc > 1 && !strcmp(argv[1], "-v");
  host::now_us = 5000;

  // Экран забыли подключить: прошивка пишет подсказку и ищет его снова.
  Wire.emu.present = false;
  setup();
  CHECK(!have_oled, "экрана нет");
  CHECK(said("Экран не найден") && said("никого нет"), "подсказка про провода");
  run_ms(1000);
  Wire.emu.present = true;
  run_ms(2500);
  CHECK(have_oled, "экран нашёлся после подключения");
  CHECK(said("Экран SSD1306 на 0x3C"), "сообщение об экране");
  CHECK(Wire.emu.on, "экран включён");
  CHECK(Wire.emu.mode == 0, "горизонтальная адресация");
  CHECK(host::ledc_pin == PIN_BUZZER && host::ledc_tones > 3 && said("Звук: музыка сцены"),
        "при старте пищалка играет загрузку: %u нот", host::ledc_tones);
  CHECK(host::ledc_hz == 0, "после мелодии пищалка молчит");

  // Живой режим.
  const uint32_t bytes0 = Wire.emu.bytes;
  run_ms(60000);
  CHECK(g_mismatch == 0, "экран расходится с кадром %u раз", g_mismatch);
  CHECK(Wire.emu.overflows == 0, "переполнение буфера Wire: %u", Wire.emu.overflows);
  CHECK(Wire.emu.stray == 0, "лишние байты на шине: %u", Wire.emu.stray);
  CHECK(Wire.emu.bytes > bytes0 + 10000, "кадры идут: %u байт", Wire.emu.bytes - bytes0);

  // Кнопка: тап, двойной тап, удержание.
  touch(80);
  run_ms(500);
  CHECK(reason_has("тап") || reason_has("тронули"), "тап дошёл до характера: «%s»", bh.last_reason());
  run_ms(4000);
  touch(80);
  run_ms(120);
  touch(80);
  run_ms(500);
  CHECK(reason_has("тап"), "двойной тап: «%s»", bh.last_reason());
  touch(1700);
  run_ms(100);
  CHECK(mode == Mode::Showcase, "удержание включает показ");
  const int before = showcase_i;
  touch(80);
  run_ms(500);
  CHECK(showcase_i == (before + 1) % kExprCount, "касание в показе — следующая эмоция");
  touch(1700);
  run_ms(100);
  CHECK(mode == Mode::Alive, "удержание возвращает живой режим");

  // Команды Serial.
  type("laugh");
  CHECK(mode == Mode::Manual && eng.expression() == Expr::laugh, "эмоция по id");
  type("42");
  CHECK(eng.expression() == (Expr)41, "эмоция по номеру");
  type("n");
  CHECK(eng.expression() == (Expr)42, "следующая");
  type("p");
  type("p");
  CHECK(eng.expression() == (Expr)40, "предыдущая");
  type("c sneeze");
  CHECK(eng.clip() == Clip::sneeze, "клип");
  type("b 10");
  CHECK(Wire.emu.contrast == 10 && contrast == 10, "яркость %u", Wire.emu.contrast);
  type("+");
  CHECK(Wire.emu.contrast == 16, "ярче: %u", Wire.emu.contrast);
  type("-");
  type("-");
  CHECK(Wire.emu.contrast == 4, "темнее: %u", Wire.emu.contrast);
  type("b 127");
  type("u");
  run_ms(2000);
  CHECK(eng.options().upside_down, "переворот");
  type("u");
  type("r");
  run_ms(2000);
  CHECK(eng.options().scanlines, "ретро-строки");
  type("r");
  type("d");
  CHECK(!eng.options().drift, "дрейф выключен");
  type("d");
  type("t 23:30");
  CHECK(said("Время 23:30"), "часы");
  type("e shake");
  CHECK(mode == Mode::Alive, "событие включает живой режим");
  run_ms(3000);
  type("l");
  CHECK(said(expression((Expr)(kExprCount - 1)).id) && said("sneeze") && said("battery_low"), "список");
  type("i");
  CHECK(said("Настроение"), "состояние");
  type("?");
  CHECK(said("Команды"), "помощь");
  type("zzz");
  CHECK(said("Не знаю такую команду"), "неизвестная команда");
  type("e nope");
  CHECK(said("Нет такого события"), "неизвестное событие");
  type("c nope");
  CHECK(said("Нет такого клипа"), "неизвестный клип");
  type("v");
  run_ms(10500);
  CHECK(said("кадр/с"), "статистика");
  type("v");

  // Показ всех эмоций по кругу.
  g_seen.clear();
  type("s 600");
  CHECK(mode == Mode::Showcase && showcase_ms == 600, "показ, 600 мс на эмоцию");
  run_ms(600u * kExprCount + 1000);
  CHECK((int)g_seen.size() == kExprCount, "показано %d из %d эмоций", (int)g_seen.size(), kExprCount);
  CHECK(said("[139/139]"), "в Serial печатается номер эмоции");

  // Модуль 1.3" на SH1106: другая адресация и смещение на 2 столбца.
  memset(Wire.emu.ram, 0, sizeof Wire.emu.ram);
  Wire.emu.sh1106 = true;
  type("h");
  CHECK(said("Экран SH1106 на 0x3C"), "переключение на SH1106");
  run_ms(8000);
  CHECK(Wire.emu.stray == 0, "SH1106 не получает команд SSD1306: %u", Wire.emu.stray);
  memset(Wire.emu.ram, 0, sizeof Wire.emu.ram);
  Wire.emu.sh1106 = false;
  type("h");
  run_ms(3000);

  // Модуль с адресом 0x3D (перемычка на обороте) находится сам.
  Wire.emu.addr = 0x3D;
  memset(Wire.emu.ram, 0, sizeof Wire.emu.ram);
  CHECK(start_oled() && oled.address() == 0x3D, "адрес 0x3D");
  CHECK(said("Экран SSD1306 на 0x3D"), "сообщение об адресе 0x3D");
  run_ms(3000);

  // Характер, рост и сцены.
  Wire.emu.addr = 0x3C;
  CHECK(start_oled(), "экран снова на 0x3C");
  type("a");
  type("me");
  CHECK(said(me.code) && said("Рост: день 1"), "кто я: позывной и первый день");
  CHECK(progress.days == 1 && progress.unlocked_count() < kExprCount, "новый брелок открыл только базу");
  type("theme cyber");
  CHECK(eng.options().theme == Theme::Cyber && Preferences::store().count("keychain/theme"), "тема сохраняется");
  run_ms(2000);
  type("x hack");
  CHECK(eng.scene() == Scene::hack, "сцена по команде");
  run_ms(6000);
  CHECK(eng.scene() == Scene::None && said("Сцена: Взлом"), "сцена доиграла и попала в Serial");
  const int before_xp = progress.unlocked_count();
  type("xp 200");
  CHECK(progress.implant >= 1 && progress.unlocked_count() > before_xp, "опыт ставит имплант: %d", progress.implant);
  run_ms(300);
  CHECK(eng.scene() == Scene::upgrade, "апгрейд показан сценой");
  run_ms(9000);
  type("meet");
  run_ms(300);
  CHECK(eng.scene() == Scene::greet && progress.friends == 1, "встреча с новым брелоком");
  run_ms(8000);
  type("t 12:00");  // днём: ночью разговор идёт только на экране
  type("duel win");
  run_ms(9000);
  CHECK(progress.duels_won == 1, "дуэль засчитана");
  run_ms(1500);
  CHECK(eng.scene() == Scene::chat, "после дуэли — секретный разговор: %s", scene_id(eng.scene()));
  {
    // Разговор слышен: обе стороны играет одна пищалка.
    const uint32_t n0 = host::ledc_tones;
    run_ms(6500);
    CHECK(host::ledc_tones - n0 >= 12, "в разговоре %u нот", host::ledc_tones - n0);
  }
  run_ms(40000);  // после разговора — свои сцены: достижение «первая победа», апгрейд
  type("date 2026-10-31");
  run_ms(1000);
  CHECK(eng.scene() == Scene::halloween, "Хэллоуин по дате: сцена %s", scene_id(eng.scene()));
  run_ms(8000);
  type("bday 05-14");
  CHECK(Preferences::store().count("keychain/bday_m"), "день рождения сохраняется");
  type("tilt 1 0");
  run_ms(800);
  CHECK(eng.face().gx > 0.5f, "наклон двигает взгляд: %.2f (сцена %s, эмоция %s)", eng.face().gx, scene_id(eng.scene()),
        expression(eng.expression()).id);
  type("tilt 0 0");
  type("serial 75");
  CHECK(me.quirks.cyber_eye && said("золотой кибер-глаз"), "чужой серийник — другой персонаж");
  type("serial");
  CHECK(!strcmp(me.code, make_identity(ESP.getEfuseMac()).code), "свой персонаж вернулся");
  run_ms(3000);

  // Перезагрузка: прогресс, тема и день рождения из флеша.
  const uint32_t xp = progress.xp;
  const int unlocked = progress.unlocked_count();
  setup();
  CHECK(progress.xp == xp && progress.unlocked_count() == unlocked, "прогресс пережил перезагрузку: %u", progress.xp);
  CHECK(eng.options().theme == Theme::Cyber && bday_m == 5 && bday_d == 14, "настройки пережили перезагрузку");
  CHECK(eng.scene() == Scene::boot, "при старте — загрузка OLERAST LINK");
  run_ms(5000);
  type("reset yes");
  CHECK(progress.xp == 0 && progress.implant == 0, "рост начат заново");

  // Звук: реплика по команде, выключатель во флеше, тихие часы.
  type("a");
  type("t 12:00");
  run_ms(3000);
  Serial.out.clear();
  type("say laugh");
  CHECK(said("Звук: laugh") && host::ledc_hz > 0, "реплика по команде: %u Гц, вывод: %s", host::ledc_hz,
        Serial.out.c_str());
  run_ms(2000);
  type("say nope");
  CHECK(said("Нет такой реплики"), "неизвестная реплика");
  type("snd");
  CHECK(!sound_on && Preferences::store().count("keychain/sound") && said("Звук выключен"), "звук выключен");
  {
    const uint32_t n0 = host::ledc_tones;
    type("say laugh");
    type("e charge_start");
    run_ms(3000);
    CHECK(host::ledc_tones == n0 && host::ledc_hz == 0, "выключенная пищалка молчит");
  }
  type("snd");
  CHECK(sound_on, "звук снова включён");
  type("e charge_stop");
  type("t 23:30");
  run_ms(4000);
  Serial.out.clear();
  type("say laugh");
  CHECK(said("тихие часы"), "ночью смеха нет");
  type("e battery_low");
  run_ms(50);
  CHECK(said("Звук: battery_low (тихо)") && host::ledc_duty == 128, "ночью о батарее — тихо: скважность %u",
        host::ledc_duty);
  run_ms(2000);
  type("t 12:00");
  type("chat");
  run_ms(300);
  CHECK(eng.scene() == Scene::chat && said("Разговор с"), "разговор по команде");
  run_ms(10000);

  // Темы, зарядка, стук, достижения, знакомые, выключение, пасхалки по времени.
  type("theme retro");
  CHECK(eng.options().theme == Theme::Retro && said("Тема: retro"), "ретро-тема");
  type("theme neon");
  CHECK(said("Формат: theme"), "неизвестная тема");
  type("theme classic");
  type("bat 33");
  type("e charge_start");
  {
    // Сцена зарядки — сразу или после эмоции-развязки прошлой сцены.
    int pct = -2;
    for (int i = 0; i < 200 && pct == -2; ++i) {
      run_ms(20);
      if (eng.scene() == Scene::charge) pct = eng.scene_params().value;
    }
    CHECK(pct == 33, "зарядка: сцена с процентом %d", pct);
  }
  run_ms(8000);
  type("e charge_stop");
  run_ms(4000);
  type("knock");
  run_ms(2500);
  CHECK(progress.earned(Ach::knock) && reason_has("стук"), "секретный стук: «%s»", bh.last_reason());
  run_ms(12000);
  Serial.out.clear();
  type("ach");
  CHECK(said("[x] KNOCK KNOCK") && said("Достижений"), "список достижений");
  type("meet");
  run_ms(20000);
  for (int d = 0; d < 2; ++d) {
    type("day");
    run_ms(15000);
    type("meet again");
    run_ms(25000);
  }
  Serial.out.clear();
  type("friends");
  CHECK(said("дней 3, BUDDY"), "три дня встреч — приятели");
  type("off");
  run_ms(8000);
  CHECK(eng.expression() == Expr::sleeping, "выключился и спит: %s", expression(eng.expression()).id);
  touch(80);
  run_ms(6000);
  CHECK(eng.expression() != Expr::sleeping, "кнопка будит: %s", expression(eng.expression()).id);
  run_ms(4000);
  type("t 13:37");
  run_ms(1500);
  CHECK(eng.scene() == Scene::banner && !strcmp(eng.scene_params().text, "1337"), "13:37 — пасхалка: %s",
        scene_id(eng.scene()));
  run_ms(20000);
  type("t 12:00");

  // Плохой ввод: сообщение о формате, и ничего не применяется.
  type("a");
  run_ms(2000);
  const uint32_t xp0 = progress.xp, steps0 = progress.steps;
  const uint8_t contrast0 = contrast;
  Serial.out.clear();
  type("t -1:00");
  CHECK(said("Формат: t 23:30"), "отрицательные часы не принимаются");
  Serial.out.clear();
  type("t 25:99");
  CHECK(said("Формат: t 23:30"), "25:99 не принимается");
  type("xp -1");
  type("xp abc");
  type("xp 10x");
  type("steps -5");
  CHECK(progress.xp == xp0 && progress.steps == steps0, "опыт и шаги не изменились: %u, %u", progress.xp,
        progress.steps);
  type("b 300");
  type("b x");
  CHECK(contrast == contrast0, "яркость не изменилась: %u", contrast);
  type("s 10");
  CHECK(mode == Mode::Alive, "s 10 не включает показ");
  type("date 2026-13-01");
  CHECK(said("Формат: date"), "13-й месяц не принимается");
  Serial.out.clear();
  type("42abc");
  CHECK(said("Не знаю такую команду"), "42abc — не эмоция №42");
  // Строка длиннее буфера не исполняется, даже если начинается с настоящей команды.
  Serial.out.clear();
  type(("xp 9" + std::string(90, '9')).c_str());
  CHECK(said("Строка длиннее") && progress.xp == xp0, "длинная строка пропущена: %u", progress.xp);
  // NaN и бесконечность в наклоне и рывке не гасят лицо.
  type("jerk 1e39 0");
  type("tilt nan 0.5");
  run_ms(1500);
  CHECK(eng.frame().count_lit() > 80, "лицо на месте после NaN: %d пикселей", eng.frame().count_lit());
  type("tilt 0 0");

  // Автояркость: в темноте и ночью тусклее, яркость пользователя — обычный уровень.
  type("t 12:00");
  type("b 128");
  run_ms(500);
  CHECK(Wire.emu.contrast == 128, "днём — яркость пользователя: %u", Wire.emu.contrast);
  type("e dark");
  run_ms(500);
  CHECK(Wire.emu.contrast == 8, "в темноте — 1/16: %u", Wire.emu.contrast);
  type("e light");
  run_ms(500);
  CHECK(Wire.emu.contrast == 128, "свет — снова обычная: %u", Wire.emu.contrast);
  type("t 23:30");
  run_ms(500);
  CHECK(Wire.emu.contrast == 32, "ночью — 1/4: %u", Wire.emu.contrast);
  type("dim");
  CHECK(Wire.emu.contrast == 128 && said("Автояркость выключена"), "без автояркости — как задано: %u",
        Wire.emu.contrast);
  type("dim");
  type("laugh");
  CHECK(Wire.emu.contrast == 128, "в ручном режиме — как задано: %u", Wire.emu.contrast);
  type("a");
  type("t 12:00");
  type("b 127");
  run_ms(1000);

  // Помеха на шине: сорвавшаяся отправка — и следом весь кадр целиком.
  {
    const uint32_t f0 = stat_frames;
    Wire.emu.fail_next = 1;
    while (stat_frames == f0) loop();
    CHECK(have_oled && screen_matches(), "после помехи кадр дошёл целиком");
  }
  // Экран отключили и подключили снова: модуль включается с нуля — выключен,
  // постраничная адресация, пустая память. Прошивка находит его, включает и перерисовывает.
  Serial.out.clear();
  Wire.emu.present = false;
  for (const uint64_t end = host::now_us + 3000000ull; host::now_us < end;) loop();
  CHECK(!have_oled && said("Экран не отвечает"), "пропажа экрана замечена");
  Wire.emu.present = true;
  Wire.emu.on = false;
  Wire.emu.mode = 2;
  Wire.emu.contrast = 0;
  memset(Wire.emu.ram, 0, sizeof Wire.emu.ram);
  for (const uint64_t end = host::now_us + 3000000ull; host::now_us < end;) loop();
  CHECK(have_oled && Wire.emu.on && Wire.emu.mode == 0 && Wire.emu.contrast == 127 && screen_matches(),
        "экран вернулся: включён, горизонтальная адресация, яркость %u, кадр на месте", Wire.emu.contrast);
  run_ms(2000);

  // Запись роста первой версии (с плат разработки) переносится и переписывается в новом формате.
  {
    uint8_t v1[56] = {};
    auto put16 = [&](int o, uint32_t v) { v1[o] = (uint8_t)v, v1[o + 1] = (uint8_t)(v >> 8); };
    auto put32 = [&](int o, uint32_t v) { put16(o, v & 0xFFFF), put16(o + 2, v >> 16); };
    put32(0, Progress::kMagic), put16(4, 1), put16(6, 9), put32(8, 1000), v1[32] = 4;
    for (int i = 0; i < kExprCount; ++i)
      if (implant_of((Expr)i) <= 4) v1[34 + i / 8] |= (uint8_t)(1u << (i % 8));
    uint32_t c = 0xFFFFFFFFu;
    for (int i = 0; i < 52; ++i) {
      c ^= v1[i];
      for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    put32(52, ~c);
    Preferences::store()["keychain/progress"].assign(v1, v1 + sizeof v1);
    setup();
    CHECK(progress.days == 9 && progress.xp == 1000 && progress.implant == 4,
          "рост версии 1 перенесён: день %u, опыт %u, имплант %u", progress.days, progress.xp, progress.implant);
    CHECK(Preferences::store()["keychain/progress"].size() == sizeof(Progress), "и сразу записан в новом формате");
    run_ms(5000);
  }

  // Переполнение millis() через 49.7 суток работы: стенд живёт дальше в любом режиме, часы не прыгают.
  {
    const uint64_t W = 1ull << 32;
    const char* const cmds[] = {"neutral", "e charge_start", "e dark"};
    for (const char* cmd : cmds) {
      g_skipped_us += (W - 90000u) * 1000ull - host::now_us;
      host::now_us = (W - 90000u) * 1000ull;
      setup();
      type(cmd);
      type("t 12:00");
      run_ms(85000);
      const uint32_t r0 = eng.stats().renders;
      run_ms(120000);
      CHECK(eng.stats().renders - r0 > 20, "«%s»: после переполнения лицо живёт (%u отрисовок за 2 мин)", cmd,
            eng.stats().renders - r0);
      CHECK(!have_oled || screen_matches(), "«%s»: экран совпадает с кадром", cmd);
      Serial.out.clear();
      type("i");
      CHECK(said("время 12:03") || said("время 12:04"), "«%s»: часы не прыгнули при переполнении", cmd);
    }
  }

  CHECK(g_mismatch == 0, "экран расходится с кадром %u раз", g_mismatch);
  CHECK(Wire.emu.overflows == 0, "переполнение буфера Wire: %u", Wire.emu.overflows);

  const double sec = (host::now_us - g_skipped_us) / 1e6;
  printf("Стенд: %.0f с виртуального времени, %u транзакций, %.1f КБ по шине (%.0f байт/с)\n", sec,
         Wire.emu.transactions, Wire.emu.bytes / 1024.0, Wire.emu.bytes / sec);
  if (verbose) fputs(Serial.out.c_str(), stdout);
  printf("%s: %d проверок, %d ошибок\n", g_fail ? "FAIL" : "OK", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
