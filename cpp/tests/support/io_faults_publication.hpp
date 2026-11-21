// SPDX-License-Identifier: AGPL-3.0-or-later
// Included by io_faults.hpp after shared fault state declarations.
#pragma once
extern "C" {
#ifdef SIC_TEST_WRAP_LINK
int __real_link(const char*, const char*);
int __wrap_link(const char* from, const char* to) {
  return faults::link.hit() ? faults::failure(faults::link) : __real_link(from, to);
}
#endif
#ifdef SIC_TEST_WRAP_UNLINK
int __real_unlink(const char*);
int __wrap_unlink(const char* path) {
  return faults::unlink.hit() ? faults::failure(faults::unlink) : __real_unlink(path);
}
#endif
#ifdef SIC_TEST_WRAP_RENAME
int __real_rename(const char*, const char*);
int __wrap_rename(const char* from, const char* to) {
  if (faults::rename.hit()) {
#ifdef SIC_TEST_WRAP_STORAGE
    if (faults::rename_race) faults::rename_race(from, to);
#endif
    return faults::failure(faults::rename);
  }
  return __real_rename(from, to);
}
#endif
#ifdef SIC_TEST_WRAP_SYSCALL
// Preserve all arguments of every unrelated variadic syscall, including calls
// with zero arguments. A C++ va_list cannot forward these without guessing the
// argument count. These test-only ABI trampolines tail-jump without a frame.
#define SIC_TEST_STRINGIFY_IMPL(value) #value
#define SIC_TEST_STRINGIFY(value) SIC_TEST_STRINGIFY_IMPL(value)
#if defined(__x86_64__)
asm(".text\n.globl __wrap_syscall\n.type __wrap_syscall,@function\n"
    "__wrap_syscall:\n"
    "cmp $" SIC_TEST_STRINGIFY(SYS_renameat2) ", %rdi\n"
    "je sic_test_publish_syscall\n"
#ifdef SIC_TEST_WRAP_NUMA
    "cmp $" SIC_TEST_STRINGIFY(SYS_set_mempolicy) ", %rdi\n"
    "je sic_test_mempolicy_syscall\n"
#endif
    "jmp __real_syscall\n"
    ".size __wrap_syscall,.-__wrap_syscall\n");
#elif defined(__aarch64__)
asm(".text\n.globl __wrap_syscall\n.type __wrap_syscall,%function\n"
    "__wrap_syscall:\n"
    "cmp x0, #" SIC_TEST_STRINGIFY(SYS_renameat2) "\n"
    "b.eq sic_test_publish_syscall\n"
#ifdef SIC_TEST_WRAP_NUMA
    "cmp x0, #" SIC_TEST_STRINGIFY(SYS_set_mempolicy) "\n"
    "b.eq sic_test_mempolicy_syscall\n"
#endif
    "b __real_syscall\n"
    ".size __wrap_syscall,.-__wrap_syscall\n");
#else
#error "The syscall fault wrapper requires an x86_64 or aarch64 ABI trampoline"
#endif
#undef SIC_TEST_STRINGIFY
#undef SIC_TEST_STRINGIFY_IMPL
long __real_syscall(long, ...);
#ifdef SIC_TEST_WRAP_NUMA
[[gnu::used]] long sic_test_mempolicy_syscall(long number, int mode, const unsigned long* mask,
                                              unsigned long count) {
  if (faults::mempolicy.hit()) return faults::failure(faults::mempolicy);
  return __real_syscall(number, mode, mask, count);
}
#endif
[[gnu::used]] long sic_test_publish_syscall(long number, int from_fd, const char* from, int to_fd,
                                            const char* to, unsigned flags) {
  if (faults::publish.hit()) return faults::failure(faults::publish);
  return __real_syscall(number, from_fd, from, to_fd, to, flags);
}
#endif
}
