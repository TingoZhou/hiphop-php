/*
   +----------------------------------------------------------------------+
   | HipHop for PHP                                                       |
   +----------------------------------------------------------------------+
   | Copyright (c) 2010-2013 Facebook, Inc. (http://www.facebook.com)     |
   | Copyright (c) 1998-2010 Zend Technologies Ltd. (http://www.zend.com) |
   +----------------------------------------------------------------------+
   | This source file is subject to version 2.00 of the Zend license,     |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.zend.com/license/2_00.txt.                                |
   | If you did not receive a copy of the Zend license and are unable to  |
   | obtain it through the world-wide-web, please send a note to          |
   | license@zend.com so we can mail you a copy immediately.              |
   +----------------------------------------------------------------------+
*/

#ifndef incl_HPHP_ARRAY_ITERATOR_H_
#define incl_HPHP_ARRAY_ITERATOR_H_

#include "hphp/runtime/base/types.h"
#include "hphp/runtime/base/smart_ptr.h"
#include "hphp/runtime/base/complex_types.h"
#include "hphp/runtime/base/hphp_array.h"
#include "hphp/util/min_max_macros.h"

namespace HPHP {
///////////////////////////////////////////////////////////////////////////////

struct TypedValue;
class c_Vector;
class c_Map;
class c_StableMap;
class c_Set;
class c_Pair;
struct Iter;

/**
 * An iteration normally looks like this:
 *
 *   for (ArrayIter iter(data); iter; ++iter) {
 *     ...
 *   }
 */

/**
 * Iterator for an immutable array.
 */
class ArrayIter {
 public:
  enum class IterKind {
    Undefined = 0,
    Array,
    Iterator, // for objects that implement Iterator or
              // IteratorAggregate
    Pair,
    Vector,
    Map,
    StableMap,
    Set
  };

  static const std::string typeAsString(IterKind iterKind) {
    switch (iterKind) {
      case IterKind::Undefined:
        return "Undefined";
      case IterKind::Array:
        return "Array";
      case IterKind::Iterator:
        return "Iterator";
      case IterKind::Vector:
        return "Vector";
      case IterKind::Map:
        return "Map";
      case IterKind::StableMap:
        return "StableMap";
      case IterKind::Set:
        return "Set";
      case IterKind::Pair:
        return "Pair";
    }
    assert(false);
    return "Unknown";
  }

  static size_t getOffsetOfIterKind() {
    // For assembly linkage.
    return offsetof(ArrayIter, m_ikind);
  }

  /**
   * Constructors.
   */
  ArrayIter();
  explicit ArrayIter(const ArrayData* data);

  enum NoInc { noInc = 0 };
  // Special constructor used by the VM. This constructor does not increment
  // the refcount of the specified array.
  ArrayIter(const ArrayData* data, NoInc) {
    setArrayData(data);
    if (data) {
      m_pos = data->iter_begin();
    } else {
      m_pos = ArrayData::invalid_index;
    }
  }
  // This is also a special constructor used by the VM. This constructor
  // doesn't increment the array's refcount and assumes that the array is not
  // empty.
  enum NoIncNonNull { noIncNonNull = 0 };
  ArrayIter(const HphpArray* data, NoIncNonNull) {
    assert(data);
    setArrayData(data);
    m_pos = data->getIterBegin();
  }
  explicit ArrayIter(CArrRef array);
  void reset();

 private:
  // not defined.
  // Either use ArrayIter(const ArrayData*) or
  //            ArrayIter(const HphpArray*, NoIncNonNull)
  explicit ArrayIter(const HphpArray*);
  template <bool incRef>
  void objInit(ObjectData* obj);

 public:
  explicit ArrayIter(ObjectData* obj);
  ArrayIter(ObjectData* obj, NoInc);
  enum TransferOwner { transferOwner };
  ArrayIter(Object& obj, TransferOwner);

  ~ArrayIter();

  explicit operator bool() { return !end(); }
  void operator++() { next(); }

  bool end() {
    if (LIKELY(hasArrayData())) {
      return m_pos == ArrayData::invalid_index;
    }
    return endHelper();
  }
  void next() {
    if (LIKELY(hasArrayData())) {
      const ArrayData* ad = getArrayData();
      assert(ad);
      assert(m_pos != ArrayData::invalid_index);
      m_pos = ad->iter_advance(m_pos);
      return;
    }
    nextHelper();
  }

