/***************************************************************************
 * Copyright (c) Johan Mabille, Sylvain Corlay, Wolf Vollprecht and         *
 * Martin Renou                                                             *
 * Copyright (c) QuantStack                                                 *
 * Copyright (c) Serge Guelton                                              *
 *                                                                          *
 * Distributed under the terms of the BSD 3-Clause License.                 *
 *                                                                          *
 * The full license is in the file LICENSE, distributed with this software. *
 ****************************************************************************/

#ifndef XSIMD_CPUID_HPP
#define XSIMD_CPUID_HPP

#include <algorithm>
#include <cstring>

#if defined(__linux__) && (defined(__ARM_NEON) || defined(_M_ARM) || defined(__riscv_vector))
#include <asm/hwcap.h>
#include <sys/auxv.h>

#ifndef HWCAP2_I8MM
#define HWCAP2_I8MM (1 << 13)
#endif

#endif

#if defined(_MSC_VER)
// Contains the definition of __cpuidex
#include <intrin.h>
#endif

#include "../types/xsimd_all_registers.hpp"

namespace xsimd
{
    namespace detail
    {
        struct supported_arch
        {

#define ARCH_FIELD_EX(arch, field_name) \
    unsigned field_name;                \
    XSIMD_INLINE bool has(::xsimd::arch) const { return this->field_name; }

#define ARCH_FIELD_EX_REUSE(arch, field_name) \
    XSIMD_INLINE bool has(::xsimd::arch) const { return this->field_name; }

#define ARCH_FIELD(name) ARCH_FIELD_EX(name, name)

            ARCH_FIELD(sse2)
            ARCH_FIELD(sse3)

            ARCH_FIELD(ssse3)
            ARCH_FIELD(sse4_1)
            ARCH_FIELD(sse4_2)
            // ARCH_FIELD(sse4a)
            ARCH_FIELD_EX(fma3<::xsimd::sse4_2>, fma3_sse42)
            ARCH_FIELD(fma4)
            // ARCH_FIELD(xop)
            ARCH_FIELD(avx)
            ARCH_FIELD_EX(fma3<::xsimd::avx>, fma3_avx)
            ARCH_FIELD(avx2)
            ARCH_FIELD(avxvnni)
            ARCH_FIELD_EX(fma3<::xsimd::avx2>, fma3_avx2)
            ARCH_FIELD(avx512f)
            ARCH_FIELD(avx512cd)
            ARCH_FIELD(avx512dq)
            ARCH_FIELD(avx512bw)
            ARCH_FIELD(avx512er)
            ARCH_FIELD(avx512pf)
            ARCH_FIELD(avx512ifma)
            ARCH_FIELD(avx512vbmi)
            ARCH_FIELD(avx512vbmi2)
            ARCH_FIELD_EX(avx512vnni<::xsimd::avx512bw>, avx512vnni_bw)
            ARCH_FIELD_EX(avx512vnni<::xsimd::avx512vbmi2>, avx512vnni_vbmi2)
            ARCH_FIELD(neon)
            ARCH_FIELD(neon64)
            ARCH_FIELD_EX(i8mm<::xsimd::neon64>, i8mm_neon64)
            ARCH_FIELD_EX(detail::sve<512>, sve)
            ARCH_FIELD_EX_REUSE(detail::sve<256>, sve)
            ARCH_FIELD_EX_REUSE(detail::sve<128>, sve)
            ARCH_FIELD_EX(detail::rvv<512>, rvv)
            ARCH_FIELD_EX_REUSE(detail::rvv<256>, rvv)
            ARCH_FIELD_EX_REUSE(detail::rvv<128>, rvv)
            ARCH_FIELD(wasm)

#undef ARCH_FIELD

