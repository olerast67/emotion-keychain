# Аналоги и prior art: коммерческие «эмоциональные компаньоны» и open-source проекты лиц на OLED

> Дата сбора: 2026-10-23. Методические оговорки (важно для автора отчёта):
> - Сайты nekoto.in, dasai.com.au, judge.me, hackster.io, circuitdigest.com, mia-cat.com, projecthub.arduino.cc из среды исследования напрямую не открывались (DNS ENOTFOUND / 403 прокси). Данные по ним взяты из сниппетов и сводок поисковой системы, а не из полного текста страницы, поэтому уверенность ниже. Страницы GitHub открывались напрямую, звёзды/лицензии/даты взяты из GitHub Search API на 2026-10-23.
> - Лимит веб-поиска на сессию исчерпан в конце работы, поэтому несколько мелких вопросов остались открытыми (они перечислены в Gaps).
> - Репозиторий `olerast67/emotion-keychain` («Брелок с OLED-экраном и анимированными эмоциями», создан 2026-10-23, 0 звёзд), скорее всего, принадлежит самому пользователю. Как prior art он не учитывается.

## 1. Что известно о Nekoto (nekoto.in) и «Product 1»

### Takeaway
Надёжной публичной информации о Nekoto / nekoto.in и его «Product 1» найти не удалось. Домен не резолвился из среды исследования. Поиск по вебу, YouTube, Instagram, X, LinkedIn и TikTok выдаёт только несвязанных тёзок. Устройство из видео стоит считать неанонсированным продуктом из соцсетей: подтверждённых характеристик, цены и даты запуска нет.

