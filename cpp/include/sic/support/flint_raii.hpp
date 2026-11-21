// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <flint/acb.h>
#include <flint/acb_mat.h>
#include <flint/acb_poly.h>
#include <flint/arb_poly.h>
#include <flint/fmpq.h>
#include <flint/fmpq_mat.h>
#include <flint/fmpz_mat.h>

#include <memory>
#include <utility>

#include "sic/error.hpp"

namespace sic::support::flint {
// Own strings returned by FLINT's allocating *_get_str / *_dump_str APIs.
struct StringFree {
  void operator()(char* text) const noexcept;
};
using String = std::unique_ptr<char, StringFree>;

// FLINT scalars own limb pointers. Transfer the complete initialized C value;
// the moved-from owner receives a fresh zero and remains safe to destroy.
template <class Ops>
class Local {
  using T = typename Ops::Value;

 public:
  Local() { Ops::init(&value_); }
  ~Local() { Ops::clear(&value_); }
  Local(const Local&) = delete;
  Local& operator=(const Local&) = delete;
  Local(Local&& other) noexcept : Local() { std::swap(value_, other.value_); }
  Local& operator=(Local&& other) noexcept {
    std::swap(value_, other.value_);
    return *this;
  }
  T* get() { return &value_; }
  const T* get() const { return &value_; }
  operator T*() { return get(); }
  operator const T*() const { return get(); }
  T* operator->() { return get(); }
  const T* operator->() const { return get(); }

 private:
  T value_;
};
struct ArbOps {
  using Value = arb_struct;
  static void init(Value* value);
  static void clear(Value* value);
};
using Arb = Local<ArbOps>;
struct AcbOps {
  using Value = acb_struct;
  static void init(Value* value);
  static void clear(Value* value);
};
using Acb = Local<AcbOps>;
struct ArfOps {
  using Value = arf_struct;
  static void init(Value* value);
  static void clear(Value* value);
};
using Arf = Local<ArfOps>;
struct MagOps {
  using Value = mag_struct;
  static void init(Value* value);
  static void clear(Value* value);
};
using Mag = Local<MagOps>;
struct FmpzOps {
  using Value = fmpz;
  static void init(Value* value);
  static void clear(Value* value);
};
using Fmpz = Local<FmpzOps>;
struct FmpqOps {
  using Value = fmpq;
  static void init(Value* value);
  static void clear(Value* value);
};
using Fmpq = Local<FmpqOps>;
struct ArbPolyOps {
  using Value = arb_poly_struct;
  static void init(Value* value);
  static void clear(Value* value);
};
using ArbPoly = Local<ArbPolyOps>;
struct AcbPolyOps {
  using Value = acb_poly_struct;
  static void init(Value* value);
  static void clear(Value* value);
};
using AcbPoly = Local<AcbPolyOps>;

// Own a complete matrix, including its row/column dimensions and all entries.
// Copies are forbidden. Moves transfer storage in constant time; move assignment
// exchanges the two matrices. get() and conversions borrow until the owner is
// moved or destroyed. Borrowers may change entries, but must not clear/reinitialize
// the matrix or replace it with a window. Windows have a separate FLINT lifetime.
template <class Ops>
class Matrix {
  using T = typename Ops::Value;

 public:
  explicit Matrix(slong rows, slong columns) {
    if (rows < 0 || columns < 0)
      throw sic::Error(sic::ErrorKind::invalid_argument, "negative matrix dimensions");
    Ops::init(&value_, rows, columns);
  }
  ~Matrix() { Ops::clear(&value_); }
  Matrix(const Matrix&) = delete;
  Matrix& operator=(const Matrix&) = delete;
  Matrix(Matrix&& other) noexcept : Matrix(0, 0) { std::swap(value_, other.value_); }
  Matrix& operator=(Matrix&& other) noexcept {
    std::swap(value_, other.value_);
    return *this;
  }
  slong rows() const { return value_.r; }
  slong columns() const { return value_.c; }
  T* get() & { return &value_; }
  const T* get() const& { return &value_; }
  T* get() && = delete;
  const T* get() const&& = delete;
  operator T*() & { return get(); }
  operator const T*() const& { return get(); }
  operator T*() && = delete;
  operator const T*() const&& = delete;
  // Prevent a delete-expression from treating the borrowed handle as owned.
  operator void*() & = delete;
  operator const void*() const& = delete;
  T* operator->() & { return get(); }
  const T* operator->() const& { return get(); }
  T* operator->() && = delete;
  const T* operator->() const&& = delete;

 private:
  T value_;
};
struct FmpzMatOps {
  using Value = fmpz_mat_struct;
  static void init(Value* value, slong rows, slong columns);
  static void clear(Value* value);
};
using FmpzMat = Matrix<FmpzMatOps>;
struct FmpqMatOps {
  using Value = fmpq_mat_struct;
  static void init(Value* value, slong rows, slong columns);
  static void clear(Value* value);
};
using FmpqMat = Matrix<FmpqMatOps>;
struct AcbMatOps {
  using Value = acb_mat_struct;
  static void init(Value* value, slong rows, slong columns);
  static void clear(Value* value);
};
using AcbMat = Matrix<AcbMatOps>;

struct WorkerCleanup {
  ~WorkerCleanup();
  WorkerCleanup() = default;
  WorkerCleanup(const WorkerCleanup&) = delete;
  WorkerCleanup& operator=(const WorkerCleanup&) = delete;
};
}  // namespace sic::support::flint
