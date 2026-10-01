/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025. Huawei Technologies Co., Ltd. All rights reserved.
 */
#ifndef __CCA_BASE_H
#define __CCA_BASE_H

#include <linux/kvm_host.h>
#include <linux/hugetlb.h>
#include <uapi/linux/kvm.h>

#include <asm/rmi_cmds.h>
#include <asm/rmi_smc.h>
#include <asm/virt.h>
#include <asm/kvm_pgtable.h>
#include <asm/cca_type.h>

struct cca_operations {
	int (*enable_cap)(struct kvm *kvm, struct kvm_enable_cap *cap);
	int (*init_realm_vm)(struct kvm *kvm);
	int (*realm_vm_enter)(struct kvm_vcpu *vcpu);
	int (*realm_vm_pre_enter)(struct kvm_vcpu *vcpu);
	int (*realm_vm_exit)(struct kvm_vcpu *vcpu, int ret);
	void (*init_sel2_hypervisor)(void);
	int (*psci_complete)(struct kvm_vcpu *calling, struct kvm_vcpu *target,
				unsigned long status);
	int (*create_vcpu)(struct kvm_vcpu *vcpu);
	void (*destroy_vcpu)(struct kvm_vcpu *vcpu);
	void (*destroy_vm)(struct kvm *kvm);
	int (*enable_realm)(struct kvm *kvm);
	u32 (*vgic_nr_lr)(void);
} ____cacheline_aligned;

struct cca_share_pages_operations {
	int (*alloc_shared_pages)(int p1, gfp_t p2, unsigned int p3);
	void (*free_shared_pages)(void *p1, unsigned int p2);
} ____cacheline_aligned;

int __init cca_operations_register(enum cca_cvm_type type, struct cca_operations *ops);
int __init cca_share_pages_ops_register(enum cca_cvm_type type,
			struct cca_share_pages_operations *ops);

int kvm_get_cvm_type(void);

int kvm_realm_enable_cap(struct kvm *kvm, struct kvm_enable_cap *cap);
void kvm_init_rme(void);

int kvm_rec_enter(struct kvm_vcpu *vcpu);
int kvm_rec_pre_enter(struct kvm_vcpu *vcpu);
int handle_rec_exit(struct kvm_vcpu *vcpu, int rec_run_ret);

int kvm_init_realm_vm(struct kvm *kvm);
void kvm_destroy_realm(struct kvm *kvm);

int kvm_create_rec(struct kvm_vcpu *vcpu);
void kvm_destroy_rec(struct kvm_vcpu *vcpu);

int realm_psci_complete(struct kvm_vcpu *calling, struct kvm_vcpu *target, unsigned long status);

u32 kvm_realm_vgic_nr_lr(void);

#endif /* __CCA_BASE_H */
