// Unit tests for the platform abstraction.
//
// No backend exists yet, so almost everything worth proving here is a
// compile-time property - which is the point rather than a limitation. The
// interfaces are being fixed before their first implementation precisely so
// that no single platform's API can shape them (design.md constraint C1), and
// the properties that guarantee that are all properties of types: the
// interfaces are abstract and destroyed virtually, handles of different kinds
// cannot be mixed, a raw integer is not a handle, and query_service<T>()
// refuses a type that is not a registered service.
//
// The runtime cases cover the two things that do have behaviour without a
// backend: service lookup routing, and the defaults every backend inherits.

#include <cstdint>
#include <type_traits>

#include <doctest/doctest.h>

#include "drawgui/platform/platform.h"

namespace {

// --- The interfaces are interfaces -------------------------------------------

static_assert(std::is_abstract_v<dg::IPlatform>);
static_assert(std::is_abstract_v<dg::IWindow>);
static_assert(std::is_abstract_v<dg::IServiceProvider>);

// IPlatformService declares no operation of its own - it is the tag every
// service interface derives from - so it is not abstract. It must still be
// impossible to construct one on its own, which its protected constructor is
// what enforces.
static_assert(!std::is_constructible_v<dg::IPlatformService>);

// Every one of them is deleted through a base pointer somewhere - IPlatform
// owns its windows, and the platform owns its services.
static_assert(std::has_virtual_destructor_v<dg::IPlatform>);
static_assert(std::has_virtual_destructor_v<dg::IWindow>);
static_assert(std::has_virtual_destructor_v<dg::IServiceProvider>);
static_assert(std::has_virtual_destructor_v<dg::IPlatformService>);

// Copying or moving a polymorphic interface slices it. The backend's window is
// its own object with its own OS handle; a copy of the IWindow subobject would
// be a window that owns nothing.
static_assert(!std::is_copy_constructible_v<dg::IWindow>);
static_assert(!std::is_move_constructible_v<dg::IWindow>);
static_assert(!std::is_copy_assignable_v<dg::IWindow>);
static_assert(!std::is_copy_constructible_v<dg::IPlatform>);
static_assert(!std::is_move_constructible_v<dg::IPlatform>);

// The service query is part of IPlatform, not a thing bolted beside it.
static_assert(std::is_base_of_v<dg::IServiceProvider, dg::IPlatform>);

// design.md section 5.8 decision 4 and clang-tidy's performance-enum-size:
// these travel in events and in the ABI, so their width is deliberate.
static_assert(sizeof(dg::WindowKind) == 1);
static_assert(sizeof(dg::CursorShape) == 1);
static_assert(sizeof(dg::PlatformError) == 1);
static_assert(sizeof(dg::LifecycleEventKind) == 1);
static_assert(sizeof(dg::NativeHandleKind) == 1);
static_assert(sizeof(dg::RenderTargetKind) == 1);
static_assert(sizeof(dg::FileChangeKind) == 1);
static_assert(sizeof(dg::LoopMode) == 1);
static_assert(sizeof(dg::ServiceId) == 2);  // matches the C ABI's uint16_t
static_assert(std::is_same_v<dg::ServiceId::raw_type, std::uint16_t>);

// --- Handles ------------------------------------------------------------------

static_assert(!dg::WindowId{}.is_valid());
static_assert(dg::WindowId::from_raw(7).is_valid());
static_assert(dg::WindowId::from_raw(7).raw() == 7);
static_assert(dg::WindowId::from_raw(7) == dg::WindowId::from_raw(7));
static_assert(dg::WindowId::from_raw(7) != dg::WindowId::from_raw(8));

// Spelled as a detection trait rather than as a requires-expression, because
// both GCC 15 and clang 21 diagnose a failed `a == b` inside a bare
// requires-expression as a hard error instead of an unsatisfied requirement.
template <typename A, typename B, typename = void>
struct IsEqualityComparable : std::false_type {};

template <typename A, typename B>
struct IsEqualityComparable<A, B, std::void_t<decltype(std::declval<A>() == std::declval<B>())>>
    : std::true_type {};

// The whole reason Handle is tagged. Without the tag these would be the same
// type, and find_window(display_id) would compile.
static_assert(!std::is_same_v<dg::WindowId, dg::DisplayId>);
static_assert(!std::is_same_v<dg::WindowId, dg::WatchId>);
static_assert(!std::is_convertible_v<dg::WindowId, dg::DisplayId>);
static_assert(IsEqualityComparable<dg::WindowId, dg::WindowId>::value);
static_assert(!IsEqualityComparable<dg::WindowId, dg::DisplayId>::value);

// And a bare integer is not a handle either, in either direction.
static_assert(!std::is_convertible_v<std::uint32_t, dg::WindowId>);
static_assert(!std::is_convertible_v<dg::WindowId, std::uint32_t>);

// --- Aggregate value types ----------------------------------------------------

static_assert(std::is_aggregate_v<dg::PlatformCaps>);
static_assert(std::is_aggregate_v<dg::WindowDesc>);
static_assert(std::is_aggregate_v<dg::DisplayInfo>);
static_assert(std::is_aggregate_v<dg::FrameTarget>);
static_assert(std::is_aggregate_v<dg::NativeWindow>);
static_assert(std::is_aggregate_v<dg::SafeAreaInsets>);
static_assert(std::is_aggregate_v<dg::PixelSize>);

// A backend opts in to each capability it can actually deliver. The opposite
// default would let a half-written backend claim everything, and the first
// symptom would be PopupHost taking the native path on a platform with no
// native popups (design.md section 5.2).
constexpr dg::PlatformCaps kDefaultCaps;
static_assert(!kDefaultCaps.multi_window);
static_assert(!kDefaultCaps.native_popup);
static_assert(!kDefaultCaps.native_menubar);
static_assert(!kDefaultCaps.system_tray);
static_assert(!kDefaultCaps.window_transparency);
static_assert(!kDefaultCaps.file_dialog);

// --- A T2 service, as a service interface is meant to be written --------------

class IFakeTray : public dg::IPlatformService {
 public:
  static constexpr dg::ServiceId kServiceId = dg::kServiceTray;

