// See the file "COPYING" in the main distribution directory for copyright.

#pragma once

// Base class for Zeek statements.  We maintain it separately from
// the bulk of Stmt.h to allow Expr.h to include it, necessary for
// Expr.h to use IntrusivePtr<Stmt>.

#include "Obj.h"
#include "IntrusivePtr.h"
#include "StmtEnums.h"
#include "TraverseTypes.h"
#include "util.h"

class Val;

class StmtList;
class CompositeHash;
class EventExpr;
class ListExpr;
class ForStmt;
class WhileStmt;
class ReturnStmt;
class CatchReturnStmt;
class IfStmt;
class ExprStmt;
class AddStmt;
class SwitchStmt;
class InitStmt;
class WhenStmt;
class PrintStmt;
class Frame;

class Reducer;
class Inliner;
class Compiler;
class CompiledStmt;
class ZBody;

class Stmt : public BroObj {
public:
	BroStmtTag Tag() const	{ return tag; }

	~Stmt() override;

	virtual IntrusivePtr<Val> Exec(Frame* f, stmt_flow_type& flow) const = 0;

	Stmt* Ref()			{ ::Ref(this); return this; }

	bool SetLocationInfo(const Location* loc) override
		{ return Stmt::SetLocationInfo(loc, loc); }
	bool SetLocationInfo(const Location* start, const Location* end) override;

	// True if the statement has no side effects, false otherwise.
	virtual bool IsPure() const;

	// True if the statement is in reduced form.
	virtual bool IsReduced(Reducer* c) const;

	Stmt* Reduce(Reducer* c);
	virtual Stmt* DoReduce(Reducer* c)	{ return this->Ref(); }

	// True if there's definitely no control flow past the statement.
	// The argument governs whether to ignore "break" statements, given
	// they mean two different things depending on whether they're in
	// a loop or a switch.  Also, if we want to know whether flow reaches
	// the *end* of a loop, then we also want to ignore break's, as
	// in that case, they do lead to flow reaching the end.
	virtual bool NoFlowAfter(bool ignore_break) const
		{ return false; }

	// Returns a duplicate of the statement so that modifications
	// can be made to statements from inlining function bodies - or
	// to the originals - without affecting other instances.
	//
	// It's tempting to think that there are some statements that
	// are safe to share across multiple functions and could just
	// return references to themselves - but since we associate
	// information such as reaching-defs with statements, even these
	// need to be duplicated.
	virtual IntrusivePtr<Stmt> Duplicate() = 0;

	virtual void Inline(Inliner* inl)	{ }

	// Compile the statement and return its opaque handle.  (For
	// statement blocks, this is whatever the compiler returns
	// when asked.)
	virtual const CompiledStmt Compile(Compiler* c) const;

#undef ACCESSOR
#define ACCESSOR(tag, ctype, name) \
        ctype* name() \
                { \
                CHECK_TAG(Tag(), tag, "Stmt::ACCESSOR", stmt_name) \
                return (ctype*) this; \
                }

#undef CONST_ACCESSOR
#define CONST_ACCESSOR(tag, ctype, name) \
        const ctype* name() const \
                { \
                CHECK_TAG(Tag(), tag, "Stmt::CONST_ACCESSOR", stmt_name) \
                return (const ctype*) this; \
                }

#undef ACCESSORS
#define ACCESSORS(tag, ctype, name) \
	ACCESSOR(tag, ctype, name) \
	CONST_ACCESSOR(tag, ctype, name)

	ACCESSORS(STMT_LIST, StmtList, AsStmtList)
	ACCESSORS(STMT_FOR, ForStmt, AsForStmt)

	CONST_ACCESSOR(STMT_WHILE, WhileStmt, AsWhileStmt)
	CONST_ACCESSOR(STMT_RETURN, ReturnStmt, AsReturnStmt)
	CONST_ACCESSOR(STMT_CATCH_RETURN, CatchReturnStmt, AsCatchReturnStmt)
	CONST_ACCESSOR(STMT_IF, IfStmt, AsIfStmt)
	CONST_ACCESSOR(STMT_EXPR, ExprStmt, AsExprStmt)
	CONST_ACCESSOR(STMT_ADD, AddStmt, AsAddStmt)
	CONST_ACCESSOR(STMT_SWITCH, SwitchStmt, AsSwitchStmt)
	CONST_ACCESSOR(STMT_WHEN, WhenStmt, AsWhenStmt)
	CONST_ACCESSOR(STMT_PRINT, PrintStmt, AsPrintStmt)
	CONST_ACCESSOR(STMT_INIT, InitStmt, AsInitStmt)
	CONST_ACCESSOR(STMT_COMPILED, ZBody, AsZBody)

#undef ACCESSORS
#undef ACCESSOR
#undef CONST_ACCESSOR

	void RegisterAccess() const	{ last_access = network_time; access_count++; }
	void AccessStats(ODesc* d) const;
	uint32_t GetAccessCount() const { return access_count; }

	void Describe(ODesc* d) const final;

	virtual void IncrBPCount()	{ ++breakpoint_count; }
	virtual void DecrBPCount();

	virtual unsigned int BPCount() const	{ return breakpoint_count; }

	virtual TraversalCode Traverse(TraversalCallback* cb) const = 0;

	const Stmt* Original() const
		{
		if ( original )
			return original->Original();
		else
			return this;
		}

protected:
	Stmt()	{ original = nullptr; }
	explicit Stmt(BroStmtTag arg_tag);

	void SetOriginal(Stmt* _orig)
		{
		if ( ! original )
			original = _orig->Ref();
		}

	void AddTag(ODesc* d) const;
	virtual void StmtDescribe(ODesc* d) const;
	void DescribeDone(ODesc* d) const;

	// Helper function called after reductions to perform
	// canonical actions.
	Stmt* TransformMe(Stmt* new_me, Reducer* c);

	// The original statement from which this statement was
	// reduced, if any.  Non-const so it can be Unref()'d.
	Stmt* original;

	BroStmtTag tag;
	int breakpoint_count;	// how many breakpoints on this statement

	// FIXME: Learn the exact semantics of mutable.
	mutable double last_access;	// time of last execution
	mutable uint32_t access_count;	// number of executions
};