  Variant first() {
    if (LIKELY(hasArrayData())) {
      const ArrayData* ad = getArrayData();
      assert(ad);
      assert(m_pos != ArrayData::invalid_index);
      return ad->getKey(m_pos);
    }
    return firstHelper();
  }
  Variant second();
  void second(Variant& v) {
    if (LIKELY(hasArrayData())) {
      const ArrayData* ad = getArrayData();
      assert(ad);
      assert(m_pos != ArrayData::invalid_index);
      v = ad->getValueRef(m_pos);
      return;
    }
    secondHelper(v);
  }
  CVarRef secondRef();

  void nvFirst(TypedValue* out) {
    const ArrayData* ad = getArrayData();
    assert(ad && m_pos != ArrayData::invalid_index);
    const_cast<ArrayData*>(ad)->nvGetKey(out, m_pos);
  }
  TypedValue* nvSecond() {
    const ArrayData* ad = getArrayData();
    assert(ad && m_pos != ArrayData::invalid_index);
    return const_cast<ArrayData*>(ad)->nvGetValueRef(m_pos);
  }

  bool hasArrayData() {
    return !((intptr_t)m_data & 1);
  }
  bool hasCollection() {
    return (!hasArrayData() && getObject()->isCollection());
  }
  bool hasIteratorObj() {
    return (!hasArrayData() && !getObject()->isCollection());
  }

  //
  // Specialized iterator for collections. Used via JIT
  //

  /**
   * Fixed is used for collections that are immutable in size.
   */
  enum class Fixed {};
  /**
   * Versionable is used for collections that are mutable and throw if
   * an insertion or deletion is made to the collection while iterating.
   */
  enum class Versionable {};
  /**
   * VersionableSparse is used for collections that are mutable and throw if
   * an insertion or deletion is made to the collection while iterating.
   * Moreover the collection elements are accessed via an iterator exposed
   * by the collection class.
   */
  enum class VersionableSparse {};

  /**
   * Whether the key needs to be refcount'ed or not. For Vector, Pair and
   * Set there is no reason to refcount the key as that is an int (Pair,
   * Vecotr) or null (Set).
   */
  enum class RefCountKey { DontRefcount, Refcount };

  // Constructors
  template<class Tuplish>
  ArrayIter(Tuplish* coll, IterKind iterKind, Fixed);
  template<class Vectorish>
  ArrayIter(Vectorish* coll, IterKind iterKind, Versionable);
  template<class Mappish>
  ArrayIter(Mappish* coll, IterKind iterKind, VersionableSparse);

  // Iterator init and next functions.  These methods are modeled after the
  // corresponding HHIR opcodes IterInit, IterIntK, and so on.  The idea is
  // to have ArrayIter construct an iterator, and to map the HHIR instructions
  // directly to these methods.
  // Todo (#2624480) - We want to create specialized IterInit and IterNext
  // opcodes for every kind of collection and array shape, and possibly teach
  // the JIT to inline some of them.
  // For now only collections go through these helpers.
  template<class Tuplish>
  int64_t iterInit(Fixed, TypedValue* valOut);
  template<class Vectorish>
  int64_t iterInit(Versionable, TypedValue* valOut);
  template<class Mappish>
  int64_t iterInit(VersionableSparse, TypedValue* valOut);
  template<class Tuplish>
  int64_t iterInitK(Fixed, TypedValue* valOut, TypedValue* keyOut);
  template<class Vectorish>
  int64_t iterInitK(Versionable, TypedValue* valOut, TypedValue* keyOut);
  template<class Mappish>
  int64_t iterInitK(
              VersionableSparse, TypedValue* valOut, TypedValue* keyOut);

  template<class Tuplish>
  int64_t iterNext(Fixed, TypedValue* valOut);
  template<class Vectorish>
  int64_t iterNext(Versionable, TypedValue* valOut);
  template<class Mappish>
  int64_t iterNext(VersionableSparse, TypedValue* valOut);
  template<class Tuplish>
  int64_t iterNextKey(Fixed, TypedValue* valOut, TypedValue* keyOut);
  template<class Vectorish>
  int64_t iterNextKey(Versionable, TypedValue* valOut, TypedValue* keyOut);
  template<class Mappish>
  int64_t iterNextKey(
              VersionableSparse, TypedValue* valOut, TypedValue* keyOut);

