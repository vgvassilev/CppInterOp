//===--- ThunkCatalogTest.cpp - Open thunk-catalog primitives -*- C++ -*-===//
//
// Part of the compiler-research project, under the Apache License v2.0 with
// LLVM Exceptions.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Utils.h"

#include "CppInterOp/CppInterOp.h"
#include "CppInterOp/CppInterOpThunks.h"

#include "gtest/gtest.h"

#include <array>
#include <cstddef>
#include <string>

using namespace TestUtils;

namespace {

// MockTraits exposes the HandlerTraits contract without pulling Python
// (or any binding) in. The hot-path Call here is never invoked in these
// tests -- we only exercise registration and lookup.
struct MockTraits {
  using Handler = int;
  static Handler& From(void* /*self*/) {
    static Handler h = 0;
    return h;
  }
  template <class R, class... Args>
  static R Call(Handler&, std::size_t /*Slot*/, Args...) {
    if constexpr (std::is_void_v<R>) return;
    else                              return R{};
  }
};

// A distinct Traits to verify rows are isolated by traits-id string.
struct OtherTraits {
  using Handler = int;
  static Handler& From(void* /*self*/) {
    static Handler h = 0;
    return h;
  }
  template <class R, class... Args>
  static R Call(Handler&, std::size_t /*Slot*/, Args...) {
    if constexpr (std::is_void_v<R>) return;
    else                              return R{};
  }
};

} // anonymous

TYPED_TEST(CPPINTEROP_TEST_MODE, ThunkCatalog_RegisterLookup) {
  // Publish one (int, int) row keyed by a synthetic signature.
  Cpp::Thunks::RegisterCatalogRow<MockTraits, int, int>(
      "MockTraits", "int (int)");

  std::vector<Decl*> Decls1;
  GetAllTopLevelDecls(
      "namespace tcat_rl {\n"
      "  struct B { virtual ~B()=default; virtual int frob(int x){return x;} };\n"
      "}\n",
      Decls1);
  Cpp::TCppScope_t B = Cpp::GetNamed("B", Decls1[0]);
  ASSERT_NE(B, nullptr);
  auto Frobs = Cpp::GetFunctionsUsingName(B, "frob");
  ASSERT_EQ(Frobs.size(), 1u);
  Cpp::TCppFunction_t Frob = Frobs.front();

  EXPECT_EQ(Cpp::MethodSignature(Frob), "int (int)");

  // Hit: slot 0 returns the registered thunk.
  void* slot0 = Cpp::LookupThunk("MockTraits", Frob, 0);
  ASSERT_NE(slot0, nullptr);
  EXPECT_EQ(slot0,
            reinterpret_cast<void*>(
                &Cpp::Thunks::dispatch<MockTraits, 0, int, int>));

  // Miss: same shape under a different traits id.
  EXPECT_EQ(Cpp::LookupThunk("OtherTraits", Frob, 0), nullptr);

  // Miss: slot out of range.
  EXPECT_EQ(Cpp::LookupThunk("MockTraits", Frob,
                             Cpp::Thunks::kMaxOverlaySlots),
            nullptr);

  // Miss: a method whose signature was not registered.
  std::vector<Decl*> Decls2;
  GetAllTopLevelDecls(
      "namespace tcat_rl2 {\n"
      "  struct C { virtual ~C()=default; virtual bool g(int){return 0;} };\n"
      "}\n",
      Decls2);
  Cpp::TCppScope_t C = Cpp::GetNamed("C", Decls2[0]);
  Cpp::TCppFunction_t G = Cpp::GetFunctionsUsingName(C, "g").front();
  EXPECT_EQ(Cpp::LookupThunk("MockTraits", G, 0), nullptr);
}

TYPED_TEST(CPPINTEROP_TEST_MODE, ThunkCatalog_TraitsIsolation) {
  // Two distinct HandlerTraits register the same signature. Both rows
  // must remain retrievable; neither overwrites the other.
  Cpp::Thunks::RegisterCatalogRow<MockTraits,  bool, int>(
      "MockTraits",  "bool (int)");
  Cpp::Thunks::RegisterCatalogRow<OtherTraits, bool, int>(
      "OtherTraits", "bool (int)");

  std::vector<Decl*> Decls;
  GetAllTopLevelDecls(
      "namespace tcat_iso {\n"
      "  struct B { virtual ~B()=default; virtual bool ping(int){return 0;} };\n"
      "}\n",
      Decls);
  Cpp::TCppScope_t B = Cpp::GetNamed("B", Decls[0]);
  Cpp::TCppFunction_t Ping = Cpp::GetFunctionsUsingName(B, "ping").front();

  void* m = Cpp::LookupThunk("MockTraits",  Ping, 0);
  void* o = Cpp::LookupThunk("OtherTraits", Ping, 0);
  EXPECT_NE(m, nullptr);
  EXPECT_NE(o, nullptr);
  EXPECT_NE(m, o); // distinct instantiations -> distinct addresses
}

TYPED_TEST(CPPINTEROP_TEST_MODE, ThunkCatalog_MethodSignature_Canonical) {
  // Typedefs and using-aliases must resolve to the canonical type.
  std::vector<Decl*> Decls;
  GetAllTopLevelDecls(
      "namespace tcat_sig {\n"
      "  using MyInt = int;\n"
      "  struct B { virtual ~B()=default;\n"
      "    virtual MyInt h(MyInt x){return x;}\n"
      "    virtual void  k(){}\n"
      "    virtual int   j(int a, int b){return a+b;}\n"
      "  };\n"
      "}\n",
      Decls);
  Cpp::TCppScope_t B = Cpp::GetNamed("B", Decls[0]);
  EXPECT_EQ(Cpp::MethodSignature(Cpp::GetFunctionsUsingName(B, "h").front()),
            "int (int)");
  EXPECT_EQ(Cpp::MethodSignature(Cpp::GetFunctionsUsingName(B, "k").front()),
            "void ()");
  EXPECT_EQ(Cpp::MethodSignature(Cpp::GetFunctionsUsingName(B, "j").front()),
            "int (int,int)");
}

TYPED_TEST(CPPINTEROP_TEST_MODE, ThunkCatalog_NullArgsAreNoOp) {
  // The runtime helpers must reject null inputs without crashing or
  // poisoning the catalog.
  void* slots[1] = { reinterpret_cast<void*>(0xDEADBEEF) };
  Cpp::RegisterThunkRow(nullptr, "x", slots, 1);                 // no-op
  Cpp::RegisterThunkRow("t", nullptr, slots, 1);                 // no-op
  Cpp::RegisterThunkRow("t", "x", nullptr, 1);                   // no-op
  Cpp::RegisterThunkRow("t", "x", slots, 0);                     // no-op

  EXPECT_EQ(Cpp::LookupThunk(nullptr, nullptr, 0), nullptr);
  EXPECT_EQ(Cpp::MethodSignature(nullptr), "");
}
