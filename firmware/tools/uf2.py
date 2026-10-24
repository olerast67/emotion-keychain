# После сборки платы rev B: firmware.uf2 рядом с firmware.hex. Его можно перетащить
# на диск KEYCHAIN, который появляется в режиме загрузчика.
import os

Import("env")  # noqa: F821


def make_uf2(source, target, env):
    fw = env.PioPlatform().get_package_dir("framework-arduinoadafruitnrf52")
    conv = os.path.join(fw, "tools", "uf2conv", "uf2conv.py")
    hexf = str(target[0])
    env.Execute('"$PYTHONEXE" "%s" "%s" -c -f 0xADA52840 -o "%s"' % (conv, hexf, hexf[:-4] + ".uf2"))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.hex", make_uf2)  # noqa: F821