  public:
  const ArrayData* getArrayData() {
    assert(hasArrayData());
    return m_data;
  }
  ssize_t getPos() {
    return m_pos;
  }
  void setPos(ssize_t newPos) {
    m_pos = newPos;
  }
  IterKind getIterKind() const {
    return m_ikind;
  }
  void setIterKind(IterKind iterKind) {
    m_ikind = iterKind;
  }

  ObjectData* getObject() {
    assert(!hasArrayData());
    return (ObjectData*)((intptr_t)m_obj & ~1);
  }

  private:
  c_Vector* getVector() {
    assert(hasCollection() && getCollectionType() == Collection::VectorType);
    return (c_Vector*)((intptr_t)m_obj & ~1);
  }
  c_Map* getMap() {
    assert(hasCollection() && getCollectionType() == Collection::MapType);
    return (c_Map*)((intptr_t)m_obj & ~1);
  }
  c_StableMap* getStableMap() {
    assert(hasCollection() && getCollectionType() == Collection::StableMapType);
    return (c_StableMap*)((intptr_t)m_obj & ~1);
  }
  c_Set* getSet() {
    assert(hasCollection() && getCollectionType() == Collection::SetType);
    return (c_Set*)((intptr_t)m_obj & ~1);
  }
  c_Pair* getPair() {
    assert(hasCollection() && getCollectionType() == Collection::PairType);
    return (c_Pair*)((intptr_t)m_obj & ~1);
  }
  Collection::Type getCollectionType() {
    ObjectData* obj = getObject();
    return obj->getCollectionType();
  }
  ObjectData* getIteratorObj() {
    assert(hasIteratorObj());
    return getObject();
  }

  void setArrayData(const ArrayData* ad) {
    assert((intptr_t(ad) & 1) == 0);
    m_data = ad;
  }
  void setObject(ObjectData* obj) {
    assert((intptr_t(obj) & 1) == 0);
    m_obj = (ObjectData*)((intptr_t)obj | 1);
  }

  bool endHelper();
  void nextHelper();
  Variant firstHelper();
  void secondHelper(Variant& v);

  union {
    const ArrayData* m_data;
    ObjectData* m_obj;
  };
 public:
  ssize_t m_pos;
 private:
  int m_version;
  IterKind m_ikind;

  friend struct Iter;
};

///////////////////////////////////////////////////////////////////////////////

/**
 * FullPos provides the necessary functionality for supporting "foreach by
 * reference" (also called "strong foreach"). Note that the runtime does not
 * use FullPos directly, but instead uses two classes derived from FullPos
 * (MutableArrayIter and MArrayIter).
 *
 * In the common case, a FullPos is bound to a variable (m_var) when it is
 * initialized. m_var points to an inner cell which points to the array to
 * iterate over. For certain use cases, a FullPos is instead bound directly to
 * an array which m_data points to.
 *
 * Foreach by reference is a pain. Iteration needs to be robust in the face of
 * two challenges: (1) the case where an element is unset during iteration, and
 * (2) the case where user code modifies the inner cell to be a different array
 * or a non-array value. In such cases, we should never crash and ideally when
 * an element is unset we should be able to keep track of where we are in the
 * array.
 *
 * FullPos works by "registering" itself with the array being iterated over.
 * The array maintains a linked list of the FullPos's actively iterating over
 * it. When an element is unset, the FullPos's that were pointing to that
 * element are moved back one position before the element is unset. Note that
 * it is possible for an iterator to point to the position before the first
 * element (this is what the "reset" flag is for). This dance allows FullPos to
 * keep track of where it is in the array even when elements are unset.
 *
 * FullPos has also has a m_container field to keep track of which array it has
 * "registered" itself with. By comparing the array pointed to by m_var with
 * the array pointed to by m_container, FullPos can detect if user code has
 * modified the inner cell to be a different array or a non-array value. When
 * this happens, the FullPos unregisters itself with the old array (pointed to
 * by m_container) and registers itself with the new array (pointed to
 * by m_var->m_data.parr) and resumes iteration at the position pointed to by
 * the new array's internal cursor (ArrayData::m_pos). If m_var points to a
 * non-array value, iteration terminates.
 */
class FullPos {
 protected:
  FullPos() : m_pos(0), m_container(NULL), m_next(NULL) {}

