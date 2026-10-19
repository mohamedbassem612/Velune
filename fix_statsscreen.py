#!/usr/bin/env python3
# Fixes the build error in StatsScreen.kt (line ~545: Argument type mismatch Int?).
# Run from the project root (the folder that contains app/):
#   python3 fix_statsscreen.py
import os, sys, shutil

PATH = "app/src/main/kotlin/com/nikhil/yt/ui/screens/StatsScreen.kt"
OLD_VARIANTS = [
    "                                        album.songCountListened ?: 0,\n                                        album.songCountListened\n",
    "                                        album.songCountListened!!,\n                                        album.songCountListened\n",
]
NEW = "                                        album.songCountListened ?: 0,\n                                        album.songCountListened ?: 0\n"

if not os.path.exists(PATH):
    print("ERROR: file not found. Run this from the project root (the folder that has app/ inside it).")
    sys.exit(1)

raw = open(PATH, encoding="utf-8", newline="").read()
crlf = "\r\n" in raw
s = raw.replace("\r\n", "\n")

OLD = next((o for o in OLD_VARIANTS if s.count(o) == 1), None)
if OLD is None and s.count(NEW) == 1:
    print("Already fixed. Nothing to do.")
    sys.exit(0)

if OLD is None:
    print("ERROR: the expected code was not found. The file may differ from the one I expected. Nothing was changed.")
    sys.exit(1)

shutil.copyfile(PATH, PATH + ".bak-fix")
s = s.replace(OLD, NEW)
open(PATH, "w", encoding="utf-8", newline="").write(s.replace("\n", "\r\n") if crlf else s)
print("Fixed StatsScreen.kt. A backup was saved as StatsScreen.kt.bak-fix")
print("Now run Build > Make Project in Android Studio.")
