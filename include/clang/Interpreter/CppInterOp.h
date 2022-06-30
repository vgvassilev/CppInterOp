//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Vassil Vassilev <vvasilev@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_INTERPRETER_CPPINTEROP_H
#define CLING_INTERPRETER_CPPINTEROP_H

#include <string>
#include <vector>

namespace cling {
namespace Cpp {
  using TCppIndex_t = size_t;
  using TCppScope_t = void*;
  using TCppType_t = void*;
  using TCppFunction_t = void*;
  using TCppSema_t = void *;

  bool IsNamespace(TCppScope_t scope);
  // See TClingClassInfo::IsLoaded
  bool IsComplete(TCppScope_t scope);

  size_t SizeOf(TCppScope_t scope);
  bool IsBuiltin(TCppType_t type);

  bool IsTemplate(TCppScope_t handle);

  bool IsAbstract(TCppType_t klass);

  bool IsEnum(TCppScope_t handle);

  bool IsVariable(TCppScope_t scope);

  std::string GetName(TCppType_t klass);

  std::string GetCompleteName(TCppType_t klass);

  std::vector<TCppScope_t> GetUsingNamespaces(TCppScope_t scope);

  TCppScope_t GetGlobalScope(TCppSema_t sema);

  TCppScope_t GetScope(TCppSema_t sema, const std::string &name,
                       TCppScope_t parent);

  TCppScope_t GetScopeFromCompleteName(TCppSema_t sema,
                                       const std::string &name);

  TCppScope_t GetNamed(TCppSema_t sema, const std::string &name,
                       TCppScope_t parent);

  TCppScope_t GetParentScope(TCppScope_t scope);

  TCppScope_t GetScopeFromType(TCppType_t type);

  TCppScope_t GetBaseClass(TCppType_t klass, TCppIndex_t ibase);

  bool IsSubclass(TCppScope_t derived, TCppScope_t base);

  std::vector<TCppFunction_t> GetClassMethods(TCppScope_t klass);

  std::vector<TCppFunction_t> GetFunctionsUsingName(TCppSema_t sema,
                                                    TCppScope_t scope,
                                                    const std::string &name);
} // end namespace Cpp

} // end namespace cling

#endif // CLING_INTERPRETER_CPPINTEROP_H