 public:
  void release() { delete this; }

  // Returns true if the iterator points past the last element (or if
  // it points before the first element)
  bool end() const;

  // Move the iterator forward one element
  bool advance();

  // Returns true if the iterator points to a valid element
  bool prepare();

  ArrayData* getArray() const {
    return hasVar() ? getData() : getAd();
  }

  bool hasVar() const {
    return m_var && !(intptr_t(m_var) & 3LL);
  }
  bool hasAd() const {
    return bool(intptr_t(m_data) & 1LL);
  }
  const Variant* getVar() const {
    assert(hasVar());
    return m_var;
  }
  ArrayData* getAd() const {
    assert(hasAd());
    return (ArrayData*)(intptr_t(m_data) & ~1LL);
  }
  void setVar(const Variant* val) {
    m_var = val;
  }
  void setAd(ArrayData* val) {
    m_data = (ArrayData*)(intptr_t(val) | 1LL);
  }
  ArrayData* getContainer() const {
    return m_container;
  }
  void setContainer(ArrayData* arr) {
    m_container = arr;
  }
  FullPos* getNext() const {
    return (FullPos*)(m_resetBits & ~1);
  }
  void setNext(FullPos* fp) {
    assert((intptr_t(fp) & 1) == 0);
    m_resetBits = intptr_t(fp) | intptr_t(getResetFlag());
  }
  bool getResetFlag() const {
    return m_resetBits & 1;
  }
  void setResetFlag(bool reset) {
    m_resetBits = intptr_t(getNext()) | intptr_t(reset);
  }

 protected:
  ArrayData* getData() const {
    assert(hasVar());
    return m_var->is(KindOfArray) ? m_var->getArrayData() : nullptr;
  }
  ArrayData* cowCheck();
  void escalateCheck();
  ArrayData* reregister();

  // m_var/m_data are used to keep track of the array that were are supposed
  // to be iterating over. The low bit is used to indicate whether we are using
  // m_var or m_data. A helper function getArray() is provided to retrieve the
  // array that this FullPos is supposed to be iterating over.
  union {
    const Variant* m_var;
    ArrayData* m_data;
  };
 public:
  // m_pos is an opaque value used by the array implementation to track the
  // current position in the array.
  ssize_t m_pos;
 private:
  // m_container keeps track of which array we're "registered" with. Normally
  // getArray() and m_container refer to the same array. However, the two may
  // differ in cases where user code has modified the inner cell to be a
  // different array or non-array value.
  ArrayData* m_container;
  // m_next is used so that multiple FullPos's iterating over the same array
  // can be chained together into a singly linked list. The low bit of m_next
  // is used to track the state of the "reset" flag.
  union {
    FullPos* m_next;
    intptr_t m_resetBits;
  };
};

/**
 * Range which visits each entry in a list of FullPos. Removing the
 * front element will crash but removing an already-visited element
 * or future element will work.
 */
class FullPosRange {
 public:
  explicit FullPosRange(FullPos* list) : m_fpos(list) {}
  FullPosRange(const FullPosRange& other) : m_fpos(other.m_fpos) {}
  bool empty() const { return m_fpos == 0; }
  FullPos* front() const { assert(!empty()); return m_fpos; }
  void popFront() { assert(!empty()); m_fpos = m_fpos->getNext(); }
 private:
  FullPos* m_fpos;
};

/**
 * MutableArrayIter is used internally within the HipHop runtime in several
 * places. Ideally, we should phase it out and use MArrayIter instead.
 */
class MutableArrayIter : public FullPos {
 public:
  MutableArrayIter(const Variant* var, Variant* key, Variant& val);
  MutableArrayIter(ArrayData* data, Variant* key, Variant& val);
  ~MutableArrayIter();

  bool advance();

 private:
  Variant* m_key;
  Variant* m_valp;
};

/**
 * MArrayIter is used by the VM to handle the MIter* instructions
 */
class MArrayIter : public FullPos {
 public:
  MArrayIter() { m_data = NULL; }
  explicit MArrayIter(const RefData* ref);
  explicit MArrayIter(ArrayData* data);
  ~MArrayIter();

