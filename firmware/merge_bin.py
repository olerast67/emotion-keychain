# После сборки склеивает загрузчик, таблицу разделов и прошивку в один файл
# .pio/build/<env>/stand-<env>.bin. Его можно прошить с адреса 0x0 из браузера
# (https://espressif.github.io/esptool-js/) без установки PlatformIO.
Import("env")  # noqa: F821


def merge_bin(source, target, env):
    out = env.subst("$BUILD_DIR/stand-${PIOENV}.bin")
    parts = [(off, env.subst(img)) for off, img in env.get("FLASH_EXTRA_IMAGES", [])]
    parts.append((env.subst("$ESP32_APP_OFFSET"), str(target[0])))
    args = " ".join('%s "%s"' % (off, img) for off, img in parts)
    env.Execute(
        '"$PYTHONEXE" "$OBJCOPY" --chip ${BOARD_MCU} merge_bin --flash_size 4MB -o "%s" %s' % (out, args)
    )


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin)  # noqa: F821
