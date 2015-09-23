/*
 * Copyright (C) 2008 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "jit/ExecutableAllocator.h"

#define INCL_DOS
#include <os2.h>

using namespace js::jit;

size_t ExecutableAllocator::determinePageSize()
{
    return 4096u;
}

void*
js::jit::AllocateExecutableMemory(void* addr, size_t bytes, unsigned permissions, const char* tag,
                                  size_t pageSize)
{
    MOZ_ASSERT(bytes % pageSize == 0);
    MOZ_ASSERT(permissions == PAG_READ | PAG_WRITE | PAG_EXECUTE);

    void* p = nullptr;
#if defined(MOZ_OS2_HIGH_MEMORY)
    if (DosAllocMem(&p, bytes, OBJ_ANY | PAG_COMMIT | permissions))
#endif
        if (DosAllocMem(&p, bytes, PAG_COMMIT | permissions))
            return nullptr;

    return p;
}

void
js::jit::DeallocateExecutableMemory(void* addr, size_t bytes, size_t pageSize)
{
    MOZ_ASSERT(bytes % pageSize == 0);

    DosFreeMem(addr);
}

ExecutablePool::Allocation ExecutableAllocator::systemAlloc(size_t n)
{
    void* allocation = AllocateExecutableMemory(nullptr, n, PAG_READ | PAG_WRITE | PAG_EXECUTE,
                                                "js-jit-code", pageSize);
    ExecutablePool::Allocation alloc = { reinterpret_cast<char*>(allocation), n };
    return alloc;
}

void ExecutableAllocator::systemRelease(const ExecutablePool::Allocation& alloc)
{
    DeallocateExecutableMemory(alloc.pages, alloc.size, pageSize);
}

#if ENABLE_ASSEMBLER_WX_EXCLUSIVE
#error "ASSEMBLER_WX_EXCLUSIVE not yet suported on this platform."
#endif

