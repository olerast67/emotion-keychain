# До сборки платы rev B: номера, под которыми брелок виден компьютеру и другим брелокам, — из
# раздела [sale] в platformio.ini (одно место для своих номеров перед продажей, docs/selling.md).
#   usb_vid, usb_pid   — USB VID/PID прошивки. Ядро Adafruit берёт их из первой пары hwids в
#                        описании платы (boards/keychain_revb.json), поэтому здесь hwids
#                        подменяются до сборки ядра;
#   usb_pid_boot       — PID загрузчика: вторая пара hwids, по ней PlatformIO находит брелок в
#                        загрузчике. Сам загрузчик свои номера берёт из своего board.h — его
#                        пересобирают отдельно (bootloader/README.md);
#   ble_company        — Company ID в пакете встреч: FACE_ADV_COMPANY для движка
#                        (engine/include/face/encounter.h), и для src, и для библиотеки движка.
# Раздела нет — номера из описания платы и 0xFFFF, как раньше.
Import("env")  # noqa: F821

config = env.GetProjectConfig()  # noqa: F821
board = env.BoardConfig()  # noqa: F821


def number(name, default):
    raw = config.get("sale", name, default) if config.has_section("sale") else default
    try:
        n = int(str(raw).strip(), 0)
    except ValueError:
        n = -1
    if not 0 <= n <= 0xFFFF:
        raise SystemExit("platformio.ini, [sale] %s = %s: нужно 16-битное число, например 0x1234" % (name, raw))
    return "0x%04X" % n


hwids = board.get("build.hwids")
vid = number("usb_vid", hwids[0][0])
pid = number("usb_pid", hwids[0][1])
pid_boot = number("usb_pid_boot", hwids[1][1] if len(hwids) > 1 else hwids[0][1])
company = number("ble_company", "0xFFFF")
board.update("build.hwids", [[vid, pid], [vid, pid_boot]])
env.Append(CPPDEFINES=[("FACE_ADV_COMPANY", company)])  # noqa: F821
print("Номера: USB %s:%s (загрузчик %s:%s), Company ID %s" % (vid, pid, vid, pid_boot, company))
