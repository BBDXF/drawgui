// T2 optional services and the query that finds them.
//
// design.md section 5.14 refuses to answer "which platform features exist" as
// a list, because that list cannot be enumerated in advance and every entry
// added to IPlatform forces every backend to implement or stub it. It defines
// tiers instead. T1 is what every target platform has and a GUI cannot work
// without, and lives as direct methods on IPlatform / IWindow. T2 is what
// only some platforms have and an application can survive without, and lives
// here.
//
// Section 5.14.1 explicitly demotes the open_file_dialog() and
// show_notification() sketches out of T1, and names the T2 set: tray, native
// menubar, notifications, file dialogs, global hotkeys, single instance, URL
// scheme, and system colour-scheme following.
//
// The rule this machinery exists to enforce is section 5.14.2's: capability
// must be queryable rather than discovered by trial. Asking for a service a
// backend does not have returns nullptr at query time - it never returns an
// object that fails ambiguously on first use.
//
//   if (auto* tray = platform->query_service<ITrayService>()) { ... }
//
// The interfaces themselves are not defined here. A service interface is
// written in the change that first implements it, which is what keeps this
// from becoming the unbounded list section 5.14 rejects. Promotion from T3 is
// governed by section 5.14.3: three or more target platforms, a single
// cross-platform shape, and two real use cases.

#pragma once

#include <concepts>
#include <cstdint>

#include "drawgui/platform/types.h"

namespace dg {

namespace detail {
struct ServiceIdTag;
}  // namespace detail

// Stable numeric identity for a service, shared with the C ABI's
// `const void* dg_query_service(dg_app_t*, uint16_t service_id)` (design.md
// section 5.14.2). Like prop_id (section 5.8 decision 4), an id is never
// reused and never renumbered: it is an ABI contract.
//
// Not a scoped enum, deliberately. The ABI accepts an arbitrary uint16_t from
// a host, and a scoped enum cannot represent a value that is not one of its
// enumerators - converting an unrecognized id into one would either be
// undefined or, with an eight-bit underlying type, fold 0x0101 onto kTray and
// answer a query for a service nobody has with the system tray.
using ServiceId = Handle<detail::ServiceIdTag, std::uint16_t>;

// The T2 set design.md section 5.14.1 names. A service is registered here
// when the change that implements it lands; the id is what the ABI passes and
// what the interface type carries.
inline constexpr ServiceId kServiceTray = ServiceId::from_raw(1);
inline constexpr ServiceId kServiceNativeMenuBar = ServiceId::from_raw(2);
inline constexpr ServiceId kServiceNotifications = ServiceId::from_raw(3);
inline constexpr ServiceId kServiceFileDialog = ServiceId::from_raw(4);
inline constexpr ServiceId kServiceGlobalHotkeys = ServiceId::from_raw(5);
inline constexpr ServiceId kServiceSingleInstance = ServiceId::from_raw(6);
inline constexpr ServiceId kServiceUrlScheme = ServiceId::from_raw(7);
inline constexpr ServiceId kServiceColorScheme = ServiceId::from_raw(8);

// Base of every T2 service interface. Carries no behaviour - it exists so
// that "a service" is a type the compiler recognizes, and so that every
// service is destroyed through a virtual destructor rather than by whoever
// happened to hold the pointer.
class IPlatformService {
 public:
  IPlatformService(const IPlatformService&) = delete;
  IPlatformService& operator=(const IPlatformService&) = delete;
  IPlatformService(IPlatformService&&) = delete;
  IPlatformService& operator=(IPlatformService&&) = delete;

  virtual ~IPlatformService() = default;

 protected:
  IPlatformService() = default;
};

// A type that query_service<T>() will accept: a service interface that names
// its own id.
//
//   class ITrayService : public dg::IPlatformService {
//    public:
//     static constexpr dg::ServiceId kServiceId = dg::kServiceTray;
//     ...
//   };
//
// Binding the id to the type is what removes the second way to get this
// wrong. A caller that passed both would eventually pass a mismatched pair,
// and the static_cast inside query_service() would then hand back a pointer
// to the wrong interface with no diagnostic anywhere.
//
// The id requirement is written as a requires-expression rather than as
// std::same_as<decltype(T::kServiceId), const ServiceId>, because the latter
// makes a type without the member a hard error under GCC 15 instead of an
// unsatisfied constraint - and "this type is not a service" has to be a
// question that can be asked, not one that breaks the build.
template <typename T>
concept PlatformService = std::derived_from<T, IPlatformService> && requires {
  { T::kServiceId } -> std::same_as<const ServiceId&>;
};

// The query half of IPlatform, separated so that it can be implemented and
// tested without a windowing backend.
class IServiceProvider {
 public:
  IServiceProvider(const IServiceProvider&) = delete;
  IServiceProvider& operator=(const IServiceProvider&) = delete;
  IServiceProvider(IServiceProvider&&) = delete;
  IServiceProvider& operator=(IServiceProvider&&) = delete;

  virtual ~IServiceProvider() = default;

  // Returns nullptr when this platform does not provide T. The result is
  // owned by the platform and stays valid for as long as the platform does.
  //
  // Not virtual: there is exactly one correct implementation of the type-to-id
  // mapping, and a backend that could override it could break the invariant
  // that the returned pointer really is a T*.
  template <PlatformService T>
  [[nodiscard]] T* query_service() {
    return static_cast<T*>(query_service_raw(T::kServiceId));
  }

 protected:
  IServiceProvider() = default;

  // The single method a backend implements. It must return either nullptr or
  // a pointer to the interface registered under `id`; returning anything else
  // is undefined behaviour, and is why the ServiceId lives on the interface
  // type rather than being passed in beside it.
  //
  // void* rather than IPlatformService*, matching the C ABI's return type:
  // recovering T* from a polymorphic base would be a downcast, and a downcast
  // across a boundary where the base carries no way to check itself is worse
  // than the cast it replaces.
  [[nodiscard]] virtual void* query_service_raw(ServiceId id) = 0;
};

}  // namespace dg
