/*
 * Copyright (C) 2008 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "libc_init_common.h"

#include <elf.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/auxv.h>
#include <sys/time.h>
#include <unistd.h>
#include <dlfcn.h>

#include "private/bionic_auxv.h"
#include "private/bionic_ssp.h"
#include "private/bionic_tls.h"
#include "private/KernelArgumentBlock.h"
#include "pthread_internal.h"

#include "private/libc_logging.h"
#include "__a_bionic_hook.h"

extern "C" abort_msg_t** __abort_message_ptr;
extern "C" int __system_properties_init(void);
extern "C" int __set_tls(void* ptr);
extern "C" int __set_tid_address(int* tid_address);

void __libc_init_vdso();

// Not public, but well-known in the BSDs.
const char* __progname;

// Declared in <unistd.h>.
char** environ;

// Declared in "private/bionic_ssp.h".
uintptr_t __stack_chk_guard = 0;



//#define HOOK_PTHREAD
#ifdef USE_HKMALLOC
typedef void (*PFN_android_set_application_target_sdk_version)(int target);
static PFN_android_set_application_target_sdk_version  host_android_set_application_target_sdk_version  = 0;

void init_pthread_(KernelArgumentBlock* args) {
  void* hostlib =  0;
#if defined(__LP64__)
  hostlib = vmosdlopen("/system/lib64/libdl_android.so", RTLD_NOW | RTLD_LOCAL);
  if (hostlib == 0)
    hostlib = vmosdlopen("/system/lib64/libdl.so", RTLD_NOW | RTLD_LOCAL);
#else
  hostlib = vmosdlopen("/system/lib/libdl_android.so", RTLD_NOW | RTLD_LOCAL);
  if (hostlib == 0)
    hostlib = vmosdlopen("/system/lib/libdl.so", RTLD_NOW | RTLD_LOCAL);
#endif
  if (hostlib)
      host_android_set_application_target_sdk_version = (PFN_android_set_application_target_sdk_version)vmosdlsym(hostlib, "android_set_application_target_sdk_version");

  if (host_android_set_application_target_sdk_version)
      host_android_set_application_target_sdk_version(23);
}
#else
void init_pthread_(KernelArgumentBlock* args) {
  (void)args;
}
#endif


/* Init TLS for the initial thread. Called by the linker _before_ libc is mapped
 * in memory. Beware: all writes to libc globals from this function will
 * apply to linker-private copies and will not be visible from libc later on.
 *
 * Note: this function creates a pthread_internal_t for the initial thread and
 * stores the pointer in TLS, but does not add it to pthread's thread list. This
 * has to be done later from libc itself (see __libc_init_common).
 *
 * This function also stores a pointer to the kernel argument block in a TLS slot to be
 * picked up by the libc constructor.
 */
void __libc_init_tls(KernelArgumentBlock& args) {
  __libc_auxv = args.auxv;

#if LIBCVM_pthread_create==0
  static void* tls[BIONIC_TLS_SLOTS];
  static pthread_internal_t main_thread;
  main_thread.tls = tls;

  // Tell the kernel to clear our tid field when we exit, so we're like any other pthread.
  // As a side-effect, this tells us our pid (which is the same as the main thread's tid).
  main_thread.tid = __set_tid_address(&main_thread.tid);
  main_thread.set_cached_pid(main_thread.tid);

  // We don't want to free the main thread's stack even when the main thread exits
  // because things like environment variables with global scope live on it.
  // We also can't free the pthread_internal_t itself, since that lives on the main
  // thread's stack rather than on the heap.
  pthread_attr_init(&main_thread.attr);
  main_thread.attr.flags = PTHREAD_ATTR_FLAG_USER_ALLOCATED_STACK | PTHREAD_ATTR_FLAG_MAIN_THREAD;
  main_thread.attr.guard_size = 0; // The main thread has no guard page.
  main_thread.attr.stack_size = 0; // User code should never see this; we'll compute it when asked.
  // TODO: the main thread's sched_policy and sched_priority need to be queried.

  __init_thread(&main_thread, false);
  __init_tls(&main_thread);
  __set_tls(main_thread.tls);
  tls[TLS_SLOT_BIONIC_PREINIT] = &args;

  __init_alternate_signal_stack(&main_thread);
#else
// 判断静态
//	if (0==__libc_host)
	{
		void** tls = __get_tls();
		tls[TLS_SLOT_BIONIC_PREINIT] = &args;
		return ;
	}
#endif
}

extern "C" int __system_property_get(const char *name, char *value);

void __libc_init_common(KernelArgumentBlock& args) {
  // Initialize various globals.
  environ = args.envp;
  errno = 0;
  __libc_auxv = args.auxv;
  __progname = args.argv[0] ? args.argv[0] : "<unknown>";
  __abort_message_ptr = args.abort_message_ptr;

  // AT_RANDOM is a pointer to 16 bytes of randomness on the stack.
  __stack_chk_guard = *reinterpret_cast<uintptr_t*>(getauxval(AT_RANDOM));

#if LIBCVM_pthread_create==0
  // Get the main thread from TLS and add it to the thread list.
  pthread_internal_t* main_thread = __get_thread();

// 判断静态
//if (0==__libc_host)
{
  _pthread_internal_add(main_thread);
}
#endif

  __system_properties_init(); // Requires 'environ'.
  registerVmGetPropFunc((void*)&__system_property_get);
  // __process_pid_init();
  __libc_init_vdso();
  getuid();
}

/* This function will be called during normal program termination
 * to run the destructors that are listed in the .fini_array section
 * of the executable, if any.
 *
 * 'fini_array' points to a list of function addresses. The first
 * entry in the list has value -1, the last one has value 0.
 */
void __libc_fini(void* array) {
  void** fini_array = reinterpret_cast<void**>(array);
  const size_t minus1 = ~(size_t)0; /* ensure proper sign extension */

  /* Sanity check - first entry must be -1 */
  if (array == NULL || (size_t)fini_array[0] != minus1) {
    return;
  }

  /* skip over it */
  fini_array += 1;

  /* Count the number of destructors. */
  int count = 0;
  while (fini_array[count] != NULL) {
    ++count;
  }

  /* Now call each destructor in reverse order. */
  while (count > 0) {
    void (*func)() = (void (*)()) fini_array[--count];

    /* Sanity check, any -1 in the list is ignored */
    if ((size_t)func == minus1) {
      continue;
    }

    func();
  }

#ifndef LIBC_STATIC
  {
    extern void __libc_postfini(void) __attribute__((weak));
    if (__libc_postfini) {
      __libc_postfini();
    }
  }
#endif
}
