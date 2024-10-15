#ifndef ENVIRONMENT_H_INCLUDED
#define ENVIRONMENT_H_INCLUDED

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"
#include <iostream>
#include <stdexcept>

using namespace clang;

class StackFrame {
  std::map<Decl *, long> mVars;
  std::map<Stmt *, long> mExprs;
  std::map<Decl *, std::vector<long>> mArrays;

public:
  long retValue = 0;
  StackFrame() : mVars(), mExprs() {}

  void bindDecl(Decl *decl, long val) { mVars[decl] = val; }
  long getDeclVal(Decl *decl) {
    auto rel = mVars.find(decl);
    if (rel == mVars.end()) {
      decl->dump();
      llvm::errs() << "Address: " << decl << "\n";
      throw(std::runtime_error("VarNotFound"));
    }
    return rel->second;
  }
  std::optional<long> tryGetDeclVal(Decl *decl) {
    auto rel = mVars.find(decl);
    if (rel == mVars.end()) {
      return {};
    }
    return rel->second;
  }
  void bindDeclArray(Decl *decl, long num) {
    mArrays[decl] = std::vector<long>(num);
  }
  void setArrayVal(Decl *decl, long val, long index) {
    mArrays[decl][index] = val;
  }
  long getArrayVal(Decl *decl, long index) {
    auto rel = mArrays.find(decl);
    if (rel == mArrays.end()) {
      decl->dump();
      llvm::errs() << "Address: " << decl << "\n";
      throw(std::runtime_error("ArrayNotFound"));
    }
    return rel->second.at(index);
  }
  void bindStmt(Stmt *stmt, long val) { mExprs[stmt] = val; }
  long getStmtVal(Stmt *stmt) {
    auto rel = mExprs.find(stmt);
    if (rel == mExprs.end()) {
      stmt->dump();
      llvm::errs() << "Address: " << stmt << "\n";
      throw(std::runtime_error("StmtNotFound"));
    }
    return rel->second;
  }
  std::optional<long> tryGetStmtVal(Stmt *stmt) {
    auto rel = mExprs.find(stmt);
    if (rel == mExprs.end()) {
      return {};
    }
    return rel->second;
  }
};

class Environment {
  std::vector<StackFrame> mStack;

  FunctionDecl *mFree;
  FunctionDecl *mMalloc;
  FunctionDecl *mInput;
  FunctionDecl *mOutput;

  FunctionDecl *mEntry;

  std::map<Decl *, long> gVars;

  long getGlobalDeclVal(Decl *decl) {
    if (gVars.find(decl) == gVars.end()) {
      decl->dump();
      throw(std::runtime_error("GlobalDeclNotFound"));
    }
    return gVars.find(decl)->second;
  }

  Decl *getBaseDecl(Expr *expr) {
    if (DeclRefExpr *declRef = dyn_cast<DeclRefExpr>(expr)) {
      return declRef->getDecl();
    } else if (ImplicitCastExpr *castExpr = dyn_cast<ImplicitCastExpr>(expr)) {
      return getBaseDecl(castExpr->getSubExpr());
    } else {
      return nullptr;
    }
  }

public:
  Environment()
      : mStack(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL),
        mEntry(NULL) {}

  void handleGlobalDecl(TranslationUnitDecl *unit,
                        std::vector<VarDecl *> &gVarDeclList) {
    mStack.push_back(StackFrame());
    for (TranslationUnitDecl::decl_iterator i = unit->decls_begin(),
                                            e = unit->decls_end();
         i != e; ++i) {
      if (auto *fdecl = dyn_cast<FunctionDecl>(*i)) {
        if (fdecl->getName().equals("FREE")) {
          mFree = fdecl;
        } else if (fdecl->getName().equals("MALLOC")) {
          mMalloc = fdecl;
        } else if (fdecl->getName().equals("GET")) {
          mInput = fdecl;
        } else if (fdecl->getName().equals("PRINT")) {
          mOutput = fdecl;
        } else if (fdecl->getName().equals("main")) {
          mEntry = fdecl;
        }
      } else if (VarDecl *vdecl = dyn_cast<VarDecl>(*i)) {
        gVarDeclList.push_back(vdecl);
      }
    }
  }

