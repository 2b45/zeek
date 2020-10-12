// See the file "COPYING" in the main distribution directory for copyright.

// Values used in ZAM execution, and also for representing records and
// vectors during interpreter execution.

#pragma once

#include <unordered_set>

#include "Dict.h"
#include "Expr.h"
#include "IntrusivePtr.h"

ZEEK_FORWARD_DECLARE_NAMESPACED(StringVal, zeek);
ZEEK_FORWARD_DECLARE_NAMESPACED(AddrVal, zeek);
ZEEK_FORWARD_DECLARE_NAMESPACED(SubNetVal, zeek);
ZEEK_FORWARD_DECLARE_NAMESPACED(File, zeek);
ZEEK_FORWARD_DECLARE_NAMESPACED(Func, zeek);
ZEEK_FORWARD_DECLARE_NAMESPACED(ListVal, zeek);
ZEEK_FORWARD_DECLARE_NAMESPACED(OpaqueVal, zeek);
ZEEK_FORWARD_DECLARE_NAMESPACED(PatternVal, zeek);
ZEEK_FORWARD_DECLARE_NAMESPACED(TableVal, zeek);
ZEEK_FORWARD_DECLARE_NAMESPACED(RecordVal, zeek);
ZEEK_FORWARD_DECLARE_NAMESPACED(VectorVal, zeek);
ZEEK_FORWARD_DECLARE_NAMESPACED(Type, zeek);
ZEEK_FORWARD_DECLARE_NAMESPACED(Val, zeek);

namespace zeek {

// Only needed for compiled code.
class IterInfo;

typedef std::vector<zeek::IntrusivePtr<zeek::Val>> val_vec;

// A bit of this mirrors BroValUnion, but BroValUnion captures low-level
// representation whereas we aim to keep Val structure for more complex
// Val's.
//
// Ideally we'd use IntrusivePtr's for memory management, but we can't
// given we have a union and thus on destruction C++ doesn't know which
// member flavor to destruct.
//
// Note that a ZAMValUnion by itself is ambiguous: it doesn't track its
// type.  This makes them consume less memory and cheaper to copy.  It
// does however require a separate way to determine the type.  Generally
// this is doable using surrounding context, or can be statically determined
// in the case of optimization/compilation.
//
// An alternative would be to use std::variant, but (1) it tracks the
// variant type, and (2) it won't allow access to the managed_val member,
// which not only simplifies memory management but also is required for
// sharing of ZAM frame slots.
union ZAMValUnion {
	// Constructor for hand-populating the values.
	ZAMValUnion() { managed_val = nullptr; }

	// Construct from a given Bro value with a given type.
	ZAMValUnion(zeek::IntrusivePtr<zeek::Val> v, const zeek::IntrusivePtr<zeek::Type>& t);

	// True if when interpreting the value as having the given type,
	// it's a nil pointer.
	bool IsNil(const zeek::IntrusivePtr<zeek::Type>& t) const;

	// Convert to a Bro value.
	zeek::IntrusivePtr<zeek::Val> ToVal(const zeek::IntrusivePtr<zeek::Type>& t) const;

	// Used for bool, int, enum.
	bro_int_t int_val;

	// Used for count, counter, port.
	bro_uint_t uint_val;

	// Used for double, time, interval.
	double double_val;

	// The types are all variants of Val, zeek::Type, or more fundamentally
	// zeek::Obj.  They are raw pointers rather than IntrusivePtr's because
	// unions can't contain the latter.  For memory management, we use
	// Ref/Unref.
	zeek::StringVal* string_val;
	zeek::AddrVal* addr_val;
	zeek::SubNetVal* subnet_val;
	zeek::File* file_val;
	zeek::Func* func_val;
	zeek::ListVal* list_val;
	zeek::OpaqueVal* opaque_val;
	zeek::PatternVal* re_val;
	zeek::TableVal* table_val;
	zeek::RecordVal* record_val;
	zeek::VectorVal* vector_val;
	zeek::Type* type_val;

	// Used for direct "any" values.
	zeek::Val* any_val;

