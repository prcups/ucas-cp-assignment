#include "clang/AST/ASTConsumer.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

#include "Environment.h"

class InterpreterVisitor : public EvaluatedExprVisitor<InterpreterVisitor> {
public:
	explicit InterpreterVisitor (const ASTContext &context, Environment *env)
		: EvaluatedExprVisitor (context), mEnv (env) {}
	virtual ~InterpreterVisitor() {}

	virtual void VisitBinaryOperator (BinaryOperator *bop) {
		VisitStmt (bop);
		mEnv->handleBinOp (bop);
	}
	virtual void VisitDeclRefExpr (DeclRefExpr *expr) {
		VisitStmt (expr);
		mEnv->handleDeclRef (expr);
	}
	virtual void VisitCastExpr (CastExpr *expr) {
		VisitStmt (expr);
		mEnv->handleCast (expr);
	}
	virtual void VisitCallExpr (CallExpr *call) {
		VisitStmt (call);
		if (!mEnv->tryCallBuiltInFunc (call)) {
			mEnv->createFuncStack (call);
			Visit (call->getDirectCallee()->getBody());
			mEnv->deleteFuncStack (call);
		}
	}
	virtual void VisitDeclStmt (DeclStmt *declstmt) {
		VisitStmt (declstmt);
		mEnv->handleDeclStmt (declstmt);
	}
	virtual void VisitIntegerLiteral (IntegerLiteral *integer) {
		mEnv->handleIntliteral (integer);
	}
	virtual void VisitReturnStmt (ReturnStmt *retstmt) {
		VisitStmt (retstmt);
		mEnv->handleRetStmt (retstmt);
	}
	virtual void VisitIfStmt (IfStmt *ifstmt) {
		if (ifstmt->hasInitStorage()) Visit (ifstmt->getInit());
		auto cond = ifstmt->getCond();
		Visit (cond);
		if (mEnv->checkCondition (cond)) Visit (ifstmt->getThen());
		else if (ifstmt->hasElseStorage()) Visit (ifstmt->getElse());
	}
	virtual void VisitUnaryOperator (UnaryOperator *uop) {
		VisitStmt (uop);
		mEnv->handleUnaryOperator (uop);
	}
	virtual void VisitForStmt (ForStmt *forstmt) {
		if (forstmt->getInit()) Visit (forstmt->getInit());
		auto cond = forstmt->getCond();
		Visit (cond);
		while (mEnv->checkCondition (cond)) {
			Visit (forstmt->getBody());
			Visit (forstmt->getInc());
			Visit (cond);
		}
	}
	virtual void VisitWhileStmt (WhileStmt *whilestmt) {
		auto cond = whilestmt->getCond();
		Visit (cond);
		while (mEnv->checkCondition (cond)) {
			Visit (whilestmt->getBody());
			Visit (cond);
		}
	}
	virtual void VisitArraySubscriptExpr (ArraySubscriptExpr *arrayexpr) {
		VisitStmt (arrayexpr);
		mEnv->handleArrayExpr (arrayexpr);
	}

private:
	Environment *mEnv;
};

class InterpreterConsumer : public ASTConsumer {
public:
	explicit InterpreterConsumer (const ASTContext &context)
		: mEnv(), mVisitor (context, &mEnv) {}
	virtual ~InterpreterConsumer() {}

	virtual void HandleTranslationUnit (clang::ASTContext &Context) {
		TranslationUnitDecl *decl = Context.getTranslationUnitDecl();
		std::vector <VarDecl *> gVarDeclList;
		mEnv.handleGlobalDecl (decl, gVarDeclList);

		for (auto vdecl : gVarDeclList) {
			if (vdecl->hasInit()) {
				mVisitor.Visit (vdecl->getInit());
			}
		}
		mEnv.initGlobalVar (gVarDeclList);

		auto *entry = mEnv.getEntry();
		mVisitor.VisitStmt (entry->getBody());
	}

private:
	Environment mEnv;
	InterpreterVisitor mVisitor;
};

class InterpreterClassAction : public ASTFrontendAction {
public:
	virtual std::unique_ptr<clang::ASTConsumer>
	CreateASTConsumer (clang::CompilerInstance &Compiler, llvm::StringRef InFile) {
		return std::unique_ptr<clang::ASTConsumer> (
		           new InterpreterConsumer (Compiler.getASTContext()));
	}
};

int main (int argc, char **argv) {
	if (argc > 1) {
		clang::tooling::runToolOnCode (
		    std::unique_ptr<clang::FrontendAction> (new InterpreterClassAction),
		    argv[1]);
	}
}