  /**
   * It is only safe to call key() and val() if all of the following
   * conditions are met:
   *  1) The calls to key() and/or val() are immediately preceded by
   *     a call to advance(), prepare(), or end().
   *  2) The iterator points to a valid position in the array.
   */
  Variant key() {
    ArrayData* data = getArray();
    assert(data && data == getContainer());
    assert(!getResetFlag() && data->validFullPos(*this));
    return data->getKey(m_pos);
  }
  CVarRef val() {
    ArrayData* data = getArray();
    assert(data && data == getContainer());
    assert(data->getCount() <= 1 || data->noCopyOnWrite());
    assert(!getResetFlag());
    assert(data->validFullPos(*this));
    return data->getValueRef(m_pos);
  }

  friend struct Iter;
};

class CufIter {
 public:
  CufIter() {}
  ~CufIter();
  const Func* func() const { return m_func; }
  void* ctx() const { return m_ctx; }
  StringData* name() const { return m_name; }

  void setFunc(const Func* f) { m_func = f; }
  void setCtx(ObjectData* obj) { m_ctx = obj; }
  void setCtx(const Class* cls) {
    m_ctx = cls ? (void*)((char*)cls + 1) : nullptr;
  }
  void setName(StringData* name) { m_name = name; }

  static uint32_t funcOff() { return offsetof(CufIter, m_func); }
  static uint32_t ctxOff()  { return offsetof(CufIter, m_ctx); }
  static uint32_t nameOff() { return offsetof(CufIter, m_name); }
 private:
  const Func* m_func;
  void* m_ctx;
  StringData* m_name;
};

struct Iter {
  const ArrayIter&   arr() const { return m_u.aiter; }
  const MArrayIter& marr() const { return m_u.maiter; }
  const CufIter&     cuf() const { return m_u.cufiter; }
        ArrayIter&   arr()       { return m_u.aiter; }
        MArrayIter& marr()       { return m_u.maiter; }
        CufIter&     cuf()       { return m_u.cufiter; }

  bool init(TypedValue* c1);
  bool next();
  void free();
  void mfree();
  void cfree();

private:
  union Data {
    Data() {}
    ArrayIter aiter;
    MArrayIter maiter;
    CufIter cufiter;
  } m_u;
} __attribute__ ((aligned(16)));

bool interp_init_iterator(Iter* it, TypedValue* c1);
bool interp_init_iterator_m(Iter* it, TypedValue* v1);
bool interp_iter_next(Iter* it);
bool interp_iter_next_m(Iter* it);

int64_t new_iter_array(Iter* dest, ArrayData* arr, TypedValue* val);
template <bool withRef>
int64_t new_iter_array_key(Iter* dest, ArrayData* arr, TypedValue* val,
                           TypedValue* key);
int64_t new_iter_object(Iter* dest, ObjectData* obj, Class* ctx,
                        TypedValue* val, TypedValue* key);
int64_t iter_next(Iter* dest, TypedValue* val);
template <bool withRef>
int64_t iter_next_key(Iter* dest, TypedValue* val, TypedValue* key);
template <bool withRef>
int64_t iter_next_any(Iter* dest, TypedValue* val, TypedValue* key);
template<class Coll, class Style, ArrayIter::IterKind iterKind>
int64_t iterInit(Iter* dest, Coll* coll, TypedValue* valOut);
template<class Coll, class Style, ArrayIter::IterKind iterKind>
int64_t iterInitK(Iter* dest, Coll* coll,
                  TypedValue* valOut, TypedValue* keyOut);
template<class Coll, class Style>
int64_t iterNext(ArrayIter* iter, TypedValue* valOut);
template<class Coll, class Style, ArrayIter::RefCountKey refCountKey>
int64_t iterNextK(ArrayIter* iter, TypedValue* valOut, TypedValue* keyOut);


int64_t new_miter_array_key(Iter* dest, RefData* arr, TypedValue* val,
                           TypedValue* key);
int64_t new_miter_object(Iter* dest, RefData* obj, Class* ctx,
                        TypedValue* val, TypedValue* key);
int64_t new_miter_other(Iter* dest, RefData* data);
int64_t miter_next_key(Iter* dest, TypedValue* val, TypedValue* key);

///////////////////////////////////////////////////////////////////////////////
}

#endif // incl_HPHP_ARRAY_ITERATOR_H_