	// Used for the compiler to hold opaque items.  Memory management
	// is explicit in the operations accessing it.
	val_vec* vvec;

	// Used by the compiler for managing "for" loops.  Implicit
	// memory management.
	IterInfo* iter_info;

	// Used for generic access to managed (reference-counted) objects.
	zeek::Obj* managed_val;
};

// True if a given type is one for which we manage the associated
// memory internally.
bool IsManagedType(const zeek::IntrusivePtr<zeek::Type>& t);

// Deletes a managed value.
inline void DeleteManagedType(ZAMValUnion& v)
	{
	Unref(v.managed_val);
	}

// The following can be set to point to a boolean that will be set
// to true if a run-time error associated with ZAMValUnion's occurs.
//
// We use this somewhat clunky coupling to enable isolating ZVal from
// ZAM compiler specifics.
extern bool* zval_error_addr;

typedef std::vector<ZAMValUnion> ZVU_vec;

class ZAM_vector {
public:
	// In the following, we use a bare pointer for the VectorVal
	// due to tricky memory management concerns, namely that ZAM_vector's
	// point to their VectorVal's and VectorVal's point to their
	// ZAM_vector's.
	ZAM_vector(zeek::VectorVal* _vv, zeek::IntrusivePtr<zeek::Type> yt, int n = 0)
	: zvec(n)
		{
		vv = _vv;

		if ( yt )
			{
			managed_yt = IsManagedType(yt) ? yt : nullptr;
			general_yt = std::move(yt);
			}
		else
			general_yt = managed_yt = nullptr;
		}

	~ZAM_vector()
		{
		if ( managed_yt )
			DeleteMembers();
		}

	zeek::IntrusivePtr<zeek::Type> YieldType() 		{ return general_yt; }
	const zeek::IntrusivePtr<zeek::Type>& YieldType() const	{ return general_yt; }

	void SetYieldType(zeek::IntrusivePtr<zeek::Type> yt)
		{
		if ( ! general_yt || general_yt->Tag() == zeek::TYPE_ANY ||
		     general_yt->Tag() == zeek::TYPE_VOID )
			{
			managed_yt = IsManagedType(yt) ? yt : nullptr;
			general_yt = std::move(yt);
			}
		}

	bool IsManagedYieldType() const	{ return managed_yt != nullptr; }

	unsigned int Size() const	{ return zvec.size(); }

	const ZVU_vec& ConstVec() const	{ return zvec; }
	ZVU_vec& ModVec()		{ return zvec; }

	// Used when access to the underlying vector is for initialization.
	ZVU_vec& InitVec(unsigned int size)
		{
		// Note, could use reserve() here to avoid pre-initializing
		// the elements.  It's not clear to me whether that suffices
		// for being able to safely assign to elements beyond the
		// nominal end of the vector rather than having to use
		// push_back.  Seems it ought to ...
		zvec.resize(size);
		return zvec;
		}

	ZAMValUnion& Lookup(int n)
		{
		return zvec[n];
		}

	// Sets the given element, with accompanying memory management.
	void SetElement(unsigned int n, ZAMValUnion& v)
		{
		if ( zvec.size() <= n )
			GrowVector(n + 1);

		if ( managed_yt )
			DeleteManagedType(zvec[n]);

		zvec[n] = v;
		}

	// Sets the given element to a copy of the given ZAMValUnion.
	// The difference between this and SetElement() is that here
	// we do Ref()'ing of the underlying value if it's a managed
	// type.  This isn't necessary for the case where 'v' has been
	// newly constructed, but is necessary if we're copying an
	// existing 'v'.
	//
	// Returns true on success, false if 'v' has never been set to
	// a value (which we can only tell for managed types).
	bool CopyElement(unsigned int n, const ZAMValUnion& v)
		{
		if ( zvec.size() <= n )
			GrowVector(n + 1);

		if ( managed_yt )
			return SetManagedElement(n, v);

		zvec[n] = v;
		return true;
		}

