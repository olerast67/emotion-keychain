// Выбор языка и английские причины реакций характера (Behavior::last_reason).
#include "face/lang.h"

namespace face {

namespace {

Lang g_lang = Lang::Ru;

bool same(const char* a, const char* b) {
  while (*a && *a == *b) ++a, ++b;
  return *a == *b;
}

struct Reason {
  const char* ru;
  const char* en;
};

// Ключ — русская строка из behavior.cpp. Тест сверяет, что переведена каждая.
const Reason kReasons[] = {
    {"секретный стук", "secret knock"},
    {"разбудили ночью", "woken up at night"},
    {"разбудили тапом", "woken up by a tap"},
    {"тронули после долгой разлуки", "touched after a long separation"},
    {"тыкают без остановки", "poked non-stop"},
    {"слишком много тапов", "too many taps"},
    {"надоедают тапами", "pestered with taps"},
    {"тройной тап", "triple tap"},
    {"двойной тап, но он не в духе", "double tap, but not in the mood"},
    {"двойной тап", "double tap"},
    {"тап, но он не в духе", "tap, but not in the mood"},
    {"отошёл после вспышки, неловко", "calmed down after an outburst, awkward"},
    {"тап после долгой паузы", "tap after a long pause"},
    {"тап", "tap"},
    {"укачало от тряски", "motion-sick from shaking"},
    {"сильно трясли", "shaken hard"},
    {"трясут, когда он зол", "shaken while angry"},
    {"встряхнули", "shaken"},
    {"падаю!", "falling!"},
    {"третье падение за 10 минут", "third fall in 10 minutes"},
    {"опять уронили", "dropped again"},
    {"упал и ударился", "fell and got hurt"},
    {"стукнули", "bumped"},
    {"взяли в руки сонного", "picked up while sleepy"},
    {"взяли после долгой разлуки", "picked up after a long separation"},
    {"взяли в руки после перерыва", "picked up after a break"},
    {"взяли в руки", "picked up"},
    {"пошли гулять", "going for a walk"},
    {"идём", "walking"},
    {"бежим, но сил мало", "running, but low on energy"},
    {"бежим", "running"},
    {"перевернули вверх ногами", "turned upside down"},
    {"долго висел вверх ногами", "hung upside down for a long time"},
    {"вернули как было", "turned back over"},
    {"громкий звук разбудил", "woken by a loud sound"},
    {"опять шумят", "noisy again"},
    {"громкий звук", "loud sound"},
    {"рядом говорят", "someone is talking nearby"},
    {"музыка сквозь сон", "music through sleep"},
    {"играет музыка", "music is playing"},
    {"внезапно стало темно", "suddenly dark"},
    {"снова светло после долгой темноты", "light again after a long darkness"},
    {"снова светло", "light again"},
    {"резко очень ярко", "suddenly very bright"},
    {"холодно", "cold"},
    {"жарко", "hot"},
    {"температура пришла в норму", "temperature back to normal"},
    {"подключили зарядку", "charger plugged in"},
    {"зарядку подключили в последний момент", "charger plugged in at the last moment"},
    {"голодного покормили", "fed while hungry"},
    {"зарядился", "fully charged"},
    {"отключили зарядку", "charger unplugged"},
    {"мало заряда", "low battery"},
    {"заряд почти закончился", "battery almost empty"},
    {"нажали кнопку", "button pressed"},
    {"выключают", "being turned off"},
    {"сам по себе, по характеру", "on its own, true to character"},
    {"сам по себе", "on its own"},
    {"заставка на зарядке попала в угол", "charging screensaver hit the corner"},
    {"настроение и обстановка", "mood and surroundings"},
    {"новый имплант", "new implant"},
    {"достижение", "achievement"},
    {"выключается: ячейка пуста", "shutting down: cell empty"},
    {"выключается", "shutting down"},
    {"день рождения хозяина", "owner's birthday"},
    {"Новый год", "New Year"},
    {"Хэллоуин", "Halloween"},
    {"пятница, 13-е", "Friday the 13th"},
    {"1 апреля", "April 1"},
    {"1 апреля: розыгрыш", "April 1: prank"},
    {"день всех влюблённых", "Valentine's Day"},
    {"29 февраля: лишний день", "February 29: a bonus day"},
    {"день числа пи", "Pi Day"},
    {"день космонавтики", "Cosmonautics Day"},
    {"4 мая", "May the 4th"},
    {"день полотенца", "Towel Day"},
    {"день программиста", "Programmers' Day"},
    {"три часа ночи, а он не спит", "3 a.m. and still awake"},
    {"встретил такого же", "met its twin character"},
    {"новый знакомый брелок", "new keychain met"},
    {"встретил знакомого", "met a familiar keychain"},
    {"два глюка синхронизировались", "two glitches synced up"},
    {"старые знакомые шепчутся", "old friends whispering"},
    {"дружба крепнет", "friendship grows"},
    {"подарок от старшего брелока", "gift from an older keychain"},
    {"выиграл дуэль", "won a duel"},
    {"проиграл дуэль", "lost a duel"},
    {"выиграл в дуэли новую эмоцию", "won a new emotion in a duel"},
    {"секретный разговор с соседом", "secret chat with a neighbour"},
    {"сюрприз", "surprise"},
};

}  // namespace

void set_lang(Lang l) { g_lang = l == Lang::En ? Lang::En : Lang::Ru; }
Lang lang() { return g_lang; }

const char* lang_id(Lang l) { return l == Lang::En ? "en" : "ru"; }

bool lang_from_id(const char* id, Lang* out) {
  if (!id) return false;
  if (same(id, "ru")) return *out = Lang::Ru, true;
  if (same(id, "en")) return *out = Lang::En, true;
  return false;
}

const char* reason_text(const char* ru, Lang l) {
  if (!ru || l != Lang::En || !*ru) return ru;
  for (const Reason& r : kReasons)
    if (same(r.ru, ru)) return r.en;
  return ru;
}

bool reason_translated(const char* ru) {
  for (const Reason& r : kReasons)
    if (same(r.ru, ru)) return true;
  return false;
}

}  // namespace face
