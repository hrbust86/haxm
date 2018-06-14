/*
 * Copyright (c) 2009 Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *   3. Neither the name of the copyright holder nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef HAX_CORE_CPU_H_
#define HAX_CORE_CPU_H_

#include "vmx.h"
#include "segments.h"
#include "config.h"
#include "vm.h"
#include "pmu.h"

#define VMXON_SUCCESS 0x0
#define VMXON_FAIL    0x1
#define VMPTRLD_FAIL  0X2

struct vcpu_t;
struct vcpu_state_t;

typedef uint32 cpuid_t;  // CPU identifier

#define NR_HMSR 6

struct hstate {
    /* ldt is not covered by host vmcs area */
    uint16 ldt_selector;
    uint16 ds;
    uint16 es;
    uint16 fs;
    uint16 gs;
    uint16 seg_valid;
#define HOST_SEG_VALID_GS 0x1
#define HOST_SEG_VALID_FS 0x2
#define HOST_SEG_VALID_DS 0x4
#define HOST_SEG_VALID_ES 0x8
    uint16 seg_not_present;
#define HOST_SEG_NOT_PRESENT_GS 0x1
    uint64 _efer;
    uint64 gs_base;
    uint64 fs_base;
    uint64 hcr2;
    struct vmx_msr hmsr[NR_HMSR];
    // IA32_PMCx, since APM v1
    uint64 apm_pmc_msrs[APM_MAX_GENERAL_COUNT];
    // IA32_PERFEVTSELx, since APM v1
    uint64 apm_pes_msrs[APM_MAX_GENERAL_COUNT];
    // IA32_TSC_AUX
    uint64 tsc_aux;
    struct hax_page *hfxpage;
    uint64 fake_gs;
    system_desc_t host_gdtr;
    system_desc_t host_idtr;
};

struct hstate_compare {
    uint32 cr0, cr2, cr3, cr4;
    uint32 cs, ds, es, fs, gs, ss, ldt, tr;
    uint32 cs_avail, ds_avail, es_avail, fs_avail, gs_avail, tr_avail, ss_avail;
    uint64 sysenter_cs, sysenter_eip, sysenter_esp, efer, pat_msr, fs_msr;
    uint64 gs_msr, rflags, rsp;
};

#define VMXON_HAX (1 << 0)

struct per_cpu_data {
    struct hax_page    *vmxon_page;
    struct hax_page    *vmcs_page;
    struct vcpu_t      *current_vcpu;
    paddr_t            other_vmcs;
    cpuid_t            cpu_id;
    uint16             vmm_flag;
    uint16             nested;
    mword              host_cr4_vmxe;

    /*
     * These fields are used to record the result of certain VMX instructions
     * when they are used in a function wrapped by smp_call_function(). This is
     * because it is not safe to call hax_error(), etc. (whose underlying
     * implementation may use a lock) from the wrapped function to log a
     * failure; doing so may cause a deadlock and thus a host reboot, especially
     * on macOS, where mp_rendezvous_no_intrs() (the legacy Darwin API used by
     * HAXM to implement smp_call_function()) is known to be prone to deadlocks:
     * https://lists.apple.com/archives/darwin-kernel/2006/Dec/msg00006.html
     */
    vmx_error_t    vmxon_err;
    vmx_error_t    vmxoff_err;
    vmx_error_t    invept_err;

    /*
     * bit 0: valid
     * bit 1: VT support
     * bit 2: NX support
     * bit 3: EM64T support

     * bit 8: VT enabled
     * bit 9: NX enabled
     * bit 10: EM64T enabled
     *
     * bit 12: VMX initialization success
     */
#define HAX_CPUF_VALID          0x1
#define HAX_CPUF_SUPPORT_VT     0x2
#define HAX_CPUF_SUPPORT_NX     0x4
#define HAX_CPUF_SUPPORT_EM64T  0x8

#define HAX_CPUF_ENABLE_VT      0x10
#define HAX_CPUF_ENABLE_NX      0x20
#define HAX_CPUF_ENABLE_EM64T   0x40

#define HAX_CPUF_INITIALIZED    0x100
    uint16                   cpu_features;
    info_t                   vmx_info;
    struct                   cpu_pmu_info pmu_info;
#ifdef  DEBUG_HOST_STATE
    struct hstate_compare    hsc_pre;
    struct hstate_compare    hsc_post;
#endif
    struct hstate            hstate;
};

extern struct per_cpu_data ** hax_cpu_data;
static struct per_cpu_data * current_cpu_data(void)
{
    uint32 cpu_id = hax_cpuid();
    return hax_cpu_data[cpu_id];
}

static struct per_cpu_data * get_cpu_data(uint32 cpu_id)
{
    return hax_cpu_data[cpu_id];
}

static vmcs_t * current_cpu_vmcs(void)
{
    struct per_cpu_data *cpu_data = current_cpu_data();
    return (vmcs_t *)hax_page_va(cpu_data->vmcs_page);
}

void cpu_init_vmx(void *arg);
void cpu_exit_vmx(void *arg);
void cpu_enter_vmx(void *arg);

void cpu_pmu_init(void *arg);

void hax_panic_log(struct vcpu_t *vcpu);
void hax_clear_panic_log(struct vcpu_t *vcpu);

vmx_error_t cpu_vmx_run(struct vcpu_t *vcpu, struct hax_tunnel *htun);
int cpu_vmx_execute(struct vcpu_t *vcpu, struct hax_tunnel *htun);

vmx_error_t vmptrld(paddr_t vmcs, struct vcpu_t *vcpu);
vmx_error_t resume(paddr_t vmcs, struct vcpu_t *vcpu);
vmx_error_t launch(paddr_t vmcs, struct vcpu_t *vcpu);

extern struct hax_page *io_bitmap_page_a;
extern struct hax_page *io_bitmap_page_b;
extern struct hax_page *msr_bitmap_page;

extern struct config_t config;
#endif  // HAX_CORE_CPU_H_
