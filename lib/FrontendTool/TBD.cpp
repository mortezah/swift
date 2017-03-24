//===--- TBD.cpp -- generates and validates TBD files ---------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "TBD.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/Decl.h"
#include "swift/AST/DiagnosticEngine.h"
#include "swift/AST/DiagnosticsFrontend.h"
#include "swift/AST/Module.h"
#include "swift/Basic/LLVM.h"
#include "swift/Demangling/Demangle.h"
#include "swift/Frontend/FrontendOptions.h"
#include "swift/TBDGen/TBDGen.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/Support/FileSystem.h"

bool swift::writeTBD(ModuleDecl *M, StringRef OutputFilename) {
  std::error_code EC;
  llvm::raw_fd_ostream OS(OutputFilename, EC, llvm::sys::fs::F_None);
  if (EC) {
    M->getASTContext().Diags.diagnose(SourceLoc(), diag::error_opening_output,
                                      OutputFilename, EC.message());
    return true;
  }
  llvm::StringSet<> symbols;
  for (auto file : M->getFiles())
    enumeratePublicSymbols(file, symbols);

  for (auto &symbol : symbols) {
    OS << symbol.getKey() << "\n";
  }

  return false;
}

bool swift::validateTBD(ModuleDecl *M, llvm::Module &IRModule) {
  auto error = false;
  for (auto file : M->getFiles())
    error |= validateTBD(file, IRModule);
  return error;
}

bool swift::validateTBD(FileUnit *file, llvm::Module &IRModule) {
  llvm::StringSet<> symbols;
  enumeratePublicSymbols(file, symbols);
  auto error = false;
  auto &diags = file->getParentModule()->getASTContext().Diags;

  // Diff the two sets of symbols, flagging anything outside their intersection.
  for (auto &nameValue : IRModule.getValueSymbolTable()) {
    auto name = nameValue.getKey();
    auto value = nameValue.getValue();
    if (auto GV = dyn_cast<llvm::GlobalValue>(value)) {
      if (!GV->isDeclaration() && GV->hasExternalLinkage()) {
        if (!symbols.erase(name)) {
          diags.diagnose(SourceLoc(), diag::symbol_in_ir_not_in_tbd, name,
                         Demangle::demangleSymbolAsString(name));
          error = true;
        }
      }
    } else {
      assert(symbols.find(name) == symbols.end() &&
             "non-global value in value symbol table");
    }
  }

  for (auto &nameEntry : symbols) {
    auto name = nameEntry.getKey();
    diags.diagnose(SourceLoc(), diag::symbol_in_tbd_not_in_ir, name,
                   Demangle::demangleSymbolAsString(name));
    error = true;
  }
  return error;
}
