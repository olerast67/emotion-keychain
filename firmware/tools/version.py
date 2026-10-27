# До сборки платы rev B: версия прошивки в FW_VERSION — `git describe` (метка, если есть,
# иначе короткий хэш коммита; «-dirty» — в рабочей копии есть незакоммиченные правки). Её
# печатают приветствие консоли и команда ver: так видно, какая сборка залита в брелок. Без git
# (архив исходников) — «unknown».
import subprocess

Import("env")  # noqa: F821


def describe(project_dir):
    try:
        out = subprocess.run(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=project_dir,
            capture_output=True,
            text=True,
            timeout=10,
        )
    except (OSError, subprocess.SubprocessError):
        return "unknown"
    ver = out.stdout.strip()
    # В макрос — только простые знаки: метка с пробелом или кавычкой не сломает сборку.
    ver = "".join(c for c in ver if c.isalnum() or c in "._-+")
    return ver if out.returncode == 0 and ver else "unknown"


version = describe(env.subst("$PROJECT_DIR"))  # noqa: F821
print("Версия прошивки: %s" % version)
env.Append(CPPDEFINES=[("FW_VERSION", '\\"%s\\"' % version)])  # noqa: F821
