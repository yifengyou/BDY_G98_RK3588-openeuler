/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 ARM Ltd.
 */

#ifndef __ASM_KVM_RME_H
#define __ASM_KVM_RME_H

#include <asm/rmi_smc.h>
#include <uapi/linux/kvm.h>

/**
 * enum realm_state - State of a Realm
 */
enum realm_state {
	/**
	 * @REALM_STATE_NONE:
	 *      Realm has not yet been created. rmi_realm_create() may be
	 *      called to create the realm.
	 */
	REALM_STATE_NONE,
	/**
	 * @REALM_STATE_NEW:
	 *      Realm is under construction, not eligible for execution. Pages
	 *      may be populated with rmi_data_create().
	 */
	REALM_STATE_NEW,
	/**
	 * @REALM_STATE_ACTIVE:
	 *      Realm has been created and is eligible for execution with
	 *      rmi_rec_enter(). Pages may no longer be populated with
	 *      rmi_data_create().
	 */
	REALM_STATE_ACTIVE,
	/**
	 * @REALM_STATE_DYING:
	 *      Realm is in the process of being destroyed or has already been
	 *      destroyed.
	 */
	REALM_STATE_DYING,
	/**
	 * @REALM_STATE_DEAD:
	 *      Realm has been destroyed.
	 */
	REALM_STATE_DEAD
};

#define REALM_HUGETLB_FOLIO_NUM 509

/* Collect realm mapped folios */
struct realm_hugetlb_folios {
	struct list_head page_node;
	unsigned long folio_num;
	unsigned long folio_addr[REALM_HUGETLB_FOLIO_NUM];
};

/**
 * struct realm - Additional per VM data for a Realm
 *
 * @state: The lifetime state machine for the realm
 * @rd: Kernel mapping of the Realm Descriptor (RD)
 * @params: Parameters for the RMI_REALM_CREATE command
 * @num_aux: The number of auxiliary pages required by the RMM
 * @vmid: VMID to be used by the RMM for the realm
 * @ia_bits: Number of valid Input Address bits in the IPA
 */
struct realm {
	enum realm_state state;

	void *rd;
	struct realm_params *params;

	unsigned long num_aux;
	unsigned int vmid;
	unsigned int ia_bits;

#ifdef CONFIG_HISI_CCA
	KABI_EXTEND(bool hisi_cca_enable)
#endif
#ifdef CONFIG_HISI_CCADA_HOST
	KABI_EXTEND(struct list_head rdev_list)
#endif
	KABI_EXTEND(spinlock_t realm_lock)
	KABI_EXTEND(struct list_head page_list)
	KABI_EXTEND(struct list_head hugetlb_page_list)
	KABI_EXTEND(struct realm_hugetlb_folios *cur_rhf)
};

/**
 * struct realm_rec - Additional per VCPU data for a Realm
 *
 * @mpidr: MPIDR (Multiprocessor Affinity Register) value to identify this VCPU
 * @rec_page: Kernel VA of the RMM's private page for this REC
 * @aux_pages: Additional pages private to the RMM for this REC
 * @run: Kernel VA of the RmiRecRun structure shared with the RMM
 */
struct realm_rec {
	unsigned long mpidr;
	void *rec_page;
	/*
	 * REC_PARAMS_AUX_GRANULES is the maximum number of granules that the
	 * RMM can require. By using that to size the array we know that it
	 * will be big enough as the page size is always at least as large as
	 * the granule size. In the case of a larger page size than 4k (or an
	 * RMM which requires fewer auxiliary granules), the array will be
	 * bigger than needed however the extra memory required is small and
	 * this keeps the code cleaner.
	 */
	struct page *aux_pages[REC_PARAMS_AUX_GRANULES];
	struct rec_run *run;
};

void _kvm_init_rme(void);
u32 kvm_realm_ipa_limit(void);
u32 _kvm_realm_vgic_nr_lr(void);
u8 kvm_realm_max_pmu_counters(void);
unsigned int kvm_realm_sve_max_vl(void);

u64 kvm_realm_reset_id_aa64dfr0_el1(const struct kvm_vcpu *vcpu, u64 val);

bool kvm_rme_supports_sve(void);

int _kvm_realm_enable_cap(struct kvm *kvm, struct kvm_enable_cap *cap);
int _kvm_init_realm_vm(struct kvm *kvm);
void _kvm_destroy_realm(struct kvm *kvm);
void kvm_realm_destroy_rtts(struct kvm *kvm, u32 ia_bits);
int _kvm_create_rec(struct kvm_vcpu *vcpu);
void _kvm_destroy_rec(struct kvm_vcpu *vcpu);

int _kvm_rec_enter(struct kvm_vcpu *vcpu);
int _kvm_rec_pre_enter(struct kvm_vcpu *vcpu);
int _handle_rec_exit(struct kvm_vcpu *vcpu, int rec_run_status);

void kvm_realm_unmap_range(struct kvm *kvm,
			   unsigned long ipa,
			   unsigned long size,
			   bool unmap_private,
			   bool may_block);
int realm_map_protected(struct realm *realm,
			unsigned long base_ipa,
			kvm_pfn_t pfn,
			unsigned long size,
			struct kvm_mmu_memory_cache *memcache);
#ifdef CONFIG_HISI_CCADA_HOST
int realm_map_mmio_protected(struct realm *realm, unsigned long ipa,
			     kvm_pfn_t pfn, unsigned long map_size,
			     struct kvm_mmu_memory_cache *memcache);
#endif
int realm_map_non_secure(struct realm *realm,
			 unsigned long ipa,
			 kvm_pfn_t pfn,
			 unsigned long size,
			 struct kvm_mmu_memory_cache *memcache);
int _realm_psci_complete(struct kvm_vcpu *source,
			struct kvm_vcpu *target,
			unsigned long status);

int realm_create_protected_data_page(struct realm *realm,
				     unsigned long ipa,
				     struct page *dst_page,
				     struct page *src_page,
				     unsigned long flags);
int realm_create_rtt_levels(struct realm *realm, unsigned long ipa, int level,
			    int max_level, struct kvm_mmu_memory_cache *mc);
int fold_rtt(struct realm *realm, unsigned long addr, int level);

static inline bool kvm_realm_is_private_address(struct realm *realm,
						unsigned long addr)
{
	return !(addr & BIT(realm->ia_bits - 1));
}

int realm_add_hugetlb_folios(struct realm *realm, struct folio *folio);

#ifdef CONFIG_HISI_CCA
static inline bool kvm_realm_supports_hisi_cca(struct realm *realm)
{
	return realm->hisi_cca_enable;
}
#endif

#endif /* __ASM_KVM_RME_H */
