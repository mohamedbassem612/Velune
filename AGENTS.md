# Instructions for Codex Local in Velune

This file is a short, durable handoff. Follow the user's current request first. Do not assume a previous chat is available or that an old report is the current state.

Reply to the user in clear Arabic unless they ask for another language. Keep code names, paths, and commands exact. You are the Codex Local agent using the local Qwen3 8B model; do not claim to be an OpenAI-hosted model or guess your model identity.

## Start each task

1. Confirm the working directory and run `git status --short --branch`. This repository has substantial uncommitted work; preserve it. Never reset, clean, overwrite, or revert changes you did not make.
2. Read only the documentation and source relevant to the requested task. Use `rg` to find code. Do not load the entire repository or a long chat into the model context.
3. State a short plan for a multi-step task, then implement it. If the request names a feature or bug ambiguously, inspect the code and make a reasonable interpretation before asking.
4. Keep changes focused. Do not change Android, TV, and Desktop together unless the task or a shared interface requires it.

## Project map

- `README.md`: Android app overview, build instructions, and project structure.
- `app/`: Android mobile and TV application; `innertube/` and other top-level modules are supporting libraries. Read `docs/TV_DEVELOPMENT.md` for TV work.
- `desktop/`: Qt Quick/QML interface with C++ services and a separate native libmpv audio process. Read `desktop/README.md` before Desktop work. `desktop/design/` contains visual references, not the active UI.
- `desktop/reports/qml-migration-0.3.0.md`: what the 2026-11-24 Desktop migration build verified and what still needs functional validation.
- `desktop/reports/settings-parity-2026-11-24.md`: implemented Desktop settings and remaining work; treat priorities there as historical notes, not a task to do everything.
- `/home/mohamed/Desktop/Velune-TV` is a separate checkout. Do not edit it unless the user explicitly directs you there.

## Work safely

- Treat the existing uncommitted files as the user's work. Inspect the relevant diff before editing. Avoid broad formatting or generated-file churn.
- Preserve user data, downloaded music, account sessions, signing material, and installed application state. Never print or commit secrets.
- Do not run destructive Git commands, publish releases, push commits, or replace installed builds unless the user asks for that outcome.
- For playback, downloads, offline behavior, and Connect, preserve existing state and failure handling; test interrupted and retry paths when relevant.
- Say clearly what was verified and what still needs a real device, account, network, or UI check. A successful build is not proof of functional behavior.

## Validation

- Android: JDK 21 and Android SDK 36. Use a focused Gradle task for the changed area. Release build examples are `./gradlew :app:assembleMobileUniversalRelease` and `./gradlew :app:assembleTvUniversalRelease`; these can take time, so run them when relevant.
- Desktop Linux: start with `desktop/README.md`. Build with `bash desktop/scripts/build-linux.sh` from the repository root. For focused native tests, follow the CMake/CTest commands documented there. Do not assume the installed app was updated by a source build.
- After edits, inspect `git diff --check`, the relevant diff, and `git status --short`. Report files changed, checks run, their results, and remaining limitations.

## Keep context small across chats

Use a new chat for each coherent task. At the end of a substantial task, give the user a concise handoff: current behavior, exact files changed, tests and results, and the next concrete step. Put durable facts in project documentation only when they are verified; do not copy a full chat transcript into this file.