            XSIMD_INLINE supported_arch() noexcept
            {
                memset(this, 0, sizeof(supported_arch));

#if XSIMD_WITH_WASM
                wasm = 1;
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
                neon = 1;
                neon64 = 1;
#if defined(__linux__) && (!defined(__ANDROID_API__) || __ANDROID_API__ >= 18)
                i8mm_neon64 = bool(getauxval(AT_HWCAP2) & HWCAP2_I8MM);
#endif
#elif defined(__ARM_NEON) || defined(_M_ARM)

#if defined(__linux__) && (!defined(__ANDROID_API__) || __ANDROID_API__ >= 18)
                neon = bool(getauxval(AT_HWCAP) & HWCAP_NEON);
#endif

#elif defined(__ARM_FEATURE_SVE) && defined(__ARM_FEATURE_SVE_BITS) && __ARM_FEATURE_SVE_BITS > 0

#if defined(__linux__) && (!defined(__ANDROID_API__) || __ANDROID_API__ >= 18)
                sve = bool(getauxval(AT_HWCAP) & HWCAP_SVE);
#endif

#elif defined(__riscv_vector) && defined(__riscv_v_fixed_vlen) && __riscv_v_fixed_vlen > 0

#if defined(__linux__) && (!defined(__ANDROID_API__) || __ANDROID_API__ >= 18)
#ifndef HWCAP_V
#define HWCAP_V (1 << ('V' - 'A'))
#endif
                rvv = bool(getauxval(AT_HWCAP) & HWCAP_V);
#endif

#elif defined(__x86_64__) || defined(__i386__) || defined(_M_AMD64) || defined(_M_IX86)

                auto get_xcr0_low = []() noexcept
                {
                    uint32_t xcr0;

#if defined(_MSC_VER) && _MSC_VER >= 1400

                    xcr0 = (uint32_t)_xgetbv(0);

#elif defined(__GNUC__)

                    __asm__(
                        "xorl %%ecx, %%ecx\n"
                        "xgetbv\n"
                        : "=a"(xcr0)
                        :
#if defined(__i386__)
                        : "ecx", "edx"
#else
                        : "rcx", "rdx"
#endif
                    );

#else /* _MSC_VER < 1400 */
#error "_MSC_VER < 1400 is not supported"
#endif /* _MSC_VER && _MSC_VER >= 1400 */
                    return xcr0;
                };

                auto get_cpuid = [](int reg[4], int level, int count = 0) noexcept
                {

#if defined(_MSC_VER)
                    __cpuidex(reg, level, count);

#elif defined(__INTEL_COMPILER)
                    __cpuid(reg, level);

#elif defined(__GNUC__) || defined(__clang__)

#if defined(__i386__) && defined(__PIC__)
                    // %ebx may be the PIC register
                    __asm__("xchg{l}\t{%%}ebx, %1\n\t"
                            "cpuid\n\t"
                            "xchg{l}\t{%%}ebx, %1\n\t"
                            : "=a"(reg[0]), "=r"(reg[1]), "=c"(reg[2]), "=d"(reg[3])
                            : "0"(level), "2"(count));

#else
                    __asm__("cpuid\n\t"
                            : "=a"(reg[0]), "=b"(reg[1]), "=c"(reg[2]), "=d"(reg[3])
                            : "0"(level), "2"(count));
#endif

#else
#error "Unsupported configuration"
#endif
                };

                int regs1[4];

                get_cpuid(regs1, 0x1);

                // OS can explicitly disable the usage of SSE/AVX extensions
                // by setting an appropriate flag in CR0 register
                //
                // https://docs.kernel.org/admin-guide/hw-vuln/gather_data_sampling.html

                unsigned sse_state_os_enabled = 1;
                unsigned avx_state_os_enabled = 1;
                unsigned avx512_state_os_enabled = 1;

                // OSXSAVE: A value of 1 indicates that the OS has set CR4.OSXSAVE[bit
                // 18] to enable XSETBV/XGETBV instructions to access XCR0 and
                // to support processor extended state management using
                // XSAVE/XRSTOR.
                bool osxsave = regs1[2] >> 27 & 1;
                if (osxsave)
                {

                    uint32_t xcr0 = get_xcr0_low();

                    sse_state_os_enabled = xcr0 >> 1 & 1;
                    avx_state_os_enabled = xcr0 >> 2 & sse_state_os_enabled;
                    avx512_state_os_enabled = xcr0 >> 6 & avx_state_os_enabled;
                }

                sse2 = regs1[3] >> 26 & sse_state_os_enabled;
                sse3 = regs1[2] >> 0 & sse_state_os_enabled;
                ssse3 = regs1[2] >> 9 & sse_state_os_enabled;
                sse4_1 = regs1[2] >> 19 & sse_state_os_enabled;
                sse4_2 = regs1[2] >> 20 & sse_state_os_enabled;
                fma3_sse42 = regs1[2] >> 12 & sse_state_os_enabled;

                avx = regs1[2] >> 28 & avx_state_os_enabled;
                fma3_avx = avx && fma3_sse42;

                int regs8[4];
                get_cpuid(regs8, 0x80000001);
                fma4 = regs8[2] >> 16 & avx_state_os_enabled;

                // sse4a = regs[2] >> 6 & 1;

                // xop = regs[2] >> 11 & 1;

                int regs7[4];
                get_cpuid(regs7, 0x7);
                avx2 = regs7[1] >> 5 & avx_state_os_enabled;

                int regs7a[4];
                get_cpuid(regs7a, 0x7, 0x1);
                avxvnni = regs7a[0] >> 4 & avx_state_os_enabled;

                fma3_avx2 = avx2 && fma3_sse42;

                avx512f = regs7[1] >> 16 & avx512_state_os_enabled;
                avx512cd = regs7[1] >> 28 & avx512_state_os_enabled;
                avx512dq = regs7[1] >> 17 & avx512_state_os_enabled;
                avx512bw = regs7[1] >> 30 & avx512_state_os_enabled;
                avx512er = regs7[1] >> 27 & avx512_state_os_enabled;
                avx512pf = regs7[1] >> 26 & avx512_state_os_enabled;
                avx512ifma = regs7[1] >> 21 & avx512_state_os_enabled;
                avx512vbmi = regs7[2] >> 1 & avx512_state_os_enabled;
                avx512vbmi2 = regs7[2] >> 6 & avx512_state_os_enabled;
                avx512vnni_bw = regs7[2] >> 11 & avx512_state_os_enabled;
                avx512vnni_vbmi2 = avx512vbmi2 && avx512vnni_bw;
#endif
            }
        };
    } // namespace detail

    XSIMD_INLINE detail::supported_arch available_architectures() noexcept
    {
        static detail::supported_arch supported;
        return supported;
    }
}

#endif