  [[nodiscard]] virtual int icon_count() const = 0;
};

class IFakeMenuBar : public dg::IPlatformService {
 public:
  static constexpr dg::ServiceId kServiceId = dg::kServiceNativeMenuBar;

  [[nodiscard]] virtual int menu_count() const = 0;
};

class FakeTray final : public IFakeTray {
 public:
  [[nodiscard]] int icon_count() const override { return 3; }
};

// Has a tray and no menu bar. The asymmetry is the point: a query that
// ignored the id and returned whatever it had would pass a symmetric fixture.
//
// query_service_raw is widened to public so the tests can drive the path the
// C ABI will take, which is the only way to reach it with an id no service
// type names.
class TrayOnlyPlatform final : public dg::IServiceProvider {
 public:
  [[nodiscard]] const IFakeTray* installed_tray() const { return &tray_; }

  [[nodiscard]] void* query_service_raw(dg::ServiceId id) override {
    return id == dg::kServiceTray ? static_cast<IFakeTray*>(&tray_) : nullptr;
  }

 private:
  FakeTray tray_;
};

class BarePlatform final : public dg::IServiceProvider {
 protected:
  [[nodiscard]] void* query_service_raw(dg::ServiceId /*id*/) override { return nullptr; }
};

static_assert(dg::PlatformService<IFakeTray>);
static_assert(dg::PlatformService<IFakeMenuBar>);

// A service interface has to name its own id, and a type that is not a
// service at all cannot be queried. query_service<T>() is constrained by this
// concept, so an unsatisfied concept is a call that does not compile.
struct NotAService {};

class UnnumberedService : public dg::IPlatformService {};

static_assert(!dg::PlatformService<NotAService>);
static_assert(!dg::PlatformService<UnnumberedService>);

}  // namespace

TEST_CASE("query_service answers by service id") {
  TrayOnlyPlatform platform;

  IFakeTray* tray = platform.query_service<IFakeTray>();
  REQUIRE(tray != nullptr);

  // The exact object the platform holds, not merely something non-null: a
  // lookup that ignored the id would still return a pointer here.
  CHECK(tray == platform.installed_tray());
  CHECK(tray->icon_count() == 3);
}

TEST_CASE("query_service reports an absent service as absent, not as a failure later") {
  TrayOnlyPlatform tray_only;
  BarePlatform bare;

  // design.md section 5.14.2: capability is queryable rather than discovered
  // by trial. The caller learns the menu bar is missing here, at the query,
  // with no call attempted and no ambiguous error code to interpret.
  CHECK(tray_only.query_service<IFakeMenuBar>() == nullptr);
  CHECK(bare.query_service<IFakeTray>() == nullptr);
  CHECK(bare.query_service<IFakeMenuBar>() == nullptr);
}

TEST_CASE("an unrecognized ABI service id cannot alias a real service") {
  TrayOnlyPlatform platform;

  // The C ABI's dg_query_service takes an arbitrary uint16_t from a host, and
  // 0x0101 is the value that proves the width of ServiceId is load-bearing:
  // narrowed to eight bits it becomes 0x01, which is kTray, and a query for a
  // service nobody has would come back with the tray.
  constexpr dg::ServiceId kUnknownAbiId = dg::ServiceId::from_raw(0x0101);

  CHECK(kUnknownAbiId != dg::kServiceTray);
  CHECK(platform.query_service_raw(kUnknownAbiId) == nullptr);
  CHECK(platform.query_service_raw(dg::kServiceTray) != nullptr);
}

