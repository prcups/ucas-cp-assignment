//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool
//--------------===//
//===----------------------------------------------------------------------===//
#ifndef ENVIRONMENT_H_INCLUDED
#define ENVIRONMENT_H_INCLUDED

#include <stdexcept>
#include <iostream>
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

class StackFrame {
  /// StackFrame maps Variable Declaration to Value
  /// Which are either integer or addresses (also represented using an Integer
  /// value)
  std::map<Decl *, int> mVars;
  std::map<Stmt *, int> mExprs;
  /// The current stmt
  Stmt *mPC;

public:
  int retValue = 0;
  StackFrame() : mVars(), mExprs(), mPC() {}

  void bindDecl(Decl *decl, int val) { mVars[decl] = val; }
  int getDeclVal(Decl *decl) {
    auto rel = mVars.find(decl);
    if (rel == mVars.end()) {
      decl->dump();
      llvm::errs() << "Address: " << decl << "\n";
      throw(std::runtime_error("VarNotFound"));
    }
    return rel->second;
  }
  std::optional <int> tryGetDeclVal(Decl *decl) {
    auto rel = mVars.find(decl);
    if (rel == mVars.end()) {
      return {};
    }
    return rel->second;
  }
  void bindStmt(Stmt *stmt, int val) { mExprs[stmt] = val; }
  int getStmtVal(Stmt *stmt) {
    auto rel = mExprs.find(stmt);
    if (rel == mExprs.end()) {
      stmt->dump();
      llvm::errs() << "Address: " << stmt << "\n";
      throw(std::runtime_error("StmtNotFound"));
    }
    return rel->second;
  }
  std::optional <int> tryGetStmtVal(Stmt *stmt) {
    auto rel = mExprs.find(stmt);
    if (rel == mExprs.end()) {
      return {};
    }
    return rel->second;
  }
  void setPC(Stmt *stmt) { mPC = stmt; }
  Stmt *getPC() { return mPC; }
};

class Environment {
  std::vector<StackFrame> mStack;

  FunctionDecl *mFree;
  FunctionDecl *mMalloc;
  FunctionDecl *mInput;
  FunctionDecl *mOutput;

  FunctionDecl *mEntry;

  std::map<Decl *, int> gVars;
  int getGlobalDeclVal(Decl *decl) {
    if (gVars.find(decl) == gVars.end()) {
      decl->dump();
      throw(std::runtime_error("GlobalDeclNotFound"));
    }
    return gVars.find(decl)->second;
  }

public:
  /// Get the declartions to the built-in functions
  Environment()
      : mStack(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL),
        mEntry(NULL) {}

  /// Initialize the Environment
  void stage1Init(TranslationUnitDecl *unit, std::vector <VarDecl *> & gVarDeclList) {
    mStack.push_back(StackFrame());
    for (TranslationUnitDecl::decl_iterator i = unit->decls_begin(),
                                            e = unit->decls_end();
         i != e; ++i) {
      if (auto *fdecl = dyn_cast<FunctionDecl>(*i)) {
        if (fdecl->getName().equals("FREE"))
          mFree = fdecl;
        else if (fdecl->getName().equals("MALLOC"))
          mMalloc = fdecl;
        else if (fdecl->getName().equals("GET"))
          mInput = fdecl;
        else if (fdecl->getName().equals("PRINT"))
          mOutput = fdecl;
        else if (fdecl->getName().equals("main"))
          mEntry = fdecl;
      } else if (VarDecl *vdecl = dyn_cast<VarDecl>(*i)) {
        gVarDeclList.push_back(vdecl);
      }
    }
  }

  void stage2Init(std::vector <VarDecl *> & gVarDeclList) {
    for (auto vdecl : gVarDeclList) {
      Stmt *initStmt = vdecl->getInit();
      if (auto rel = mStack.back().tryGetStmtVal(initStmt)) {
        gVars[vdecl] = rel.value();
      } else {
        gVars[vdecl] = 0;
      }
    }
    mStack.pop_back();
    mStack.push_back(StackFrame());
  }

  FunctionDecl *getEntry() { return mEntry; }

