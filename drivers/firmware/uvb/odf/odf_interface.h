/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: odf interface header
 * Author: zhangrui
 * Create: 2025-04-18
 */
#ifndef ODF_INTERFACE_H
#define ODF_INTERFACE_H
#include <linux/acpi.h>

#define UBIOS_OD_NAME_LEN_MAX           16
#define UBIOS_OD_VERSION                1

#define UBIOS_OD_TYPE_U8                0x1
#define UBIOS_OD_TYPE_U16               0x2
#define UBIOS_OD_TYPE_U32               0x3
#define UBIOS_OD_TYPE_U64               0x4
#define UBIOS_OD_TYPE_S8                0x5
#define UBIOS_OD_TYPE_S16               0x6
#define UBIOS_OD_TYPE_S32               0x7
#define UBIOS_OD_TYPE_S64               0x8
#define UBIOS_OD_TYPE_BOOL              0x10
#define UBIOS_OD_TYPE_CHAR              0x20
#define UBIOS_OD_TYPE_STRING            0x21
#define UBIOS_OD_TYPE_STRUCT            0x30
#define UBIOS_OD_TYPE_TABLE             0x40
#define UBIOS_OD_TYPE_FILE              0x50
#define UBIOS_OD_TYPE_LIST              0x80

#define UBIOS_OD_ROOT_NAME              "root_table"
#define UBIOS_OD_INVALID_INDEX          0xFFFF
#define UBIOS_OD_PATH_SEPARATOR         '/'

#define ODF_FILE_NAME_CALL_ID_SERVICE   "call_id_service"
#define ODF_NAME_CIS_GROUP              "group"
#define ODF_NAME_CIS_UB                 "ub"
#define ODF_NAME_CIS_OWNER              "owner"
#define ODF_NAME_CIS_CIA                "cia"
#define ODF_NAME_CIS_CALL_ID            "call_id"
#define ODF_NAME_CIS_USAGE              "usage"
#define ODF_NAME_CIS_INDEX              "index"
#define ODF_NAME_CIS_FORWARDER_ID       "forwarder"

/* odf processing */
#define ODF_FILE_NAME_VIRTUAL_BUS       "virtual_bus"
#define ODF_NAME_UVB                    "uvb"
#define ODF_NAME_SECURE                 "secure"
#define ODF_NAME_DELAY                  "delay"
#define ODF_NAME_WD                     "wd"
#define ODF_NAME_OBTAIN                 "obtain"
#define ODF_NAME_ADDRESS                "address"
#define ODF_NAME_BUFFER                 "buffer"
#define ODF_NAME_SIZE                   "size"

/* UBRT table info */
#define ACPI_SIG_UBRT                   "UBRT"
#define UBRT_UB_CONTROLLER              0
#define UBRT_UMMU                       1
#define UBRT_UB_MEMORY                  2
#define UBRT_VIRTUAL_BUS                3
#define UBRT_CALL_ID_SERVICE            4

struct ubios_od_value_struct {
	char *name;
	u8 type;
	u32 data_length;
	void *data;
};

struct ubios_od_header {
	char name[16];
	u32 total_size;
	u8 version;
	u8 reserved[3];
	u32 remaining_size;
	u32 checksum;
};

/*
Data structure of UBIOS OD Root Table show below:
|----ubios_od_root----|
|  Header           |
|  count            |
|  reserved         |
|  odfs[0]          | if not 0  --point to-->  a od file
|  odfs[...]        | if not 0  --point to-->  a od file
|  odfs[count - 1]  | if not 0  --point to-->  a od file
*/
struct ubios_od_root {
	struct ubios_od_header header;
	u16 count;
	u8 reserved[6];
	u64 odfs[];
};

struct ubios_od_table_info {
	char *table_name;
	u16 row;
	u8 col;
	char *sub_name_start;
	void *value_start;
	void *table_end;
	u32 length_per_row;
};

struct ubios_od_list_info {
	char *name;
	u8 data_type;        /* not include list type */
	u16 count;           /* value count in the list */
	void *start;         /* pointer to the first value in the list */
	void *end;           /* end of list, not include */
};

/**
 * struct ubrt_sub_tables - UBRT Sub tables
 * @type:			type of tables
 * @pointer:		address to tables
 */
struct ubrt_sub_tables {
	u8 type;
	u8 reserved[7];
	u64 pointer;
};

/**
 * struct ubios_ubrt_table - UBRT info
 * @count:			count of tables
 * @sub tables:		Sub tables[count]
 */
struct ubios_ubrt_table {
	struct acpi_table_header header;
	u32 count;
	struct ubrt_sub_tables sub_tables[];
};

int odf_get_fdt_ubiostbl(u64 *phys_addr, char *tbl);

#endif
