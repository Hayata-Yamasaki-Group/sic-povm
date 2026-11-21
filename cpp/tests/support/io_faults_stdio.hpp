// SPDX-License-Identifier: AGPL-3.0-or-later
// Included by io_faults.hpp after shared fault state declarations.
#pragma once
extern "C" {
#ifdef SIC_TEST_WRAP_OPEN
int __real_open(const char*, int, ...);
int __wrap_open(const char* path, int flags, ...) {
  mode_t mode = 0;
  if (flags & O_CREAT) {
    va_list args;
    va_start(args, flags);
    mode = va_arg(args, int);
    va_end(args);
  }
  return faults::open.hit() ? faults::failure(faults::open) : __real_open(path, flags, mode);
}
#endif
#ifdef SIC_TEST_WRAP_MKSTEMP
int __real_mkstemp(char*);
int __wrap_mkstemp(char* name) {
  if (faults::mkstemp.hit()) return faults::failure(faults::mkstemp);
  const int fd = __real_mkstemp(name);
  if (fd >= 0) faults::private_files.emplace_back(name);
  return fd;
}
#endif
#ifdef SIC_TEST_WRAP_MKDTEMP
char* __real_mkdtemp(char*);
char* __wrap_mkdtemp(char* name) {
  if (faults::mkdtemp.hit()) {
    faults::failure(faults::mkdtemp);
    return nullptr;
  }
  return __real_mkdtemp(name);
}
#endif
#ifdef SIC_TEST_WRAP_FOPEN
FILE* __real_fopen(const char*, const char*);
FILE* __wrap_fopen(const char* path, const char* mode) {
  if (faults::fopen.hit()) {
    faults::failure(faults::fopen);
    return nullptr;
  }
  return __real_fopen(path, mode);
}
#endif
#ifdef SIC_TEST_WRAP_FDOPEN
FILE* __real_fdopen(int, const char*);
FILE* __wrap_fdopen(int fd, const char* mode) {
  if (faults::fdopen.hit()) {
    faults::failure(faults::fdopen);
    return nullptr;
  }
  return __real_fdopen(fd, mode);
}
#endif

#ifdef SIC_TEST_WRAP_READ
ssize_t __real_read(int, void*, size_t);
ssize_t __wrap_read(int fd, void* data, size_t size) {
  return faults::read.hit() ? faults::failure(faults::read) : __real_read(fd, data, size);
}
#ifdef __GLIBC__
ssize_t __real___read_chk(int, void*, size_t, size_t);
ssize_t __wrap___read_chk(int fd, void* data, size_t size, size_t capacity) {
  if (size > capacity) return __real___read_chk(fd, data, size, capacity);
  return faults::read.hit() ? faults::failure(faults::read)
                            : __real___read_chk(fd, data, size, capacity);
}
#endif
#endif
#ifdef SIC_TEST_WRAP_FSTAT
int __real_fstat(int, struct stat*);
int __wrap_fstat(int fd, struct stat* state) {
  return faults::fstat.hit() ? faults::failure(faults::fstat) : __real_fstat(fd, state);
}
#endif
#ifdef SIC_TEST_WRAP_FREAD
size_t __real_fread(void*, size_t, size_t, FILE*);
size_t __wrap_fread(void* data, size_t size, size_t count, FILE* file) {
  if (faults::fread.hit()) {
    faults::failure(faults::fread);
    faults::read_error_stream = file;
    return 0;
  }
  return __real_fread(data, size, count, file);
}
#endif
#ifdef SIC_TEST_WRAP_FERROR
int __real_ferror(FILE*);
int __wrap_ferror(FILE* file) {
  return file == faults::read_error_stream ? 1 : __real_ferror(file);
}
#endif
#ifdef SIC_TEST_WRAP_FCLOSE
int __real_fclose(FILE*);
int __wrap_fclose(FILE* file) {
  if (file == faults::read_error_stream) faults::read_error_stream = nullptr;
  const bool hit = faults::fclose.hit();
  const int result = __real_fclose(file);
  return hit ? faults::failure(faults::fclose) : result;
}
#endif
#ifdef SIC_TEST_WRAP_FFLUSH
int __real_fflush(FILE*);
int __wrap_fflush(FILE* file) {
  return faults::fflush.hit() ? faults::failure(faults::fflush) : __real_fflush(file);
}
#endif
#ifdef SIC_TEST_WRAP_FWRITE
size_t __real_fwrite(const void*, size_t, size_t, FILE*);
size_t __wrap_fwrite(const void* data, size_t size, size_t count, FILE* file) {
  if (std::exchange(faults::short_fwrite, false)) return 0;
  if (faults::fwrite.hit()) {
    faults::failure(faults::fwrite);
    return 0;
  }
  return __real_fwrite(data, size, count, file);
}
#endif
#ifdef SIC_TEST_WRAP_FPUTC
int __real_fputc(int, FILE*);
int __wrap_fputc(int value, FILE* file) {
  return faults::fputc.hit() ? faults::failure(faults::fputc) : __real_fputc(value, file);
}
#endif
#ifdef SIC_TEST_WRAP_FGETC
int __real_fgetc(FILE*);
int __wrap_fgetc(FILE* file) {
  if (faults::trailing_read_error) {
    faults::trailing_read_error = false;
    sic_test_io_check(fputc('x', file) == EOF);  // The group file was opened read-only.
    return EOF;
  }
  if (faults::trailing_byte) {
    faults::trailing_byte = false;
    return 0;
  }
  return __real_fgetc(file);
}
#endif
#ifdef SIC_TEST_WRAP_FSEEKO
int __real_fseeko(FILE*, off_t, int);
int __wrap_fseeko(FILE* file, off_t offset, int origin) {
  if (faults::replace_on_seek) {
    sic_test_io_check(fflush(file) == 0);
    sic_test_io_check(ftruncate(fileno(file), 0) == 0);
    sic_test_io_check(pwrite(fileno(file), faults::replace_on_seek->data(),
                             faults::replace_on_seek->size(),
                             0) == (ssize_t)faults::replace_on_seek->size());
    faults::replace_on_seek.reset();
  }
  if (faults::seek_failure) {
    faults::seek_failure = false;
    errno = EIO;
    return -1;
  }
  return __real_fseeko(file, offset, origin);
}
#endif
}