  void initGlobalVar(std::vector<VarDecl *> &gVarDeclList) {
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
    Expr *left = bop->getLHS(), *right = bop->getRHS();
    long valLeft = mStack.back().getStmtVal(left),
        valRight = mStack.back().getStmtVal(right);

    switch (bop->getOpcode()) {
    case BO_Assign: {
      mStack.back().bindStmt(left, valRight);
      if (DeclRefExpr *declexpr = dyn_cast<DeclRefExpr>(left)) {
        auto decl = declexpr->getFoundDecl();
        mStack.back().bindDecl(decl, valRight);
      } else if (ArraySubscriptExpr *array =
                     dyn_cast<ArraySubscriptExpr>(left)) {
        auto decl = getBaseDecl(array->getBase());
        auto index = mStack.back().getStmtVal(array->getIdx());
        mStack.back().setArrayVal(decl, valRight, index);
      } else if (UnaryOperator *ptr = dyn_cast<UnaryOperator>(left)) {
        if (ptr->getOpcode() == UO_Deref) {
          auto decl = getBaseDecl(ptr->getSubExpr());
          long *address = (long*) mStack.back().getDeclVal(decl);
          *address = valRight;
        } else {
          throw(std::runtime_error("UnsupportedUnaryOp"));
        }
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
      throw(std::runtime_error("UnsupportedBinaryOp"));
    }
    }
  }

  void handleUnaryOperator(UnaryOperator *uop) {
    switch (uop->getOpcode()) {
    case UO_Deref: {
      mStack.back().bindStmt(
          uop, *((long *)mStack.back().getStmtVal(uop->getSubExpr())));
      break;
    }
    case UO_Minus: {
      mStack.back().bindStmt(uop,
                             -1 * mStack.back().getStmtVal(uop->getSubExpr()));
    }
    default: {
      throw(std::runtime_error("UnsupportedUnaryOp"));
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
        if (vardecl->getType()->isArrayType()) {
          const ConstantArrayType *array =
              dyn_cast<ConstantArrayType>(vardecl->getType().getTypePtr());
          long size = array->getSize().getSExtValue();
          mStack.back().bindDeclArray(vardecl, size);
        } else {
          mStack.back().bindDecl(
              vardecl, vardecl->hasInit()
                           ? mStack.back().getStmtVal(vardecl->getInit())
                           : 0);
        }
      }
    }
  }

  void handleDeclRef(DeclRefExpr *declref) {
    if (declref->getType()->isIntegerType() || declref->getType()->isPointerType()) {
      Decl *decl = declref->getFoundDecl();

      long val;
      if (auto valOption = mStack.back().tryGetDeclVal(decl)) {
        val = valOption.value();
      } else {
        val = getGlobalDeclVal(decl);
      }
      mStack.back().bindStmt(declref, val);
    }
  }

  void handleCast(CastExpr *castexpr) {
    if (!castexpr->getType()->isFunctionPointerType()) {
      Expr *expr = castexpr->getSubExpr();
      long val = mStack.back().getStmtVal(expr);
      mStack.back().bindStmt(castexpr, val);
    }
  }

  bool tryCallBuiltInFunc(CallExpr *callexpr) {
    auto callee = callexpr->getDirectCallee();
    if (callee == mInput) {
      long val;
      llvm::errs() << "Please Input an Integer Value : \n";
      std::cin >> val;
      mStack.back().bindStmt(callexpr, val);
    } else if (callee == mOutput) {
      Expr *decl = callexpr->getArg(0);
      auto val = mStack.back().getStmtVal(decl);
      llvm::errs() << val << '\n';
      mStack.back().bindStmt(callexpr, 0);
    } else if (callee == mMalloc) {
      Expr *decl = callexpr->getArg(0);
      auto val = mStack.back().getStmtVal(decl);
      mStack.back().bindStmt(callexpr, (long)malloc(val));
    } else if (callee == mFree) {
      Expr *decl = callexpr->getArg(0);
      auto val = mStack.back().getStmtVal(decl);
      free((void *)val);
      mStack.back().bindStmt(callexpr, 0);
    } else
      return false;
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
      newFrame.bindDecl(callee->getParamDecl(arg - call->arg_begin()),
                        mStack.back().getStmtVal(*arg));
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
    if (mStack.back().getStmtVal(expr)) {
      return true;
    } else {
      return false;
    }
  }

  void handleArrayExpr(ArraySubscriptExpr *arrayexpr) {
    auto decl = getBaseDecl(arrayexpr->getBase());
    auto index = mStack.back().getStmtVal(arrayexpr->getIdx());
    mStack.back().bindStmt(arrayexpr, mStack.back().getArrayVal(decl, index));
  }

  void handleSizeOf(UnaryExprOrTypeTraitExpr *sizeofexpr) {
    mStack.back().bindStmt(sizeofexpr, 8);
  }
};

#endif // ENVIRONMENT_H_INCLUDED