  void handleBinOp(BinaryOperator *bop) {
    Expr *left = bop->getLHS(),
          *right = bop->getRHS();
    int valLeft = mStack.back().getStmtVal(left),
          valRight = mStack.back().getStmtVal(right);

    switch (bop->getOpcode()) {
      case BO_Assign: {
        mStack.back().bindStmt(left, valRight);
        if (DeclRefExpr *declexpr = dyn_cast<DeclRefExpr>(left)) {
          Decl *decl = declexpr->getFoundDecl();
          mStack.back().bindDecl(decl, valRight);
        }
        break;
      }
      case BO_Add: {
        mStack.back().bindStmt(bop, valLeft + valRight);
        break;
      }
      case BO_Sub: {
        mStack.back().bindStmt(bop, valLeft - valRight);
        break;
      }
      case BO_Mul: {
        mStack.back().bindStmt(bop, valLeft * valRight);
        break;
      }
      case BO_Div: {
        mStack.back().bindStmt(bop, valLeft / valRight);
        break;
      }
      case BO_GT: {
        mStack.back().bindStmt(bop, valLeft > valRight ? 1 : 0);
        break;
      }
      case BO_EQ: {
        mStack.back().bindStmt(bop, valLeft == valRight ? 1 : 0);
        break;
      }
      case BO_LT: {
        mStack.back().bindStmt(bop, valLeft < valRight ? 1 : 0);
        break;
      }
      default: {
        throw(std::runtime_error("UnsupportedOp"));
      }
    }
  }

  void handleIntliteral(IntegerLiteral *integer) {
    mStack.back().bindStmt(integer, integer->getValue().getSExtValue());
  }

  void handleDeclStmt(DeclStmt *declstmt) {
    for (DeclStmt::decl_iterator it = declstmt->decl_begin(),
                                 ie = declstmt->decl_end();
         it != ie; ++it) {
      Decl *decl = *it;
      if (VarDecl *vardecl = dyn_cast<VarDecl>(decl)) {
        mStack.back().bindDecl(vardecl,
                               vardecl->hasInit() ?
                               mStack.back().getStmtVal(vardecl->getInit()) :
                               0
        );
      }
    }
  }
  void handleDeclRef(DeclRefExpr *declref) {
    mStack.back().setPC(declref);
    if (declref->getType()->isIntegerType()) {
      Decl *decl = declref->getFoundDecl();

      int val;
      try {
        val = mStack.back().tryGetDeclVal(decl).value();
      } catch (const std::bad_optional_access& e) {
        val = getGlobalDeclVal(decl);
      }

      mStack.back().bindStmt(declref, val);
    }
  }

  void handleCast(CastExpr *castexpr) {
    mStack.back().setPC(castexpr);
    if (castexpr->getType()->isIntegerType()) {
      Expr *expr = castexpr->getSubExpr();
      int val = mStack.back().getStmtVal(expr);
      mStack.back().bindStmt(castexpr, val);
    }
  }

  bool tryCallBuiltInFunc(CallExpr *callexpr) {
    mStack.back().setPC(callexpr);
    int val = 0;
    FunctionDecl *callee = callexpr->getDirectCallee();
    if (callee == mInput) {
      llvm::errs() << "Please Input an Integer Value : \n";
      std::cin >> val;
      mStack.back().bindStmt(callexpr, val);
    } else if (callee == mOutput) {
      Expr *decl = callexpr->getArg(0);
      val = mStack.back().getStmtVal(decl);
      llvm::errs() << val << '\n';
    } else if (callee == mMalloc) {
        throw(std::runtime_error("UnsupportMalloc"));
    } else if (callee == mFree) {
        throw(std::runtime_error("UnsupportFree"));
    } else {
      return false;
    }
    return true;
  }

  void createFuncStack(CallExpr *call) {
    auto callee = call->getDirectCallee();
    if (callee->getNumParams() != call->getNumArgs()) {
      call->dump();
      throw(std::runtime_error("FuncNotMatch"));
    }
    StackFrame newFrame;
    for (auto arg = call->arg_begin(); arg != call->arg_end(); arg++) {
      newFrame.bindDecl(callee->getParamDecl(arg - call->arg_begin()), mStack.back().getStmtVal(*arg));
    }
    mStack.push_back(newFrame);
  }

  void deleteFuncStack(CallExpr *call) {
    auto retVal = mStack.back().retValue;
    mStack.pop_back();
    mStack.back().bindStmt(call, retVal);
  }

  void handleRetStmt(ReturnStmt *retstmt) {
    mStack.back().retValue = mStack.back().getStmtVal(retstmt->getRetValue());
  }

  bool checkCondition(Expr *expr) {
    if (mStack.back().getStmtVal(expr))
      return true;
    else return false;
  }

  void handleUnaryOperator(UnaryOperator *uop) {
    mStack.back().bindStmt(uop, -1 * mStack.back().getStmtVal(uop->getSubExpr()));
  }
};

#endif // ENVIRONMENT_H_INCLUDED
