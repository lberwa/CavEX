import sys
import subprocess as sub
import re
import json
import os

# Versions-Code == Commit-Label; muss zum Client-Schema in screen_upgrader.py
# passen: major*10000 + minor*100 + patch (feste Feldbreite, minor/patch < 100).
version_code = None


def git(arg, platform):
    return sub.run(f"git --git-dir=.version-{platform} --work-tree=. {arg}",
                   shell=True, capture_output=True, text=True)


def read_define(lines, name):
    for line in lines:
        if line.startswith(f"#define {name}"):
            match = re.search(r"\d+", line)
            if match:
                return int(match.group())
    raise SystemError(f"'#define {name}' nicht gefunden")


def increment_version_patch(file_path):
    global version_code
    with open(file_path, "r", encoding="utf-8") as file:
        lines = file.readlines()

    major = read_define(lines, "VERSION_MAJOR")
    minor = read_define(lines, "VERSION_MINOR")

    line_found = False

    for i, line in enumerate(lines):
        if line.startswith("#define VERSION_PATCH"):

            match = re.search(r"\d+", line)

            if match:
                current_version = int(match.group())
                new_patch = current_version + 1

                lines[i] = f"#define VERSION_PATCH {new_patch}\n"
                line_found = True

                # vollstaendige Versionsnummer mit fester Feldbreite bauen
                version_code = major * 10000 + minor * 100 + new_patch

                print(
                    f"[Erfolg] Version von 0.{minor}.{current_version} auf "
                    f"0.{minor}.{new_patch} erhöht (Code {version_code})."
                )
                break

    # 3. Datei nur überschreiben, wenn die Zeile auch gefunden wurde
    if line_found:
        with open(file_path, "w", encoding="utf-8") as file:
            file.writelines(lines)
    else:
        print(f"[Fehler] '#define VERSION_PATCH' wurde in {file_path} nicht gefunden.")
        raise SystemError(FileNotFoundError, f"{file_path} not found")

increment_version_patch("source/version.h")

cpu = os.cpu_count()

result = sub.run(f"make wii -j{cpu} IS_PC_BUILD=0", shell=True)

if not result.returncode == 0:
    raise SystemError(RuntimeError, "make return Error:", result.stdout)

with open("data.json", "r", encoding="utf-8") as file:
    data = json.load(file)

wii = "wii"
pc  = "pc"

def gwii():
    git("reset", wii)
    for file in data["wii"]:
        git(f"add {file[0]}", wii)

    git(f'commit -m "{version_code}"', wii)

def gpc():
    git("reset", pc)
    for file in data["pc"]:
        git(f"add {file[0]}", pc)

    git(f'commit -m "{version_code}"', pc)

def gpush():
    git("push origin master", wii)
    git("push origin master", pc )


gwii()
gpc()
gpush()



