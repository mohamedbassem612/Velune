#!/usr/bin/env python3
# Fixes the queue selection bug: a long press selects a song but does not add it
# to the list used by the bottom menu, so the menu actions do nothing.
# Run from the project root (the folder that contains app/):
#   python3 fix_queue_selection.py
import os, sys, shutil

PATH = "app/src/main/kotlin/com/nikhil/yt/ui/player/Queue.kt"
OLD = """                                                selectedSongs.clear() // Clear all selections
                                                selectedSongs.add(windowMetadata) // Select current item
"""
NEW = """                                                selectedSongs.clear() // Clear all selections
                                                selectedItems.clear()
                                                selectedSongs.add(windowMetadata) // Select current item
                                                selectedItems.add(currentItem)
"""

if not os.path.exists(PATH):
    print("ERROR: file not found. Run this from the project root (the folder that has app/ inside it).")
    sys.exit(1)

raw = open(PATH, encoding="utf-8", newline="").read()
crlf = "\r\n" in raw
s = raw.replace("\r\n", "\n")

if s.count(NEW) == 1:
    print("Already fixed. Nothing to do.")
    sys.exit(0)

if s.count(OLD) != 1:
    print("ERROR: the expected code was not found exactly once (found %d). Nothing was changed." % s.count(OLD))
    sys.exit(1)

shutil.copyfile(PATH, PATH + ".bak-selection")
s = s.replace(OLD, NEW)
open(PATH, "w", encoding="utf-8", newline="").write(s.replace("\n", "\r\n") if crlf else s)
print("Fixed Queue.kt selection. Backup saved as Queue.kt.bak-selection")
print("Now run Build > Make Project in Android Studio.")
