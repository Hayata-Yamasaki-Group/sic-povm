// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace sic::exact {
template <class T>
void GroupCheckpoint::write(FILE* file, const T* values, size_t count) {
  if (count && fwrite(values, sizeof(T), count, file) != count)
    throw sic::Error(sic::ErrorKind::io, "group checkpoint write failed");
}

template <class T>
void GroupCheckpoint::read(FILE* file, T* values, size_t count) {
  if (count && fread(values, sizeof(T), count, file) != count)
    throw sic::Error(ferror(file) ? sic::ErrorKind::io : sic::ErrorKind::invalid_format,
                     "truncated group checkpoint");
}

}  // namespace sic::exact
