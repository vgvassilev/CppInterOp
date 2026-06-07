//===--- CppInterOpThunks.h - Variadic dispatcher + catalog ---*- C++ -*-===//
//
// Part of the compiler-research project, under the Apache License v2.0 with
// LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Header-only machinery for the thunk catalog. Each binding contributes
// a HandlerTraits struct of this shape:
//
//   struct HandlerTraits {
//     using Handler = /* binding-specific per-instance state */;
//
//     // Per-instance lookup -- typically reads the handler pointer from
//     // an extra prefix slot reserved via MakeVTableOverlay's
//     // n_extra_prefix_slots, i.e. vptr[-(kVTableOverlayPrefixSize + 1)].
//     static Handler& From(void* self);
//
//     // Marshal Args... into the target language, call the bound
//     // callable at index Slot, and marshal the result back as R.
//     template <class R, class... Args>
//     static R Call(Handler&, std::size_t Slot, Args... args);
//   };
//
// dispatch<Traits, Slot, R, Args...> is one instantiation per signature
// shape per slot; the resulting function pointer is what gets installed
// in an overlaid vtable slot.
//
// RegisterCatalogRow<Traits, R, Args...>("<sig>") instantiates one row
// (kMaxOverlaySlots thunks) and publishes it into the process-wide
// catalog via Cpp::RegisterThunkRow (resolved through Dispatch.h, so
// downstream .so's don't need to link libclangCppInterOp).
//
//===----------------------------------------------------------------------===//

#ifndef CPPINTEROP_CPPINTEROPTHUNKS_H
#define CPPINTEROP_CPPINTEROPTHUNKS_H

#include "CppInterOp/CppInterOpTypes.h"

#include <array>
#include <cstddef>
#include <utility>

namespace CppImpl {
namespace Thunks {

// Hot-path dispatcher. Each instantiation has the calling convention
// of `R (void*, Args...)`, suitable for installation in a vtable slot.
template <class Traits, std::size_t Slot, class R, class... Args>
R dispatch(void* self, Args... args) {
  return Traits::template Call<R, Args...>(Traits::From(self), Slot, args...);
}

// Maximum number of overlay slots a single row covers. Identical
// instantiations fold to one .text symbol, so a row costs only the
// pointer-array, not kMaxOverlaySlots × code.
inline constexpr std::size_t kMaxOverlaySlots = 8;

template <class Traits, class R, class... Args, std::size_t... I>
constexpr std::array<void*, sizeof...(I)>
MakeThunkRow(std::index_sequence<I...>) {
  return {{reinterpret_cast<void*>(
      &dispatch<Traits, I, R, Args...>)...}};
}

// Lazily-initialized per-(R, Args...) thunk row. Stable address; safe
// to take pointers into the underlying std::array's storage.
template <class Traits, class R, class... Args>
inline const std::array<void*, kMaxOverlaySlots>& ThunkRow() {
  static const auto row = MakeThunkRow<Traits, R, Args...>(
      std::make_index_sequence<kMaxOverlaySlots>{});
  return row;
}

} // namespace Thunks
} // namespace CppImpl

// Publication entry point. The non-template RegisterThunkRow lives in
// libclangCppInterOp.so; callers reach it through whichever `Cpp::`
// alias is in scope (CppImpl directly, or CppInternal::Dispatch via the
// runtime symbol table from Dispatch.h).
//
// The traits_id is a caller-supplied string that namespaces rows by
// binding. typeid(Traits).name() would be tempting, but it requires
// -frtti (LLVM-derived builds disable it) and -- worse -- under
// RTLD_LOCAL each .so gets its own per-TU instance of the typeinfo
// object, so addresses differ across libraries that should be sharing
// the same key. An explicit string sidesteps both issues; bindings
// expose a thin wrapper that pins it.
//
// FIXME: if the traits_id string-compare ever shows up on the slow
// path, lift Tracing.h's __PRETTY_FUNCTION__ / __FUNCSIG__ parser
// (parseEnumName) into a CTTI helper and derive traits_id automatically.
namespace CppImpl {
namespace Thunks {

template <class Traits, class R, class... Args>
inline int RegisterCatalogRow(const char* traits_id,
                              const char* signature) {
  const auto& row = ThunkRow<Traits, R, Args...>();
  Cpp::RegisterThunkRow(traits_id, signature, row.data(), row.size());
  return 0;
}

} // namespace Thunks
} // namespace CppImpl

#endif // CPPINTEROP_CPPINTEROPTHUNKS_H
