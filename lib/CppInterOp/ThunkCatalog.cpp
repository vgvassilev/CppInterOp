//===--- ThunkCatalog.cpp - Process-wide thunk-row registry ---*- C++ -*-===//
//
// Part of the compiler-research project, under the Apache License v2.0 with
// LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The runtime side of the open thunk catalog. Bindings publish per-
// (R, Args...) rows here from TU-scope initializers and look them up
// at class-creation time by canonical method signature. Templates live
// in include/CppInterOp/CppInterOpThunks.h so caller TUs control the
// instantiation point.
//
//===----------------------------------------------------------------------===//

#include "CppInterOp/CppInterOp.h"
#include "CppInterOp/CppInterOpThunks.h"

#include <array>
#include <mutex>
#include <string>
#include <unordered_map>

namespace CppImpl {

namespace {

// A row covers one (R, Args...) signature across kMaxOverlaySlots
// vtable slots. Rows are keyed by (traits_id, signature) so bindings
// with distinct HandlerTraits coexist without collisions.
struct CatalogRow {
  std::array<void*, Thunks::kMaxOverlaySlots> slots{};
};

struct CatalogKey {
  std::string traits_id;
  std::string signature;
  bool operator==(const CatalogKey& o) const {
    return traits_id == o.traits_id && signature == o.signature;
  }
};

struct CatalogKeyHash {
  std::size_t operator()(const CatalogKey& k) const noexcept {
    // FNV-1a-style mix; the registry is write-rare and read-rare so the
    // hash quality matters more than the speed.
    std::size_t h = std::hash<std::string>{}(k.traits_id);
    h ^= std::hash<std::string>{}(k.signature) + 0x9e3779b97f4a7c15ULL +
         (h << 6) + (h >> 2);
    return h;
  }
};

std::mutex& Mutex() {
  static std::mutex m;
  return m;
}

std::unordered_map<CatalogKey, CatalogRow, CatalogKeyHash>& Table() {
  static std::unordered_map<CatalogKey, CatalogRow, CatalogKeyHash> t;
  return t;
}

} // anonymous

void RegisterThunkRow(const char* traits_id,
                      const char* signature,
                      const void* const* slots,
                      std::size_t n) {
  if (!traits_id || !signature || !slots || n == 0)
    return;
  CatalogRow row;
  const std::size_t copy = n < Thunks::kMaxOverlaySlots
                               ? n : Thunks::kMaxOverlaySlots;
  for (std::size_t i = 0; i < copy; ++i)
    row.slots[i] = const_cast<void*>(slots[i]);
  std::lock_guard<std::mutex> lock(Mutex());
  // Last writer wins: a downstream library can override a built-in row
  // by registering a more specific marshaling for the same shape.
  Table()[CatalogKey{traits_id, signature}] = row;
}

void* LookupThunk(const char* traits_id,
                  TCppFunction_t method,
                  std::size_t slot) {
  if (!traits_id || !method || slot >= Thunks::kMaxOverlaySlots)
    return nullptr;
  std::string sig = MethodSignature(method);
  std::lock_guard<std::mutex> lock(Mutex());
  auto it = Table().find(CatalogKey{traits_id, sig});
  return it == Table().end() ? nullptr : it->second.slots[slot];
}

std::string MethodSignature(TCppFunction_t method) {
  if (!method)
    return {};
  std::string sig = GetTypeAsString(
      GetCanonicalType(GetFunctionReturnType(method)));
  sig += " (";
  TCppIndex_t n = GetFunctionNumArgs(method);
  for (TCppIndex_t i = 0; i < n; ++i) {
    if (i) sig += ",";
    sig += GetTypeAsString(
        GetCanonicalType(GetFunctionArgType(method, i)));
  }
  sig += ")";
  return sig;
}

} // namespace CppImpl