### Cited Findings
- По запросам «nekoto.in keychain OLED», «nekoto product 1 launch keychain», «nekoto emotion companion keychain OLED India» и `"product 1 is going to launch soon"` выдача не содержит ни одной страницы nekoto.in. Вместо неё появляются DIY-брелоки других авторов и несвязанные бренды — [поиск: Hackster Tiny OLED Keychain](https://www.hackster.io/techtalkies1/tiny-oled-keychain-with-a-face-c4c694), [GitHub InteractiveSmartKeychain](https://github.com/hazelrc/InteractiveSmartKeychain).
- Несвязанные тёзки:
  - японский магазин кошачьих дизайнерских товаров nekoto (shop.nekoto.jp) — [shop.nekoto.jp](https://shop.nekoto.jp/);
  - игра NEKOTO в Steam (разработчик loloop, релиз 10.05.2024) — [Steam](https://store.steampowered.com/app/2926270/NEKOTO/);
  - приложение для изучения японского Nekoto AI — [nekoto.ai](https://nekoto.ai/);
  - YouTube-канал NekotoArts про геймдев — [YouTube](https://www.youtube.com/c/NekotoArts);
  - VTuber @OfficialNekoto — [X](https://x.com/officialnekoto).
- Похожий по форм-фактору коммерческий продукт с индийским следом в выдаче не найден. Ближайший коммерческий «брелок-компаньон с лицом» — Eiliko от Energize Lab (см. раздел 2) — [Kickstarter](https://www.kickstarter.com/projects/energize-lab/eiliko/description).

### Inferences
- Описание из видео (синий модуль 0.96" I2C SSD1306 с 4 пинами GND/VCC/SCL/SDA, плата с аккумулятором сзади, боковая кнопка/порт) почти дословно совпадает с типичной DIY-сборкой «ESP32-C3 SuperMini или XIAO + 0.96" SSD1306 + LiPo + TP4056». Примеры таких сборок: [shawrhit/Mochi](https://github.com/shawrhit/Mochi), [Keychain_motion](https://github.com/Godcomplexx/Keychain_motion), [InteractiveSmartKeychain](https://github.com/hazelrc/InteractiveSmartKeychain). Значит, «Product 1», вероятно, собран из стандартных модулей, а не на заказной плате. Это предположение: оно опирается только на описание видео.
- Голубой цвет модуля говорит о том, что панель монохромная (одноцветная синяя или двухцветная жёлто-синяя), а не цветная, как у Dasai Mochi 3 и Eiliko.

### Gaps
- Сайт nekoto.in не открылся (DNS ENOTFOUND 2026-10-23). Возможно, домен не делегирован, ещё не запущен или блокируется в среде исследования. Нужна ручная проверка: браузер, WHOIS, Instagram/YouTube-аккаунт из видео.
- Цена, характеристики (MCU, ёмкость аккумулятора, сенсоры), дата запуска и статус «Product 1» на октябрь 2026 не установлены.

---

## 2. Похожие коммерческие продукты: железо, батарея, сенсоры, реакции

### Takeaway
Ближе всех к идее стоят Dasai Mochi (автомобильный «робот» на торпеде, OLED-лицо, гироскоп, 70+ анимаций, до 8 ч работы экрана) и Eiliko от Energize Lab (брелок/подвеска с лицом на цветном TFT 1.28", аккумулятор 400 мА·ч, ~150 мин работы, реакции на касание, тряску и голос). Anki Cozmo задал эталон процедурного OLED-лица и показал, как бороться с выгоранием. Остальные устройства (Emo, Ropet, Fuzozo, Tamagotchi) — крупнее или устроены иначе, но полезны как ориентиры по поведению и автономности.

### Cited Findings

**Dasai Mochi (Австралия, автомобильный компаньон)**
- Mochi 3: встроенный аккумулятор, по заявлению производителя «up to 8 hours of screen on time»; «improved OLED Full Colour screen»; улучшенный динамик; «over 70 animations» — [dasai.com.au/pages/mochi3](https://dasai.com.au/pages/mochi3) (данные из сниппета поиска).
- Гироскоп в Mochi 3 «react to your pulls, braking and cornering». Гироскоп опрашивается раз в 30 секунд, а auto-off таймер выключает устройство, если движения нет — [dasai.com.au/pages/mochi3](https://dasai.com.au/pages/mochi3).
- Mochi 3 совместим со всеми «шлемами» (сменными корпусами) Version 2 — [dasai.com.au/products/mochi-3](https://dasai.com.au/products/mochi-3).
- Цены в официальном магазине: Mochi 3 — AUD 85 (на момент поиска «Sold out»), Tokyo Mochi 3 и Bosozoku — AUD 99, Futures Mochi — AUD 95 — [Mochi 3](https://dasai.com.au/products/mochi-3), [Tokyo Mochi 3](https://dasai.com.au/products/tokyo-mochi-3), [Bosozoku](https://dasai.com.au/products/special-bosozoku-mochi-3), [Futures](https://dasai.com.au/products/futures-mochi).
- Mochi 2.0 описывается как «RGB OLED powered accessory» с розничной ценой около $49–50. На eBay вторичный рынок Gen 2 держится на уровне ~$50–90 — [Resell Calendar](https://resellcalendar.com/items/dasai-mochi-generation-2/), [Resell Calendar news](https://resellcalendar.com/news/news/what-is-the-dasai-mochi-and-why-is-it-reselling/), [eBay](https://www.ebay.com/shop/dasai-mochi-2?_nkw=dasai+mochi+2).
- Сторонний гайд Warstek называет аккумулятор 3500 мА·ч и рекомендует зарядку 5V 1A. Неясно, к какой версии это относится; официально не подтверждено — [warstek.com](https://warstek.com/dasai-mochi/).
- Публичного тирдауна коммерческого Mochi (чип, ёмкость АКБ) найти не удалось. Все найденные описания железа относятся к DIY-клонам — [Hack Club archive](https://archive.hackclub.com/archive/hT1RV/mhtml/html), [shawrhit/Mochi](https://github.com/shawrhit/Mochi).

**Eiliko (Energize Lab) — брелок/подвеска с лицом, самый близкий коммерческий аналог по форм-фактору**
- Носимый «AI charm bot», который крепится на сумку или связку ключей. Делает та же компания, что выпускает настольного робота Eilik — [Kickstarter](https://www.kickstarter.com/projects/energize-lab/eiliko/description), [Energize Lab](https://store.energizelab.com/products/eiliko).
- Дисплей — 1.28" TFT, полноцветный — [RobotShop](https://www.robotshop.com/products/energize-lab-eiliko-tiny-ai-charm-bot-w-unique-emotions-ai-conversation-blue), [Energize Lab](https://energizelab.com/eiliko/).
- Аккумулятор 3.7 В / 400 мА·ч: около 150 мин работы, около 40 мин зарядки, standby около 10+ ч. Габариты 68×56×84 мм, масса ~70 г. Связь: Wi-Fi 2.4 ГГц, Bluetooth для настройки через приложение — [RobotShop](https://www.robotshop.com/products/energize-lab-eiliko-tiny-ai-charm-bot-w-unique-emotions-ai-conversation-blue), [Mia-Cat review](https://mia-cat.com/en/pet-robot/eiliko-review/) (данные из сводки поиска).
- Реагирует на касание, тряску и голос. ИК-сенсоры позволяют двум Eiliko «узнавать» друг друга и становиться «soulmates». AI-чат без подписки — [Gadgetify](https://www.gadgetify.com/eiliko/), [513.toys](https://513.toys/eiliko/), [Plataforma Media](https://www.plataformamedia.com/en/2025/08/16/kickstarter-sensation-energize-labs-ai-charm-bot-eiliko-hits-3800-of-goal-and-380k-in-funding-with-support-from-over-3000-backers/).
- Kickstarter (август 2025): более $380K, более 3000 бэкеров, 3800% от цели — [Plataforma Media](https://www.plataformamedia.com/en/2025/08/16/kickstarter-sensation-energize-labs-ai-charm-bot-eiliko-hits-3800-of-goal-and-380k-in-funding-with-support-from-over-3000-backers/).
- Предзаказ в розницу от $59.90, отгрузка с ноября 2025 — [Notebookcheck](https://www.notebookcheck.net/Eiliko-pre-orders-go-live-Energize-Lab-s-next-gen-AI-charm-bot-starts-at-59-90.1125100.0.html). Mia-Cat указывает Kickstarter-планы «от ~$119 (~18 000 иен)», что расходится с этой ценой — [Mia-Cat](https://mia-cat.com/en/pet-robot/eiliko-review/).
- На AliExpress и Amazon продаётся «Eiliko Smart AI Robot Pendant» с «LED Expression Display» (Brand: Generic, производитель FeiRui). Вероятно, это сторонний товар или подделка — [Amazon](https://www.amazon.com/Eiliko-Smart-Pendant-Interaction-Companion/dp/B0GZMYSV5W), [AliExpress](https://www.aliexpress.us/item/3256811755803404.html).

**Anki Cozmo / Vector — эталон процедурного лица**
- Лицо Cozmo — монохромный OLED 128×64 в кубическом «шлеме» — [Wikipedia: Cozmo](https://en.wikipedia.org/wiki/Cozmo), [cozmo-python-sdk oled_face.py](https://github.com/anki/cozmo-python-sdk/blob/master/src/cozmo/oled_face.py).
- Защита от выгорания в Cozmo: экран выводится через строку (interlaced), при каждой смене изображения смещение строк чередуется, а одна картинка держится не дольше 30 с. Из-за этого SDK отдаёт «логический» размер лица 128×32 — [Cozmo SDK docs](http://cozmosdk.anki.com/docs/generated/cozmo.oled_face.html), [oled_face.py](https://github.com/anki/cozmo-python-sdk/blob/master/src/cozmo/oled_face.py).
- В PyCozmo, открытой Python-реализации протокола, есть примеры `procedural_face.py` и `procedural_face_expressions.py` — [PyPI pycozmo](https://pypi.org/project/pycozmo/).
- Лицо Vector — цветной экран высокого разрешения (в обзорах его называют то IPS, то OLED, то LCD). Мимику разрабатывали бывшие аниматоры Pixar и DreamWorks — [TechCrunch](https://techcrunch.com/2018/08/08/vector-is-cozmo-for-grownups/), [The Gadgeteer](https://the-gadgeteer.com/2018/12/31/anki-vector-robot-review/), [Expert Reviews](https://www.expertreviews.co.uk/archived/anki-vector-review).

**Living.ai Emo**
- Высота ~5 дюймов, масса 248 г. 8 сенсоров: дальномер, 4 датчика края стола, акселерометр, касание, освещённость. Широкоугольная камера до 60 fps для распознавания лиц, 4 сервопривода — [Keyi Robot blog](https://keyirobot.com/blogs/buying-guide/emo-cute-robot-pet-review-is-livingais-emotional-robot-worth-it), [heyupnow](https://heyupnow.com/en-eu/products/living-ai-emo-robot) (источники продавцов, возможна предвзятость).

**Ropet и Fuzozo — AI-питомцы с «глазами-экранами»**
- Ropet (CES 2025): $299, Pro — $319. Экран служит глазами. Аккумулятор 4400 мА·ч, около 2 ч работы при 30 мин зарядки — [Forbes](https://www.forbes.com/sites/prakharkhanna/2025/01/07/ces-2025-ropet-can-be-your-emotional-ai-pet-companion-for-299/). В более позднем обзоре — 2.5–3.5 ч, по автономности «того же порядка, что EMO и Loona, и хуже Eilik или Moflin» — [Robot Review Desk](https://robotreviewdesk.com/reviews/ropet-review/).
- Fuzozo (RoboPoet): глаза-дисплей показывают эмоции. В публичном месте звук можно выключить и общаться только через выражение глаз. Полного заряда хватало на день умеренного использования — [TechNode, 2026-10-08](https://technode.com/2026/10/08/what-if-your-ai-had-fur-i-spent-several-days-with-fuzozo/). По данным CIW, у Fuzozo нет камеры — [CIW](https://www.ciw.news/p/china-ai-companions).

**Tamagotchi (Bandai) — ориентир по автономности «питомца»**
- Tamagotchi Paradise работает от AAA. По отзывам покупателей, пары батареек хватает примерно на 1.5 месяца, потому что устройство «быстро уходит в standby». Это пользовательские оценки, не спецификация — [desertcart](https://www.desertcart.ae/products/735696968-paradise-purple-sky).
- Tamagotchi Uni: встроенный Li-Po с зарядкой по USB-C, «до 1.5 недели» работы — [desertcart](https://www.desertcart.sg/products/533501177-uni-pink).

**Брелоки и «электронные питомцы» на AliExpress (2024–2026)**
- На AliExpress много «virtual pet keychain» с цветными LCD, реплик Bandai Tamagotchi в виде брелоков и плюшевых «AI Cat» с «8 animated emoji eyes». Значительная часть страниц похожа на автогенерированный SEO-контент — [AliExpress virtual pet keychain](https://www.aliexpress.com/w/wholesale-virtual-pet-keychain.html), [AliExpress wiki article](https://www.aliexpress.com/s/wiki-ssr/article/2in1-interactive-pet-keychain), [AliExpress pendant AI](https://www.aliexpress.com/w/wholesale-pendant-ai.html).
- Смежный пример: брелок-фонарик Nitecore Tini 3 с OLED-дисплеем и USB-C за $40 — [Notebookcheck](https://www.notebookcheck.net/Tini-3-Nitecore-introduces-keychain-flashlight-with-OLED-display-and-USB-C.990410.0.html).
- Исторический пример: KeyChainino — брелок на ATtiny с питанием от CR2032 — [Open Electronics](https://store.open-electronics.org/KEYCHAININO).

### Inferences
- Коммерческие устройства с цветным экраном, Wi-Fi и динамиком держатся от ~2.5 ч (Eiliko, 400 мА·ч) до «до 8 ч экрана» (Dasai). Монохромный SSD1306 с MCU, который умеет глубоко засыпать и будиться акселерометром, — реальный способ обойти их по автономности. Это главное потенциальное преимущество DIY-брелока.
- Dasai показывает рабочий паттерн: гироскоп не крутится непрерывно, а опрашивается раз в 30 с, и устройство выключается, если движения нет. Правило «не трогают — спим» стоит повторить.
- Опыт Cozmo показывает, что статичное лицо на OLED выгорает. Рабочие приёмы: смещать изображение или строки и ограничивать время показа статичной картинки (≤30 с).
- Eiliko задаёт планку рынка для брелоков: ~$60, цветной экран, AI-чат. Монохромный OLED-брелок разумно позиционировать иначе — как дешёвый, долго работающий и офлайновый.

### Gaps
- Чип, ёмкость аккумулятора и реальные токи Dasai Mochi 3 неизвестны: тирдауна нет, FCC-документы не проверены.
- Не найдено ни одного тирдауна Eiliko (MCU, драйвер дисплея).
- Конкретные AliExpress/Taobao-брелоки именно с монохромным OLED-лицом (а не LCD-тамагочи) в 2024–2026 не найдены. Скорее всего, они продаются под другими названиями. Сохраняется неопределённость.
- Loona и Moflin подробно не изучались (не брелоки, приоритет низкий).

---

## 3. Open-source проекты: библиотеки лиц, клоны Mochi, брелоки, десктоп-питомцы

### Takeaway
Есть два зрелых способа рисовать лицо на SSD1306:
- **Процедурный:** FluxGarage RoboEyes (917★, GPL-3.0) и playfultechnology esp32-eyes (382★, GPL-3.0, 18 эмоций в духе Cozmo).
- **Растровый:** клоны Dasai Mochi (upiir — 366★, MIT; huykhoong — 181★), где кадры из GIF или Rive конвертируются в массивы C.

Брелоков с лицом на GitHub уже десятки (HackCharm, Keychain_motion, InteractiveSmartKeychain, Tofu, NFC-eye и др.), но почти никто не публикует измеренные токи и время работы. Лицензии GPL-3.0 у RoboEyes и esp32-eyes заражают всю прошивку, а анимации Dasai защищены авторским правом. Из этого и надо исходить при выборе, что переиспользовать.

### Cited Findings

**Библиотеки процедурных глаз**
- **FluxGarage/RoboEyes** — 917★, 119 forks, GPL-3.0, последний push 2026-01-20, C++/Arduino. Использует Adafruit GFX и рисует глаза процедурно (ширина, высота, радиус скругления, расстояние между глазами).
  - Moods: DEFAULT, TIRED, ANGRY, HAPPY.
  - Анимации: autoblinker, idle (случайный взгляд), confused (горизонтальная тряска), laugh (вертикальная тряска), 8 направлений взгляда, curiosity, sweat, flicker.
  - Ограничение FPS задаётся в `begin()`.
  - Источник: [GitHub](https://github.com/FluxGarage/RoboEyes); звёзды — [GitHub search](https://github.com/FluxGarage/RoboEyes).
- **mchobby/micropython-roboeyes** — 35★, GPL-3.0, последний push 2025-03-25. Порт RoboEyes на MicroPython (FrameBuffer, SSD1306, тесты на 128×64 и 0.91" 128×32, вероятно Raspberry Pi Pico). Добавлены moods FROZEN, SCARY, CURIOUS и анимация wink — [GitHub](https://github.com/mchobby/micropython-roboeyes).
- yousseftechdev/RoboEyes-Micropython-SSD1306 (4★) и RoboEyesTFT (11★) — [GitHub](https://github.com/yousseftechdev/RoboEyesTFT).
- **playfultechnology/esp32-eyes** — 382★, GPL-3.0, последний push 2024-03-07.
  - «There are currently 18 emotions» (enum в FaceEmotions.hpp). Пресеты лежат в EyePresets.h (высота, наклон, радиус, смещение).
  - Архитектура: Face → Behaviour (взвешенный выбор эмоции «рулеткой») + Expression (переходы) + BlinkAssistant + LookAssistant (`LookAt(x,y)`, −1…1).
  - Графика на u8g2. Основан на ESP32_Faces (luisllamas), вдохновлён Cozmo.
  - Источник: [GitHub](https://github.com/playfultechnology/esp32-eyes).
- Форки и производные esp32-eyes: hackffm/ESP32S3_OLED_Eyes (5★, GPL-3.0, сенсорное управление) — [GitHub](https://github.com/hackffm/ESP32S3_OLED_Eyes); MarcoCiau/oled-robot-face-esp32 (демо 7 эмоций, туториал на испанском) — [GitHub](https://github.com/MarcoCiau/oled-robot-face-esp32).
- **ggldnl/Procedural-Expression-Library** — GPL-3.0. Каждый глаз задан 8-точечным полигоном в координатах [0,1]. Переходы — интерполяция точек, моргание — интерполяция в тонкий прямоугольник. Автор называет её «heavy both computationally and memory-wise», это proof of concept — [GitHub](https://github.com/ggldnl/Procedural-Expression-Library).
- **intellar/oled_eye_display** — 141★, MIT. «Реалистичные» глаза на OLED с Arduino Nano, команды приходят по USB. Подробности — в блоге [intellar.ca](https://www.intellar.ca/blog/animated-eye-oled) — [GitHub](https://github.com/intellar/oled_eye_display).
- **stack-chan/stack-chan** — 1805★, Apache-2.0, активен (push 2026-10-20). Робот на M5Stack (стандарт — CoreS3), прошивка на JavaScript, есть редактор лица «Arrange eyes and a mouth». M5Stack продаёт готовый M5StackChan AI Desktop Robot (K151) — [GitHub](https://github.com/stack-chan/stack-chan). Отдельный репозиторий m5stack/StackChan — 1410★ — [GitHub](https://github.com/m5stack/StackChan).
- **stack-chan/m5stack-avatar** — 237★, MIT, заморожен на v0.10.0. Expression Happy/Angry/Sad и т.д., лицо рисуется в отдельном потоке, lip-sync через AquesTalk — [GitHub](https://github.com/stack-chan/m5stack-avatar).
- **Pwnagotchi** (evilsocket) — 9239★, у GitHub лицензия NOASSERTION. Лица — текстовые kaomoji на e-paper, например:
  - `LOOK_R '( ⚆_⚆)'`, `SLEEP '(⇀‿‿↼)'`, `BORED '(-__-)'`, `INTENSE '(°▃▃°)'`, `COOL '(⌐■_■)'`;
  - `HAPPY '(•‿‿•)'`, `EXCITED '(ᵔ◡◡ᵔ)'`, `LONELY '(ب__ب)'`, `SAD '(╥☁╥ )'`, `ANGRY "(-_-')"`, `FRIEND '(♥‿‿♥)'`, `BROKEN '(☓‿‿☓)'`;
  - всего ~25 констант.
  - Источник: [faces.py](https://github.com/evilsocket/pwnagotchi/blob/master/pwnagotchi/ui/faces.py). Форк jayofelony/pwnagotchi — 2962★ — [GitHub](https://github.com/jayofelony/pwnagotchi). Мод PWNAGOTCHI-CUSTOM-FACES-MOD (301★) заменяет лица на PNG-спрайты — [GitHub](https://github.com/roodriiigooo/PWNAGOTCHI-CUSTOM-FACES-MOD).
- Piumy — e-paper-проект в духе Pwnagotchi: у каждого настроения свой пул вариантов kaomoji, из которых строится анимация — [Piumy docs](https://mintlify.wiki/chamilonster/Piumy/concepts/e-paper-face).
- Базовые графические библиотеки: u8g2 (6729★) — [GitHub](https://github.com/olikraus/u8g2); Adafruit_SSD1306 (2116★) — [GitHub](https://github.com/adafruit/Adafruit_SSD1306).

**Клоны Dasai Mochi (растровые анимации)**
- **upiir/esp32s3_oled_dasai_mochi** — 366★, MIT, push 2025-03-25. Seeed XIAO ESP32S3 + SSD1306 128×32 I2C. Анимации делаются в Rive, экспортируются PNG-последовательностью, правятся в Photopea и конвертируются в C-массивы через image2cpp. Видео: [youtu.be/QOoszpg0BsM](https://youtu.be/QOoszpg0BsM) — [GitHub](https://github.com/upiir/esp32s3_oled_dasai_mochi).
- **huykhoong/esp32_dasai_mochi_clone_and_how_to** — 181★, лицензии нет, push 2025-05-12. GIF/видео обрезаются в ffmpeg и конвертируются в .h через gif2cpp. Эмоции «extracted from» чужого YouTube-видео, «Full image rights belong to the original creator». README противоречив: заявлены OLED и «15 FPS on ILI9341 / ST7789» — [GitHub](https://github.com/huykhoong/esp32_dasai_mochi_clone_and_how_to).
- **pham-tuan-binh/watcher-mochi** — 48★, Apache-2.0 (только код), push 2026-09-11. Прошивка для SenseCAP Watcher (ESP32-S3): 63 GIF на microSD.
  - Поведение: тап — случайная анимация со звуком; 30 с без действий — автопроигрывание; 5 мин — deep sleep.
  - Права на GIF: «All rights to the animations belong to Dasai».
  - Источник: [GitHub](https://github.com/pham-tuan-binh/watcher-mochi).
- **HARAJIT05/XERO** — 61★. ESP32 + SSD1306 I2C, «inspired by DASAI MOCHI V2». Лицензия противоречива: в боковой панели EPL-2.0, в README — MIT — [GitHub](https://github.com/HARAJIT05/XERO).
- Прочие: LukeStrazz/StrazzTunedBuddy (109★, без лицензии, 2023) — [GitHub](https://github.com/LukeStrazz/StrazzTunedBuddy); NeoDoggy/BetterMochi (50★, MIT) — [GitHub](https://github.com/NeoDoggy/BetterMochi); upiir/lego_dasai_mochi (11★, MIT, 0.85" 128×128 IPS) — [GitHub](https://github.com/upiir/lego_dasai_mochi); camiu01/OTA-Bot-Mochi (векторные глаза, IMU-наклон, BLE) — [GitHub](https://github.com/camiu01/OTA-Bot-Mochi); octarudin/mochi-bot (ESP32-C3 SuperMini + MPU-6050 + 1.3" OLED, неблокирующий код) — [GitHub](https://github.com/octarudin/mochi-bot).
- **shawrhit/Mochi** — 1★, лицензия не указана.
  - Железо: ESP32-C3 SuperMini, 0.96" SSD1306, TTP223 (единственный ввод), пьезо, TP4056+DW01, LiPo «300–500 mAh».
  - Лица хранятся как PROGMEM-битмапы.
  - Рендер идёт в кооперативном `loop()` на `millis()`, без FreeRTOS-задач, «чтобы не прерывать I2C».
  - «Power Off» = light sleep: ток падает «from ~120mA to ~100µA».
  - При brownout на экран выводится «BATTERY LOW», и устройство принудительно засыпает, чтобы не уйти в boot-loop.
  - Источник: [GitHub](https://github.com/shawrhit/Mochi).
- vignarth/mini-mochi-bot: LiPo 800 мА·ч (WLY102040) + TP4056, питание 5 В на VIN ESP32-C3 — [GitHub](https://github.com/vignarth/mini-mochi-bot). Клон на XIAO ESP32-C3 обходится без отдельного TP4056 благодаря встроенному заряднику — [Hackster](https://www.hackster.io/lsw031105guge-bit/how-a-noob-can-clone-the-mochi-project-cdf60d). Ремикс на Thingiverse: аккумулятор 16340 + TP4056 USB-C + 1.3" OLED — [Thingiverse](https://www.thingiverse.com/thing:7255174/files).

**Брелоки с лицом (ближайшие DIY-аналоги)**
- **Godcomplexx/Keychain_motion** (0★, push 2026-09-30) — самый продуманный по поведению.
  - Железо: v1 — ESP32-C3 SuperMini + MPU-6050; v2 — nRF52840 (EBYTE E73) + LIS2DW12, акселерометр ещё не запаян. Дисплей 0.96" SSD1306.
  - Сон: через 30 с без движения — SLEEP, ещё через 30 с OLED гаснет, будит IMU.
  - Цель автономности — 6 ч на 350 мА·ч (то есть ≤58.3 мА в среднем). Реальные токи «must be measured».
  - Реакции: «Picked up → eyes snap open», наклон — плавный взгляд, рывок — широко раскрытые глаза и «подскок», тряска — нахмуренные брови и дрожь.
  - Состояния: Happy при подключении зарядки, Bored через 30 с одиночества, Asleep через 5 мин.
  - Параметр «Bond» 0–100 растёт от общения и спадает за часы.
  - Синхронизация времени по BLE из Android-приложения.
  - Лица процедурные: «there are no sprite sheets and a new mood costs one `switch` case». Форма век портирована из «Pip eye engine» (Hamza Yeşilmen), а эта часть под некоммерческой лицензией.
  - Источник: [GitHub](https://github.com/Godcomplexx/Keychain_motion).
- **shiva233/HackCharm** — 8★, MIT, 2025-08-15. Круглый брелок: XIAO ESP32-S3, OLED 128×128, акселерометр ADXL362 (тряска), тактовая кнопка, LiPo 450 мА·ч. Собственная плата от JLCPCB («Everything but the screen and battery are on the PCB»), 3D-печатный корпус — [GitHub](https://github.com/shiva233/HackCharm).
- **hazelrc/InteractiveSmartKeychain** — 6★, лицензия не указана, 2026-03-04.
  - Железо и софт: ESP32-C3 SuperMini, SSD1306, RoboEyes.
  - Жесты: 1–5 тапов и удержание ≥600 мс («pet»).
  - Wi-Fi-портал WiFiManager (AP «RoboEyes-Setup»), NTP-часы.
  - Сна нет: он указан как будущая доработка.
  - Источник: [GitHub](https://github.com/hazelrc/InteractiveSmartKeychain).
- **TechTalkies 123_Keychain** — Seeed XIAO ESP32 + 1.3" OLED 128×64 I2C. LiPo припаян к пэдам XIAO, слайд-выключатель стоит последовательно с «+» батареи, STL корпуса прилагаются — [GitHub](https://github.com/TechTalkies/Youtube-2/tree/main/123_Keychain). Вариант MCU расходится: Hackster пишет XIAO ESP32C3, CircuitDigest — XIAO ESP32-S3 — [Hackster](https://www.hackster.io/techtalkies1/tiny-oled-keychain-with-a-face-c4c694), [CircuitDigest](https://circuitdigest.com/news/built-a-compact-diy-oled-keychain-gadget-powered-by-a-rechargeable-xiao-esp32-s3-microcontroller).
- **HardCoreTools/Blinking-eye-keychain-with-NFC** — 1★, MIT, 2026-10-21. ATtiny1616, 0.96" SSD1306 I2C, NTAG I2C+ (пробуждение от NFC-поля), LiPo 100 мА·ч, зарядка только внешняя, слайд-выключатель — [GitHub](https://github.com/HardCoreTools/Blinking-eye-keychain-with-NFC).
- **sagarrryadavv/Tofu_the_keychain_bot** — лицензии нет, 2026-04-30.
  - Железо: разогнанный STM32C011J6M6, 0.96" SH1106, 2 ёмкостные площадки.
  - Эмоции и функции: смех, еда, сон, злость, плач, Pomodoro 25 мин, «dizzy».
  - Bare-metal I2C: прямой доступ к регистрам, развёрнутые циклы, «OLED address reset every frame».
  - Источник: [GitHub](https://github.com/sagarrryadavv/Tofu_the_keychain_bot).
- Другие брелоки:
  - Guaggy/attiny85-keychain — лицо на OLED 128×64, «предсказания», игры и QR-код в 8 КБ — [GitHub](https://github.com/Guaggy/attiny85-keychain);
  - Pruthvirajtipe/mimo-project — [GitHub](https://github.com/Pruthvirajtipe/mimo-project);
  - V1kraman/Oled-Eyes-Keychain (MIT) — [GitHub](https://github.com/V1kraman/Oled-Eyes-Keychain);
  - Jay-Masih-Ekka/iot-oled-display-keychain (ESP32-C3, 1.3", I2C 400 кГц для видео) — [GitHub](https://github.com/Jay-Masih-Ekka/iot-oled-display-keychain);
  - kirama4789/oled-draw-keychain (рисунок 1024 байта по MQTT на SSD1306) — [GitHub](https://github.com/kirama4789/oled-draw-keychain).
- CIKI Mini Companion Keychain (calvvin): Arduino Nano R3 + OLED 128×64 + аккумулятор 3.7 В, 3D-печать, пошаговой инструкции нет — [Hackster](https://hackster.io/calvvin/ciki-mini-companion-keychain-760210).
- На странице тега «keychain» в Hackaday есть проект на XIAO ESP32-C3 с OLED 0.49" и LiPo 40 мА·ч — [Hackaday tag](https://hackaday.com/tag/keychain/).

**Десктоп-питомцы с сенсорами (полезные паттерны поведения)**
- **SukunDev/ESP32-Pet-Robot** — 27★, MIT, 2025-11-05. ESP32-C3-DevKitM-1 + SSD1306 + TTP223 + микрофон + MPU6050, RoboEyes плюс собственный RoboEyesManager.
  - Простой → через 5 с sleepy, через 10 с asleep.
  - Касание → случайно happy или angry; касание во сне → angry.
  - Тряска, подъём или падение → scared; наклон → взгляд в сторону наклона.
  - Звук во сне → scared или angry.
  - Источник: [GitHub](https://github.com/SukunDev/ESP32-Pet-Robot).
- **ZnFr60/ESP32C3-Desk-pet** — 9★, GPL-3.0 с пометкой «for learning and exchange only», 2026-08-25. ESP32-C3 + SSD1306 + MPU6050.
  - Выражения: «23+» (happy, surprised, sleepy, angry, pain, dizzy, thinking, bored…).
  - Активности: питьё, чтение, еда, танцы и т.д.
  - Mood 0–100 растёт от общения и спадает со временем.
  - Реакции: падение → pain, тряска → dizzy, щелчок → anger, наклон → взгляд. Есть шагомер.
  - Питание: auto-dim, idle sleep, deep sleep. Wi-Fi выключается после синхронизации времени.
  - Источник: [GitHub](https://github.com/ZnFr60/ESP32C3-Desk-pet).
- Другие питомцы:
  - jagraj-babbar/Smart-DeskPet-Robot (MIT; ESP32-C3, «Pixar-style» глаза, MPU6050, deep sleep) — [GitHub](https://github.com/jagraj-babbar/Smart-DeskPet-Robot);
  - milan-sony/deskey-v2 (MIT; реагирует на контекст ПК) — [GitHub](https://github.com/milan-sony/deskey-v2);
  - denuwanbogoda33/ESP-EMO (10★) — [GitHub](https://github.com/denuwanbogoda33/ESP-EMO);
  - серия «Starbie» Hack Club (XIAO ESP32-C3 + OLED + MPU6050 + DHT11) — [GitHub](https://github.com/Sagarpillai/squarbie).

**Тамагочи-эмуляторы**
- **GaryZ88/ArduinoGotchi** — 251★, GPL-2.0, 2023-10-16.
  - Эмулирует Tamagotchi P1 на TamaLib с оптимизацией под Arduino UNO (32 КБ Flash, 2 КБ RAM).
  - Железо: SSD1306 128×64 на U8g2, 3 кнопки, buzzer. Дисплей обновляется с частотой 3 fps.
  - Автосохранение в EEPROM раз в 60 мин, «to preserve EEPROM lifespan».
  - ROM в комплект не входит из-за авторских прав.
  - Источник: [GitHub](https://github.com/GaryZ88/ArduinoGotchi).
- jcrona/mcugotchi (151★, GPL-2.0) — [GitHub](https://github.com/jcrona/mcugotchi); GMMan/flipperzero-tamagotch-p1 (181★) — [GitHub](https://github.com/GMMan/flipperzero-tamagotch-p1); sqfmi/tamawatchy (28★, GPL-3.0) — [GitHub](https://github.com/sqfmi/tamawatchy); mindovermiles262/tamaputer (M5Stack Cardputer) — [GitHub](https://github.com/mindovermiles262/tamaputer).

### Inferences
- **Что переиспользовать с учётом лицензий:**
  - RoboEyes и esp32-eyes — GPL-3.0. Если прошивку распространять (например, продавать брелоки), придётся открыть весь код под GPL. Для личного проекта ограничений нет.
  - Permissive-источники идей и кода: m5stack-avatar (MIT), intellar (MIT), HackCharm (MIT), NFC-eye (MIT), upiir (MIT), watcher-mochi (Apache-2.0, но без GIF).
  - Kaomoji-лица Pwnagotchi — фактически набор текстовых строк. Сам стиль «( ⚆_⚆)» свободен, но лицензию файла нужно проверить.
- **Анимации Dasai Mochi** в клонах (huykhoong, watcher-mochi и др.) прямо названы чужой собственностью. Для своего продукта нужны собственные кадры (Rive → PNG → image2cpp, как у upiir) или процедурная генерация.
- **Процедурный подход** (RoboEyes, esp32-eyes, Keychain_motion) экономит Flash и позволяет плавно смешивать состояния. Битмапы проще для «мультяшных» выражений вроде «> <» и звуковых волн «listening», но занимают 1 КБ на кадр 128×64. Практичный вариант — гибрид: процедурные глаза плюс небольшие спрайты-оверлеи (звуковые волны, «z», сердечки, пот).
- **Повторяющиеся поведенческие паттерны,** которые стоит взять:
  - простой → сонливость → сон с таймерами (5/10 с у SukunDev, 30 с / 5 мин у Keychain_motion и watcher-mochi);
  - подъём → пробуждение, тряска → dizzy, падение → pain, наклон → взгляд;
  - счётчик «привязанности» 0–100, который растёт от общения и спадает со временем (Keychain_motion, ZnFr60);
  - реакция «happy» на подключение зарядки.
- Ни один DIY-брелок не публикует измеренного времени работы. Аккуратный замер токов и честная цифра автономности сами по себе выделят проект.
- Проектные решения в брелоках: слайд-выключатель последовательно с батареей (TechTalkies, NFC-eye), пробуждение по NFC-полю (HardCoreTools), сон с пробуждением от IMU (Keychain_motion), сон при brownout против boot-loop (shawrhit).

### Gaps
- Точные списки эмоций esp32-eyes (18) и ZnFr60 (23) в README не приведены, они лежат в исходниках (FaceEmotions.hpp и т.д.) и не проверялись.
- Лицензия «Pip eye engine» и его репозиторий не найдены поиском. Известно только описание из README Keychain_motion: «not OSI open source, personal non-commercial use only».
- Отдельный проект «intellar Oled_Eyes» под таким именем не найден. У intellar есть `oled_eye_display` (141★, MIT), `tft_face_display`, `Dual_Display_ESP32`.
- Проекты «Wall-E eyes» и Instructables/Hackster «Emo robot DIY» отдельно не изучались, кроме найденных на GitHub (ESP-EMO, EMO-Lite и т.п.).
- Лицензию Pwnagotchi в файле LICENSE я не проверял: GitHub API показывает NOASSERTION.

---

## 4. Готовые платы как база для брелока и какие проекты их используют

### Takeaway
Ближе всего к готовому брелоку Seeed XIAO nRF52840 Sense: 6-осевой IMU, PDM-микрофон и зарядник BQ25101 уже на плате, deep sleep ~5 мкА по данным продавцов. Среди ESP32 лучшие токи сна у XIAO ESP32-S3 и C6 (~14–15 мкА), у C3 — ~44 мкА. ESP32-C3 с OLED 0.42" (72×40) стоит ~$5, но зарядника на ней нет. M5StickC Plus2 и AtomS3 — готовые корпусированные устройства, но с цветными LCD и уже сняты с производства. В DIY-проектах доминируют ESP32-C3 SuperMini и XIAO ESP32-S3/C3.

### Cited Findings
- **Seeed XIAO nRF52840 Sense:**
  - на плате есть PDM-микрофон и 6-осевой IMU;
  - зарядка LiPo через BQ25101 (ток 50/100 мА, вход 5 В), LiPo припаивается к пэдам BAT;
  - deep sleep / System OFF ~5 мкА (заявление продавцов);
  - источники: [The Pi Hut](https://thepihut.com/products/seeed-xiao-ble-nrf52840-sense), [RobotShop](https://jp.robotshop.com/en/products/seeed-studio-xiao-nrf52840-sense-xiao-ble-sense), [ShillehTek manual](https://shillehtek.com/blogs/shillehtek-product-manuals/xiao-seeed-nrf52840-pre-soldered-with-usb-c-cable-manual).
- **Seeed XIAO ESP32:**
  - deep sleep по таблице Seeed: C6 ~15 мкА, C3 ~44 мкА, S3 ~14 мкА; modem-sleep ~30/24/25 мА — [Seeed XIAO ESP32 series](https://www.seeed.cc/product/seeed-studio-xiao-esp32-series), [Fab Academy](https://fabacademy.org/2024/labs/kannai/Instruction/tips/battery/);
  - ток заряда C3 в источниках расходится: 350/100 мА против 50/100 мА — [Elektor](https://elektor.com/products/seeed-studio-xiao-esp32c3), [mischianti](https://mischianti.org/seeed-studio-xiao-esp32-c3-high-resolution-pinout-datasheet-schema-and-specs/);
  - модель Qoitech (800 мА·ч, 500 мс активности раз в 60 с): S3 ≈ 1.2 мес, C3 ≈ 2.5 мес, C6 ≈ 2.3 мес. Это модель, а не стендовое измерение, и для S3 она расходится с порядком по сну из даташита — [Qoitech](https://www.qoitech.com/?p=19961);
  - у XIAO C3 нет пользовательского LED, но красный LED заряда горит, когда USB подключён без батареи — [mischianti](https://mischianti.org/seeed-studio-xiao-esp32-c3-high-resolution-pinout-datasheet-schema-and-specs/). Поисковая сводка также упоминала, что у клонов ESP32-C6 SuperMini RGB-светодиод разряжает батарею во сне, но к какому из найденных источников это относится, неясно, так что утверждение не проверено.
- **ESP32-C3 с OLED 0.42"** (01Space / Aitver):
  - ESP32-C3FN4, 4 МБ, керамическая антенна, ~25×20 мм;
  - видимая область 72×40 (контроллер SSD1306 со смещением от 128×64): SDA — GPIO5, SCL — GPIO6, LED — GPIO8;
  - питание 3.3–6 В, USB-C, ~$4.99 на AliExpress;
  - зарядник LiPo в источниках не упоминается;
  - источники: [Notebookcheck](https://www.notebookcheck.net/This-tiny-ESP32-C3-development-board-comes-with-an-even-tinier-0-42-inch-display.1239291.0.html), [Notebookcheck RU](https://www.notebookcheck-ru.com/EHta-kroshechnaja-plata-ESP32-C3-postavljaetsja-s-eshche-bolee-kroshechnym-0-42-djuimovym-displeem.1239604.0.html), [Zephyr docs](https://docs.zephyrproject.org/latest/boards/01space/esp32c3_042_oled/doc/index.html), [Otronic](https://www.otronic.nl/en/esp32-c3-ontwikkelbord-met-mini-042-inch-midi-oled.html).
- **M5StickC Plus2:**
  - аккумулятор 200 мА·ч (у Plus было 120);
  - 1.14" ST7789V2 135×240, IMU, микрофон, ИК, buzzer;
  - цена: ¥4,180 / £24 без НДС / ~$18–22;
  - снят с производства, преемник — M5StickS3;
  - источники: [Switch Science](https://www.switch-science.com/products/9350), [atomic14](https://www.atomic14.com/esp32/boards/m5stickc-plus2/).
- **AtomS3:** поисковая сводка по Switch Science сообщала, что модель снята с производства (преемник — AtomS3R) и стоила около ¥3,014, но точный URL карточки AtomS3 не получен, так что это не проверено (см. Gaps).
- **SenseCAP Watcher** (ESP32-S3, microSD) — база для watcher-mochi — [GitHub](https://github.com/pham-tuan-binh/watcher-mochi).
- **Какие проекты на каких платах:**
  - XIAO ESP32-S3: upiir Mochi, HackCharm, nex.pod (XIAO ESP32-S3 multi-mode device) — [upiir](https://github.com/upiir/esp32s3_oled_dasai_mochi), [HackCharm](https://github.com/shiva233/HackCharm), [nex.pod](https://github.com/Ekam-wadhwa1/nex.pod);
  - XIAO ESP32-C3: Mochi-клон на Hackster, Squarbie/Starbie, брелок с Hackaday 0.49" — [Hackster](https://www.hackster.io/lsw031105guge-bit/how-a-noob-can-clone-the-mochi-project-cdf60d), [squarbie](https://github.com/Sagarpillai/squarbie), [Hackaday](https://hackaday.com/tag/keychain/);
  - ESP32-C3 SuperMini: shawrhit/Mochi, InteractiveSmartKeychain, Keychain_motion v1, octarudin/mochi-bot, Yukiene404/RoboEyes-ESP32-C3-Emotion-Display — [shawrhit](https://github.com/shawrhit/Mochi), [hazelrc](https://github.com/hazelrc/InteractiveSmartKeychain), [Keychain_motion](https://github.com/Godcomplexx/Keychain_motion), [octarudin](https://github.com/octarudin/mochi-bot), [Yukiene404](https://github.com/Yukiene404/RoboEyes-ESP32-C3-Emotion-Display);
  - nRF52840 (модуль E73): Keychain_motion v2 — [GitHub](https://github.com/Godcomplexx/Keychain_motion);
  - M5Stack CoreS3: Stack-chan — [GitHub](https://github.com/stack-chan/stack-chan); M5Stack Cardputer: tamaputer — [GitHub](https://github.com/mindovermiles262/tamaputer).

### Inferences
- Для монохромного брелока с IMU и микрофоном (режим «listening») XIAO nRF52840 Sense закрывает почти всю электронику (MCU+BLE, IMU, микрофон, зарядник) на плате 21×17.5 мм. Останется добавить OLED, кнопку и LiPo. Размер — из общих знаний о форм-факторе XIAO, в этой сессии не проверялся.
- Если нужен Wi-Fi (NTP, погода), разумнее XIAO ESP32-S3 или C6: сон ~14–15 мкА против ~44 мкА у C3. Платы ESP32-C3 SuperMini дешёвые, но их ток во сне зависит от LED и LDO, его нужно мерить.
- Плата ESP32-C3 0.42" не подходит под требование «0.96" 128×64»: экран 72×40 и нет зарядника. Её можно использовать как дешёвый стенд для отладки.
- Для пользователя в РФ/СНГ: всё перечисленное, кроме Seeed XIAO, в основном китайские модули с AliExpress. Доступность XIAO на AliExpress в РФ отдельно не проверялась (см. Gaps).

### Gaps
- Наличие и цены Seeed XIAO (nRF52840 Sense, ESP32-S3/C3/C6), Waveshare и LilyGO на AliExpress с доставкой в РФ на октябрь 2026 не проверены: исчерпан лимит поиска.
- Waveshare ESP32-C3 с OLED 0.42" и платы LilyGO с LiPo-зарядкой отдельно не изучались. Найдена только плата 01Space/Aitver.
- Характеристики дисплея и IMU AtomS3 и точный URL карточки AtomS3 у Switch Science не получены.
- Реальные (измеренные) токи XIAO nRF52840 Sense с активными IMU и микрофоном в найденных источниках отсутствуют.

---

## 5. Что хвалят и на что жалуются пользователи (выводы для дизайна функций)

### Takeaway
Главные претензии к коммерческим компаньонам — короткая автономность (Eiliko ~2.5 ч, Ropet 2–3.5 ч, Futures Mochi 3–4 ч) и угасание новизны (у EMO, по одному отзыву, к 6-й неделе). Хвалят «живость» за счёт контекстных реакций, которых пользователь ещё не видел. Есть и мелкие жалобы: блики, неподходящий зарядный кабель, подписки. Отдельный урок даёт Cozmo: статичное OLED-лицо выгорает.

### Cited Findings
- **Dasai Mochi:**
  - средняя оценка Mochi 3 — 4.77/5 при ~1500 отзывах. Виджет отзывов магазина может показывать отфильтрованную выборку — [judge.me](https://judge.me/reviews/stores/dasai.com.au/products/mochi-3);
  - Futures Mochi держит заряд «about 3 to 4 hours», покупатель называет это очень мало — [Futures Mochi](https://dasai.com.au/products/futures-mochi);
  - про Tokyo Mochi 3 один покупатель пишет, что быстро разряжается и требует частой зарядки, другой — что заряда хватило на ~3 дня — [Tokyo Mochi 3](https://dasai.com.au/products/tokyo-mochi-3);
  - Mochi 3 хвалят: на автовстречах выдерживает 2–3 ч активного использования на максимальной подсветке, у одного владельца не разрядился почти за 2 недели (езда ~20 мин в день). Минусы: ежедневная зарядка, блики, «included charger doesn't fit» — [judge.me](https://judge.me/reviews/stores/dasai.com.au/products/mochi-3).
- **Новизна и скука:**
  - обзор EMO через 90 дней: «hedonic treadmill kicking in by week 6» без новых сценариев и обновлений; робот не перемещается по комнате, поэтому разнообразие ситуаций ограничено. Источник — блог продавца роботов, возможна предвзятость — [Keyi Robot](https://keyirobot.com/blogs/buying-guide/emo-after-90-days-is-the-novelty-gone-or-does-it-still-feel-alive);
  - обзор Eilik: через 6 недель автор всё ещё «occasionally getting a reaction I haven't seen before» благодаря контекстным триггерам — [Govelomatrix](https://govelomatrix.com/eiliko-robot-review-2026/);
  - Eilik «skips utility entirely» и целиком построен на эмоциях и реакциях на касание — [Gagadget](https://gagadget.com/en/682539-ai-desk-robots/).
- **Подписки:** Vector, по словам продавца б/у устройства, «needs a $47 per annum account to work 100%» (единичное свидетельство) — [eBay](https://www.ebay.de/itm/235938855105). Eiliko подчёркивает AI-чат без подписки — [Plataforma Media](https://www.plataformamedia.com/en/2025/08/16/kickstarter-sensation-energize-labs-ai-charm-bot-eiliko-hits-3800-of-goal-and-380k-in-funding-with-support-from-over-3000-backers/).
- **Автономность AI-питомцев:**
  - Ropet: 2 ч при 4400 мА·ч, автономность названа «not particularly impressive» — [Forbes](https://www.forbes.com/sites/prakharkhanna/2025/01/07/ces-2025-ropet-can-be-your-emotional-ai-pet-companion-for-299/); 2.5–3.5 ч, разговорные функции на практике ограничены — [Robot Review Desk](https://robotreviewdesk.com/reviews/ropet-review/), [Keyi Robot](https://keyirobot.com/blogs/buying-guide/ropet-ultimate-review-of-ai-pet-robot-in-2025);
  - Eiliko: ~150 мин — [RobotShop](https://www.robotshop.com/products/energize-lab-eiliko-tiny-ai-charm-bot-w-unique-emotions-ai-conversation-blue).
- **Тихий режим:** Fuzozo хвалят за то, что в публичном месте звук можно выключить и общаться только через выражение глаз — [TechNode](https://technode.com/2026/10/08/what-if-your-ai-had-fur-i-spent-several-days-with-fuzozo/).
- **Tamagotchi Paradise:** у покупателей хорошая автономность за счёт быстрого standby (~1.5 мес на AAA); один хотел бы зарядку по USB — [desertcart](https://www.desertcart.ae/products/735696968-paradise-purple-sky).
- **Выгорание OLED:** в Cozmo встроены interlace со сменой строк и лимит 30 с на одну картинку — [Cozmo SDK](http://cozmosdk.anki.com/docs/generated/cozmo.oled_face.html).
- **Наблюдения из DIY:**
  - в Keychain_motion честно сказано, что реальное время работы ещё нужно измерить — [GitHub](https://github.com/Godcomplexx/Keychain_motion);
  - в shawrhit/Mochi активный ток ~120 мА, light sleep ~100 мкА — [GitHub](https://github.com/shawrhit/Mochi);
  - у InteractiveSmartKeychain сна нет вовсе — [GitHub](https://github.com/hazelrc/InteractiveSmartKeychain);
  - в ZnFr60 `setCpuFrequencyMhz()` фактически ничего не делает, поэтому самый экономичный режим пропущен — [GitHub](https://github.com/ZnFr60/ESP32C3-Desk-pet).

### Inferences
- **Батарея — главный конкурентный параметр.** Цель «несколько дней на 150–300 мА·ч» обходит все найденные коммерческие аналоги. Для этого нужны сон с пробуждением от IMU или кнопки и короткие «сессии» показа лица (паттерн Dasai: опрос гироскопа раз в 30 с и auto-off).
- **Против скуки** работают:
  - контекстные, редкие реакции (Eilik): время суток, подъём, тряска, падение, зарядка, долгое одиночество;
  - медленно меняющееся внутреннее состояние («bond» или mood 0–100, как у Keychain_motion и ZnFr60);
  - «пасхалки», которые выпадают редко.
  Взвешенно-случайный выбор (рулетка esp32-eyes) снижает предсказуемость.
- **Против выгорания:** держать среднюю яркость низкой, постоянно слегка сдвигать лицо (idle-взгляд RoboEyes это даёт), гасить экран в простое. Статичный «сон» можно рисовать в разных позициях.
- **Против «раздражает»:** реакции должны быть беззвучными или с отключаемым звуком (урок Fuzozo), а не будить хозяина в кармане. Тряска при ходьбе не должна постоянно вызывать «dizzy»: нужны пороги и гистерезис. Последнее — инференс, прямых жалоб пользователей на это не найдено.
- **Мелочи из отзывов Dasai:** штатный USB-C без экзотических кабелей, и блики стоит учесть при выборе защитного окна (матовое или антибликовое).

### Gaps
- Прямых обсуждений на Reddit найти не удалось: домен reddit.com недоступен инструменту поиска. Тезис «скучно через неделю» подтверждён только блогами продавцов и обзорами, а не массовыми отзывами.
- Реальных отзывов на Eiliko (батарея, надоедает ли) не найдено, кроме сводки Mia-Cat, которую не удалось открыть.
- Количественных данных о выгорании SSD1306 в DIY-брелоках (за сколько недель появляется след) не найдено. Это тема соседнего исследователя («pitfalls»).
