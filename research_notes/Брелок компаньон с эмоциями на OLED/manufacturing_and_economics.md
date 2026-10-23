# Производство малыми партиями, DFM и юнит-экономика OLED-брелка «компаньон с эмоциями» (50 / 200 / 1000 / 5000 шт.)

*Методика. Собрано 23.10.2026. В этой сессии прямое скачивание страниц (WebFetch/curl) было недоступно: прокси блокировал jlcpcb.com, rbc.ru, habr.com и другие сайты. Поэтому все цифры взяты из выдачи и сниппетов поиска с указанием исходных URL. Большинство цен — с сайтов поставщиков и из блогов, часто без даты. Перед расчётом бюджета их надо перепроверить в живых калькуляторах (JLCPCB, Rezonit, Protolabs, ЮKassa, СДЭК). Курс для пересчёта: 83,6 ₽/$ (ЦБ на 17.10.2026) — [1000bankov/Финам](https://1000bankov.ru/kurs/news/id/128532), [ppt.ru](https://ppt.ru/currencies/usd/01-10-2026).*

---

## 1. Сборка печатных плат в Китае (JLCPCB / PCBWay / Seeed), модуль или «chip-down»

### Takeaway
Для 50–1000 шт. дешевле всего собирать плату в JLCPCB. Economic PCBA стоит $8 за запуск, $1,50 за трафарет и $0,0017 за точку SMT, плюс $3 за каждый уникальный «extended»-компонент. Для платы на ~150 точек сама сборка выходит примерно в $0,3–0,6 за штуку, так что дороже всего обходятся компоненты (модуль, OLED, АКБ). До объёмов ~10–50 тыс. шт. в год выгоднее брать предсертифицированный модуль (ESP32-C3-MINI-1 за ~$2–3 или nRF52-модуль за ~$4–6.5), а не разводить чип на своей плате.

### Cited Findings
**Тарифы JLCPCB**
- В составе цены сборки FAQ JLCPCB (сентябрь 2026) указывает: setup fee $8.00, Stencil $1.50, SMT Assembly $0.0017 per joint — [JLCPCB FAQ](https://jlcpcb.com/help/article/pcb-assembly-faqs). Страница SMT-сервиса и гайд 2026 года называют $8 setup и $0.0016 за точку — [JLCPCB SMT](https://jlcpcb.com/pcb-assembly), [schemalyzer 2025](https://www.schemalyzer.com/en/blog/manufacturing/jlcpcb/jlcpcb-assembly-guide).
- Standard assembly стоит $25 за сторону, двусторонняя сборка — $50. Economic — $8 — [JLCPCB pricing](https://jlcpcb.com/help/article/pcb-assembly-price).
- Extended-компоненты: $3 за каждый уникальный компонент (feeder loading). Preferred Extended Parts на Economic освобождены от этого сбора. JLCPCB периодически отменяет сбор для части популярных extended-деталей — [JLCPCB FAQ](https://jlcpcb.com/help/article/98-pcb-assembly-faqs), [typingmind/aklofas skill](https://www.typingmind.com/skills/aklofas-jlcpcb).
- Выводные компоненты (ручная пайка): $3.5 hand-soldering labor fee + $0.0173 за точку — [JLCPCB FAQ](https://jlcpcb.com/help/article/pcb-assembly-faqs). В блоге 2025 года указано $0.0157 за точку — [poly.nomial blog](https://blog.poly.nomial.co.uk/2025-07-05-tips-for-getting-pcbs-made-with-jlcpcb.html). На форуме описывают так: первый THT-компонент стоит ~$3.50, дальше ~$0.02 за точку — [pedalpcb forum](https://forum.pedalpcb.com/threads/getting-started-with-pcba-at-jlcpcb.26836/).
- Прочие сборы (страница 2025 года): Large-size fee $59.23 за заказ (для крупных плат, к брелоку не относится) — [JLCPCB pricing](https://jlcpcb.com/help/article/pcb-assembly-price).
- По странице JLCPCB, Economic PCBA рассчитана на 2–50 шт., Standard — на более крупные партии. Около 90% заказов собирают за 24 ч, большинство — за 48 ч — [JLCPCB prototype assembly](https://jlcpcb.com/solutions/pcb-prototype-assembly). Сторонние гайды для планирования дают 3–7 дней — [NextPCB 2026](https://www.nextpcb.com/blog/jlcpcb-vs-pcbway-vs-nextpcb-comparison-2026), [claudeskills/kicad-happy](https://claudeskills.info/skills/aklofas/kicad-happy/jlcpcb/).
- Сравнение 2026 года: setup Economic у JLCPCB от $8.18. По оценке автора, JLCPCB Economic «вне конкуренции» для простых односторонних плат на 2–50 шт., а PCBWay гибче на гибридных и сложных сборках — [lead-pcb 2026](https://lead-pcb.com/blog/jlcpcb-vs-pcbway).

**PCBWay и Seeed**
- PCBWay рекламирует прототипную сборку за $29 для 1–20 шт., трафарет $15 — [PCBWay PCBA](https://www.pcbway.com/pcb-assembly.html), [PCBWay](https://www.pcbway.com/). Промо-цена не включает трафарет, компоненты, SMT-работу и доставку — [lead-pcb 2026](https://lead-pcb.com/blog/jlcpcb-vs-pcbway). Сторонняя оценка базового сбора PCBWay: ~$50 за 1–5 плат, $75 за 6–20, $100 за 21–50, $150 за 50+ (не официальный прайс) — [anypcba](https://www.anypcba.com/decoding-pcbway-assembly-cost-a-comprehensive-guide.html). По словам пользователя форума, на закупку компонентов там наценка до 2× — [Contextual Electronics forum](https://forum.contextualelectronics.com/t/pcbway-assembly-service-my-experience/2541).
- Актуальных цен Seeed Fusion PCBA не нашлось. Последний обзор датирован 2019 годом — [squix blog 2019](https://blog.squix.org/2019/09/pcb-assembly-service-review.html).

**Цены на модули и ключевые компоненты**
- ESP32-C3-MINI-1-N4 на LCSC: от $2.0095 (опт), $3.0602 за 1 шт. Вариант -H4: $2.9911 за 1 шт., $2.168 от 1300 шт. — [LCSC N4](https://www.lcsc.com/product-detail/RF-Modules_Espressif-Systems-ESP32-C3-MINI-1-N4_C2838502.html), [LCSC H4](https://lcsc.com/product-detail/wifi-modules_espressif-systems-esp32-c3-mini-1-h4_C2935306.html). На DigiKey N4 стоит $3.26 за 1 шт. и $2.199 от 650 шт. — [DigiKey](https://www.digikey.com/en/products/detail/espressif-systems/ESP32-C3-MINI-1-N4/13877574).
- Ebyte E73 на nRF52832 (E73-2G4M04S1B): $4.42 при 2–99 шт., $4.10 от 1000 шт. — [Alibaba](https://www.alibaba.com/product-detail/Ebyte-E73-2G4M04S1B-NRF52832-Chip-BLE_60731135500.html). E73 на nRF52840: $5.43 у RobotShop, $6.54 в магазине Ebyte — [RobotShop](https://www.robotshop.com/products/ebyte-e73-2g4m08s1c-iot-solutions-modules-nrf52840-ble50-smd-24ghz-nordic-wireless-module), [Ebyte](https://ebyteiot.com/products/2-4ghz-ble-mesh-small-smd-e73-2g4m08s1c-nordic-nrf52840-module-small-size-ble-5-0). Holyiot YJ-17095 (nRF52832, 9.4×9.25 мм) в Индии — ₹549 без налога — [Hubtronics](https://www.hubtronics.in/holyiot).
- Голая панель 0.96" 128×64 SSD1306 с FPC: $2.49 (FPC под разъём) и $2.36 (FPC под пайку) у BuyDisplay — [BuyDisplay FPC](https://www.buydisplay.com/blue-128x64-0-96-inch-oled-display-top-contact-connector-fpc-ssd1306), [BuyDisplay](https://www.buydisplay.com/monochrome-0-96-128x64-oled-display-module-ssd1306-blue-and-yellow). 30-pin, шаг 0.7 мм, под пайку на eBay: $3.14, от 4 шт. по $2.89 — [eBay](https://www.ebay.com/itm/174525046632).
- LIS2DH12TR на LCSC: $0.36–0.56 от 1000 шт., ~$0.61–0.92 за 1 шт. — [LCSC C110926](https://www.lcsc.com/product-image/C110926.html) (из соседних заметок hardware_components.md).

**Модуль или chip-down**
- По расчёту Punchthrough, переход с модуля (~$6) на chip-down (~$2) с печатной антенной экономит ~$4 на устройстве. Рассматривать chip-down стоит от 10 000–25 000 шт. в год — [Punchthrough](https://punchthrough.com/ble-module-vs-chip-down/). Oxit и MarkReady оценивают точку безубыточности в 50–100 тыс. шт. — [Oxit](https://oxit.com/module-or-chip-down-design-a-cost-comparison-and-breakeven-analysis/), [MarkReady](https://markready.io/learn/fcc-certification-cost).
- Только на сертификации разница между предсертифицированным модулем ESP32/nRF52 и chip-down составляет $5,500–14,500 (MarkReady). FCC-тесты кастомного радиодизайна стоят $15–30 тыс., с предсертифицированным модулем — $3–5 тыс. Риск повторного теста у первой своей конструкции — 30–50% — [MarkReady](https://markready.io/learn/fcc-certification-cost), [Hubble](https://hubble.com/community/comparisons/why-most-first-hardware-products-should-use-a-module-instead-of-a-raw-chip/).
- Модульная сертификация не отменяет испытаний конечного изделия: плата-носитель, корпус и кабели влияют на излучение — [MarkReady modular](https://markready.io/learn/fcc-module-certification).

### Inferences
- Расчёт сборки в JLCPCB для платы ~150 SMT-точек с ~8–10 уникальными extended-деталями. Работа: 150 × $0.0017 ≈ $0.26 за плату. Фиксированные расходы на заказ ≈ $8 + $1.5 + 8–10 × $3 ≈ $34–40, плюс $3.5 за ручную пайку, если нужна. При 50 шт. это ~$1.0 на плату, при 200 — ~$0.45–0.5, при 1000 (Standard, $25 setup) — ~$0.30. Собственно сборка — копейки, цену платы на 80–90% определяют компоненты.
- Как удешевить: (1) брать Basic/Preferred-детали, потому что каждый уникальный extended стоит $3 за заказ; (2) собирать плату с одной стороны, иначе Standard обойдётся в $50 вместо $25; (3) панелизовать (например, 2×5 брелоков на панели) — это снижает цену голой платы и часто снимает минимальные размеры; (4) батарею подключать JST-разъёмом или паять вручную на своей финальной сборке, а не отдавать на THT в JLCPCB, — так проще и логистика, и безопасность (см. раздел 4).
- OLED на голом FPC. Если FPC с разъёмом (ZIF), его можно просто вставить на финальной сборке. Если FPC под пайку, его паяет вручную человек (EMS или сам продавец), потому что машинная SMT-сборка с таким шлейфом не работает. Готовый модуль 0.96" ($2.9–3.1) дороже голой панели ($2.4–2.5), толще и требует пайки гребёнки. Для брелока практичнее голая панель на ZIF-разъёме.
- При 50–5000 шт. правильный выбор — модуль ESP32-C3-MINI-1 ($2–3) или nRF52-модуль ($4–6.5). Chip-down начинает окупаться с ~10–50 тыс. шт. в год и при этом требует собственной RF-сертификации. Это за пределами сценариев 50–5000.

### Gaps
- Не удалось открыть живой калькулятор JLCPCB/PCBWay: цена голой платы (например, 25×35 мм, 2 слоя) при 200/1000 шт., стоимость и правила панелизации у JLCPCB в 2026 году не подтверждены.
- Актуальный прайс Seeed Fusion PCBA не найден.
- Цены nRF52832/nRF52840/nRF54L15 как отдельных чипов на LCSC не найдены (только ChipDip, см. раздел 2). Цены модулей Minew не найдены.
- Не найдено подтверждения, что JLCPCB в 2026 году паяет FPC/батарею как услугу «box build» для малых партий.

---

## 2. Производство и импорт в России: EMS, компоненты, оплата Китаю, таможня, логистика

### Takeaway
В 2026 году напрямую из России в JLCPCB не заплатить и туда не отправить: работают только посредники с наценкой. Российские EMS (Резонит и др.) берут заказы от 1 шт. со сроком от 3–5 рабочих дней. Но за счёт фиксированных расходов (подготовка 4–6 тыс. ₽, минимальный заказ ~25 тыс. ₽, компоненты по российским ценам) монтаж малой партии у них в несколько раз дороже за плату, чем в JLCPCB. Импорт облагается НДС 22% (с 01.01.2026). Платёжные агенты берут 2–8%. Из рисков 2026 года — новый технологический сбор на электронику (до 5 000 ₽ за единицу, ставки и дата старта неясны) и правила ввоза литиевых батарей (опасный груз класса 9).

### Cited Findings
**Российские EMS: цены и сроки**
- Резонит берёт монтаж от 1 шт. Срочный — от 5 рабочих дней, суперсрочный — от 3, стандартный — 17 рабочих дней (без учёта оплаты и доставки). Крупные серии — от 17 рабочих дней — [Резонит монтаж](https://www.rezonit.ru/assembly/), [суперсрочный](https://www.rezonit.ru/assembly/supersrochnyy-montazh/), [стандартный](https://www.rezonit.ru/assembly/standartnyy-montazh-prototipov-i-melkikh-seriy/), [прайс](https://www.rezonit.ru/price_assembly/).
- Трафарет в Резоните делают за 1 рабочий день, экспресс — за 6 часов. Подготовка трафарета под несколько плат на одном листе — 300 ₽ за каждое объединение — [Резонит трафареты](https://www.rezonit.ru/stencil/), [прайс трафаретов](https://www.rezonit.ru/price_stencil/).
- Пользователь Хабра пишет, что счёт Резонита бывает на 10–20% выше онлайн-калькулятора. Крупным заказом в компании считают ~1000 изделий средней сложности — [Хабр, комментарии](https://habr.com/ru/articles/925082/comments/), [Хабр Карьера/Резонит](https://career.habr.com/companies/rezonit).
- Типичные российские тарифы (прайсы часто без даты): монтаж «от 5 коп. за точку» на крупных сериях; 0,40 ₽ за точку при 50–100 тыс. точек (меньше — по запросу); DIP — от 2,0 ₽ за точку; подготовка производства с трафаретом — 4000 ₽ плюс 200 ₽ за настройку каждого вида компонента; запуск линии — 6000 ₽ при первом заказе и 2500 ₽ при повторном; минимальный заказ у части подрядчиков — 25 000 ₽ — [smtproline](https://smtproline.ru/price.html), [necompany](https://necompany.ru/kontraktnoe-proizvodstvo/tsena/), [smdsmd](https://smdsmd.ru/services/kontraktnoe_proizvodstvo/460/), [berelcom](https://berelcom.ru/ceny-na-montazh-pechatnyh-plat), [aksioma21](https://aksioma21.ru/).
- Позиции российских EMS (это реклама, не исследования): локальное производство выгоднее для прототипов и средних серий, Китай — для очень крупных тиражей и стабильной конструкции. Зарплаты в Китае, по их словам, сравнялись с российскими или выше — [Райт Электроникс](https://rightelectronics.ru/blog/proizvodstvo-v-kitae-vs-rossiya), [kedrsolutions](https://kedrsolutions.ru/blog/rossiya-ili-kitaj-gde-vygodnee-razmestit-proizvodstvo-ehlektroniki).
- Кейс на Хабре (2025): у клиента-производителя эпилятора в серии вздувалась и отклеивалась клавиатура, и производство корпуса перенесли из России в Китай — [Хабр 899308](https://habr.com/ru/articles/899308/).

**Компоненты в России**
- ChipDip: nRF52832-QFAA-R — 157 шт. на складе, 350 ₽ в розницу, 289 ₽ от 10 шт., 276 ₽ от 100 шт. Отладка ESP32-PICO-KIT-1 — 2 670 ₽ — [ChipDip nRF52](https://www.chipdip.ru/catalog/popular/nrf52), [ChipDip ESP32-PICO-KIT](https://www.chipdip.ru/product/esp32-pico-kit-1-otladochnaya-plata-na-baze-espressif-9000592984).
- Агрегатор efind.ru показывает разрозненные остатки nRF52/ESP32 у российских дистрибьюторов (например, nRF52832-CIAA-R около 200 ₽, другие позиции под заказ за 4–7 недель). Есть китайские продавцы с оплатой в рублях и минимальным заказом от 3000 ₽ — [efind nRF52](https://efind.ru/offer/nRF52), [efind ESP-32](https://efind.ru/offer/ESP-32).

**JLCPCB/LCSC из России**
- Блог на DRIVE2 (январь 2026): оплата из России и отправка в Россию и Беларусь у JLCPCB заблокированы. Обходят через Казахстан или форвардера со складом в Китае — [DRIVE2](https://www.drive2.ru/b/724663375929284753/). По данным pcbchina.ru, JLCPCB с 5 марта 2022 года не принимает оплату из России и не доставляет туда. Посредник берёт рубли и включает доставку — [pcbchina.ru](https://pcbchina.ru/help/jlcpcb/). JLCPOST: от 1100 ₽ за 5 плат 100×100 мм, 2 слоя (рекламная цена) — [jlcpost.ru](https://jlcpost.ru/). Это расходится с соседними заметками (hardware_components.md), где сказано, что «LCSC и JLCPCB доставляют в РФ». Скорее всего, прямой путь работает частично, через сторонние карты и адреса.
- Хабр: через посредника наземная доставка с LCSC/JLCPCB занимает 13–29 дней, авиа — 4–8 рабочих дней до Москвы по тарифу $45/кг — [Хабр 925082](https://habr.com/ru/articles/925082/).
- Гайд по оплате (2026, рекламный): предоплаченная виртуальная карта и телефон для SMS 3-D Secure — [ПиплБот](https://pyyplbot.com/kak-oplatit/jlcpcb/).

**Оплата китайским поставщикам**
- Комиссии платёжных агентов: от 0,9% у отдельных агентов до 5–8% от инвойса. Типичный диапазон — 1,5–5% (Хабр) или 2–5% (DTF), плюс наценка на курс конвертации. Сроки: 2–5 рабочих дней в юанях через CIPS, 3–7 дней через агента — [Хабр/Exnode](https://habr.com/ru/companies/Exnode/articles/1084168/), [vc.ru](https://vc.ru/invest/2997988-oplata-postavshchikam-v-kitae-kak-izbezhat-oshibok-i-zaderzhek), [DTF](https://dtf.ru/ask/5059116-platezhnyy-agent-dlya-oplaty-v-kitay-postavshchikam), [icustoms](https://www.icustoms.ru/blog/oplata_postavschiku_v_kitay_v_2026_godu:_shemyi,_platejnyie_agentyi_i_komissii), [vc.ru crypto](https://vc.ru/crypto/2980454-oplata-inostrannyh-schetov-effektivnye-sposoby-dlya-biznesa).
- Мелкие платежи в юанях агенты берут неохотно из-за фиксированной части комиссии — [Хабр/Exnode](https://habr.com/ru/companies/Exnode/articles/1084168/).

**Таможня и налоги на импорт (2026)**
- Базовая ставка НДС с 01.01.2026 — 22% (закон 425-ФЗ от 28.11.2025). При импорте применяется та же ставка, что и внутри страны. База — таможенная стоимость плюс пошлина и акциз. Пример: товар на 1 000 000 ₽ при пошлине 5% → импортный НДС 231 000 ₽ — [Главкнига](https://glavkniga.ru/situations/k500968), [ppt.ru](https://ppt.ru/amp/news/nds/nds-22-s-2026-goda-zakon-opublikovan), [1С ИТС](https://its.1c.ru/db/content/newscomm/src/497590.htm).
- Технологический сбор за ввоз и производство продукции с электронной компонентной базой: потолок 5 000 ₽ за единицу, конкретные ставки по категориям определит правительство. По одной оценке — 3–5% от стоимости для массовых сегментов (ненадёжно). Старт сначала назначили на 1 сентября 2026 года, в публикациях июня 2026 года — уже 1 декабря 2026 года — [ppt.ru](https://ppt.ru/news/idea/v-rossii-vvedut-tekhnologicheskii-sbor-dlya-podderzhki-elektroniki), [klerk](https://www.klerk.ru/blogs/avisto/689901/), [spmag](https://spmag.ru/articles/novyj-tehnologicheskij-sbor-s-1-sentyabrya-2026-goda-v-rossii), [bitrix24](https://www.bitrix24.ru/journal/tekhnologicheskiy-sbor/), [astral](https://astral.ru/aj/elem/novyy-zheleznyy-nalog-kak-tekhnologicheskiy-sbor-izmenit-rynok-elektroniki-v-rossii/).
- Личные посылки: порог €200 и 31 кг на посылку (с 01.04.2024). Сверх порога — 15% от превышения, но не менее €2 за кг. Снижение порога до €100 и ставки до 5% с 01.07.2026 обсуждали, но, по найденным источникам, отложили. Минпромторг: «пока оставляем €200», дальше поэтапное снижение — [РБК](https://www.rbc.ru/rbcfreenews/68c013969a7947f658270779), [bpd-express](https://bpd-express.de/news/customs-duty-payment-parcels-to-russia), [demica.es](https://demica.es/ru/customs.html), [prorosta (прогноз)](https://prorosta.com/posylki-s-2026-goda/).
- Старая статья на Хабре (дата не указана, НДС тогда 18%): пошлина на ввозимую электронику нулевая, платится только импортный НДС — [Хабр 212097](https://habr.com/ru/articles/212097/).

**Логистика: «белая» и «карго»**
- Карго — $1,3–4,5 за кг. Низкие тарифы — для партий от 100–500 кг. Срок 13–18 дней, но при усиленном контроле задержки бывают 30–90 дней. Скрытые расходы — 15–30% — [kompas-logistic](https://kompas-logistic.ru/blog/kak-vyibrat-dostavku-iz-kitaya-v-2026-godu), [agb-global](https://agb-global.com/skolko-stoit-1-kg-kargo-kitay-tsena-tarify-2026), [agb-global карго](https://agb-global.com/cargo-kitay-rossiya-2026).
- «Белая» доставка — $3–5 за кг с НДС и документами, по другому источнику $3–10 за кг плюс пошлины и НДС. Срок 25–45 дней. Авиа — 3–8 дней, но это самый дорогой вариант — [kompas-logistic](https://kompas-logistic.ru/blog/kak-vyibrat-dostavku-iz-kitaya-v-2026-godu), [agb-global](https://agb-global.com/dostavka-kitai-cena-tarify-2026).

### Inferences
- Сравним монтаж 200 плат по 150 точек (30 000 точек) в России и в Китае. В России: 0,4–1,0 ₽ за точку (12–30 тыс. ₽) плюс подготовка 4 000 ₽ плюс ~10 видов компонентов × 200 ₽ = 2 000 ₽, но не меньше минимального заказа 25 000 ₽. Итого ~25–36 тыс. ₽, то есть ~125–180 ₽ ($1,5–2,2) за плату, без компонентов. В JLCPCB то же самое стоит ~$0,45–0,5 за плату. Компоненты в России дороже: nRF52832 у ChipDip — 276–350 ₽ ($3,3–4,2) за голый чип, тогда как готовый модуль E73 в Китае стоит $4,1–4,4. Российский путь оправдан для быстрых итераций EVT (3–5 рабочих дней против 2–6 недель с доставкой из Китая) и для чистых документов. Серию 200+ выгоднее собирать в Китае и ввозить «по-белому».
- Продавцу, который продаёт с сайта как ИП или ООО, нужен «белый» импорт с ГТД: без него нет документов происхождения товара, нечем подтвердить расходы на УСН и подать на EAC (сертификацию разбирает другой исследователь). Карго подходит только для прототипов и оснастки, не для товарной партии.
- Технологический сбор (до 5 000 ₽ за единицу, распространяется и на ввоз, и на производство продукции с ЭКБ) — главный неизвестный параметр для 2027 года. Если для «прочих устройств» ставку определят в процентах (по оценкам 3–5%), для брелока себестоимостью ~1 500 ₽ это ~50–75 ₽. Если введут фиксированный сбор в сотни рублей, юнит-экономика заметно изменится. Нужно следить за постановлением правительства.

### Gaps
- Не найдены прайсы и минимальные заказы А-КОНТРАКТ, PCBtech, Платан, Промэлектроника (монтаж и компоненты).
- Не подтверждена ставка ввозной пошлины ЕТТ ЕАЭС на готовое устройство (код ТН ВЭД — 8543 70 или 9503 для игрушек) и на PCBA. Нужна консультация таможенного брокера.
- Нет официальных ставок технологического сбора по категориям и точной даты начала.
- Легальный статус параллельного импорта Nordic/Espressif и санкционные ограничения на эти чипы в 2026 году не проверены.
- Не проверены схема оплаты криптовалютой (USDT) поставщикам и её правовые риски в 2026 году.

---

## 3. Корпус: FDM / SLA / MJF / вакуумное литьё / литьё под давлением / ЧПУ, окно, ушко, крепёж, логотип

### Takeaway
Порядок по объёмам такой. 50–200 шт. — своя ферма FDM-принтеров Bambu Lab (~$0,3–1 материала и амортизации на корпус) или JLC3DP SLA/MJF (от $0,3–1 за деталь, на практике ~$2–3 за мелкую деталь с доставкой). Вакуумное литьё для крошечного корпуса не выгодно: силиконовая форма на 15–25 отливок стоит $200–1000. С ~1000 шт. окупается алюминиевая пресс-форма: в Китае $1–5 тыс., у Protolabs от ~$1,5 тыс., в России от 50–130 тыс. ₽. Деталь тогда стоит ~$1–3. Для 5000 шт. стоит брать сталь или многогнездовую форму. Для логотипа: тампопечать — $0,65–1,45 за штуку плюс клише $75–120, лазерная гравировка на своём оборудовании почти бесплатна в пересчёте на штуку.

### Cited Findings
**FDM (своя ферма Bambu Lab)**
- Калькулятор для A1: печать 40 г PLA ≈ $1,05–1,10 всего, из них ~$0,80 филамент (по $20/кг), ~$0,05 электричество (по $0,17/кВт·ч), ~$0,24 амортизация — [resincalc A1](https://resincalc.com/bambu-lab-a1-print-cost-calculator). Деталь 100 г PLA за 5 часов ≈ $2,50. PLA стоит $15–25/кг, ABS/ASA — $25–35/кг — [resincalc P1S](https://resincalc.com/bambu-lab-p1s-print-cost-calculator).
- P1S потребляет ~0,1 кВт·ч за час на PLA, в среднем ~105 Вт на PLA и ~150 Вт на ABS/ASA — [3dprintkosten](https://3dprintkosten.nl/en/bambu-lab-p1s-cost), [resincalc P1S](https://resincalc.com/bambu-lab-p1s-print-cost-calculator).
- Ферма из 10 P1S при загрузке 75% тратит электричества на ~£329 в месяц. Амортизацию считают из ресурса принтера ~3000 часов — [layermath](https://layermath.com/blog/bambu-lab-running-cost-2026).
- Достоверных данных о реальной производительности (деталей на принтер в день), доле брака и времени постобработки не нашлось — [layermath](https://layermath.com/blog/bambu-lab-running-cost-2026), [Bambu forum](https://forum.bambulab.com/t/how-do-you-price-a-given-print/36732).

**SLA/MJF/SLS-сервисы**
- JLC3DP: детали от $0,30 (SLA, MJF, SLM, FDM, SLS), MJF — от $1. Цена зависит от объёма материала, размера и отделки — [JLC3DP quote](https://jlc3dp.com/3d-printing-quote), [JLC3DP MJF](https://jlc3dp.com/blog/hp-mjf-printer-price), [JLC3DP events](https://jlc3dp.com/events/professional-3d-printing).
- Пример с Hackaday: мелкие ненагруженные SLA-детали из дешёвой бежевой смолы обошлись в $2,70 с доставкой — [hackaday.io](https://hackaday.io/page/395320-trying-out-jlcs-3d-printing-service).
- Сторонние ориентиры при 1 шт.: SLA — $15–45, MJF — $20–60. MJF становится самой дешёвой технологией на деталь от ~30 шт. — [fabdigit](https://www.fabdigit.com/guides/3d-printing-cost/), [jawstec 2026](https://www.jawstec.com/how-much-does-3d-printing-cost-2026-guide/).
- Москва (прайсы без даты): SLA от 50 ₽/г при минимальном заказе 5 000 ₽, у другого сервиса от 180 ₽/г или от 50 ₽/см³. SLS — от 80–85 ₽/г. PA12 (SLS) в СКАТ — 65 ₽/см³. MJF без опубликованной цены — [meritogroup SLA](https://meritogroup.ru/stoimost-3d-pechati-sla-fotopolimerom/), [NKTech3D](https://msk.nktech3d.ru/3d-pechat-po-tehnologii-sla), [СКАТ SLS](https://skat3d.ru/uslugi/3d-pechat/tekhnologii/sls), [lab3dprint](https://lab3dprint.ru/services/tseny/), [modelica3d](https://modelica3d.ru/blog/skolko-stoit-3d-pechat-raschyot-tseni-za/).

**Вакуумное (уретановое) литьё в силикон**
- Силиконовая форма стоит $200–1000, сложная — до $3000. Деталь — $10–100. Одна форма выдерживает ~15–25 отливок — [lkprototype](https://lkprototype.com/silicone-mold-cost/), [hlhrapid](https://hlhrapid.com/blog/urethane-casting-cost/), [rjcmold](https://rjcmold.com/urethane-casting-cost/), [jcadusa 2026](https://jcadusa.com/vacuum-casting-cost-2025/).
- При тираже 20 шт. выходит ~$20–150 за деталь. Может понадобиться вторая форма — [lkprototype price guide](https://lkprototype.com/vacuum-casting-price-guide/).

**Литьё под давлением, алюминиевые формы**
- Protolabs: алюминиевая форма от ~$1 495, отгрузка — за 7 дней и быстрее. Тариф «prototyping» рассчитан на объём до 2000 деталей с гарантией не менее 2000 впрысков. Тариф «on-demand» — для объёмов больше 2000, с бесплатной заменой изношенной формы. При переходе с одного на другой стоимость формы засчитывается. Полировка SPI-A2 удорожает форму и увеличивает срок — [Protolabs aluminum](https://www.protolabs.com/resources/blog/aluminum-mold-tooling-for-injection-molding/), [Protolabs pricing](https://www.protolabs.com/help-center/pricing-and-payment-options/), [Protolabs aluminum vs steel](https://www.protolabs.com/resources/blog/aluminum-vs-steel-tooling/).
- Небольшая одногнездовая алюминиевая форма — ~$1,5–5 тыс., большие стальные — $25–100 тыс. и выше — [Jaycon](https://www.jaycon.com/injection-moulding-price-a-2025-guide-for-engineers-procurement/).
- Китай, быстрая оснастка: $1 000–5 000 при ресурсе 500–5 000 впрысков. Алюминий — от $1 500 со сроком 2–3 недели, с возможностью перехода на сталь. В Китае сталь нередко дешевле, поэтому многие формовщики по умолчанию делают стальные формы даже для малых серий — [ABIS Mold](https://www.abismould.com/info/breaking-down-injection-mold-costs-103163410.html), [kmwcnc 2026](https://kmwcnc.com/aluminum-injection-molding-cost-2026-full-pricing-breakdown-real-examples-cost-saving-tips/), [Alibaba guide](https://electronics.alibaba.com/buyingguides/plastic-enclosures-china-how-to-choose-wisely).
- Цена детали при 1000 шт.: $1–3 с алюминиевой формы; $5–10 с учётом амортизации оснастки. Полукастомный корпус (вырезы и логотип) — $3,50–6,80 при оснастке $1 200–2 500 — [kmwcnc](https://kmwcnc.com/aluminum-injection-molding-cost-2026-full-pricing-breakdown-real-examples-cost-saving-tips/), [Alibaba guide](https://electronics.alibaba.com/buyingguides/plastic-enclosures-china-how-to-choose-wisely).
- Россия: алюминиевая пресс-форма — от 50 000 ₽ и от 2 недель, стальная — от 200 000 ₽ и от месяца. Форма для пластикового корпуса — от 130 000 ₽ при тиражах 1 000–100 000 шт. Для изделия до 200 мм — 110–600 тыс. ₽ — [spbplast](https://spbplast.ru/stoimost-press-formyi-dlya-litya), [tmth](https://tmth.ru/stoimost-press-formyi-dlya-litya), [pressforma-trayana](https://pressforma-trayana.ru/), [topstar-cmt 2026](https://topstar-cmt.ru/2026/07/24/melkoseriynoe-lityo-plastmass/).
- Полноценная стальная оснастка для корпуса: «от середины до верха пятизначных сумм»; одна команда оценивала её в $10–20 тыс. — [EEVblog](https://www.eevblog.com/forum/projects/enclosures-for-the-midnight-engineer/25/), [hackaday.io](https://hackaday.io/project/165038/log/167074). Типовая оснастка для ABS/PC — $3 000–10 000 — [Alibaba seller blog 2026](https://seller.alibaba.com/blogs/2026/southeast-asia/electronics/enclosure-material-selection-guide-alibaba-b2b).

**Соединение половин корпуса и логотип**
- Ультразвуковая сварка: сонотрод и оснастка для простой детали — «нижние тысячи долларов», стандартные сонотроды — ~$295–2 000. Сварочный аппарат обычно стоит у контрактного производителя. Защёлки — самый дешёвый вариант, без дополнительного оборудования, но их труднее загерметизировать. Винты в бобышки или вставки выбирают, когда корпус нужно вскрывать для ремонта. Сварной корпус вскрыть нельзя, а сам шов требует специальной геометрии (energy directors) — [projects-house](http://www.projects-house.com/articles/manufacturing-technologies/ultrasonic-welding-plastic-parts/), [Sonitek](https://www.sonitek.com/products/ultrasonic-welding-horns-sonotrodes-part-fixturing-nests), [nylonplastic](https://nylonplastic.com/plastic-welding-methods), [bjultrasonic](https://www.bjultrasonic.com/ultrasonic-welding-vs-traditional-joining-techniques-a-cost-benefit-analysis/).
- Тампопечать в один цвет: $1,45 за штуку при 50 шт. и $0,65 при 1000. Каждый дополнительный цвет — +$0,40–0,50. Клише — $75–120, повтор — $50. Есть вариант «$120 setup + $1,00–1,25 за оттиск», есть «setup $95 + $0,45 за оттиск до 500 шт.» (2024, ABS/PC). PP требует обработки пламенем или коронным разрядом (+$0,10–0,20 за штуку). Свой станок для тампопечати — $3 000–6 000 — [classicprinting](https://www.classicprintingandsigns.com/pages/pad-printing-pricing), [padprintservices](https://www.mobile.padprintservices.com/pricing_m.html), [afmeng](https://www.afmeng.com/Production/Pad_Printing_Production/Pad_Print_Pricing.aspx), [automarkco](https://automarkco.com/understanding-the-costs-of-pad-printing/).

### Inferences
- **50 шт.** — печать на своём FDM-принтере (PETG/ASA, матовая фактура) или SLA в JLC3DP. Корпус-брелок весит ~8–15 г, материал обходится в ~$0,2–0,4. Вместе с амортизацией и электричеством — ~$0,3–0,6 (пересчёт по калькулятору resincalc). Логотип — лазером или рельефом прямо в модели.
- **200 шт.** — 2–4 принтера Bambu. На одном столе помещается ~10–20 половинок, цикл печати 3–6 часов, то есть один принтер даёт ~40–80 половинок в сутки. 200 корпусов по 2 детали — это ~5–10 принтеро-суток. Цифры примерные: реальных данных о производительности не нашлось. Альтернатива — MJF в JLC3DP: качество выше, слоёв не видно, но плюс доставка и импорт.
- **1000 шт.** — алюминиевая форма в Китае ($1,5–5 тыс. за 2–3 детали: верх, низ и при желании прозрачное окно; при семейной форме дешевле) или в России (130–600 тыс. ₽ ≈ $1,6–7 тыс.). Амортизация — $1,5–5 на устройство. При 1000 шт. литьё дешевле FDM-фермы, только если учитывать труд на печать и постобработку. По чистой себестоимости материала FDM всё ещё выигрывает, так что литьё оправдывают стабильное качество, цвет и плотные защёлки.
- **5000 шт.** — стальная или многогнездовая форма с ресурсом 50–100 тыс. впрысков. Амортизация падает до ~$0,5–1 на штуку.
- **Окно над OLED.** Самое дешёвое решение — лазерная резка 1–1,5 мм PMMA или PC, вклейка на двусторонний скотч. Более технологичное — литьё прозрачной детали или окно двухкомпонентным литьём. Цен не нашлось, это оценка.
- **Ушко для брелока.** Его стоит заложить как сплошную петлю из основного материала толщиной ≥2–3 мм. На FDM ориентировать слои так, чтобы нагрузка шла вдоль нитей. Ещё лучше — металлическая вставка или разъёмное кольцо через сквозное отверстие в обеих половинах корпуса. Это вывод из общей практики, данных испытаний на отрыв не найдено.
- **Соединение половин.** Для 50–1000 шт. нужны винты M1.4–M2 или защёлки плюс винт. Корпус должен вскрываться для гарантийной замены АКБ. Ультразвуковая сварка имеет смысл только у литьевого производства при 5000+ шт.

### Gaps
- Нет данных о реальной производительности фермы Bambu (деталей в сутки), о доле брака и времени постобработки.
- Нет цен на лазерную резку окна из акрила или PC и на оптически прозрачный скотч малыми партиями.
- Нет данных о прочности ушка (испытания на отрыв, падение) для FDM, MJF и литьевого корпуса.
- Не найдены условия «JLCPCB injection molding» (стоимость формы и минимальный заказ). Прямой доступ к сайту был заблокирован, поиск их не показал.
- Нет цен на ЧПУ-корпус из алюминия для брелока. Есть только упоминание, что фрезеровка в JLC стоит разумно — [EEVblog](https://www.eevblog.com/forum/projects/enclosures-for-the-midnight-engineer/25/).

---

## 4. Аккумуляторы: закупка, сертификаты, MOQ, цены, перевозка

### Takeaway
LiPo 502030 на 250 мА·ч с платой защиты (PCM) на Alibaba и Made-in-China стоит ~$0,45–1,35 при минимальном заказе 1–1000 шт., в европейской рознице — €4–6. Документы UN38.3 и MSDS поставщики заявляют, но нужно запросить сам отчёт по конкретной модели. Литиевые батареи — опасный груз класса 9. Для авиаперевозки отдельных элементов (UN3480) действует ограничение заряда ≤30%. Батареи в составе устройства (UN3481) возить проще. Отсюда практичное решение: ставить АКБ на финальной сборке, а ввозить по-белому с полным пакетом документов.

### Cited Findings
- Alibaba, 502030 250 мА·ч: $0,75 при 2–4 999 шт., $0,60 при 5 000–9 999, $0,45 от 10 000. Разброс по листингам — $0,30–1,88 при минимальном заказе 1–100 шт. — [Alibaba](https://www.alibaba.com/product-detail/Factory-price-customized-502030-250mAh-3_1600440068780.html), [Alibaba showroom](https://www.alibaba.com/showroom/250mah-rechargeable-lipo-battery-cell-502030.html).
- Производители на Made-in-China: Shenzhen A&S Power — $1,15–1,25 FOB от 1 000 шт. Другой поставщик — $1,35 при 10–999 шт., $1,25 при 1 000–4 999, $1,15 при 5 000–9 999 — [A&S Power](https://m.made-in-china.com/company-szaspower/productlist/group/627572315), [made-in-china price](https://www.made-in-china.com/price/prodetail_Lithium-Battery_hXpEzZGbHwrx.html).
- Поставщики заявляют сертификаты CE/RoHS/UN38.3/MSDS, а также UL, KC, IEC62133 и FCC. Это заявления продавцов, сами отчёты в выдаче не показаны — [VATS battery](https://vatsbattery.en.made-in-china.com/product/hOfEPsbJYlVt/China-Factory-502030-3-7V-250-mAh-0-925wh-Lithium-Li-Polymer-Lipo-Batteries-UL-Kc-IEC62133-CE-RoHS-FCC-Un38-3-MSDS.html).
- Розница: Латвия — €5,90 за штуку, от 20 шт. — €4,05. eBay (продавец из Шэньчжэня) — 10 шт. за ~€7,33. Параметры типового элемента: 0,93 Вт·ч, 20×30×5 мм, 6 г, разъём JST PHR-2 — [ormix.lv](https://ormix.lv/en/catalog/item/id/26788), [eBay.de](https://www.ebay.de/itm/388626422115), [UNIT Electronics](https://uelectronics.com/producto/bateria-lipo-3-7v-250mah-502030/?add-to-cart=310133).
- Перевозка: литиевые батареи — класс 9 опасных грузов. Для UN3480 по авиа SoC должен быть не выше 30%. Нужны MSDS/SDS, UN 38.3 Test Summary и DGD (по одному источнику — для партий свыше 5 кг). Одни компании пишут, что оптовые АКБ самолётом не везут, а батареи в составе техники — можно; другие предлагают авиадоставку Li-ion — [declarant-rus](https://declarant-rus.ru/dostavka-litij-ionnyh-batarej-iz-kitaya/), [asia-import 2026](https://asia-import.ru/blog/2026/07/21/dostavka-litievyh-akkumulyatorov-iz-kitaya-klass-9/), [notifikat](https://www.notifikat.ru/delivery/aviadostavka-akkumulyatorov-iz-kitaya/), [akb-sales](https://akb-sales.ru/poleznaya-informatsiya/dostavka-akkumulyatorov-iz-kitaya-trebovaniya-k-perevozke-kommercheskih-partij.html).
- По данным одной логистической компании, с апреля 2026 года ввоз литиевых батарей в Россию ужесточили, штрафы — до 500 тыс. ₽. Официального подтверждения нет — [ГТК Восток](https://gtkvostok.ru/press-center/information/novye-pravila-dlya-litievykh-batarey-kak-ne-poteryat-tovar-i-500-tysyach-rublej/).
- Морем действуют правила IMDG и SP188, по дороге — ДОПОГ/ADR. Железная дорога — компромисс по цене и сроку — [asia-import](https://asia-import.ru/blog/2026/07/21/dostavka-litievyh-akkumulyatorov-iz-kitaya-klass-9/), [railtrans](https://railtrans.pro/news/kak-privezti-litievye-batarei-iz-kitaya).

### Inferences
- Бюджет на АКБ: ~$0,75–1,35 за штуку при 200–1000 шт. и ~$0,6–1,15 при 5000. Дешёвым листингам по $0,30–0,45 доверять нельзя: у дешёвых элементов реальная ёмкость часто ниже заявленной. Это общая практика, данных измерений не найдено. Входной контроль — выборочный разряд 5–10 элементов из партии и проверка срабатывания PCM.
- У поставщика нужно запросить: UN38.3 Test Summary именно на эту модель, MSDS/SDS, отчёт IEC 62133 (для маркетплейсов и сертификации), паспорт PCM (пороги OVP, UVP, OCP). Также стоит заказать нужные провода и разъём — JST-SH или пайку, — чтобы не переделывать их вручную.
- Чтобы не связываться с UN3480 при ввозе, АКБ можно покупать у того же EMS и ставить в устройство в Китае. Груз тогда идёт как UN3481 «в оборудовании». Но в этом случае нужна финальная сборка в Китае. Другой вариант — ввозить АКБ отдельной партией железной дорогой через специализированного перевозчика.

### Gaps
- Не найдены российские оптовые поставщики LiPo 502030 с ценами в рублях.
- Нет независимых данных о разбросе ёмкости у дешёвых китайских элементов.
- Нет официального подтверждения новых правил ввоза литиевых АКБ с апреля 2026 года.

---

## 5. Производственный процесс: прошивка, тестовые стенды, серийные номера, burn-in, трудозатраты, выход годных

### Takeaway
Для 50–1000 шт. достаточно простого стенда. Прошивка — через Tag-Connect TC2030 (кабель $59–77, ресурс 100 тыс. циклов) или самодельный стенд на pogo-pin (сотни долларов). Для ESP32-C3 есть официальный Flash Download Tool в Factory mode — до 8 устройств одновременно. Для nRF52 подходит nrfutil с выбором устройства по `--serial-number`. Готовые FCT-стенды стоят от ~$999 (FixturFab), «настоящий» bed-of-nails — $2–10 тыс. Опыт Flipper: стенд нужен на каждую плату, разрабатывать его не легче, чем само устройство, а дефекты всплывают только на объёме (пример — 1,4%).

### Cited Findings
- Tag-Connect TC2030-MCP — $59 без VAT, TC2030-PKT — £52 (или $72–76), PKT-ICESPI-NL — $77. Пружинные контакты рассчитаны на 100 000+ циклов. Кабели с «ножками» подходят и для производства — [Debug Store](https://thedebugstore.com/products/tag-connect-tc2030-mcp-cable-uk), [Debug Store PKT](https://thedebugstore.com/products/tag-connect-tc2030-pkt-programming-cable), [Tag-Connect](https://www.tag-connect.com/product/tc2030-pkt-6-pin-cable-with-legs-for-use-with-microchip-pickit-3).
- Стенды: FixturFab — dev fixtures от ~$999, Dev Pro от ~$1 799. Bed-of-nails для FCT — $2 000–10 000. Простой однослойный стенд с проводным монтажом — сотни долларов, clamshell или стенд с большим числом контактов — от $1 500. Изготовление — 3–7 рабочих дней. Bed-of-nails выгоднее flying probe начиная с ~500–1 000 шт. — [FixturFab](https://fixturfab.com/), [FixturFab guide](https://fixturfab.com/articles/functional-test-system-overview/), [PCBSync](https://pcbsync.com/nre-costs-pcb/), [JLCPCB blog](https://jlcpcb.com/blog/bed-of-nails-pcb-testing), [AtlasPCB](https://www.atlaspcb.com/blog/pcb-electrical-testing-flying-probe-vs-fixture/).
- Дешёвые китайские pogo-pin серии P50 — ₹8–25 за штуку. Самодельный стенд на 50 точек — ₹2000–8000 (блог низкой достоверности) — [Zbotic](https://zbotic.in/pcb-test-point-design-bed-of-nails-and-pogo-pin-setup/).
- ESP32-C3: официальный Flash Download Tool прошивает до 8 устройств одновременно в Factory mode (интерфейс заблокирован от случайных кликов), работает только под Windows и закрыт. Для автоматизации есть esptool. Espressif продаёт стенды на 1 и на 4 модуля. Уникальные заводские данные (серийник, ключи) создаются утилитой ESP-IDF mass_mfg как отдельный NVS-образ на каждое устройство — [ESP-Techpedia](https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/get-started/try-firmware/try-firmware-factory.html), [ESP-FAQ](https://docs.espressif.com/projects/esp-faq/en/latest/development-environment/firmware-update.html), [ESP-IDF mass_mfg](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/mass_mfg.html), [Espressif Flash Tool](https://documentation.espressif.com/projects/esp-test-tools/en/latest/esp32c6/production_stage/tools/flash_download_tool.html).
- Flash Encryption, Secure Boot и NVS Encryption Flash Download Tool завершает автоматически после прошивки — [Espressif blog](https://developer.espressif.com/blog/flash-encryption-and-secure-boot/).
- nRF52: nrfutil умеет list, program, recover и erase, работает с J-Link и MCUboot. Если подключено несколько устройств, нужное выбирают через `--serial-number`. В pynrfjprog одновременно открыт только один экземпляр HighLevel API. При таймауте J-Flash на nRF52840 после 32 КБ советуют снизить скорость SWD до 1000–2000 кГц — [Nordic nRF Util](https://www.nordicsemi.com/Products/Development-tools/nRF-Util), [Hubble nrfutil](https://hubble.com/community/guides/getting-started-with-nrfutil-the-command-line-tool-every-nordic-developer-needs/), [pynrfjprog](https://pypi.org/project/pynrfjprog/10.22.0), [Nordic DevZone](https://devzone.nordicsemi.com/f/nordic-q-a/115797/how-to-use-jflash-to-flash-an-nrf52840).
- Flipper Zero сделал отдельный стенд для каждой своей платы и пишет, что это «огромный объём работы, сравнимый с разработкой полноценных устройств», а тестировать нужно как можно раньше. Многие дефекты проявляются только на большом объёме. Пример: pogo-pin на плате iButton смещались в печи из-за лишней пасты, брак составил 1,4%. В производстве участвовали три фабрики — [Flipper blog](https://blog.flipper.net/electronics-testing/), [Hackaday](https://hackaday.com/2021/07/24/how-the-flipper-zero-hacker-multitool-gets-made-and-tested/).
- Стоимость труда в Москве: стажёр-монтажник РЭА без опыта в «Технополисе Москва» получает 90 000–123 000 ₽ в месяц на руки (вакансии hh.ru, 2026) — [hh.ru](https://hh.ru/vacancy/135989941), [hh.ru](https://hh.ru/vacancy/135919865).

### Inferences
- **Минимальный набор для 200 шт.** Посадочное место TC2030 (или 4–6 тестовых площадок) на плате. Простой стенд: 3D-печатное ложе, 6–10 pogo-pin, USB-адаптер (CMSIS-DAP или J-Link EDU для nRF; USB-JTAG прямо в ESP32-C3 через USB-C) и скрипт на Python. Бюджет — $100–400 своими силами или ~$1 000 у FixturFab.
- **Скрипт проверки (~30–60 с на устройство).** (1) Прошить заводскую тестовую прошивку и записать серийник в NVS или UICR. (2) Проверить шахматку и заливку OLED — смотрит оператор или камера. (3) Нажатие кнопки. (4) WHO_AM_I акселерометра и реакцию на наклон. (5) Микрофон по уровню шума или хлопку. (6) Ток заряда через USB-тестер (у BQ25180 или MCP73831 — по заданному резистору). (7) Ток сна через шунт или Nordic PPK2, критерий — например, <10–20 µA. (8) Прошить продуктовую прошивку и залить результат в CSV или Google Sheet по серийнику.
- **Burn-in.** 2–4 часа анимаций на столе плюс полный цикл заряда и разряда на выборке 10% (или на всех 50 первых). Так ловят «холодную» пайку FPC и бракованные АКБ.
- **Трудозатраты на финальную сборку** (оценка). Вставить FPC, приклеить OLED и окно, подключить АКБ, закрыть корпус, повесить карабин, упаковать — ~8–15 мин. Плюс тест ~2–3 мин. Годовой фонд при 90–123 тыс. ₽ на руки — ~140–180 тыс. ₽ в месяц на работодателя с НДФЛ и страховыми взносами. Это ~850–1 100 ₽ в час при 165 часах, то есть ~150–300 ₽ (~$2–3,5) на устройство. Если собирает сам автор или фрилансер-сдельщик, выходит дешевле.
- **Выход годных.** Для JLCPCB на простой плате закладывать 1–3% брака PCBA. Для ручной пайки FPC и АКБ — 2–5% переделок на первых партиях (по аналогии с 1,4% у Flipper на одном виде брака). Резерв комплектующих — +5–10% к партии.

### Gaps
- Нет публичных данных о доле брака у JLCPCB или PCBWay для малых партий.
- Нет надёжных данных о времени финальной сборки похожих брелоков (Dasai Mochi и др.).
- Нет официального руководства Nordic по параллельной заводской прошивке nRF52 на нескольких программаторах.

---

## 6. Упаковка и аксессуары, брендинг мерча

### Takeaway
Печатная коробка (самосборная или складная) при 200 шт. стоит ~$2–4, при 1000 — ~$0,9–1,8, при 5000 — ~$1,0 и меньше. Цифровая печать доступна от 100–250 шт., офсет окупается только с 500–1000 шт. Брендинг для мерча — логотип лазером, рельефом или тампопечатью на корпусе ($0,65–1,45 за штуку), брендированная коробка или стикер, карточка с QR-кодом инструкции. USB-C кабель в комплект можно не класть (это оценка).

### Cited Findings
- Минимальные тиражи: цифровая печать — от 250 шт. (по другому источнику, от 100–250 шт. на дизайн), флексо — от 500. Офсет редко окупается ниже 500–1000 шт. — [Brandt Box](https://brandtbox.com/blog/custom-packaging-cost-guide/), [packhit](https://packhit.com/packaging/custom/cost/).
- Складная коробка 4×4×8" с клапанами — $1,36 при 500 шт. и $0,89 при 1000. По другому источнику, $1,40–2,80 при 500 и $0,70–1,40 при 2000. Подготовка к тиражу 1000 шт. — $300–500. Некоторые поставщики не берут плату за штамп и клише — [qinprinting](https://www.qinprinting.com/custom-packaging-cost/), [PackPro 2026](https://packpropkg.com/how-much-does-custom-packaging-cost-2026-pricing/), [yourspackaging](https://yourspackaging.com/how-much-do-custom-boxes-cost-in-2026-a-complete-wholesale-pricing-guide/), [packagingvista](https://packagingvista.com/custom-boxes).
- Самосборная коробка 9×6×4" — $1,10–1,65 при 500 шт. и $0,85–1,40 при 1000. Коробка для электроники 8×6×3" — $2,80 при 500, $1,80 при 1000, $1,00 при 5000. Жёсткие подарочные коробки — $2,50–14,00 при 500 шт. — [Brandt Box](https://brandtbox.com/blog/custom-packaging-cost-guide/), [Cubit 2026](https://www.cubitpackaging.com/blog/how-much-does-custom-packaging-cost-2026).
- Тампопечать логотипа — $0,65–1,45 за штуку плюс клише $75–120 (см. раздел 3) — [classicprinting](https://www.classicprintingandsigns.com/pages/pad-printing-pricing).

### Inferences
- Коробки по ссылкам рассчитаны на крупные товары, а для брелока ~40×30×12 мм хватит маленькой коробки ~70×50×30 мм или блистера с картонной подложкой. При 200 шт. выгоднее крафтовая коробка из стока плюс брендированный стикер, вкладыш на цифровой печати и пакетик с застёжкой. Ориентир — ~50–120 ₽ за комплект, данных по российским типографиям не найдено. Полноцветная коробка под заказ — с 1000 шт.
- Карабин и кольцо стоят ~$0,1–0,4 за комплект на AliExpress или Alibaba (оценка, цены не найдены). Кабель USB-C брать не стоит: экономия ~$0,3–1 и меньше коробка (оценка).
- Мерчевая ценность: лимитированные цвета корпуса (на FDM переход на другой филамент ничего не стоит), номер экземпляра на задней крышке (лазер), подпись или маскот автора в анимации загрузки. Всё это без затрат на оснастку.

### Gaps
- Нет цен российских типографий (Москва, СПб) на малые коробки и вкладыши тиражом 200–1000 шт.
- Нет цен на карабины и кольца оптом.
- Нет данных о стоимости блистерной упаковки (термоформовки) малыми тиражами.

---

## 7. Юнит-экономика: пример себестоимости при 200 и 1000 шт., наценка, розничная цена в рублях и маржа

### Takeaway
Расчёт ниже — оценка автора на основе найденных цен. Полная переменная себестоимость с доставкой при 200 шт. (Китай PCBA + своя FDM-печать) — ~1 170–1 950 ₽ ($14–23). Сверх этого — разовые расходы NRE ~$1–2,5 тыс. (~420–1 050 ₽ на штуку при 200 шт.). При 1000 шт. с алюминиевой формой — ~1 150–2 050 ₽ с учётом амортизации формы. Аналоги на российском рынке: Dasai Mochi стоит 11,7–13,3 тыс. ₽, российский аналог Cutty — 5 200 ₽, Tamagotchi — $45–60. Реалистичная цена на своём сайте — 3 990–5 990 ₽. Это ~2,3–5× от переменной себестоимости, в пределах отраслевых правил (2,5–4× BOM / 3–5× COGS), с вкладом в маржу ~1 250–2 750 ₽ на штуку после NRE.

### Cited Findings
**Цены аналогов**
- Dasai (официальный сайт, AUD): Kawaii Mochi — $99, Futures Mochi — $95, Mini Mochi 1.5 — $40, Mega Mochi — $149,99. В USD: Futures — $60, Mochi 3 — $54,99. Несколько «официальных» доменов-клонов дают скидки ~70%, их подлинность не подтверждена — [dasai.com.au](https://dasai.com.au/collections/mochi-dasai), [dasai.com.au all](https://dasai.com.au/collections/all), [Futures Mochi](https://dasai.com.au/products/futures-mochi).
- Россия: Dasai Mochi gen3 в SpbTuning — 12 600 ₽. На shopozz — ~11 690–13 290 ₽, редкие издания до 14 890 ₽. Российский аналог Cutty на Ozon — 5 200 ₽, «в 5 раз дешевле», на момент проверки нет в наличии — [SpbTuning](https://spbtuning.ru/products/24504/), [shopozz](https://shopozz.ru/brand/dasai), [Ozon Cutty](https://www.ozon.ru/product/robot-v-avtomobil-cutty-chernyy-dasai-mochi-analog-1701339302/), [dasai-mochi.ru](https://dasai-mochi.ru/).
- Eilik (настольный): $139,99 (было $149), €159,90 в BerryBase, CHF 239 в MediaMarkt. Существует брелок Eiliko «Tiny AI Charm Bot», цена не найдена — [RobotShop Eiliko](https://ca.robotshop.com/products/energize-lab-eiliko-tiny-ai-charm-bot-w-unique-emotions-ai-conversation-pink), [BerryBase](https://www.berrybase.de/energize-lab/), [MediaMarkt](https://www.mediamarkt.ch/fr/product/_energizer-eilik-robot-educatif-bleu-155304692.html).
- Tamagotchi Paradise — ~$45 (Walmart $41,82). Tamagotchi Uni — $52–59,99 по распродажам. Япония: Paradise — 6 380 ¥, Uni — 8 250 ¥ — [Walmart](https://www.walmart.com/ip/15434768583), [Walmart Uni](https://www.walmart.com/ip/18375900440), [pricey.jp](https://pricey.jp/web/articles/3923).
- DIY-настольный питомец Deskimon (ESP32-S3 + 3D-печатный корпус) собирается за ~$30. Emo — $279–379 — [Hackster](https://www.hackster.io/news/the-30-cure-for-office-boredom-e24fc978d2bd), [keyirobot](https://keyirobot.com/blogs/buying-guide/emo-robot-price-is-this-ai-pet-worth-the-high-price-tag).

**Правила ценообразования**
- Розничная цена — 2,5–4× от BOM. Ниже ~2,5× продукт рискует быть убыточным — [Predictable Designs](https://predictabledesigns.com/how-to-price-your-new-electronic-hardware-product/), [Hackster](https://www.hackster.io/news/the-definitive-guide-to-pricing-your-new-electronic-hardware-product-7305c19d2102).
- 2–4× COGS (в идеале не менее 3×) или 3–5× COGS при массовом выпуске. К первой оценке COGS стоит добавить +20% запаса. Онлайн-ритейлеры берут 15–20%, физическая розница — 30–35% — [CADCrowd](https://www.cadcrowd.com/blog/how-to-price-a-new-consumer-electronic-product-for-profit-on-hardware-manufacturing-cost/), [Bolt «Hardware by the numbers»](https://blog.bolt.io/hardware-retail-exits/), [madebygenie](https://www.madebygenie.com/blog/how-to-price-consumer-product-cogs-retail-margin-wholesale).
- Kickstarter рекомендует закладывать маржу не менее 50% до вычета комиссий — [Kickstarter updates](https://updates.kickstarter.com/why-kickstarter-campaigns-fail-after-funding-and-how-to-avoid-it/).

**Комиссии каналов и налоги в России (2026)**
- Ozon, электроника: 5–15%. С 06.04.2026 логистика и хранение включены в процент комиссии. Возвраты по FBS с января 2026 платные — 50–150 ₽ за единицу плюс утилизация. С 01.07.2026 осталось три ценовых градации: до 100 ₽, 100–300 ₽ и свыше 300 ₽ — [lowmart](https://lowmart.ru/articles/ozon-logistics-in-2026-rates-fbo-fbs-and-realfbs/), [guruseller](https://guruseller.ru/ozon-uproshchaet-komissii-s-1-iyulya-2026/), [otveto](https://otveto.ru/blog/uchet-tovarov/komissiya-ozon-dlya-prodavtsov-stavki-fbo-fbs-kategorii/), [bidline](https://bidline.ru/blog/komissiya-ozon-2026).
- ЮKassa, интернет-эквайринг: от 2,4% за оплату картой, плата только за успешные платежи. Минимальная ставка — от 0,4% + НДС (без указания способа оплаты) — [1000bankov](https://1000bankov.ru/rko/tarify/yoomoney/yukassa---internet-ekvayring-dlya-ip-i-ooo/), [toolfox](https://toolfox.ru/services/s/yookassa).
- СДЭК: «Экспресс лайт» — ~600 ₽ за 500 г, встречаются цены 350–800 ₽ за 0,5 кг (без даты) — [Яндекс.Карты/СДЭК](https://yandex.com.tr/maps/org/cdek/40039284530/prices/), [Yango Maps](https://maps.yango.com/en-ae/org/cdek/78330162979/).
- УСН в 2026 году: освобождение от НДС, пока доход не превысит 20 млн ₽ (в 2027 году порог — 15 млн, с 2028-го — 10 млн). Выше — НДС 5% (до 272,5 млн ₽) или 7% (до 490,5 млн) без вычета входного НДС, либо 22% с вычетом — [Моё дело](https://www.moedelo.org/club/nalogovyj-uchet/nds-dlya-ip-v-2026-godu), [buh.ru](https://buh.ru/news/pri-kakikh-usloviyakh-organizatsiya-i-ip-na-usn-vprave-v-2026-godu-primenyat-ponizhennye-stavki-nds.html), [ФНС](https://www.nalog.gov.ru/html/sites/www.rn92.nalog.ru/seminar/NDS_28012026.pdf).

### Inferences
**Пример A: 200 шт., PCBA в JLCPCB по-белому, своя FDM-печать, продажа с сайта.** Цены за штуку, $ → ₽ по 83,6.
| Статья | $ | ₽ | Основание |
|---|---|---|---|
| Модуль ESP32-C3-MINI-1 (или nRF52 E73: +$1,5–2) | 2,2–3,0 | 185–250 | LCSC |
| OLED 0.96" голая панель FPC + ZIF | 2,4–2,6 | 200–220 | BuyDisplay |
| LIS2DH12 | 0,6–0,9 | 50–75 | LCSC |
| Зарядник, LDO, USB-C, кнопка, пассивы, микрофон (опционально) | 1,0–2,0 | 85–170 | оценка (данные — в заметках по компонентам) |
| Голая плата (панель) | 0,2–0,5 | 17–42 | оценка |
| Сборка JLCPCB (работа + фиксированные расходы) | 0,45–0,55 | 38–46 | расчёт по тарифам |
| АКБ 502030 с PCM | 0,75–1,35 | 63–113 | Alibaba / MIC |
| Корпус FDM (2 детали) + акриловое окно | 0,5–1,0 | 40–85 | resincalc + оценка |
| Карабин и кольцо | 0,2–0,4 | 17–33 | оценка |
| Упаковка (сток + стикер + вкладыш) | 0,6–2,0 | 50–170 | оценка; печатная под заказ — $2–4 |
| Доставка Китай→РФ, «белая» ($3–10/кг, ~60–100 г на шт.) | 0,3–1,0 | 25–85 | kompas / agb |
| Платёжный агент 2–8% от инвойса ~$9–11 | 0,2–0,9 | 17–75 | Хабр / DTF |
| Импортный НДС 22% (на УСН без НДС к вычету не принимается) | 2,0–2,5 | 170–210 | 425-ФЗ |
| Финальная сборка, прошивка, тест (~12–18 мин) | 2,0–3,5 | 170–300 | hh.ru + оценка |
| Брак и переделки (3–5%) | 0,5–0,8 | 40–70 | оценка |
| **Итого переменная себестоимость с доставкой** (сумма строк) | **~14–23** | **~1 170–1 950** | |

NRE на партию 200 шт. (без сертификации): 2–3 итерации прототипов EVT/DVT ($300–800), стенд и Tag-Connect ($150–1 000), 2–3 принтера Bambu, если их нет (~$600–1 500), образцы упаковки ($100–300). Итого ~$1 000–2 500, или ~420–1 050 ₽ на штуку при 200 шт.

Продажа с сайта по 4 990 ₽: эквайринг ~2,4–3,5% (120–175 ₽), резерв на возвраты и гарантию ~5% (250 ₽), УСН «доходы» 6% (~300 ₽; ставка общеизвестна, но в этом исследовании не проверена). Доставку платит покупатель, иначе ещё 350–800 ₽. Вклад в маржу до NRE — ~2 300–3 150 ₽ на штуку (46–63%). После NRE — ~1 250–2 750 ₽ на штуку, то есть ~0,25–0,55 млн ₽ на всю партию 200 шт. Выручка ~1 млн ₽ — далеко до порога НДС для УСН в 20 млн ₽.

**Пример B: 1000 шт., алюминиевая форма, JLCPCB Standard.** Электроника — ~$5,6–7,3 (470–610 ₽). АКБ — $0,6–1,25. Литьевые детали — ~$0,5–3 (40–250 ₽). Амортизация формы $1,5–5 тыс. на 1000 шт. — $1,5–5 (125–420 ₽). Печатная коробка — $0,9–1,8. Тампопечать — $0,65. Логистика, агент и НДС — ~200–300 ₽. Труд (~10 мин) — ~150 ₽. **Итого ~1 150–2 050 ₽ ($14–25)**: близко к примеру A, потому что амортизация формы съедает экономию. Выигрыш в однородном качестве и цвете, а не в цене. Стенд и NRE на штуку — ~100–250 ₽.
**5000 шт.**: амортизация оснастки падает до ~$0,3–1. Себестоимость — ~900–1 300 ₽ (оценка). Chip-down всё ещё не окупается: точка безубыточности — от 10–100 тыс. шт. в год.

**Розничная цена в России.**
- «Мерч-импульс» — 2 990–3 990 ₽ (на уровне Tamagotchi Paradise ~$45 ≈ 3 760 ₽). При переменной себестоимости ~1 170–1 950 ₽ это всего 1,5–3,4×, а с NRE на 200 шт. — опасно мало.
- **Рекомендуемая — 4 490–5 990 ₽** («коллекционный лимитированный мерч», ниже Dasai Mochi в 2–3 раза и на уровне Cutty 5 200 ₽). Это ~2,3–5× от переменной себестоимости, или ~1,5–3,7× с учётом NRE при 200 шт.
- Премиальная лимитка с номером и автографом — 6 990–7 990 ₽ для ядра аудитории.
- На Ozon добавляется комиссия 5–15% и платные возвраты: при 4 990 ₽ это ~250–750 ₽ на штуку. Для первой партии свой сайт выгоднее.

### Gaps
- Нет точной ставки ввозной пошлины на устройство (см. раздел 2). Не учтён технологический сбор.
- Не учтены сертификация (EAC/ТР ТС 020 и др.), маркировка и юридические расходы — это раздел другого исследователя.
- Нет российских цен на печатную упаковку, карабины и окно.
- Ставка УСН 6% и страховые взносы не проверены в этом поиске.

---

## 8. Кейсы: малые партии железа, типичные перерасходы и задержки

### Takeaway
Задержки — норма. По исследованию Моллика (471 tech/design-проект на Kickstarter), вовремя поставили только 24,9%, а средняя задержка составила 2,4 месяца. По другой выборке, опоздали 61% проектов. Главные причины — проблемы производства, логистика, масштабирование, изменения объёма работ, сертификация. Чаще всего недооценивают фулфилмент (доставка, таможня, упаковка) и тестовые стенды.

### Cited Findings
- Моллик (2013): из 471 проекта категорий «технологии» и «дизайн» вовремя поставили 24,9%, ещё 33% на момент замера не поставили вообще. У 126 опоздавших средняя задержка — 2,4 месяца. Прямой провал — всего 3,6%. Проекты, собравшие в 10× больше цели, вдвое реже поставляют в срок. Причины: проблемы производства, сложность доставки, масштаб, объём работ, неожиданная сертификация. Kickstarter оспаривал, как заголовки подали результаты — [BrainStation](https://brainstation.io/magazine/75-of-tech-projects-funded-on-kickstarter-dont-finish-on-time), [DSHR blog](https://blog.dshr.org/2017/10/crowdfunding.html), [TechCrunch](https://techcrunch.com/?p=609561).
- Hauge и Chimahusky: из 288 профинансированных проектов 61% отгрузили с опозданием — [DSHR blog](https://blog.dshr.org/2017/10/crowdfunding.html). Магистерская работа Chalmers «Hardware is Hard» — о причинах задержек — [Chalmers ODR](https://odr.chalmers.se/items/f2a5c510-eeef-4d36-aef0-85c5118e5aea).
- Фулфилмент недооценивают. Candylab Toys столкнулся со скрытыми расходами после кампании, FunForge поглотил ~$250 000 непредвиденных расходов — [Kickstarter updates](https://updates.kickstarter.com/why-kickstarter-campaigns-fail-after-funding-and-how-to-avoid-it/). Дизайнер отправил почти 12 000 единиц, и одна почта обошлась ему в $25 000 (~$2 за единицу) — [Fast Company](https://www.fastcompany.com/1672681/life-after-kickstarter-5-costly-lessons-from-a-kickstarter-backed-designer).
- Растущее число SKU, функций и stretch goals ломает фулфилмент — [tcf.team](https://www.tcf.team/blog/biggest-crowdfunding-failures).
- Flipper Zero: несколько раз переделывал электронику под компоненты с длинными сроками поставки. Пластиковый дефект GPIO-разъёма остановил тестовую линию больше чем на неделю. Карантин в Шэньчжэне — ещё неделя задержки. Корпус дорабатывали в отдельных итерациях — [Flipper blog](https://blog.flipper.net/manufacturing-and-shipping-update/), [Flipper cases](https://blog.flipper.net/case-manufacturing/), [Flipper bringing cases](https://blog.flipper.net/bringing-cases-to-perfection/).
- Российский кейс (2025): серийная клавиатура на корпусе эпилятора вздувалась, и производство перенесли из России в Китай — [Хабр 899308](https://habr.com/ru/articles/899308/).
- Аутсорс разработки консультантам привёл к переусложнённому и слишком дорогому в производстве продукту (воспоминание комментатора HN) — [HN](https://news.ycombinator.com/item?id=10114529).

### Inferences
- Для креатора главные риски — не сама плата, а (1) корпус и ушко, (2) АКБ и их логистика, (3) тест и прошивка 200 устройств руками, (4) доставка покупателям и возвраты. Вывод: продавать предзаказом с честным сроком «+6–8 недель» и запасом 50–100% к плану.
- Не добавлять варианты (цвета и функции) после старта предзаказа: каждый новый SKU дорожает на всех этапах.

### Gaps
- Не найдены опубликованные кейсы русскоязычных креаторов или мейкеров, выпустивших электронный мерч партией 200–1000 шт. (на vc.ru, Хабре, Ozon), с цифрами себестоимости.
- Не найдено пост-мортемов именно OLED-брелоков (Dasai Mochi и аналоги) о производстве.

---

## 9. График: от рабочего прототипа до первых 200 проданных штук (сжатые EVT/DVT/PVT)

### Takeaway
Для команды из 1–2 человек реалистично 14–24 недели от рабочего макета на devboard до отгрузки 200 шт. Отраслевые нормы: EVT — 4–8 недель, DVT — 6–12 (самый длинный этап), PVT — 2–6 недель. На каждую итерацию платы из Китая в Россию через посредника добавляется 2–6 недель на доставку. Если требуется сертификация EAC с испытаниями, она идёт параллельно с DVT и PVT.

### Cited Findings
- EVT — 4–6 недель и 10–50 шт. (по другим источникам, 4–8 или 6–10 недель на цикл с переделкой). DVT — 6–12 или 8–16 недель, обычно планируют ~8. PVT — 2–6 или 4–10 недель, по одному источнику ~3 недели и 10–20% от первого заказа. Мягкая оснастка — 6–9 недель, твёрдая — ~12 недель. На DVT обычно 1–2 ревизии формы (T1, T2). Сертификация часто идёт параллельно с PVT — [Instrumental whitepaper](https://instrumental.com/wp-content/uploads/2020/10/EVT-DVT-PVT-Whitepaper.pdf), [OPD](https://www.opd-design.com/evt-dvt-pvt-product-development-stages-guide/), [Titoma](https://titoma.com/blog/evt-dvt-pvt-testing/), [AJProTech](https://ajprotech.com/blog/guides-and-templates/evt-dvt-and-pvt-hardware-validation-stages-explained.html), [Beli](https://belitechnologies.com/blog/evt-dvt-pvt-hardware-build-stages).
- Сборка JLCPCB — 24–48 часов (заявлено), 3–7 дней (на практике). Доставка через посредника — 13–29 дней наземкой или 4–8 рабочих дней авиа по $45/кг. «Белая» партия — 25–45 дней — [JLCPCB](https://jlcpcb.com/solutions/pcb-prototype-assembly), [Хабр 925082](https://habr.com/ru/articles/925082/), [kompas-logistic](https://kompas-logistic.ru/blog/kak-vyibrat-dostavku-iz-kitaya-v-2026-godu).
- Резонит: монтаж в России от 3–5 рабочих дней в срочном режиме — [Резонит](https://www.rezonit.ru/assembly/).
- Алюминиевая форма: Protolabs — до 7 дней, Китай — 2–3 недели, Россия — от 2 недель — [Protolabs](https://www.protolabs.com/resources/blog/aluminum-mold-tooling-for-injection-molding/), [kmwcnc](https://kmwcnc.com/aluminum-injection-molding-cost-2026-full-pricing-breakdown-real-examples-cost-saving-tips/), [spbplast](https://spbplast.ru/stoimost-press-formyi-dlya-litya).

### Inferences
Сжатый план на 200 шт. (корпус FDM, PCBA в Китае, финальная сборка в России):
| Неделя | Этап | Что делается |
|---|---|---|
| 0–2 | Заморозка архитектуры | Схема, выбор модуля (ESP32-C3-MINI-1 / nRF52 E73), разводка 1-й ревизии с учётом DFM: односторонний монтаж, Basic-детали, TC2030, тестовые точки, ZIF под OLED, JST под АКБ |
| 2–7 | **EVT** (10–20 плат) | Заказ в JLCPCB Economic (или Резонит для скорости), доставка 2–4 недели, bring-up, замер тока сна, итерации корпуса на FDM (5–10 версий за 1–2 недели) |
| 7–12 | **DVT** (30–50 шт., ревизия B) | Финальная плата, корпус, окно, ушко, тесты: падение 1–1,5 м, рывок за кольцо, 50 циклов заряда, нагрев. Скрипт и стенд для теста. Образцы упаковки. Старт сертификации, если нужна. Предзаказ у аудитории — по прототипам |
| 12–18 | **PVT / пилот** (200 шт.) | Заказ PCBA и АКБ (1–2 недели), «белая» доставка (4–6 недель), параллельно печать 400 половинок корпуса (5–10 принтеро-суток) и упаковка |
| 18–21 | Сборка и тест | 200 × ~15 мин ≈ 50 человеко-часов плюс burn-in; отгрузка СДЭК |
| +4–8 | Резерв | По статистике Kickstarter: задержка в среднем 2,4 месяца, 75% проектов опаздывают |

- Для 1000 шт. с литьём добавить 4–8 недель на форму и T1/T2-образцы. Для 5000 шт. со стальной формой — ~12 недель.

### Gaps
- Нет данных о сроках сертификации EAC в 2026 году (это задача другого исследователя).
- Нет источника о сроках конкретно для 1–2-человечных команд. План выше — сжатая оценка по общим нормам.
