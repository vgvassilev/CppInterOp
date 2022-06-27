
#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Transaction.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/RecordLayout.h"
#include "clang/Basic/Version.h"
#include "clang/Config/config.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"

#include "gtest/gtest.h"

using namespace clang;
using namespace llvm;
namespace Cpp {
using TCppScope_t = void *;
bool IsNamespace(TCppScope_t scope) {
  Decl *D = static_cast<Decl *>(scope);
  return isa<NamespaceDecl>(D);
}
// See TClingClassInfo::IsLoaded
bool IsComplete(TCppScope_t scope) {
  if (!scope)
    return false;
  Decl *D = static_cast<Decl *>(scope);
  if (auto *CXXRD = dyn_cast<CXXRecordDecl>(D))
    return CXXRD->hasDefinition();
  else if (auto *TD = dyn_cast<TagDecl>(D))
    return TD->getDefinition();

  // Everything else is considered complete.
  return true;
}

size_t SizeOf(TCppScope_t scope) {
  assert(scope);
  if (!IsComplete(scope))
    return 0;

  if (auto *RD = dyn_cast<RecordDecl>(static_cast<Decl *>(scope))) {
    ASTContext &Context = RD->getASTContext();
    const ASTRecordLayout &Layout = Context.getASTRecordLayout(RD);
    return Layout.getSize().getQuantity();
  }

  return 0;
}
} // namespace Cpp

// This function isn't referenced outside its translation unit, but it
// can't use the "static" keyword because its address is used for
// GetMainExecutable (since some platforms don't support taking the
// address of main, and some platforms can't implement GetMainExecutable
// without being given the address of a function in the main executable).
std::string GetExecutablePath(const char *Argv0, void *MainAddr) {
  return llvm::sys::fs::getMainExecutable(Argv0, MainAddr);
}

static std::string MakeResourcesPath() {
  // Dir is bin/ or lib/, depending on where BinaryPath is.
  void *MainAddr = (void *)(intptr_t)GetExecutablePath;
  std::string BinaryPath = GetExecutablePath(/*Argv0=*/nullptr, MainAddr);

  // build/tools/clang/unittests/Interpreter/Executable -> build/
  llvm::StringRef Dir = llvm::sys::path::parent_path(BinaryPath);

  Dir = llvm::sys::path::parent_path(Dir);
  Dir = llvm::sys::path::parent_path(Dir);
  Dir = llvm::sys::path::parent_path(Dir);
  Dir = llvm::sys::path::parent_path(Dir);
  Dir = llvm::sys::path::parent_path(Dir);
  llvm::SmallString<128> P(Dir);
  llvm::sys::path::append(P, llvm::Twine("lib") + CLANG_LIBDIR_SUFFIX, "clang",
                          CLANG_VERSION_STRING);

  return std::string(P.str());
}

static std::unique_ptr<cling::Interpreter> createInterpreter() {
  std::string MainExecutableName =
      llvm::sys::fs::getMainExecutable(nullptr, nullptr);
  std::string ResourceDir = MakeResourcesPath();
  std::vector<const char *> ClingArgv = {"-resource-dir", ResourceDir.c_str()};
  ClingArgv.insert(ClingArgv.begin(), MainExecutableName.c_str());
  return llvm::make_unique<cling::Interpreter>(ClingArgv.size(), &ClingArgv[0]);
}
std::unique_ptr<cling::Interpreter> Interp;
static void GetAllTopLevelDecls(const std::string &code,
                                std::vector<Decl *> &Decls) {
  Interp = createInterpreter();
  cling::Transaction *T = nullptr;
  Interp->declare(code, &T);
  for (auto DCI = T->decls_begin(), E = T->decls_end(); DCI != E; ++DCI) {
    if (DCI->m_Call != cling::Transaction::kCCIHandleTopLevelDecl)
      continue;
    assert(DCI->m_DGR.isSingleDecl());
    Decls.push_back(DCI->m_DGR.getSingleDecl());
  }
}

// Check that the CharInfo table has been constructed reasonably.
TEST(ScopeReflectionTest, IsNamespace) {
  std::vector<Decl*> Decls;
  GetAllTopLevelDecls("namespace N {} class C{}; int I;", Decls);
  EXPECT_TRUE(Cpp::IsNamespace(Decls[0]));
  EXPECT_FALSE(Cpp::IsNamespace(Decls[1]));
  EXPECT_FALSE(Cpp::IsNamespace(Decls[2]));
}

TEST(ScopeReflectionTest, IsComplete) {
  std::vector<Decl*> Decls;
  GetAllTopLevelDecls(
      "namespace N {} class C{}; int I; struct S; enum E : int; union U{};",
      Decls);
  EXPECT_TRUE(Cpp::IsComplete(Decls[0]));
  EXPECT_TRUE(Cpp::IsComplete(Decls[1]));
  EXPECT_TRUE(Cpp::IsComplete(Decls[2]));
  EXPECT_FALSE(Cpp::IsComplete(Decls[3]));
  EXPECT_FALSE(Cpp::IsComplete(Decls[4]));
  EXPECT_TRUE(Cpp::IsComplete(Decls[5]));
}

TEST(ScopeReflectionTest, SizeOf) {
  std::vector<Decl*> Decls;
  std::string code = R"(namespace N {} class C{}; int I; struct S;
                        enum E : int; union U{}; class Size4{int i;};
                        struct Size16 {short a; double b;};
                       )";
  GetAllTopLevelDecls(code, Decls);
  EXPECT_EQ(Cpp::SizeOf(Decls[0]), (size_t)0);
  EXPECT_EQ(Cpp::SizeOf(Decls[1]), (size_t)1);
  EXPECT_EQ(Cpp::SizeOf(Decls[2]), (size_t)0);
  EXPECT_EQ(Cpp::SizeOf(Decls[3]), (size_t)0);
  EXPECT_EQ(Cpp::SizeOf(Decls[4]), (size_t)0);
  EXPECT_EQ(Cpp::SizeOf(Decls[5]), (size_t)1);
  EXPECT_EQ(Cpp::SizeOf(Decls[6]), (size_t)4);
  EXPECT_EQ(Cpp::SizeOf(Decls[7]), (size_t)16);
}