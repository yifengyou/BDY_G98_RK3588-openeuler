/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __SPE_H
#define __SPE_H

#define SPE_BUFFER_MAX_SIZE		(PAGE_SIZE)
#define SPE_BUFFER_SIZE			(PAGE_SIZE / 16)

#define SPE_SAMPLE_PERIOD		1024

#define SPE_RECORD_BUFFER_MAX_RECORDS	(100)
#define SPE_RECORD_ENTRY_SIZE		sizeof(struct mem_sampling_record)
#define ARMV8_SPE_MEM_SAMPLING_PDEV_NAME "arm,mm_spe,spe-v1"

/* boost_spe sampling controls */
#define SYS_OMHTPG_EL1			sys_reg(3, 0, 15, 8, 2)
#define SYS_OMHTPG_EL1_RMCF_SHIFT	0
#define SYS_OMHTPG_EL1_RMCF_MASK	0x3UL
#define SYS_OMHTPG_EL1_RMEN		GENMASK(2, 2)
#define SYS_OMHTPG_EL1_RMEN_SHIFT	2
#define SYS_OMHTPG_EL1_PAFL		GENMASK(3, 3)
#define SYS_OMHTPG_EL1_PAFL_SHIFT	3
#define SYS_OMHTPG_EL1_PAFL_MASK	0x7FFFFFFUL
#define SYS_OMHTPG_EL1_PAFLMK_SHIFT	30
#define SYS_OMHTPG_EL1_PAFLMK_MASK	0x7FFFFFFUL
#define SYS_OMHTPG_EL1_PAEN_SHIFT	57

#define SYS_OMHTPG_EL1_RMPAFLEN_SHIFT	58
#define SYS_OMHTPG_EL1_POP_UOP_SEL	GENMASK(59, 59)
#define SYS_OMHTPG_EL1_SFT_CFG_SHIFT	60
#define SYS_OMHTPG_EL1_SFT_CFG_MASK	0x3UL
#define SYS_OMHTPG_EL1_REC_SEL		GENMASK(62, 62)

struct boost_spe_contol {
	u32				boost_spe_en_cfg;
	u32				pa_flt_pt;
	u32				pa_flt_mask;
	u64				sft_cfg;
	bool				boost_spe_pa_flt_en;
	bool				rmt_acc_en;
	bool				rmt_acc_pa_flt_en;
	bool				pop_uop_sel;
	bool				record_sel;
};

struct mm_spe {
	struct pmu			pmu;
	struct platform_device		*pdev;
	cpumask_t			supported_cpus;
	struct hlist_node		hotplug_node;
	struct boost_spe_contol		boost_spe;
	int				irq; /* PPI */
	u16				pmsver;
	u16				min_period;
	u16				counter_sz;
	u64				features;
	u16				max_record_sz;
	u16				align;
	u64				sample_period;
	local64_t			period_left;
	bool				jitter;
	bool				load_filter;
	bool				store_filter;
	bool				branch_filter;
	u64				inv_event_filter;
	u16				min_latency;
	u64				event_filter;
	bool				ts_enable;
	bool				pa_enable;
	u8				pct_enable;
	bool				exclude_user;
	bool				exclude_kernel;
	bool				support_boost_spe;
};

struct mm_spe_buf {
	void				*cur;		/* for spe raw data buffer */
	int				size;
	int				period;
	void				*base;

	void				*record_base;	/* for spe record buffer */
	int				record_size;
	int				nr_records;
};

#ifdef CONFIG_ARM_SPE_MEM_SAMPLING
void mm_spe_add_probe_status(void);
int mm_spe_percpu_buffer_alloc(int cpu);
int mm_spe_buffer_alloc(void);
void mm_spe_percpu_buffer_free(int cpu);
void mm_spe_buffer_free(void);
struct mm_spe *mm_spe_get_desc(void);
#else
static inline void mm_spe_add_probe_status(void) { }
static inline int mm_spe_percpu_buffer_alloc(int cpu) { return 0; }
static inline int mm_spe_buffer_alloc(void) { return 0; }
static inline void mm_spe_percpu_buffer_free(int cpu) { }
static inline void mm_spe_buffer_free(void) { }
static inline struct mm_spe *mm_spe_get_desc(void) { return NULL; }
#endif
#endif /* __SPE_H */