TEST_CASE("PlatformCaps compares every capability it carries") {
  const dg::PlatformCaps none;

  dg::PlatformCaps caps;
  caps.multi_window = true;
  CHECK(caps != none);

  caps = none;
  caps.native_popup = true;
  CHECK(caps != none);

  caps = none;
  caps.native_menubar = true;
  CHECK(caps != none);

  caps = none;
  caps.system_tray = true;
  CHECK(caps != none);

  caps = none;
  caps.window_transparency = true;
  CHECK(caps != none);

  caps = none;
  caps.file_dialog = true;
  CHECK(caps != none);
}

TEST_CASE("WindowDesc defaults describe a plain top-level window") {
  const dg::WindowDesc desc;

  CHECK(desc.title.empty());
  CHECK(desc.logical_size == dg::Size{800.0F, 600.0F});
  CHECK_FALSE(desc.position.has_value());
  CHECK(desc.kind == dg::WindowKind::kNormal);
  CHECK_FALSE(desc.owner.is_valid());
  CHECK(desc.resizable);
  CHECK(desc.decorated);
  CHECK(desc.initially_visible);

  // design.md section 5.11.5: an alpha framebuffer has to be requested when
  // the graphics context is created, so this is a creation-time flag and off
  // by default. It is not IWindow::set_opacity(), which is whole-window
  // compositing opacity and stays mutable.
  CHECK_FALSE(desc.transparent_framebuffer);
}

TEST_CASE("an owned window kind carries its owner") {
  dg::WindowDesc desc;
  desc.kind = dg::WindowKind::kPopup;
  desc.owner = dg::WindowId::from_raw(4);

  CHECK(desc.owner.is_valid());
  CHECK(desc.owner == dg::WindowId::from_raw(4));
  CHECK(desc.owner != dg::WindowId{});
}

TEST_CASE("the default GL framebuffer is not the absence of a framebuffer") {
  const dg::FrameTarget nothing;
  CHECK(nothing.kind == dg::RenderTargetKind::kNone);
  CHECK(nothing.handle == 0);
  CHECK(nothing.pixel_size.is_empty());
  CHECK(nothing.sample_count == 1);

  // A GL window's on-screen target is framebuffer name 0, so the handle alone
  // cannot say whether there is anything to draw into. That is what the kind
  // tag is for, and this is the case that proves it.
  const dg::FrameTarget on_screen{dg::RenderTargetKind::kOpenGlFramebuffer, 0,
                                  dg::PixelSize{1920, 1080}, 1, 8};

  CHECK(on_screen.handle == nothing.handle);
  CHECK(on_screen != nothing);
  CHECK_FALSE(on_screen.pixel_size.is_empty());
}

TEST_CASE("a failed platform operation carries the reason instead of a null pointer") {
  const dg::PlatformResult<dg::IWindow*> failed{
      dg::Unexpected{dg::PlatformError::kWindowCreationFailed}};

  REQUIRE_FALSE(failed.has_value());
  CHECK(failed.error() == dg::PlatformError::kWindowCreationFailed);
  CHECK(failed.value_or(nullptr) == nullptr);

  // An occluded window is a distinct, non-alarming outcome rather than the
  // same failure as a lost graphics context.
  const dg::PlatformResult<dg::FrameTarget> skipped{
      dg::Unexpected{dg::PlatformError::kFrameNotAvailable}};
  CHECK(skipped.error() != dg::PlatformError::kGraphicsContextLost);

  const dg::PlatformResult<void> succeeded;
  CHECK(succeeded.has_value());
}

TEST_CASE("lifecycle events default to nothing having changed") {
  const dg::LifecycleEvent event;

  // Desktop backends never send these, which is exactly why the shape has to
  // be settled before a desktop backend exists (design.md section 5.14.7).
  CHECK_FALSE(event.window.is_valid());
  CHECK(event.safe_area == dg::SafeAreaInsets{});

  dg::LifecycleEvent low_memory;
  low_memory.kind = dg::LifecycleEventKind::kLowMemory;
  CHECK(low_memory != event);
}

TEST_CASE("a native handle is absent until a backend fills one in") {
  const dg::NativeWindow native;

  CHECK(native.kind == dg::NativeHandleKind::kNone);
  CHECK(native.handle == nullptr);
  CHECK(native.display == nullptr);
}
