# После сборки платы rev B:
#   firmware.uf2         — перетащить на диск KEYCHAIN в режиме загрузчика;
#   firmware-factory.hex — всё сразу для программатора или заводской прошивки: загрузчик с
#                          SoftDevice, приложение и отметка «приложение рабочее». Залил —
#                          и брелок сразу запускается, дальше обновления по USB.
import os

Import("env")  # noqa: F821


# Журнал во флеше (src/keychain/storage.h) занимает 0xEA000–0xECFFF: прошивка должна
# кончаться раньше, иначе запись настроек стёрла бы её код.
STORE_START = 0xEA000
APP_START = 0x26000  # после MBR и SoftDevice S140 6.1.1
BOOTLOADER_SETTINGS = 0xFF000
BOOTLOADER_HEX = os.path.join("bootloader", "emotion_keychain_revb_bootloader-0.11.0-keychain_s140_6.1.1.hex")


def read_hex(path):
    """Intel HEX → {адрес: байт}."""
    data, base = {}, 0
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line.startswith(":"):
                continue
            raw = bytes.fromhex(line[1:])
            if (sum(raw) & 0xFF) != 0:
                raise SystemExit("%s: битая контрольная сумма строки %s" % (path, line))
            n, addr, kind = raw[0], raw[1] << 8 | raw[2], raw[3]
            payload = raw[4:4 + n]
            if kind == 0:
                for i, b in enumerate(payload):
                    data[base + addr + i] = b
            elif kind == 2:
                base = (payload[0] << 8 | payload[1]) << 4
            elif kind == 4:
                base = (payload[0] << 8 | payload[1]) << 16
            elif kind == 1:
                break
    return data


def write_hex(path, data):
    out, upper = [], -1
    addrs = sorted(data)
    i = 0
    while i < len(addrs):
        start = addrs[i]
        if start >> 16 != upper:
            upper = start >> 16
            rec = bytes([2, 0, 0, 4, upper >> 8, upper & 0xFF])
            out.append(":" + (rec + bytes([(-sum(rec)) & 0xFF])).hex().upper())
        chunk = [data[start]]
        while (len(chunk) < 16 and i + len(chunk) < len(addrs) and addrs[i + len(chunk)] == start + len(chunk)
               and (start + len(chunk)) >> 16 == upper):
            chunk.append(data[start + len(chunk)])
        rec = bytes([len(chunk), (start >> 8) & 0xFF, start & 0xFF, 0]) + bytes(chunk)
        out.append(":" + (rec + bytes([(-sum(rec)) & 0xFF])).hex().upper())
        i += len(chunk)
    out.append(":00000001FF")
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")


def check_end(app):
    end = max(app) + 1
    if min(app) < APP_START:
        raise SystemExit("прошивка начинается на 0x%X, раньше SoftDevice (0x%X)" % (min(app), APP_START))
    if end > STORE_START:
        raise SystemExit("прошивка кончается на 0x%X и залезает на журнал с 0x%X" % (end, STORE_START))
    print("Прошивка кончается на 0x%X, журнал с 0x%X" % (end, STORE_START))


def make_factory(app, out, project_dir):
    bl_path = os.path.join(project_dir, BOOTLOADER_HEX)
    if not os.path.exists(bl_path):
        print("Нет %s — firmware-factory.hex не собран" % bl_path)
        return
    merged = read_hex(bl_path)
    overlap = [a for a in app if a in merged]
    if overlap:
        raise SystemExit("приложение и загрузчик пересекаются с 0x%X" % min(overlap))
    merged.update(app)
    # Настройки загрузчика: bank_0 = 1 (приложение рабочее), crc = 0 (не проверять) — как цель
    # flash-skip-crc в Makefile загрузчика. Наш загрузчик и без этого принимает приложение,
    # залитое программатором, но так надёжнее.
    for i, b in enumerate((1, 0, 0, 0)):
        merged[BOOTLOADER_SETTINGS + i] = b
    write_hex(out, merged)
    print("Заводская прошивка: %s (загрузчик + SoftDevice + приложение)" % out)


def after_hex(source, target, env):
    hexf = str(target[0])
    app = read_hex(hexf)
    check_end(app)
    fw = env.PioPlatform().get_package_dir("framework-arduinoadafruitnrf52")
    conv = os.path.join(fw, "tools", "uf2conv", "uf2conv.py")
    env.Execute('"$PYTHONEXE" "%s" "%s" -c -f 0xADA52840 -o "%s"' % (conv, hexf, hexf[:-4] + ".uf2"))
    make_factory(app, hexf[:-4] + "-factory.hex", env.subst("$PROJECT_DIR"))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", after_hex)  # noqa: F821
