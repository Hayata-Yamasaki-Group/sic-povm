// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sic/core/hdf5.hpp"

#include "sic/limits.hpp"
#ifdef SIC_HAVE_HDF5
#include <hdf5.h>
#endif

namespace sic::core::interchange {
[[noreturn]] void h5_fail(ErrorKind kind, const std::string& message) {
  throw sic::Error(kind, message);
}
bool have_h5() {
#ifdef SIC_HAVE_HDF5
  return true;
#else
  return false;
#endif
}
#ifdef SIC_HAVE_HDF5
// HDF5 also reports truncated content through its I/O error class. Only a
// recorded native errno distinguishes an operating-system read failure.
ErrorKind h5_failure_kind(ErrorKind fallback) {
  if (fallback != ErrorKind::invalid_format) return fallback;
  bool io_error = false;
  H5Ewalk2(
      H5E_DEFAULT, H5E_WALK_DOWNWARD,
      [](unsigned, const H5E_error2_t* error, void* context) -> herr_t {
        if (error->desc) {
          const char* at = std::strstr(error->desc, "errno = ");
          if (at) {
            at += 8;
            unsigned value = 0;
            const auto result = std::from_chars(at, at + std::strlen(at), value);
            if (result.ec == std::errc{} && value != 0) *static_cast<bool*>(context) = true;
          }
        }
        return 0;
      },
      &io_error);
  return io_error ? ErrorKind::io : fallback;
}
// RAII wrapper: every HDF5 identifier is closed on all paths, and every creating call is checked.
struct H5Id {
  hid_t id = -1;
  herr_t (*closer)(hid_t) = nullptr;
  H5Id(hid_t i, herr_t (*c)(hid_t), const char* what, ErrorKind kind = ErrorKind::io)
      : id(i), closer(c) {
    if (id < 0) h5_fail(h5_failure_kind(kind), std::string("HDF5: ") + what + " failed");
  }
  H5Id(const H5Id&) = delete;
  H5Id& operator=(const H5Id&) = delete;
  ~H5Id() {
    if (id >= 0 && closer) closer(id);
  }
  void close_checked(const char* what) {
    if (id >= 0) {
      const auto closing = id;
      id = -1;
      if (closer(closing) < 0) h5_fail(ErrorKind::io, std::string("HDF5: ") + what + " failed");
    }
  }
  operator hid_t() const { return id; }
};
void h5ok(herr_t r, const char* what, ErrorKind kind = ErrorKind::io) {
  if (r < 0) h5_fail(h5_failure_kind(kind), std::string("HDF5: ") + what + " failed");
}

void write_h5(const std::string& path, const sic::core::Vector& psi,
              const std::vector<StringAttribute>& str_attrs,
              const std::vector<NumericAttribute>& num_attrs) {
  require_finite(psi, "HDF5 psi");
  H5Id fapl(H5Pcreate(H5P_FILE_ACCESS), H5Pclose, "H5Pcreate(file access)");
  h5ok(H5Pset_libver_bounds(fapl, H5F_LIBVER_EARLIEST, H5F_LIBVER_V18), "H5Pset_libver_bounds");
  H5Id fcpl(H5Pcreate(H5P_FILE_CREATE), H5Pclose, "H5Pcreate(file create)");
  H5Id f(H5Fcreate(path.c_str(), H5F_ACC_TRUNC, fcpl, fapl), H5Fclose, "H5Fcreate");
  H5Id ct(H5Tcreate(H5T_COMPOUND, 16), H5Tclose, "H5Tcreate");
  h5ok(H5Tinsert(ct, "r", 0, H5T_NATIVE_DOUBLE), "H5Tinsert r");
  h5ok(H5Tinsert(ct, "i", 8, H5T_NATIVE_DOUBLE), "H5Tinsert i");
  hsize_t dim = psi.size();
  H5Id sp(H5Screate_simple(1, &dim, nullptr), H5Sclose, "H5Screate_simple");
  H5Id dcpl(H5Pcreate(H5P_DATASET_CREATE), H5Pclose, "H5Pcreate(dataset create)");
  h5ok(H5Pset_obj_track_times(dcpl, 0), "H5Pset_obj_track_times");
  H5Id ds(H5Dcreate2(f, "psi", ct, sp, H5P_DEFAULT, dcpl, H5P_DEFAULT), H5Dclose, "H5Dcreate2");
  h5ok(H5Dwrite(ds, ct, H5S_ALL, H5S_ALL, H5P_DEFAULT, psi.data()), "H5Dwrite");
  H5Id st(H5Tcopy(H5T_C_S1), H5Tclose, "H5Tcopy");
  h5ok(H5Tset_size(st, H5T_VARIABLE), "H5Tset_size");
  h5ok(H5Tset_cset(st, H5T_CSET_UTF8), "H5Tset_cset");
  H5Id scalar(H5Screate(H5S_SCALAR), H5Sclose, "H5Screate(scalar)");
  for (const auto& [k, v] : str_attrs) {
    H5Id a(H5Acreate2(ds, k.c_str(), st, scalar, H5P_DEFAULT, H5P_DEFAULT), H5Aclose, "H5Acreate2");
    const char* p = v.c_str();
    h5ok(H5Awrite(a, st, &p), "H5Awrite");
    a.close_checked("H5Aclose");
  }
  for (const auto& [k, v] : num_attrs) {
    H5Id a(H5Acreate2(ds, k.c_str(), H5T_NATIVE_DOUBLE, scalar, H5P_DEFAULT, H5P_DEFAULT), H5Aclose,
           "H5Acreate2");
    h5ok(H5Awrite(a, H5T_NATIVE_DOUBLE, &v), "H5Awrite");
    a.close_checked("H5Aclose");
  }
  h5ok(H5Fflush(f, H5F_SCOPE_LOCAL), "H5Fflush");
  scalar.close_checked("H5Sclose");
  st.close_checked("H5Tclose");
  ds.close_checked("H5Dclose");
  dcpl.close_checked("H5Pclose");
  sp.close_checked("H5Sclose");
  ct.close_checked("H5Tclose");
  f.close_checked("H5Fclose");
  fcpl.close_checked("H5Pclose");
  fapl.close_checked("H5Pclose");
}
sic::core::Vector read_h5(const std::string& path) {
  // Check filesystem access separately from the HDF5 content parser.
  std::ifstream input(path, std::ios::binary);
  if (!input) h5_fail(ErrorKind::io, "HDF5: cannot open " + path);
  input.peek();
  if (input.bad()) h5_fail(ErrorKind::io, "HDF5: read failed for " + path);
  H5Id f(H5Fopen(path.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT), H5Fclose, "H5Fopen",
         ErrorKind::invalid_format);
  H5Id ds(H5Dopen2(f, "psi", H5P_DEFAULT), H5Dclose, "H5Dopen2", ErrorKind::invalid_format);
  H5Id sp(H5Dget_space(ds), H5Sclose, "H5Dget_space", ErrorKind::invalid_format);
  hsize_t dim = 0;
  if (H5Sget_simple_extent_ndims(sp) != 1)
    h5_fail(ErrorKind::invalid_format, "HDF5: psi is not one-dimensional");
  h5ok(H5Sget_simple_extent_dims(sp, &dim, nullptr), "H5Sget_simple_extent_dims",
       ErrorKind::invalid_format);
  const InputLimits limits;
  if (dim > limits.maximum_dimension || dim > limits.maximum_bytes / sizeof(Complex))
    h5_fail(ErrorKind::resource, "HDF5: dimension/allocation policy exceeded");
  sic::core::Vector v(static_cast<std::size_t>(dim));
  H5Id ct(H5Tcreate(H5T_COMPOUND, 16), H5Tclose, "H5Tcreate");
  h5ok(H5Tinsert(ct, "r", 0, H5T_NATIVE_DOUBLE), "H5Tinsert r");
  h5ok(H5Tinsert(ct, "i", 8, H5T_NATIVE_DOUBLE), "H5Tinsert i");
  h5ok(H5Dread(ds, ct, H5S_ALL, H5S_ALL, H5P_DEFAULT, v.data()), "H5Dread",
       ErrorKind::invalid_format);
  require_finite(v, "HDF5 psi", ErrorKind::invalid_format);
  return v;
}
#else
void write_h5(const std::string&, const Vector&, const std::vector<StringAttribute>&,
              const std::vector<NumericAttribute>&) {
  h5_fail(ErrorKind::unsupported, "HDF5 support is disabled");
}
Vector read_h5(const std::string&) { h5_fail(ErrorKind::unsupported, "HDF5 support is disabled"); }
#endif
}  // namespace sic::core::interchange