	void Insert(unsigned int index, ZAMValUnion& element)
		{
		ZVU_vec::iterator it;

		if ( index < zvec.size() )
			{
			it = std::next(zvec.begin(), index);
			if ( managed_yt )
				DeleteIfManaged(index);
			}
		else
			it = zvec.end();

		zvec.insert(it, element);
		}

	void Remove(unsigned int index)
		{
		if ( managed_yt )
			DeleteIfManaged(index);

		auto it = std::next(zvec.begin(), index);
		zvec.erase(it);
		}

	void Resize(unsigned int new_num_elements)
		{
		zvec.resize(new_num_elements);
		}

protected:
	bool SetManagedElement(int n, const ZAMValUnion& v);
	void GrowVector(int size);

	void DeleteMembers();

	// Deletes the given element if necessary.
	void DeleteIfManaged(int n)
		{
		if ( managed_yt )
			DeleteManagedType(zvec[n]);
		}

	// The underlying set of ZAM values.
	ZVU_vec zvec;

	// The associated main value.  A raw pointer for reasons explained
	// above.
	zeek::VectorVal* vv;

	// The yield type of the vector elements.  Only non-nil if they
	// are managed types.
	zeek::IntrusivePtr<zeek::Type> managed_yt;

	// The yield type of the vector elements, whether or not it's
	// managed.  We use a lengthier name to make sure we never
	// confuse this with managed_yt.
	zeek::IntrusivePtr<zeek::Type> general_yt;
};

class ZAM_record {
public:
	// Similarly to ZAM_vector, we use a bare pointer for the RecordVal
	// to simplify the memory management given the pointer cycle.
	ZAM_record(zeek::RecordVal* _v, zeek::IntrusivePtr<zeek::RecordType> _rt);

	~ZAM_record()
		{
		DeleteManagedMembers();
		}

	unsigned int Size() const	{ return zvec.size(); }

	void Assign(unsigned int field, ZAMValUnion v)
		{
		if ( IsInRecord(field) && IsManaged(field) )
			Unref(zvec[field].managed_val);

		zvec[field] = v;
		is_in_record[field] = true;
		}

	// Direct access to fields for assignment.  *The caller
	// is expected to deal with memory management.*
	ZAMValUnion& SetField(unsigned int field)
		{
		is_in_record[field] = true;
		return zvec[field];
		}

	// Used for a slight speed gain in RecordType::Create().
	void RefField(unsigned int field)
		{ zeek::Ref(zvec[field].managed_val); }

	ZAMValUnion& Lookup(unsigned int field, bool& error)
		{
		error = false;

		if ( ! IsInRecord(field) && ! SetToDefault(field) )
			error = true;

		return zvec[field];
		}

	zeek::IntrusivePtr<zeek::Val> NthField(unsigned int field)
		{
		bool error;
		auto f = Lookup(field, error);

		if ( error )
			return nullptr;

		return f.ToVal(FieldType(field));
		}

	void DeleteField(unsigned int field)
		{
		if ( IsInRecord(field) && IsManaged(field) )
			Unref(zvec[field].managed_val);

		is_in_record[field] = false;
		}

	bool HasField(unsigned int field)
		{
		return IsInRecord(field);
		}

	bool IsInRecord(unsigned int offset) const
		{ return is_in_record[offset]; }
	bool IsManaged(unsigned int offset) const
		{ return is_managed[offset]; }

protected:
	friend class zeek::RecordVal;

	zeek::IntrusivePtr<zeek::Type> FieldType(int field) const
		{ return rt->GetFieldType(field); }

	bool SetToDefault(unsigned int field);

	void Grow(unsigned int new_size)
		{
		zvec.resize(new_size);
		}

	// Removes the given field.
	void Delete(unsigned int field)
		{ DeleteManagedType(zvec[field]); }

	void DeleteManagedMembers();

	// The underlying set of ZAM values.
	ZVU_vec zvec;

	// The associated main value.
	zeek::RecordVal* rv;

	// And a handy pointer to its type.
	zeek::IntrusivePtr<zeek::RecordType> rt;

	// Whether a given field exists (for optional fields).
	std::vector<bool> is_in_record;

	// Whether a given field requires explicit memory management.
	const std::vector<bool>& is_managed;
};

}
