# drawgui

drawgui is a lightweight self-drawn GUI kernel written in C++20, architected
after Flutter's layered rendering model, exporting a pure C ABI so any
language can embed it.

## Unique value proposition

Relative to Qt QML or Flutter, the value of drawgui is **"a self-drawn GUI
kernel that any language can embed through a C ABI"** - not "another complete
desktop application framework". A host language needs no Dart VM, no QML
engine, and no embedded Chromium - only the ability to call C functions.

## Accepted trade-off

This project does not compete with Qt or Flutter on feature completeness. It
wins on embedding cost and language neutrality, and will lag long-term on
accessibility, complex text editing, and depth of native integration.

## Current status

Design complete; P0-P2 in progress. Everything before P0 was design-only.

## Platform scope

The current phase targets Linux and Windows desktop. macOS, Android and iOS
are deferred, with the platform abstraction shaped so they remain addable.

## Build prerequisites

- CMake >= 3.24
- Ninja
- clang or gcc with C++20 support

## Documentation

The full design document lives at `doc/design.md` (written in Chinese).
