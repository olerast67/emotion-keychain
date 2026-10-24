# После сборки платы rev B: firmware.uf2 рядом с firmware.hex. Его можно перетащить
# на диск KEYCHAIN, который появляется в режиме загрузчика.
import os

Import("env")  # noqa: F821


# Журнал во флеше (src/keychain/storage.h) занимает 0xEA000–0xECFFF: прошивка должна
# кончаться раньше, иначе запись настроек стёрла бы её код.
STORE_START = 0xEA000


def check_end(hexf):
    end, base = 0, 0
    with open(hexf) as f:
        for line in f:
            n, addr, kind = int(line[1:3], 16), int(line[3:7], 16), int(line[7:9], 16)
            if kind == 4:
                base = int(line[9:13], 16) << 16
            elif kind == 2:
                base = int(line[9:13], 16) << 4
            elif kind == 0:
                end = max(end, base + addr + n)
    if end > STORE_START:
        raise SystemExit("прошивка кончается на 0x%X и залезает на журнал с 0x%X" % (end, STORE_START))
    print("Прошивка кончается на 0x%X, журнал с 0x%X" % (end, STORE_START))


def make_uf2(source, target, env):
    check_end(str(target[0]))
    fw = env.PioPlatform().get_package_dir("framework-arduinoadafruitnrf52")
    conv = os.path.join(fw, "tools", "uf2conv", "uf2conv.py")
    hexf = str(target[0])
    env.Execute('"$PYTHONEXE" "%s" "%s" -c -f 0xADA52840 -o "%s"' % (conv, hexf, hexf[:-4] + ".uf2"))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", make_uf2)  # noqa: F821
