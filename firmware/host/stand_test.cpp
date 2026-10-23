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
  type("duel win");
  run_ms(9000);
  CHECK(progress.duels_won == 1, "дуэль засчитана");
  type("date 2026-10-31");
  run_ms(1000);
  CHECK(eng.scene() == Scene::halloween, "Хэллоуин по дате");
  run_ms(8000);
  type("bday 05-14");
  CHECK(Preferences::store().count("keychain/bday_m"), "день рождения сохраняется");
  type("tilt 1 0");
  run_ms(800);
  CHECK(eng.face().gx > 0.5f, "наклон двигает взгляд: %.2f (сцена %s, эмоция %s)", eng.face().gx, scene_id(eng.scene()),
        expression(eng.expression()).id);
  type("tilt 0 0");
  type("serial 9");
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
  CHECK(eng.scene() == Scene::boot, "при старте — загрузка NEURAL LINK");
  run_ms(5000);
  type("reset yes");
  CHECK(progress.xp == 0 && progress.implant == 0, "рост начат заново");

  CHECK(g_mismatch == 0, "экран расходится с кадром %u раз", g_mismatch);
  CHECK(Wire.emu.overflows == 0, "переполнение буфера Wire: %u", Wire.emu.overflows);

  const double sec = host::now_us / 1e6;
  printf("Стенд: %.0f с виртуального времени, %u транзакций, %.1f КБ по шине (%.0f байт/с)\n", sec,
         Wire.emu.transactions, Wire.emu.bytes / 1024.0, Wire.emu.bytes / sec);
  if (verbose) fputs(Serial.out.c_str(), stdout);
  printf("%s: %d проверок, %d ошибок\n", g_fail ? "FAIL" : "OK", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
