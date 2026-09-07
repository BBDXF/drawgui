// File watching - the capability constraint C1 was written about.
//
// design.md section 2 records that inotify plus POSIX signals are what locked
// this project's predecessor to Linux permanently. Those are not exotic
// dependencies; they are what anyone reaches for when hot reload has to work
// today. C1's answer is that the platform seam exists before the first
// implementation, so the interface is shaped by what a watcher must promise
// rather than by what inotify happens to report. FSEvents coalesces, and
// ReadDirectoryChangesW reports per-directory - neither can reproduce
// inotify's exact event stream, so this interface does not ask them to.
//
// Two consumers, both already named in the design: JSX hot reload, and theme
// directory reload (section 5.7.6), which reuses this rather than growing its
// own watcher.

#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "drawgui/platform/types.h"

namespace dg {

namespace detail {
struct WatchIdTag;
}  // namespace detail

// Identifies one registration, so it can be cancelled.
using WatchId = Handle<detail::WatchIdTag>;

enum class FileChangeKind : std::uint8_t {
  kCreated,

  // Contents or metadata changed. Backends differ in how many of these a
  // single save produces - an editor that writes through a temporary file and
  // renames may produce a create, and a coalescing backend may produce one
  // event for several writes. A consumer must therefore be idempotent and
  // re-read the file rather than trust a change count.
  kModified,

  kRemoved,
};

struct FileChange {
  FileChangeKind kind = FileChangeKind::kModified;

  // The watched path, or a path beneath it when a directory was watched.
  std::string path;

  friend bool operator==(const FileChange&, const FileChange&) = default;
};

// Called on the thread that owns the event loop, never from a watcher thread.
// Backends that receive changes elsewhere queue them and deliver here, so a
// consumer never needs a lock to touch the render tree.
using FileWatchCallback = std::function<void(const FileChange&)>;

}  // namespace dg
