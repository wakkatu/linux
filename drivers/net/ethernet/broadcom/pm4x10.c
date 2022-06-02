/*
 * Copyright (C) 2016 Broadcom
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/soc/bcm/iproc-cmic.h>
#include "pm.h"
#include "merlin16_ucode.h"

#define JUMBO_MAXSZ  0x3fe8

#define debug(fmt, args...) do {} while (0)


#define PM_CORE_ADDR(port)			((1 << (port + 1)) + 1)

#define PM_PMD_X1_CTL_REG(port)							(0x00009010 | (PM_CORE_ADDR(port) << 19))
#define PM_PMD_X4_CTL_REG(port)							(0x0000c010 | (PM_CORE_ADDR(port) << 19))
#define PM_CKRST_LN_CLK_RST_N_PWRDWN_CTL_REG(port)		(0x0001d0b1 | (PM_CORE_ADDR(port) << 19))
#define PM_DIG_TOP_USER_CTL0_REG(port)					(0x0001d104 | (PM_CORE_ADDR(port) << 19))
#define PM_TXFIR_MISC_CTL1_REG(port)					(0x0001d139 | (PM_CORE_ADDR(port) << 19))

#define TSC_OPERATION_WRITE                      0x1
#define TSC_OPERATION_READ                       0x0


/* S-channel address */
/* Per port registers */
#define XLMIB_GRx64(port)								(0x00000000 + port)
#define XLMIB_GRx127(port)								(0x00000100 + port)
#define XLMIB_GRx255(port)								(0x00000200 + port)
#define XLMIB_GRx511(port)								(0x00000300 + port)
#define XLMIB_GRx1023(port)								(0x00000400 + port)
#define XLMIB_GRx1518(port)								(0x00000500 + port)
#define XLMIB_GRx1522(port)								(0x00000600 + port)
#define XLMIB_GRx2047(port)								(0x00000700 + port)
#define XLMIB_GRx4095(port)								(0x00000800 + port)
#define XLMIB_GRx9216(port)								(0x00000900 + port)
#define XLMIB_GRx16383(port)							(0x00000a00 + port)
#define XLMIB_GRxPkt(port)								(0x00000b00 + port)
#define XLMIB_GRxUCA(port)								(0x00000c00 + port)
#define XLMIB_GRxMCA(port)								(0x00000d00 + port)
#define XLMIB_GRxBCA(port)								(0x00000e00 + port)
#define XLMIB_GRxFCS(port)								(0x00000f00 + port)
#define XLMIB_GRxCF(port)								(0x00001000 + port)
#define XLMIB_GRxPF(port)								(0x00001100 + port)
#define XLMIB_GRxUO(port)								(0x00001300 + port)
#define XLMIB_GRxWSA(port)								(0x00001500 + port)
#define XLMIB_GRxALN(port)								(0x00001600 + port)
#define XLMIB_GRxFLR(port)								(0x00001700 + port)
#define XLMIB_GRxOVR(port)								(0x00001a00 + port)
#define XLMIB_GRxJBR(port)								(0x00001b00 + port)
#define XLMIB_GRxMTUE(port)								(0x00001c00 + port)
#define XLMIB_GRxVLN(port)								(0x00001f00 + port)
#define XLMIB_GRxDVLN(port)								(0x00002000 + port)
#define XLMIB_GRxTRFU(port)								(0x00002100 + port)
#define XLMIB_GRxPOK(port)								(0x00002200 + port)
#define XLMIB_GRxUND(port)								(0x00003400 + port)
#define XLMIB_GRxFRG(port)								(0x00003500 + port)
#define XLMIB_GRxByt(port)								(0x00003d00 + port)
#define XLMIB_GTx64(port)								(0x00004000 + port)
#define XLMIB_GTx127(port)								(0x00004100 + port)
#define XLMIB_GTx255(port)								(0x00004200 + port)
#define XLMIB_GTx511(port)								(0x00004300 + port)
#define XLMIB_GTx1023(port)								(0x00004400 + port)
#define XLMIB_GTx1518(port)								(0x00004500 + port)
#define XLMIB_GTx1522(port)								(0x00004600 + port)
#define XLMIB_GTx2047(port)								(0x00004700 + port)
#define XLMIB_GTx4095(port)								(0x00004800 + port)
#define XLMIB_GTx9216(port)								(0x00004900 + port)
#define XLMIB_GTx16383(port)							(0x00004a00 + port)
#define XLMIB_GTxPOK(port)								(0x00004b00 + port)
#define XLMIB_GTxPkt(port)								(0x00004c00 + port)
#define XLMIB_GTxUCA(port)								(0x00004d00 + port)
#define XLMIB_GTxMCA(port)								(0x00004e00 + port)
#define XLMIB_GTxBCA(port)								(0x00004f00 + port)
#define XLMIB_GTxPF(port)								(0x00005000 + port)
#define XLMIB_GTxJBR(port)								(0x00005200 + port)
#define XLMIB_GTxFCS(port)								(0x00005300 + port)
#define XLMIB_GTxCF(port)								(0x00005400 + port)
#define XLMIB_GTxOVR(port)								(0x00005500 + port)
#define XLMIB_GTxFRG(port)								(0x00005c00 + port)
#define XLMIB_GTxErr(port)								(0x00005d00 + port)
#define XLMIB_GTxVLN(port)								(0x00005e00 + port)
#define XLMIB_GTxDVLN(port)								(0x00005f00 + port)
#define XLMIB_GTxUFL(port)								(0x00006100 + port)
#define XLMIB_GTxNCL(port)								(0x00006e00 + port)
#define XLMIB_GTxBYT(port)								(0x00006f00 + port)

#define XLPORT_CONFIG(port)								(0x00020000 + port)
#define XLMAC_CTRL(port)								(0x00060000 + port)
#define  XLMAC_CTRL__SW_LINK_STATUS						12
#define  XLMAC_CTRL__XGMII_IPG_CHECK_DISABLE			11
#define  XLMAC_CTRL__SOFT_RESET							6
#define  XLMAC_CTRL__LOCAL_LPBK							2
#define  XLMAC_CTRL__RX_EN								1
#define  XLMAC_CTRL__TX_EN								0
#define XLMAC_MODE(port)								(0x00060100 + port)
#define  XLMAC_MODE_OFFSET								0x1
#define  XLMAC_MODE__SPEED_MODE_L						6
#define  XLMAC_MODE__SPEED_MODE_R						4
#define  XLMAC_MODE__SPEED_MODE_WIDTH					3
#define   SPEED_MODE_LINK_10M							0x0
#define   SPEED_MODE_LINK_100M							0x1
#define   SPEED_MODE_LINK_1G							0x2
#define   SPEED_MODE_LINK_2G5							0x3
#define   SPEED_MODE_LINK_10G_PLUS						0x4
#define  XLMAC_MODE__SPEED_MODE_RESETVALUE				0x4
#define  XLMAC_MODE__NO_SOP_FOR_CRC_HG					3
#define  XLMAC_MODE__NO_SOP_FOR_CRC_HG_WIDTH			1
#define  XLMAC_MODE__NO_SOP_FOR_CRC_HG_RESETVALUE		0x0
#define  XLMAC_MODE__HDR_MODE_L							2
#define  XLMAC_MODE__HDR_MODE_R							0
#define  XLMAC_MODE__HDR_MODE_WIDTH 					3
#define   HDR_MODE_IEEE									0x0
#define   HDR_MODE_HG_PLUS								0x1
#define   HDR_MODE_HG_2									0x2
#define   HDR_MODE_SOP_ONLY_IEEE						0x5
#define XLMAC_TX_CTRL(port)								(0x00060400 + port)
#define  XLMAC_TX_CTRL__AVERAGE_IPG_R					12
#define  XLMAC_TX_CTRL__PAD_EN							4
#define  XLMAC_TX_CTRL__CRC_MODE_R						0
#define   CRC_MODE_APPEND								0x0
#define   CRC_MODE_KEEP									0x1
#define   CRC_MODE_REPLACE								0x2
#define   CRC_MODE_PER_PKT_MODE							0x3
#define XLMAC_TX_MAC_SA(port)							(0x00060500 + port)
#define XLMAC_RX_MAX_SIZE(port)							(0x00060800 + port)
#define  XLMAC_RX_MAX_SIZE__RX_MAX_SIZE_L				13
#define  XLMAC_RX_MAX_SIZE__RX_MAX_SIZE_R				0
#define  XLMAC_RX_MAX_SIZE__RX_MAX_SIZE_WIDTH			14
#define XLMAC_RX_CTRL(port)								(0x00060600 + port)
#define  XLMAC_RX_CTRL__STRIP_CRC						2
#define XLMAC_RX_LSS_CTRL(port)							(0x00060a00 + port)
#define  XLMAC_RX_LSS_CTRL__DROP_TX_DATA_ON_LINK_INTERRUPT	6
#define  XLMAC_RX_LSS_CTRL__DROP_TX_DATA_ON_REMOTE_FAULT	5
#define  XLMAC_RX_LSS_CTRL__DROP_TX_DATA_ON_LOCAL_FAULT	4
#define  XLMAC_RX_LSS_CTRL__REMOTE_FAULT_DISABLE		1
#define  XLMAC_RX_LSS_CTRL__LOCAL_FAULT_DISABLE			0
#define XLMAC_PAUSE_CTRL(port)							(0x00060d00 + port)
#define  XLMAC_PAUSE_CTRL__RX_PAUSE_EN					18
#define  XLMAC_PAUSE_CTRL__TX_PAUSE_EN					17
#define XLMAC_PFC_CTRL(port)							(0x00060e00 + port)
#define  XLMAC_PFC_CTRL__PFC_REFRESH_EN					32
/* General type registers */
#define XLPORT_MODE_REG									(0x02020a00)
#define  XLPORT_MODE_REG__RESET_MASK					0x3f
#define  XLPORT_MODE_REG__XPORT0_CORE_PORT_MODE_L		5
#define  XLPORT_MODE_REG__XPORT0_CORE_PORT_MODE_R		3
#define  XLPORT_MODE_REG__XPORT0_CORE_PORT_MODE_WIDTH	3
#define   XPORT0_CORE_PORT_MODE_QUAD					0x0
#define   XPORT0_CORE_PORT_MODE_TRI_012					0x1
#define   XPORT0_CORE_PORT_MODE_TRI_023					0x2
#define   XPORT0_CORE_PORT_MODE_DUAL					0x3
#define   XPORT0_CORE_PORT_MODE_SINGLE					0x4
#define  XLPORT_MODE_REG__XPORT0_PHY_PORT_MODE_L		2
#define  XLPORT_MODE_REG__XPORT0_PHY_PORT_MODE_R		0
#define  XLPORT_MODE_REG__XPORT0_PHY_PORT_MODE_WIDTH 	3
#define   XPORT0_PHY_PORT_MODE_QUAD						0x0
#define   XPORT0_PHY_PORT_MODE_TRI_012					0x1
#define   XPORT0_PHY_PORT_MODE_TRI_023					0x2
#define   XPORT0_PHY_PORT_MODE_DUAL						0x3
#define   XPORT0_PHY_PORT_MODE_SINGLE					0x4
#define XLPORT_ENABLE_REG								(0x02020b00)
#define  XLPORT_ENABLE_REG__PORT3						3
#define  XLPORT_ENABLE_REG__PORT2						2
#define  XLPORT_ENABLE_REG__PORT1						1
#define  XLPORT_ENABLE_REG__PORT0						0
#define XLPORT_MAC_CONTROL								(0x02021000)
#define  XLPORT_MAC_CONTROL__RX_DUAL_CYCLE_TDM_EN		5
#define  XLPORT_MAC_CONTROL__RX_NON_LINEAR_QUAD_TDM_EN	3
#define  XLPORT_MAC_CONTROL__RX_FLEX_TDM_ENABLE			2
#define  XLPORT_MAC_CONTROL__XMAC0_BYPASS_OSTS			1
#define  XLPORT_MAC_CONTROL__XMAC0_RESET				0
#define XLPORT_XGXS0_CTRL_REG							(0x02021400)
#define  XLPORT_XGXS0_CTRL_REG__RefSel        8
#define  XLPORT_XGXS0_CTRL_REG__RefCMOS       7
#define  XLPORT_XGXS0_CTRL_REG__Pwrdwn_CML_LC 6
#define  XLPORT_XGXS0_CTRL_REG__Pwrdwn_CML    5
#define  XLPORT_XGXS0_CTRL_REG__IDDQ					4
#define  XLPORT_XGXS0_CTRL_REG__PWRDWN					3
#define  XLPORT_XGXS0_CTRL_REG__Refin_EN      2
#define  XLPORT_XGXS0_CTRL_REG__RSTB_HW					0
#define XLPORT_WC_UCMEM_CTRL							(0x02021900)
#define  XLPORT_WC_UCMEM_CTRL__ACCESS_MODE				0
#define XLPORT_MIB_RESET								(0x02022400)
#define  XLPORT_MIB_RESET__CLR_CNT_L					3
#define  XLPORT_MIB_RESET__CLR_CNT_R					0
#define XLPORT_INTR_STATUS								(0x02022900)
#define XLPORT_INTR_ENABLE								(0x02022a00)
#define XLPORT_SOFT_RESET								(0x02020c00)
#define  XLPORT_SOFT_RESET__PORT3						3
#define  XLPORT_SOFT_RESET__PORT2						2
#define  XLPORT_SOFT_RESET__PORT1						1
#define  XLPORT_SOFT_RESET__PORT0						0
#define XLPORT_POWER_SAVE								(0x02020d00)
#define  XLPORT_POWER_SAVE__XPORT_CORE0					0

#define XLPORT_PORT_FIELD(reg, port)					reg##__PORT##port
#define XLPORT_PORT_FIELD_SET(_r, _p, _v)  { \
	if (_p == 0)    	val |= (1 << _r##__PORT0); \
	else if (_p == 1)   val |= (1 << _r##__PORT1); \
	else if (_p == 2)   val |= (1 << _r##__PORT2); \
	else if (_p == 3)   val |= (1 << _r##__PORT3); \
}

#define XLPORT_PORT_FIELD_CLEAR(_r, _p, _v)  { \
	if (_p == 0)    	val &= ~(1 << _r##__PORT0); \
	else if (_p == 1)   val &= ~(1 << _r##__PORT1); \
	else if (_p == 2)   val &= ~(1 << _r##__PORT2); \
	else if (_p == 3)   val &= ~(1 << _r##__PORT3); \
}

static u32 pm4x10_enabled = 0;

static inline void
xlmac_reg64_write(u32 addr, u64 val)
{
	iproc_cmic_schan_reg64_write(CMIC_BLOCK_TYPE_APM, addr, val);
}

static inline u64
xlmac_reg64_read(u32 addr)
{
	return iproc_cmic_schan_reg64_read(CMIC_BLOCK_TYPE_APM, addr);
}

static inline void
xlport_reg32_write(u32 addr, u32 val)
{
	iproc_cmic_schan_reg32_write(CMIC_BLOCK_TYPE_APM, addr, val);
}

static inline u32
xlport_reg32_read(u32 addr)
{
	return iproc_cmic_schan_reg32_read(CMIC_BLOCK_TYPE_APM, addr);
}


/* MDIO address for each lane in this PM */
static u32 lane_mdio_addr[4] = { 3, 4, 5, 6 };

static inline void
pm_phy_sbus_write(u32 lane, u32 addr, u32 val, u32 mask, u32 shift)
{
	u32 device, mem_data[4];

  /* TSC register address (indirect access) */
  if ((addr == 0x0002) || (addr == 0x0003) || ((addr <= 0xc340) && (addr >= 0x9000)))
  	device = 0; /* PCS (TSC) */
  else
  	device = 1; /* PMA/PMD (Physical Media Device or called serdes(merlin)) */

  mem_data[0] = (device << 27) | (lane_mdio_addr[lane] << 19) | (lane << 16) | addr;
  mem_data[1] = ((val << shift) << 16) |       /* data */
                (~(mask << shift) & 0xffff);   /* mask */
  mem_data[2] = TSC_OPERATION_WRITE;
  mem_data[3] = 0;
	iproc_cmic_schan_ucmem_write(CMIC_BLOCK_TYPE_APM, mem_data);
	}

static inline u32
pm_phy_sbus_read(u32 lane, u32 addr, u32 *val)
{
	u32 device, mem_data[4];

	/* TSC register address (indirect access) */
  if ((addr == 0x0002) || (addr == 0x0003) || ((addr <= 0xc340) && (addr >= 0x9000)))
  	device = 0; /* PCS (TSC) */
  else
  	device = 1; /* PMA/PMD (Physical Media Device or called serdes(merlin)) */

  mem_data[0] = (device << 27) | (lane_mdio_addr[lane] << 19) | (lane << 16) | addr;
  mem_data[1] = 0;
  mem_data[2] = TSC_OPERATION_READ;
  mem_data[3] = 0;
	iproc_cmic_schan_ucmem_write(CMIC_BLOCK_TYPE_APM, mem_data);
	*val = iproc_cmic_schan_ucmem_read(CMIC_BLOCK_TYPE_APM, mem_data);
	return 0;
}


static void cmpw(u8 readonly, u32 addr, u32 val)
{
	u8 i;
	
	if (readonly)
  {
    //printk("(read only) Reg addr = 0x%x, current = 0x%x, expect = 0x%x\n", reg_addr, get_val, reg_data_val);
  } else
  {
   	//printk("(write) Reg addr = 0x%x, current = 0x%x, expect = 0x%x\n", reg_addr, get_val, reg_data_val);
    for (i=0; i<4; ++i)  /* per lane */
  	  if ((i % 2) == 0)
        pm_phy_sbus_write(i, addr, val, 0xffff, 0);
  }
}

static inline u32
pm_phy_configure(int port)
{
	cmpw(0, 0x0002, 0x600d);	cmpw(0, 0x0003, 0x8770);	cmpw(0, 0x000d, 0x0000);	cmpw(0, 0x000e, 0x0000);
	cmpw(0, 0x0096, 0x0000);	cmpw(0, 0x0097, 0x0000);	cmpw(0, 0x0098, 0x0000);  cmpw(0, 0x0099, 0x0000);
  cmpw(0, 0x009a, 0x0000);  cmpw(0, 0x009b, 0x0000);  cmpw(0, 0x9000, 0x6000);  cmpw(0, 0x9001, 0x00aa);
  cmpw(0, 0x9003, 0xe4e4);  cmpw(0, 0x9004, 0x0083);  cmpw(0, 0x9005, 0x0000);  cmpw(0, 0x9007, 0x0000);
  cmpw(0, 0x9008, 0x0000);  cmpw(0, 0x9009, 0x0000);  cmpw(0, 0x900a, 0xf800);  cmpw(0, 0x900e, 0x0312);
  cmpw(0, 0x9010, 0x0003);  cmpw(0, 0x9011, 0x0000);  cmpw(1, 0x9012, 0x0002);  cmpw(1, 0x9013, 0x0002);
  cmpw(0, 0x9014, 0x0000);  cmpw(0, 0x9030, 0x0000);  cmpw(0, 0x9031, 0x0028);  cmpw(0, 0x9032, 0x0000);
  cmpw(0, 0x9033, 0x0000);  cmpw(0, 0x9034, 0x0000);  cmpw(0, 0x9035, 0x0000);  cmpw(0, 0x9037, 0x0000);
  cmpw(0, 0x9038, 0x0000);  cmpw(0, 0x9039, 0x0000);  cmpw(0, 0x903a, 0x0000);  cmpw(0, 0x903b, 0x0000);
  cmpw(0, 0x903c, 0x0000);  cmpw(0, 0x903d, 0x0000);  cmpw(0, 0x903e, 0x0000);  cmpw(0, 0x9040, 0x0000);
  cmpw(0, 0x9041, 0x0000);  cmpw(0, 0x9042, 0x0000);  cmpw(0, 0x9043, 0x0000);  cmpw(0, 0x9044, 0x0000);
  cmpw(0, 0x9045, 0x0000);  cmpw(0, 0x9050, 0x0000);  cmpw(0, 0x9051, 0x0000);  cmpw(0, 0x9052, 0x0000);
  cmpw(0, 0x9053, 0x0000);  cmpw(0, 0x9054, 0x0000);  cmpw(0, 0x9055, 0x0000);  cmpw(0, 0x9056, 0x0000);
  cmpw(0, 0x9057, 0x0000);  cmpw(0, 0x9058, 0x0000);  cmpw(0, 0x9059, 0x0000);  cmpw(0, 0x905a, 0x0000);
  cmpw(0, 0x9060, 0x0000);  cmpw(0, 0x9061, 0x0000);  cmpw(0, 0x9062, 0x0000);  cmpw(1, 0x90b1, 0x0000);
  cmpw(0, 0x90b3, 0x0000);  cmpw(0, 0x90b4, 0x001d);  cmpw(0, 0x90b5, 0xffff);  cmpw(0, 0x9123, 0x3fff);
  cmpw(0, 0x9130, 0x7690);  cmpw(0, 0x9131, 0xc4f0);  cmpw(0, 0x9132, 0xe647);  cmpw(0, 0x9140, 0x0000);
  cmpw(0, 0x9141, 0x0000);  cmpw(0, 0x9142, 0x0000);  cmpw(0, 0x9220, 0x0101);  cmpw(0, 0x9221, 0x6140);
  cmpw(0, 0x9222, 0xeea7);  cmpw(0, 0x9230, 0x4010);  cmpw(0, 0x9231, 0x0400);  cmpw(0, 0x9232, 0x0041);
  cmpw(0, 0x9233, 0x8090);  cmpw(0, 0x9234, 0xa0b0);  cmpw(0, 0x9235, 0xc0d0);  cmpw(0, 0x9236, 0xe070);
  cmpw(0, 0x9237, 0x0001);  cmpw(0, 0x9238, 0xf0f0);  cmpw(0, 0x9239, 0xf0f0);  cmpw(0, 0x923a, 0xf0f0);
  cmpw(0, 0x923b, 0xf0f0);  cmpw(0, 0x923c, 0x0003);  cmpw(0, 0x9240, 0x0000);  cmpw(0, 0x9241, 0x0000);
  cmpw(0, 0x9242, 0x0000);  cmpw(0, 0x9243, 0x0000);  cmpw(0, 0x9244, 0x0000);  cmpw(0, 0x9245, 0x0000);
  cmpw(0, 0x9246, 0x0000);  cmpw(0, 0x9247, 0x0000);  cmpw(0, 0x9248, 0x0000);  cmpw(0, 0x9251, 0x029a);
  cmpw(0, 0x9252, 0x0000);  cmpw(0, 0x9253, 0x10ed);  cmpw(0, 0x9254, 0x0000);  cmpw(0, 0x9255, 0x14d4);
  cmpw(0, 0x9256, 0x029a);  cmpw(0, 0x9257, 0x8382);  cmpw(0, 0x9258, 0x0bb8);  cmpw(0, 0x9259, 0x0a6a);
  cmpw(0, 0x925a, 0x029a);  cmpw(0, 0x925b, 0x0a6a);  cmpw(0, 0x925c, 0x029a);  cmpw(0, 0x925d, 0x3b5f);
  cmpw(0, 0x925e, 0x006b);  cmpw(0, 0x9260, 0x0000);  cmpw(0, 0x9261, 0x0000);  cmpw(0, 0x9262, 0x00ff);
  cmpw(0, 0x9263, 0x0002);  cmpw(0, 0x9264, 0x0000);  cmpw(0, 0x9270, 0xff00);  cmpw(0, 0x9272, 0x0000);
  cmpw(0, 0x9273, 0x0000);  cmpw(0, 0x9274, 0x0400);  cmpw(0, 0x9275, 0x0000);  cmpw(0, 0x9276, 0x0000);
  cmpw(0, 0x9277, 0x0000);  cmpw(0, 0x9278, 0x0000);  cmpw(0, 0x9279, 0x0000);  cmpw(0, 0x927a, 0x0000);
  cmpw(0, 0x9280, 0xff00);  cmpw(0, 0x9282, 0x0000);  cmpw(0, 0x9283, 0x0000);  cmpw(0, 0x9284, 0x0400);
  cmpw(0, 0x9285, 0x0000);  cmpw(0, 0x9286, 0x0000);  cmpw(0, 0x9287, 0x0000);  cmpw(0, 0x9288, 0x0000);
  cmpw(0, 0x9289, 0x0000);  cmpw(0, 0x928a, 0x0000);  cmpw(0, 0x9290, 0xff00);  cmpw(0, 0x9292, 0x0000);
  cmpw(0, 0x9293, 0x0000);  cmpw(0, 0x9294, 0x0400);  cmpw(0, 0x9295, 0x0000);  cmpw(0, 0x9296, 0x0000);
  cmpw(0, 0x9297, 0x0000);  cmpw(0, 0x9298, 0x0000);  cmpw(0, 0x9299, 0x0000);  cmpw(0, 0x929a, 0x0000);
  cmpw(0, 0x92a0, 0xff00);  cmpw(0, 0x92a2, 0x0000);  cmpw(0, 0x92a3, 0x0000);  cmpw(0, 0x92a4, 0x0400);
  cmpw(0, 0x92a5, 0x0000);  cmpw(0, 0x92a6, 0x0000);  cmpw(0, 0x92a7, 0x0000);  cmpw(0, 0x92a8, 0x0000);
  cmpw(0, 0x92a9, 0x0000);  cmpw(0, 0x92aa, 0x0000);  cmpw(0, 0xa000, 0xfffc);  cmpw(0, 0xa001, 0x8030);
  cmpw(0, 0xa002, 0x0070);  cmpw(0, 0xa003, 0x0064);  cmpw(1, 0xa011, 0x0812);  cmpw(0, 0xa020, 0x0000);
  cmpw(0, 0xa021, 0x0000);  cmpw(0, 0xa022, 0x0000);  cmpw(0, 0xa023, 0x1400);  cmpw(0, 0xa024, 0x030f);
  cmpw(0, 0xa031, 0x0000);  cmpw(0, 0xa032, 0x0000);  cmpw(0, 0xa080, 0x0000);  cmpw(0, 0xa081, 0x0000);
  cmpw(0, 0xa085, 0x0000);  cmpw(0, 0xa086, 0x8000);  cmpw(0, 0xc010, 0xc003);  cmpw(0, 0xc011, 0x0000);
  cmpw(1, 0xc012, 0x000e);  cmpw(0, 0xc013, 0x000c);  cmpw(0, 0xc014, 0x0000);  cmpw(0, 0xc018, 0x0000);
  cmpw(0, 0xc019, 0x0000);  cmpw(0, 0xc040, 0x0000);  cmpw(0, 0xc041, 0x0000);  cmpw(0, 0xc042, 0x0000);
  cmpw(0, 0xc043, 0x0000);
  //cmpw(0, 0xc050, 0x0135);  /* speed = 10M   */
  //cmpw(0, 0xc050, 0x0136);  /* speed = 100M  */
  cmpw(0, 0xc050, 0x0137);  /* speed = 1000M */
  //cmpw(0, 0xc050, 0x011c);  /* speed = 10G   */
  cmpw(1, 0xc051, 0x0000);  cmpw(0, 0xc052, 0x0000);  cmpw(1, 0xc054, 0xef62);  cmpw(0, 0xc055, 0x0000);
  cmpw(0, 0xc058, 0x0000);  cmpw(0, 0xc060, 0x0000);  cmpw(0, 0xc061, 0x0000);  cmpw(0, 0xc070, 0x1c00);
  cmpw(0, 0xc072, 0x0e05);  cmpw(0, 0xc073, 0x0830);  cmpw(0, 0xc074, 0x0010);  cmpw(0, 0xc075, 0x0021);
  cmpw(0, 0xc076, 0x0000);  cmpw(0, 0xc077, 0x0040);  cmpw(0, 0xc078, 0x0004);  cmpw(0, 0xc079, 0x0000);
  cmpw(0, 0xc07a, 0x0000);  cmpw(0, 0xc100, 0x0000);  cmpw(0, 0xc101, 0x0000);  cmpw(0, 0xc102, 0x0000);
  cmpw(0, 0xc103, 0x0000);  cmpw(0, 0xc104, 0x0000);  cmpw(0, 0xc105, 0x0000);  cmpw(0, 0xc111, 0x0000);
  cmpw(0, 0xc112, 0x01b4);  cmpw(0, 0xc113, 0x01cb);  cmpw(0, 0xc114, 0x0000);  cmpw(1, 0xc120, 0x4811);
  cmpw(1, 0xc121, 0x0084);  cmpw(0, 0xc130, 0x0000);  cmpw(0, 0xc131, 0x2000);  cmpw(0, 0xc132, 0x442c);
  cmpw(0, 0xc133, 0x0000);  cmpw(0, 0xc134, 0x0870);  cmpw(0, 0xc135, 0x0000);  cmpw(0, 0xc136, 0x0000);
  cmpw(0, 0xc137, 0x0001);  cmpw(0, 0xc139, 0x0000);  cmpw(0, 0xc13d, 0x14a0);  cmpw(0, 0xc140, 0x0000);
  cmpw(0, 0xc141, 0x0000);  cmpw(0, 0xc142, 0x0000);  cmpw(0, 0xc143, 0x0000);  cmpw(0, 0xc144, 0x0000);
  cmpw(0, 0xc145, 0x0000);  cmpw(0, 0xc146, 0x0000);  cmpw(0, 0xc147, 0x0000);  cmpw(0, 0xc148, 0x0000);
  cmpw(0, 0xc149, 0x0000);  cmpw(0, 0xc14a, 0x0000);  cmpw(0, 0xc14b, 0x0000);  cmpw(0, 0xc14c, 0x0000);
  cmpw(0, 0xc14d, 0x0000);  cmpw(0, 0xc14e, 0x0000);  cmpw(0, 0xc152, 0x0000);  cmpw(0, 0xc153, 0x0000);
  cmpw(0, 0xc154, 0x0000);  cmpw(0, 0xc155, 0x0000);  cmpw(0, 0xc156, 0x0000);  cmpw(0, 0xc157, 0x0000);
  cmpw(0, 0xc158, 0x0000);  cmpw(0, 0xc159, 0x0000);  cmpw(0, 0xc15a, 0x0000);  cmpw(0, 0xc15b, 0x0000);
  cmpw(0, 0xc15c, 0x0000);  cmpw(0, 0xc161, 0x0000);  cmpw(0, 0xc162, 0x0000);  cmpw(0, 0xc163, 0x0000);
  cmpw(0, 0xc170, 0x0000);  cmpw(0, 0xc171, 0x0000);  cmpw(0, 0xc172, 0x0000);  cmpw(0, 0xc173, 0x0000);
  cmpw(0, 0xc174, 0x0000);  cmpw(0, 0xc175, 0x0000);  cmpw(0, 0xc176, 0x0000);  cmpw(0, 0xc177, 0x0000);
  cmpw(0, 0xc178, 0x0000);  cmpw(0, 0xc179, 0x0000);  cmpw(0, 0xc17a, 0x0000);  cmpw(0, 0xc17b, 0x0000);
  cmpw(0, 0xc17c, 0x0000);  cmpw(0, 0xc17d, 0x0000);  cmpw(0, 0xc180, 0x0000);  cmpw(0, 0xc181, 0x0056);
  cmpw(0, 0xc182, 0x0005);  cmpw(0, 0xc183, 0x2000);  cmpw(0, 0xc184, 0x0001);  cmpw(0, 0xc185, 0x02a1);
  cmpw(0, 0xc186, 0x0168);  cmpw(0, 0xc187, 0x0000);  cmpw(0, 0xc188, 0x0000);  cmpw(0, 0xc190, 0x0000);
  cmpw(0, 0xc191, 0x0000);  cmpw(0, 0xc192, 0x0000);  cmpw(0, 0xc193, 0x0000);  cmpw(0, 0xc194, 0x0000);
  cmpw(0, 0xc195, 0x0000);  cmpw(0, 0xc196, 0x0000);  cmpw(0, 0xc197, 0x0000);  cmpw(0, 0xc198, 0x0000);
  cmpw(0, 0xc199, 0x0000);  cmpw(0, 0xc19a, 0x0000);  cmpw(0, 0xc1a0, 0x0000);  cmpw(0, 0xc1a1, 0x0000);
  cmpw(0, 0xc1a2, 0x0000);  cmpw(0, 0xc1a3, 0x0000);  cmpw(0, 0xc1a4, 0x0000);  cmpw(0, 0xc1a5, 0x0000);
  cmpw(0, 0xc1a6, 0x0000);  cmpw(0, 0xc1a7, 0x0000);  cmpw(0, 0xc1a8, 0x0000);  cmpw(0, 0xc1a9, 0x0000);
  cmpw(0, 0xc1aa, 0x0000);  cmpw(0, 0xc1ab, 0x0030);  cmpw(0, 0xc1ac, 0x0000);  cmpw(0, 0xc1ad, 0x0001);
  cmpw(0, 0xc1ae, 0x0000);  cmpw(0, 0xc253, 0x4000);  cmpw(0, 0xc30a, 0x0000);  cmpw(0, 0xc30b, 0x0000);
  cmpw(0, 0xc330, 0x0002);  cmpw(0, 0xc340, 0x0011);  cmpw(0, 0xd001, 0x0205);  cmpw(0, 0xd002, 0x0690);
  cmpw(0, 0xd003, 0x00f0);  cmpw(0, 0xd004, 0x2401);  cmpw(1, 0xd005, 0xf07c);  cmpw(1, 0xd006, 0x0002);
  cmpw(1, 0xd007, 0x6969);  cmpw(1, 0xd008, 0x3414);  cmpw(1, 0xd009, 0x5878);  cmpw(1, 0xd00a, 0x00e0);
  cmpw(0, 0xd00b, 0x0060);  cmpw(0, 0xd00d, 0x0805);  cmpw(0, 0xd00e, 0x0000);  cmpw(0, 0xd010, 0x0028);
  cmpw(0, 0xd011, 0x0200);  cmpw(0, 0xd012, 0x0087);  cmpw(0, 0xd013, 0x1c1e);  cmpw(0, 0xd014, 0x35ad);
  cmpw(0, 0xd015, 0x35af);  cmpw(0, 0xd016, 0x340d);  cmpw(0, 0xd017, 0x0000);  cmpw(0, 0xd018, 0x0011);
  cmpw(0, 0xd019, 0x0000);  cmpw(1, 0xd01a, 0x0004);  cmpw(1, 0xd01b, 0x0dff);  cmpw(1, 0xd01c, 0x0000);
  cmpw(1, 0xd01d, 0x0000);  cmpw(1, 0xd01e, 0x0880);  cmpw(0, 0xd020, 0x0000);  cmpw(0, 0xd021, 0x0000);
  cmpw(0, 0xd022, 0x0000);  cmpw(0, 0xd023, 0x0000);  cmpw(0, 0xd024, 0x0000);  cmpw(0, 0xd025, 0x0000);
  cmpw(0, 0xd026, 0x0000);  cmpw(0, 0xd027, 0x8400);  cmpw(0, 0xd029, 0x0000);  cmpw(0, 0xd02a, 0x0000);
  cmpw(0, 0xd02b, 0x2e02);  cmpw(0, 0xd02c, 0x0000);  cmpw(0, 0xd02d, 0x0000);  cmpw(0, 0xd02e, 0xc400);
  cmpw(0, 0xd030, 0xa404);  cmpw(0, 0xd031, 0x2060);  cmpw(0, 0xd032, 0x0100);  cmpw(0, 0xd033, 0x0000);
  cmpw(1, 0xd034, 0xfff0);  cmpw(1, 0xd035, 0x00d0);  cmpw(1, 0xd036, 0xfffa);  cmpw(1, 0xd037, 0x0096);
  cmpw(1, 0xd038, 0x000b);  cmpw(1, 0xd039, 0x0108);  cmpw(1, 0xd03a, 0x2118);  cmpw(1, 0xd03b, 0x0000);
  cmpw(1, 0xd03c, 0x0000);  cmpw(1, 0xd03d, 0x0000);  cmpw(1, 0xd03e, 0x009f);  cmpw(0, 0xd040, 0x00b8);
  cmpw(0, 0xd041, 0x0000);  cmpw(0, 0xd042, 0x0001);  cmpw(0, 0xd050, 0x0004);  cmpw(0, 0xd051, 0x0052);
  cmpw(0, 0xd052, 0x0310);  cmpw(0, 0xd053, 0x0000);  cmpw(0, 0xd054, 0x0000);  cmpw(0, 0xd055, 0x0000);
  cmpw(0, 0xd056, 0x0000);  cmpw(0, 0xd060, 0x0000);  cmpw(0, 0xd061, 0x0000);  cmpw(0, 0xd062, 0x0000);
  cmpw(0, 0xd063, 0x0004);  cmpw(0, 0xd064, 0x0a00);  cmpw(0, 0xd065, 0x0032);  cmpw(1, 0xd066, 0x0002);
  cmpw(0, 0xd067, 0x03f5);  cmpw(0, 0xd070, 0x0000);  cmpw(0, 0xd071, 0x0000);  cmpw(0, 0xd072, 0x0000);
  cmpw(0, 0xd073, 0x3100);  cmpw(0, 0xd074, 0x0004);  cmpw(0, 0xd075, 0x0004);  cmpw(0, 0xd078, 0x0000);
  cmpw(0, 0xd079, 0x0000);  cmpw(0, 0xd07a, 0x0000);  cmpw(0, 0xd07b, 0x0000);  cmpw(0, 0xd07c, 0x0000);
  cmpw(1, 0xd07d, 0x0002);
  //cmpw(0, 0xd080, 0x8008);  /* speed = 10M */
  //cmpw(0, 0xd080, 0x8008);  /* speed = 100M */
  cmpw(0, 0xd080, 0x8008);  /* speed = 1000M */
  //cmpw(0, 0xd080, 0x8000);  /* speed = 10G */
  cmpw(0, 0xd081, 0x0001);  cmpw(0, 0xd083, 0x0000);  cmpw(0, 0xd085, 0x0000);  cmpw(0, 0xd086, 0x0000);
  cmpw(1, 0xd089, 0x0000);  cmpw(0, 0xd08a, 0x0000);  cmpw(1, 0xd08b, 0x0000);  cmpw(1, 0xd08c, 0x0000);
  cmpw(0, 0xd08e, 0x0001);  cmpw(0, 0xd090, 0x1c40);  cmpw(0, 0xd091, 0x1048);  cmpw(0, 0xd092, 0x7e92);
  cmpw(0, 0xd093, 0x288f);  cmpw(0, 0xd094, 0x0820);  cmpw(0, 0xd095, 0x07a0);  cmpw(0, 0xd096, 0x14f8);
  cmpw(0, 0xd097, 0x0121);  cmpw(0, 0xd098, 0x0000);  cmpw(0, 0xd099, 0x0088);  cmpw(0, 0xd0a0, 0x2800);
  cmpw(0, 0xd0a1, 0x0744);  cmpw(0, 0xd0a2, 0x5250);  cmpw(0, 0xd0a3, 0x1556);  cmpw(0, 0xd0a4, 0x0000);
  cmpw(0, 0xd0a5, 0x2800);  cmpw(0, 0xd0a6, 0x0001);  cmpw(0, 0xd0a7, 0x0aa0);  cmpw(0, 0xd0a8, 0x0000);
  cmpw(0, 0xd0a9, 0x0000);  cmpw(0, 0xd0aa, 0x0088);  cmpw(0, 0xd0b0, 0x2480);  cmpw(0, 0xd0b1, 0x0007);
  cmpw(0, 0xd0b2, 0x0080);  cmpw(0, 0xd0b3, 0x460e);  cmpw(0, 0xd0b4, 0x0501);  cmpw(0, 0xd0b5, 0x1405);
  cmpw(0, 0xd0b6, 0x0000);  cmpw(0, 0xd0b7, 0x0000);  cmpw(0, 0xd0b8, 0x4442);  cmpw(0, 0xd0b9, 0x5285);
  cmpw(0, 0xd0ba, 0x0015);  cmpw(0, 0xd0be, 0x0000);  cmpw(0, 0xd0bf, 0x0001);  cmpw(0, 0xd0c0, 0x5229);
  cmpw(0, 0xd0c1, 0x0008);  cmpw(0, 0xd0c2, 0xfd29);  cmpw(1, 0xd0c8, 0x0333);  cmpw(1, 0xd0c9, 0x0303);
  cmpw(1, 0xd0ca, 0x0000);  cmpw(1, 0xd0cb, 0x0001);  cmpw(1, 0xd0cc, 0x0009);  cmpw(0, 0xd0d0, 0x0602);
  cmpw(0, 0xd0d1, 0x002a);  cmpw(0, 0xd0d2, 0x000e);  cmpw(0, 0xd0d3, 0x0000);  cmpw(0, 0xd0d4, 0x0000);
  cmpw(0, 0xd0d5, 0x0000);  cmpw(0, 0xd0d6, 0x0000);  cmpw(0, 0xd0d7, 0x0000);  cmpw(0, 0xd0d8, 0x0002);
  cmpw(0, 0xd0d9, 0x0000);  cmpw(0, 0xd0da, 0x8000);  cmpw(0, 0xd0db, 0x0000);  cmpw(0, 0xd0dc, 0x0000);
  cmpw(0, 0xd0e0, 0x0000);  cmpw(0, 0xd0e1, 0x000a);  cmpw(0, 0xd0e2, 0x0002);  cmpw(0, 0xd0e3, 0x0000);
  cmpw(0, 0xd0e8, 0x0002);  cmpw(0, 0xd0f0, 0x0363);  cmpw(0, 0xd0f1, 0x0001);  cmpw(0, 0xd0f2, 0x0000);
  cmpw(0, 0xd0f3, 0x0000);  cmpw(0, 0xd0f4, 0xa271);  cmpw(0, 0xd0f5, 0x0000);  cmpw(0, 0xd0f6, 0x0000);
  cmpw(0, 0xd0f7, 0x8604);  cmpw(1, 0xd0f8, 0x0000);  cmpw(1, 0xd0f9, 0x001c);  cmpw(1, 0xd0fa, 0x403c);
  cmpw(0, 0xd0fe, 0x0000);  cmpw(0, 0xd100, 0xff00);  cmpw(0, 0xd101, 0xff00);  cmpw(0, 0xd102, 0xff00);
  cmpw(0, 0xd103, 0xff00);  cmpw(0, 0xd104, 0xff00);  cmpw(0, 0xd105, 0xff00);  cmpw(0, 0xd106, 0xff00);
  cmpw(0, 0xd107, 0xff00);  cmpw(0, 0xd108, 0xff00);  cmpw(0, 0xd109, 0xff00);  cmpw(0, 0xd10a, 0xff00);
  cmpw(0, 0xd10b, 0xff00);  cmpw(0, 0xd10c, 0xff00);  cmpw(0, 0xd10d, 0xff00);  cmpw(0, 0xd10e, 0xff00);
  cmpw(0, 0xd110, 0x0000);  cmpw(0, 0xd111, 0x0020);  cmpw(1, 0xd112, 0x0000);  cmpw(0, 0xd113, 0xc140);
  cmpw(0, 0xd11b, 0x00aa);  cmpw(0, 0xd11c, 0x4155);  cmpw(0, 0xd11d, 0x4914);  cmpw(0, 0xd11e, 0x000f);
  cmpw(0, 0xd120, 0x1ffa);  cmpw(0, 0xd121, 0xfff0);  cmpw(0, 0xd122, 0x0fff);  cmpw(0, 0xd123, 0x1007);
  cmpw(0, 0xd124, 0x8240);  cmpw(0, 0xd125, 0x8160);  cmpw(0, 0xd126, 0x0000);  cmpw(1, 0xd128, 0x4f52);
  cmpw(1, 0xd129, 0x1fdb);  cmpw(1, 0xd12a, 0xbffd);  cmpw(1, 0xd12b, 0xfff3);  cmpw(1, 0xd12c, 0xfff3);
  cmpw(1, 0xd12d, 0x23ff);  cmpw(1, 0xd12e, 0x5288);  cmpw(0, 0xd130, 0x01f4);  cmpw(0, 0xd131, 0x00c8);
  cmpw(1, 0xd141, 0x003f);  cmpw(1, 0xd142, 0x0003);  cmpw(1, 0xd143, 0x0000);  cmpw(1, 0xd144, 0x003f);
  cmpw(1, 0xd145, 0x0003);  cmpw(1, 0xd146, 0x0000);  cmpw(1, 0xd147, 0x0010);  cmpw(0, 0xd150, 0x0000);
  cmpw(0, 0xd151, 0x0101);  cmpw(0, 0xd152, 0x0202);  cmpw(0, 0xd153, 0x0303);  cmpw(0, 0xd161, 0x0000);
  cmpw(0, 0xd162, 0x0000);  cmpw(0, 0xd163, 0x0000);  cmpw(0, 0xd164, 0x0003);  cmpw(0, 0xd167, 0x0000);
  cmpw(0, 0xd168, 0x041c);  cmpw(1, 0xd16c, 0x0000);  cmpw(0, 0xd171, 0x0000);  cmpw(0, 0xd172, 0x0000);
  cmpw(0, 0xd174, 0x0003);  cmpw(0, 0xd177, 0x0000);  cmpw(1, 0xd17c, 0x0000);  cmpw(0, 0xd17d, 0x0001);
  cmpw(0, 0xd180, 0x8000);  cmpw(0, 0xd181, 0x0001);  cmpw(0, 0xd183, 0x0000);  cmpw(1, 0xd185, 0x0000);
  cmpw(0, 0xd186, 0x0000);  cmpw(1, 0xd189, 0x0000);  cmpw(1, 0xd18a, 0x0000);  cmpw(1, 0xd18b, 0x0000);
  cmpw(1, 0xd18c, 0x0000);  cmpw(0, 0xd18e, 0x0001);  cmpw(0, 0xd190, 0x8000);  cmpw(0, 0xd191, 0x0001);
  cmpw(0, 0xd193, 0x0000);  cmpw(1, 0xd195, 0x0000);  cmpw(0, 0xd196, 0x0000);  cmpw(1, 0xd199, 0x0000);
  cmpw(0, 0xd19a, 0x0000);  cmpw(1, 0xd19b, 0x0000);  cmpw(1, 0xd19c, 0x0000);  cmpw(0, 0xd19e, 0x0001);
  cmpw(0, 0xd200, 0x0003);  cmpw(0, 0xd201, 0x000b);  cmpw(0, 0xd202, 0x0011);  cmpw(0, 0xd200, 0x0003);
  cmpw(1, 0xd203, 0x0001);  cmpw(0, 0xd204, 0x0800);  cmpw(0, 0xd205, 0x2000);  cmpw(0, 0xd206, 0x0200);
  cmpw(0, 0xd207, 0x0000);  cmpw(0, 0xd208, 0x0800);  cmpw(0, 0xd209, 0x2000);  cmpw(1, 0xd20a, 0x0200);
  cmpw(1, 0xd20b, 0x0000);  cmpw(0, 0xd20c, 0x0000);  cmpw(0, 0xd20d, 0x0000);  cmpw(0, 0xd20e, 0x0000);
  cmpw(1, 0xd210, 0x02ec);  cmpw(0, 0xd211, 0x0000);  cmpw(0, 0xd212, 0x0000);  cmpw(1, 0xd213, 0x0000);
  cmpw(1, 0xd214, 0x0000);  cmpw(0, 0xd215, 0x0000);  cmpw(0, 0xd216, 0x0007);  cmpw(1, 0xd217, 0x0000);
  cmpw(1, 0xd218, 0x0802);  cmpw(1, 0xd219, 0x0802);  cmpw(1, 0xd21a, 0x7a90);  cmpw(0, 0xd21b, 0x0000);
  cmpw(0, 0xd220, 0x0000);  cmpw(0, 0xd221, 0x0000);  cmpw(1, 0xd222, 0x0000);  cmpw(1, 0xd223, 0x0018);
  cmpw(0, 0xd224, 0x0000);  cmpw(0, 0xd225, 0x8401);  cmpw(0, 0xd226, 0x0000);  cmpw(1, 0xd227, 0x0000);
  cmpw(0, 0xd228, 0x0101);  cmpw(0, 0xd229, 0x0000);  cmpw(1, 0xd22a, 0x0007);  cmpw(0, 0xffdc, 0x001f);
  cmpw(0, 0xffdd, 0x404d);  cmpw(0, 0xffde, 0x0000);  cmpw(0, 0xffdf, 0x0000);
}


static inline u32
pm_ucode_download(u8 *ucode_image, u16 ucode_len)
{
	u32 get_val;
  u8 i, wrdata_lsb;
  u16 wrdata_lsw, ucode_len_padded, count = 0;  

  /* Check array pointer */
  if (ucode_image == (u8 *)NULL) {
    printk("uCode Image is empty !!\n");
    return -1;
	}

  /* Check ucode size */ 
  if (ucode_len > (32768)) {                      /* 16 x 2048 */
    printk("Can't fit all of the firmware into the device load table(max = 16 x 2048 bytes) \n");
    return -1;
  }
	
  //[1] EFUN(wrc_micro_master_clk_en(0x1));                   /* Enable clock to microcontroller subsystem */
  /* [0xd200] Write to Clock control register 0 to enable micro core clock (m0) */
  pm_phy_sbus_write(0, 0xd200, 0x0001, 0x0001, 0);
  
  //[2] EFUN(wrc_micro_master_rstb(0x1));                     /* De-assert reset to microcontroller sybsystem */
  /* [0xd201] Write to Reset control registers 0 to make micro_master_rstb = 1 */
  pm_phy_sbus_write(0, 0xd201, 0x0001, 0x0001, 0);
  
  //[3] EFUN(wrc_micro_master_rstb(0x0));                     /* Assert reset to microcontroller sybsystem - Toggling reset */
  /* [0xd201] Write to Reset control registers 0 to make micro_master_rstb = 0 */
  pm_phy_sbus_write(0, 0xd201, 0x0000, 0x0001, 0);

  //[4] EFUN(wrc_micro_master_rstb(0x1));                     /* De-assert reset to microcontroller sybsystem */
  /* [0xd201] Write to Reset control registers 0 to make micro_master_rstb = 1 */
  pm_phy_sbus_write(0, 0xd201, 0x0001, 0x0001, 0);
  
  //[5] EFUN(wrc_micro_ra_init(0x1));                         /* Set initialization command to initialize code RAM */
  /* [0xd202] Write to rmi to ahb control register 0 to initialize code RAMs */
  pm_phy_sbus_write(0, 0xd202, 0x0001, 0x0003, 8);
  
  //[6] EFUN(merlin16_INTERNAL_poll_micro_ra_initdone(sa__, 250)); /* Poll for micro_ra_initdone = 1 to indicate initialization done */
  /* [0xd203] Read from ahb status register 0 to make sure all are done */
  for (i=0; i<100; ++i)
  {
  	pm_phy_sbus_read(0, 0xd203, &get_val);
  	if (get_val == 0x1)    /* code/data RAM initialization process is complete */
  		break;
    udelay(2500);
  }
  if (i == 100)
  {
  	printk("code/data RAM initialization process is timeout !!\n");
  	return -1;
  }
  
  //[7] EFUN(wrc_micro_ra_init(0x0));                         /* Clear initialization command */
  /* [0xd202] Write to rmi to ahb control register 0 to clear intialize code/data RAM command */
  pm_phy_sbus_write(0, 0xd202, 0x0000, 0x0003, 8);
  
  ucode_len_padded = ((ucode_len + 3) & 0xFFFC);        /* Aligning ucode size to 4-byte boundary */
  
  /* Code to Load microcode */
  //[8] EFUN(wrc_micro_autoinc_wraddr_en(0x1));               /* To auto increment RAM write address */
  /* [0xd202] Write to rmi to ahb control register 0 to make Automatic increment write address enable */
  pm_phy_sbus_write(0, 0xd202, 0x0001, 0x0001, 12);
  
  //[9] EFUN(wrc_micro_ra_wrdatasize(0x1));                   /* Select 16bit transfers */
  /* [0xd202] Write to rmi to ahb control register 0 to select write data size = 16 bits */
  pm_phy_sbus_write(0, 0xd202, 0x0001, 0x0003, 0);
  
  //[10] EFUN(wrc_micro_ra_wraddr_msw(0x0));                  /* Upper 16bits of start address of Program RAM where the ucode is to be loaded */
  /* [0xd205] Write to rmi to ahb write address MSW (bits 31:16) register with 0 */
  pm_phy_sbus_write(0, 0xd205, 0x0000, 0xffff, 0);
  
  //[11] EFUN(wrc_micro_ra_wraddr_lsw(0x0));                  /* Lower 16bits of start address of Program RAM where the ucode is to be loaded */
  /* [0xd206] Write to rmi to ahb write data LSW (bits 15:0) register with 0 (starting address = 0x0) */
  pm_phy_sbus_write(0, 0xd206, 0x0000, 0xffff, 0);
  
  do {                                                        /* ucode_image loaded 16bits at a time */
  	/* wrdata_lsb read from ucode_image; zero padded to 4byte boundary */
    wrdata_lsb = (count < ucode_len) ? ucode_image[count] : 0x0;
    count++;
    /* wrdata_msb read from ucode_image; zero padded to 4byte boundary */
    wrdata_lsw = (count < ucode_len) ? ucode_image[count] : 0x0;
    count++;
    /* 16bit wrdata_lsw formed from 8bit msb and lsb values read from ucode_image */
    wrdata_lsw = ((wrdata_lsw << 8) | wrdata_lsb);
    
    //[12] EFUN(wrc_micro_ra_wrdata_lsw(wrdata_lsw));         /* Program RAM lower 16bits write data */
    /* [0xd206] Write to rmi to ahb write data LSW (bits 15:0) register with data */
    pm_phy_sbus_write(0, 0xd206, wrdata_lsw, 0xffff, 0);
  } while (count < ucode_len_padded);                 /* Loop repeated till entire image loaded (upto the 4byte boundary) */
  
  //[13] EFUN(wrc_micro_ra_wrdatasize(0x2));                  /* Select 32bit transfers as default */
  /* [0xd202] Write to rmi to ahb control register 0 to Select 32bit transfers as default */
  pm_phy_sbus_write(0, 0xd202, 0x0002, 0x0003, 0);
  
  //[14] EFUN(wrc_micro_core_clk_en(0x1));                    /* Enable clock to M0 core */
  /* [0xd200] Write to Clock control register 0 to enable micro core clock enable (m0) */
  pm_phy_sbus_write(0, 0xd200, 0x0001, 0x0001, 1);
  
  return 0;
}

static int __xlmac_credit_reset(int port)
{
	return 0;
}

static void __xlmac_enable_set(int port, bool enable)
{
	u64 ctrl, octrl;
	int soft_reset;

	ctrl = xlmac_reg64_read(XLMAC_CTRL(port));
	octrl = ctrl;
	/* Don't disable TX since it stops egress and hangs if CPU sends */
	ctrl |= (1 << XLMAC_CTRL__TX_EN);
	ctrl &= ~(1 << XLMAC_CTRL__RX_EN);
	if (enable) {
		ctrl |= (1 << XLMAC_CTRL__RX_EN);
	}

	if (ctrl == octrl) {
		/* SDK-49952 fix soluition :
		 *  >> to avoid the unexpected early return to prevent this problem.
		 *  1. Problem occurred for disabling process only.
		 *  2. To comply origianl designing senario, XLMAC_CTRLr.SOFT_RESETf is
		 *	  used to early check to see if this port is at disabled state
		 *	  already.
		 */
		soft_reset = ctrl & (1 << XLMAC_CTRL__SOFT_RESET);
		if ((enable) || (!enable && soft_reset)){
			return;
		}
	}

	ctrl |= (1 << XLMAC_CTRL__SOFT_RESET);
	if (enable) {
		/* Reset EP credit before de-assert SOFT_RESET */
		__xlmac_credit_reset(port);
		/* Deassert SOFT_RESET */
		ctrl &= ~(1 << XLMAC_CTRL__SOFT_RESET);
	}

	xlmac_reg64_write(XLMAC_CTRL(port), ctrl);
}

static int __xlmac_enable_get(int port)
{
	u64 ctrl;
	int tx_en, rx_en;

	ctrl = xlmac_reg64_read(XLMAC_CTRL(port));
	tx_en = ctrl & (1 << XLMAC_CTRL__TX_EN);
	rx_en = ctrl & (1 << XLMAC_CTRL__RX_EN);

	return (tx_en && rx_en);
}

static int __xlmac_speed_set(int port, int speed)
{
	u64 speed_cfg, val64;
	int enable;

  //pm_phy_sbus_write(port, 0xd080, 0x8008, 0x8008, 0); /* CKRST_CTRL_OSR_MODE_CONTROL */

	if (speed == 1000) {
		speed_cfg = SPEED_MODE_LINK_1G;
		pm_phy_sbus_write(port, 0xc050, 0x0037, 0x01ff, 0); /* SC_X4_CONTROL_CONTROL */
	} else if (speed == 100) {
		speed_cfg = SPEED_MODE_LINK_100M;
		pm_phy_sbus_write(port, 0xc050, 0x0036, 0x01ff, 0); /* SC_X4_CONTROL_CONTROL */
	} else if (speed == 10) {
		speed_cfg = SPEED_MODE_LINK_10M;
		pm_phy_sbus_write(port, 0xc050, 0x0035, 0x01ff, 0); /* SC_X4_CONTROL_CONTROL */
	} else {
		printk("%s: Invalid xlport speed(%d)!\n", __func__, speed);
		return -1;
	}

	pm_phy_sbus_write(port, 0xc050, 0x0000, 0x0001, 8); /* SC_X4_CONTROL_CONTROL */
	pm_phy_sbus_write(port, 0xc050, 0x0001, 0x0001, 8); /* SC_X4_CONTROL_CONTROL */

	enable = __xlmac_enable_get(port);
	/* disable before updating the speed */
	if (enable) {
		__xlmac_enable_set(port, 0);
	}

	/* Update the speed */
	val64 = xlmac_reg64_read(XLMAC_MODE(port));
	val64 &= ~(0x70);
	val64 |= speed_cfg << XLMAC_MODE__SPEED_MODE_R;
	xlmac_reg64_write(XLMAC_MODE(port), val64);
	debug("%s XLMAC_MODE = 0x%llx\n",  __func__, xlmac_reg64_read(XLMAC_MODE(port)));

	if (enable) {
		__xlmac_enable_set(port, 1);
	}
	return 0;
}

static int __xlmac_rx_max_size_set(int port, int value)
{
	u64 val64;
	u64 mask64;

	val64 = xlmac_reg64_read(XLMAC_RX_MAX_SIZE(port));
	mask64 = (1 << XLMAC_RX_MAX_SIZE__RX_MAX_SIZE_WIDTH) - 1;
	val64 &= ~(mask64 << XLMAC_RX_MAX_SIZE__RX_MAX_SIZE_R);
	val64 |= value << XLMAC_RX_MAX_SIZE__RX_MAX_SIZE_R;
	xlmac_reg64_write(XLMAC_RX_MAX_SIZE(port), val64);

	return 0;
}

static int __xlmac_tx_mac_addr_set(int port, u8 *mac)
{
	u64 val64;

	/* set our local address */
	debug("GMAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	val64 = (u64)htonl(*(u32 *)&mac[2]);
	val64 |= ((u64)htons(*(u32 *)mac) << 32);
	xlmac_reg64_write(XLMAC_TX_MAC_SA(port), val64);

	debug("%s XLMAC_TX_MAC_SA = 0x%llx\n",  __func__, xlmac_reg64_read(XLMAC_TX_MAC_SA(port)));
	return 0;
}

static int __xlmac_init(int port)
{
	u64 val64;

	/* Disable Tx/Rx, assume that MAC is stable (or out of reset) */
	val64 = xlmac_reg64_read(XLMAC_CTRL(port));
	val64 &= ~(1 << XLMAC_CTRL__XGMII_IPG_CHECK_DISABLE);
	val64 &= ~(1 << XLMAC_CTRL__RX_EN);
	val64 &= ~(1 << XLMAC_CTRL__TX_EN);
	xlmac_reg64_write(XLMAC_CTRL(port), val64);

	/* XLMAC_RX_CTRL */
	val64 = xlmac_reg64_read(XLMAC_RX_CTRL(port));
	val64 &= ~(1 << XLMAC_RX_CTRL__STRIP_CRC);
	xlmac_reg64_write(XLMAC_RX_CTRL(port), val64);

	/* XLMAC_TX_CTRL */
	val64 = xlmac_reg64_read(XLMAC_TX_CTRL(port));
	val64 &= ~(0x3 << XLMAC_TX_CTRL__CRC_MODE_R);
	val64 |= (CRC_MODE_REPLACE << XLMAC_TX_CTRL__CRC_MODE_R);
	val64 |= (1 << XLMAC_TX_CTRL__PAD_EN);
	xlmac_reg64_write(XLMAC_TX_CTRL(port), val64);

	/* PAUSE */
	val64 = xlmac_reg64_read(XLMAC_PAUSE_CTRL(port));
	val64 |= 1 << XLMAC_PAUSE_CTRL__RX_PAUSE_EN;
	val64 |= 1 << XLMAC_PAUSE_CTRL__TX_PAUSE_EN;
	xlmac_reg64_write(XLMAC_PAUSE_CTRL(port), val64);

	/* PFC */
	val64 = xlmac_reg64_read(XLMAC_PFC_CTRL(port));
	val64 |= ((u64)1 << XLMAC_PFC_CTRL__PFC_REFRESH_EN);
	xlmac_reg64_write(XLMAC_PFC_CTRL(port), val64);

	/* Set jumbo max size (8000 byte payload) */
	__xlmac_rx_max_size_set(port, JUMBO_MAXSZ);

	/* XLMAC_RX_LSS_CTRL */
	val64 = xlmac_reg64_read(XLMAC_RX_LSS_CTRL(port));
	val64 |= 1 << XLMAC_RX_LSS_CTRL__DROP_TX_DATA_ON_LINK_INTERRUPT;
	val64 |= 1 << XLMAC_RX_LSS_CTRL__DROP_TX_DATA_ON_REMOTE_FAULT;
	val64 |= 1 << XLMAC_RX_LSS_CTRL__DROP_TX_DATA_ON_LOCAL_FAULT;
	xlmac_reg64_write(XLMAC_RX_LSS_CTRL(port), val64);

	/* Disable loopback and bring XLMAC out of reset */
	val64 = xlmac_reg64_read(XLMAC_CTRL(port));
	val64 &= ~(1 << XLMAC_CTRL__SOFT_RESET);;
	val64 &= ~(1 << XLMAC_CTRL__LOCAL_LPBK);
	val64 |= 1 << XLMAC_CTRL__RX_EN;
	val64 |= 1 << XLMAC_CTRL__TX_EN;
	xlmac_reg64_write(XLMAC_CTRL(port), val64);

	return 0;
}

static int __tsc_reset(int in_reset)
{
	u32 val;

	val = xlport_reg32_read(XLPORT_XGXS0_CTRL_REG);
	if (in_reset) {
		//val &= ~(7 << XLPORT_XGXS0_CTRL_REG__RefSel);
		val |= (1 << XLPORT_XGXS0_CTRL_REG__IDDQ);
		//val &= ~(1 << XLPORT_XGXS0_CTRL_REG__Refin_EN);
		val |= 1 << XLPORT_XGXS0_CTRL_REG__PWRDWN;
		val &= ~(1 << XLPORT_XGXS0_CTRL_REG__RSTB_HW);
	} else {
		//val |= (5 << XLPORT_XGXS0_CTRL_REG__RefSel);
		val &= ~(1 << XLPORT_XGXS0_CTRL_REG__IDDQ);
		//val |= (1 << XLPORT_XGXS0_CTRL_REG__Refin_EN);
		val &= ~(1 << XLPORT_XGXS0_CTRL_REG__PWRDWN);
		//val &= ~(0 << XLPORT_XGXS0_CTRL_REG__RSTB_HW);
		val |= (1 << XLPORT_XGXS0_CTRL_REG__RSTB_HW);
	}
	xlport_reg32_write(XLPORT_XGXS0_CTRL_REG, val);


/*	if (!in_reset)
	{
  	msleep(1);
	  val |= (1 << XLPORT_XGXS0_CTRL_REG__RSTB_HW);
		xlport_reg32_write(XLPORT_XGXS0_CTRL_REG, val);
	} */
	msleep(1);
	return 0;
}

static void pm4x10_tsc_config(int port)
{
#ifdef CONFIG_IPROC_EMULATION
	printf("skip %s... in emulation\n", __func__);
#else
	u32 val;
	int i;

	/* Global PMD reset controls */
	pm_phy_sbus_write(port, 0x9010, 0x0000, 0xffff, 0);
	udelay(10);
	pm_phy_sbus_write(port, 0x9010, 0x0003, 0xffff, 0);
	udelay(10);
	
	/* PMD lane reset controls */
	for (i=0; i<4; ++i)
	{
	  pm_phy_sbus_write(i, 0xc010, 0x0000, 0xc003, 0);
	  udelay(10);
	  pm_phy_sbus_write(i, 0xc010, 0xc003, 0xc003, 0);
	}
	/* set refclk_sel = 156.25 MHz */
	pm_phy_sbus_write(port, 0x9000, 0x0003, 0x0007, 13);
	/* set heartbeat_count_1us = 0x271 */
	pm_phy_sbus_write(port, 0xd0f4, 0x0271, 0x03ff, 0);
#endif
	return 0;
}

/*****************************************************************************
*****************************************************************************/
static void pm4x10_xlport_mac_enable(int port)
{
	__xlmac_enable_set(port, 1);
}

static void pm4x10_xlport_mac_disable(int port)
{
	__xlmac_enable_set(port, 0);
}

//static int pm4x10_xlport_speed_set(int port, int speed)
int pm4x10_xlport_speed_set(int port, int speed)
{
	return __xlmac_speed_set(port, speed);
}

//static int pm4x10_xlport_mac_addr_set(int port, u8 *mac)
int pm4x10_xlport_mac_addr_set(int port, u8 *mac)
{
	return __xlmac_tx_mac_addr_set(port, mac);
}

static int pm4x10_xlport_max_packet_size_set(int port, int value)
{
	return __xlmac_rx_max_size_set(port, value);
}

//static int pm4x10_xlport_loopback_set(int port, int lb_type, int lb_en)
int pm4x10_xlport_loopback_set(int port, int lb_type, int lb_en)
{
	u64 val64;
	u32 val32;
//printk("  (%s) enter.....port = %d, lb_type = %d, lb_en = %d\n", __func__, port, lb_type, lb_en);

	switch(lb_type) {
		case pmLoopbackMac:
			val64 = xlmac_reg64_read(XLMAC_CTRL(port));
	    val64 &= ~(1 << XLMAC_CTRL__LOCAL_LPBK);
	    if (lb_en) {
		    printk("(%s) MAC port = %d, lb_en = %d\n", __func__, port, lb_en);
		    val64 |= (1 << XLMAC_CTRL__LOCAL_LPBK);
	    }
	    xlmac_reg64_write(XLMAC_CTRL(port), val64);
			break;

		case pmLoopbackPhy:
			if (lb_en) {
				printk("(%s) PHY port = %d, lb_en = %d\n", __func__, port, lb_en);
				pm_phy_sbus_write(port*2, 0x9009, (1 << port*2), 0x000f, 4);  /* MAIN0_LOOPBACK_CONTROL */
				pm_phy_sbus_write(port*2, 0xc014, 0x0043, 0x0043, 0);  /* PMD_X4_OVERRIDE */
				pm_phy_sbus_write(port*2, 0xc010, 0x0001, 0x0001, 8);  /* PMD_X4_CONTROL */
			} else {
				pm_phy_sbus_write(port*2, 0x9009, 0x0000, 0x000f, 4);  /* MAIN0_LOOPBACK_CONTROL */
				pm_phy_sbus_write(port*2, 0xc014, 0x0000, 0x0043, 0);  /* PMD_X4_OVERRIDE */
				pm_phy_sbus_write(port*2, 0xc010, 0x0000, 0x0001, 8);  /* PMD_X4_CONTROL */
			}
			break;

		default:
			break;
	}
	return 0;
}

//static int pm4x10_xlport_stats_get(int port, struct iproc_pm_stats *stats)
int pm4x10_xlport_stats_get(int port, struct iproc_pm_stats *stats)
{
	stats->rx_frames = xlmac_reg64_read(XLMIB_GRxPkt(port));
	stats->rx_frame_good = xlmac_reg64_read(XLMIB_GRxPOK(port));
	stats->rx_bytes = xlmac_reg64_read(XLMIB_GRxByt(port));
	stats->rx_frame_64 = xlmac_reg64_read(XLMIB_GRx64(port));
	stats->rx_frame_127 = xlmac_reg64_read(XLMIB_GRx127(port));
	stats->rx_frame_255 = xlmac_reg64_read(XLMIB_GRx255(port));
	stats->rx_frame_511 = xlmac_reg64_read(XLMIB_GRx511(port));
	stats->rx_frame_1023 = xlmac_reg64_read(XLMIB_GRx1023(port));
	stats->rx_frame_1518 = xlmac_reg64_read(XLMIB_GRx1518(port));
	stats->rx_frame_1522 = xlmac_reg64_read(XLMIB_GRx1522(port));
	stats->rx_frame_jumbo = xlmac_reg64_read(XLMIB_GRx2047(port)) +
							xlmac_reg64_read(XLMIB_GRx4095(port)) +
							xlmac_reg64_read(XLMIB_GRx9216(port)) +
							xlmac_reg64_read(XLMIB_GRx16383(port));
	stats->rx_frame_unicast = xlmac_reg64_read(XLMIB_GRxUCA(port));
	stats->rx_frame_multicast = xlmac_reg64_read(XLMIB_GRxMCA(port));
	stats->rx_frame_broadcast = xlmac_reg64_read(XLMIB_GRxBCA(port));
	stats->rx_frame_control = xlmac_reg64_read(XLMIB_GRxCF(port));
	stats->rx_frame_pause = xlmac_reg64_read(XLMIB_GRxPF(port));
	stats->rx_frame_jabber = xlmac_reg64_read(XLMIB_GRxJBR(port));
	stats->rx_frame_fragment = xlmac_reg64_read(XLMIB_GRxFRG(port));
	stats->rx_frame_vlan = xlmac_reg64_read(XLMIB_GRxVLN(port));
	stats->rx_frame_dvlan = xlmac_reg64_read(XLMIB_GRxDVLN(port));
	stats->rx_frame_fcs_error = xlmac_reg64_read(XLMIB_GRxFCS(port));
	stats->rx_frame_unsupport = xlmac_reg64_read(XLMIB_GRxUO(port));
	stats->rx_frame_wrong_sa = xlmac_reg64_read(XLMIB_GRxWSA(port));
	stats->rx_frame_align_err = xlmac_reg64_read(XLMIB_GRxALN(port));
	stats->rx_frame_length_err = xlmac_reg64_read(XLMIB_GRxFLR(port));
	stats->rx_frame_oversize = xlmac_reg64_read(XLMIB_GRxOVR(port));
	stats->rx_frame_mtu_err = xlmac_reg64_read(XLMIB_GRxMTUE(port));
	stats->rx_frame_truncated_err = xlmac_reg64_read(XLMIB_GRxTRFU(port));
	stats->rx_frame_undersize = xlmac_reg64_read(XLMIB_GRxUND(port));
	stats->tx_frames = xlmac_reg64_read(XLMIB_GTxPkt(port));
	stats->tx_frame_good = xlmac_reg64_read(XLMIB_GTxPOK(port));
	stats->tx_bytes = xlmac_reg64_read(XLMIB_GTxBYT(port));
	stats->tx_frame_64 = xlmac_reg64_read(XLMIB_GTx64(port));
	stats->tx_frame_127 = xlmac_reg64_read(XLMIB_GTx127(port));
	stats->tx_frame_255 = xlmac_reg64_read(XLMIB_GTx255(port));
	stats->tx_frame_511 = xlmac_reg64_read(XLMIB_GTx511(port));
	stats->tx_frame_1023 = xlmac_reg64_read(XLMIB_GTx1023(port));
	stats->tx_frame_1518 = xlmac_reg64_read(XLMIB_GTx1518(port));
	stats->tx_frame_1522 = xlmac_reg64_read(XLMIB_GTx1522(port));
	stats->tx_frame_jumbo = xlmac_reg64_read(XLMIB_GTx2047(port)) +
							xlmac_reg64_read(XLMIB_GTx4095(port)) +
							xlmac_reg64_read(XLMIB_GTx9216(port)) +
							xlmac_reg64_read(XLMIB_GTx16383(port));
	stats->tx_frame_unicast = xlmac_reg64_read(XLMIB_GTxUCA(port));
	stats->tx_frame_multicast = xlmac_reg64_read(XLMIB_GTxMCA(port));
	stats->tx_frame_broadcast = xlmac_reg64_read(XLMIB_GTxBCA(port));
	stats->tx_frame_control = xlmac_reg64_read(XLMIB_GTxCF(port));
	stats->tx_frame_pause = xlmac_reg64_read(XLMIB_GTxPF(port));
	stats->tx_frame_jabber = xlmac_reg64_read(XLMIB_GTxJBR(port));
	stats->tx_frame_fragment = xlmac_reg64_read(XLMIB_GTxFRG(port));
	stats->tx_frame_vlan = xlmac_reg64_read(XLMIB_GTxVLN(port));
	stats->tx_frame_dvlan = xlmac_reg64_read(XLMIB_GTxDVLN(port));
	stats->tx_frame_fcs_error = xlmac_reg64_read(XLMIB_GTxFCS(port));
	stats->tx_frame_oversize = xlmac_reg64_read(XLMIB_GTxOVR(port));
	stats->tx_frame_error = xlmac_reg64_read(XLMIB_GTxErr(port));
	stats->tx_frame_fifo_underrun = xlmac_reg64_read(XLMIB_GTxUFL(port));
	stats->tx_frame_collision = xlmac_reg64_read(XLMIB_GTxNCL(port));

	return 0;
}

//static int pm4x10_xlport_mib_reset(int port)
int pm4x10_xlport_mib_reset(int port)
{
	u32 val;

	/* MIB reset */
	val = xlport_reg32_read(XLPORT_MIB_RESET);
	val |= (1 << port) << XLPORT_MIB_RESET__CLR_CNT_R;
	xlport_reg32_write(XLPORT_MIB_RESET, val);

	val &= ~((1 << port) << XLPORT_MIB_RESET__CLR_CNT_R);
	xlport_reg32_write(XLPORT_MIB_RESET, val);

	return 0;
}

//static int pm4x10_pm_xlport_port_config(int port, int enable)
int pm4x10_pm_xlport_port_config(int port, int enable)
{
	u32 val;

	if (enable) {
		/* Soft reset */
		val = xlport_reg32_read(XLPORT_SOFT_RESET);
		XLPORT_PORT_FIELD_SET(XLPORT_SOFT_RESET, port, val);
		xlport_reg32_write(XLPORT_SOFT_RESET, val);

		XLPORT_PORT_FIELD_CLEAR(XLPORT_SOFT_RESET, port, val);
		xlport_reg32_write(XLPORT_SOFT_RESET, val);

		/* Port enable */
		val = xlport_reg32_read(XLPORT_ENABLE_REG);
		XLPORT_PORT_FIELD_SET(XLPORT_ENABLE_REG, port, val);
		xlport_reg32_write(XLPORT_ENABLE_REG, val);

		/* Init MAC */
		__xlmac_init(port);

#if 0 //FIXME
		/* LSS */
		val = xlport_reg32_read(XLPORT_FAULT_LINK_STATUS(port));
		val |= 1 << XLPORT_FAULT_LINK_STATUS__REMOTE_FAULT;
		val |= 1 << XLPORT_FAULT_LINK_STATUS__LOCAL_FAULT;
		xlport_reg32_write(XLPORT_FAULT_LINK_STATUS(port), val);
#endif /* 0 */

		/* MIB reset */
		val = xlport_reg32_read(XLPORT_MIB_RESET);
		val |= (1 << port) << XLPORT_MIB_RESET__CLR_CNT_R;
		xlport_reg32_write(XLPORT_MIB_RESET, val);

		val &= ~((1 << port) << XLPORT_MIB_RESET__CLR_CNT_R);
		xlport_reg32_write(XLPORT_MIB_RESET, val);
	} else {
		/* Port disable */
		val = xlport_reg32_read(XLPORT_ENABLE_REG);
		XLPORT_PORT_FIELD_CLEAR(XLPORT_ENABLE_REG, port, val);
		xlport_reg32_write(XLPORT_ENABLE_REG, val);

		/* Soft reset */
		val = xlport_reg32_read(XLPORT_SOFT_RESET);
		XLPORT_PORT_FIELD_CLEAR(XLPORT_SOFT_RESET, port, val);
		xlport_reg32_write(XLPORT_SOFT_RESET, val);
	}

	return 0;
}

static int pm4x10_pm_disable(void)
{
	u32 val;

	/* Put MAC in reset */
	val = xlport_reg32_read(XLPORT_MAC_CONTROL);
	val |= 1 << XLPORT_MAC_CONTROL__XMAC0_RESET;
	xlport_reg32_write(XLPORT_MAC_CONTROL, val);

	/* Put Serdes in reset*/
	__tsc_reset(1);

	return 0;
}

static int pm4x10_pm_enable(void)
{
	u32 val;
	u32 mask;

	/* Power Save */
	val = xlport_reg32_read(XLPORT_POWER_SAVE);
	val &= ~(1 << XLPORT_POWER_SAVE__XPORT_CORE0);
	xlport_reg32_write(XLPORT_POWER_SAVE, val);

	/* Port configuration */
	val = xlport_reg32_read(XLPORT_MODE_REG);
	mask = (1 << XLPORT_MODE_REG__XPORT0_CORE_PORT_MODE_WIDTH) - 1;
	val &= ~(mask << XLPORT_MODE_REG__XPORT0_CORE_PORT_MODE_R);
	val |= (XPORT0_CORE_PORT_MODE_QUAD << XLPORT_MODE_REG__XPORT0_CORE_PORT_MODE_R);
	mask = (1 << XLPORT_MODE_REG__XPORT0_PHY_PORT_MODE_WIDTH) - 1;
	val &= ~(mask << XLPORT_MODE_REG__XPORT0_PHY_PORT_MODE_R);
	val |= (XPORT0_CORE_PORT_MODE_QUAD << XLPORT_MODE_REG__XPORT0_PHY_PORT_MODE_R);
	xlport_reg32_write(XLPORT_MODE_REG, val);

	/* Get Serdes OOR */
	__tsc_reset(1);
	__tsc_reset(0);

	/* Bring MAC OOR */
	val = xlport_reg32_read(XLPORT_MAC_CONTROL);
	val &= ~(1 << XLPORT_MAC_CONTROL__XMAC0_RESET);
	xlport_reg32_write(XLPORT_MAC_CONTROL, val);

	return 0;
}

int pm4x10_pm_init(struct iproc_pm_ops *pm_ops, u8 land_idx)
{
#if 0
	pm_ops = kmalloc(sizeof(struct iproc_pm_ops), GFP_KERNEL);
	if (!pm_ops) {
		return -ENOMEM;
	}
	pm_ops->port_enable= pm4x10_pm_xlport_port_config;
	pm_ops->port_speed = pm4x10_xlport_speed_set;
	pm_ops->port_loopback = pm4x10_xlport_loopback_set;
	pm_ops->port_mac_addr = pm4x10_xlport_mac_addr_set;
	pm_ops->port_stats = pm4x10_xlport_stats_get;
	pm_ops->port_stats_clear = pm4x10_xlport_mib_reset;
#endif

#if 0	
	if (!pm4x10_enabled)  /* check initialize for first time */
#else
  if (pm4x10_enabled == 0x100)  /* don't do it now */
#endif
	{
	  /* configure TSC registers before download ucode */
	  pm4x10_tsc_config(land_idx);
	  
	  /* ucode download */
    pm_ucode_download(merlin16_ucode, merlin16_ucode_len);
    
    /* Configure TSC/merlin16 registers */
    pm_phy_configure(land_idx);
    
    /* Enable PM4x10 */
		pm4x10_pm_enable();
		pm4x10_enabled++;
	}

	return 0;
}

int pm4x10_pm_deinit(struct iproc_pm_ops *pm_ops)
{
#if 0
	kfree(pm_ops);
	pm_ops = NULL;
#endif
	pm4x10_enabled--;
	if (!pm4x10_enabled) {
		pm4x10_pm_disable();
	}

	return 0;
}

#if 0
static void pm4x10_xlport_config(int port)
{
	u32 val;
	u64 val64;

	/* xlport_mac_config */
	val = xlport_reg32_read(XLPORT_ENABLE_REG);
	val |= ((1 << XLPORT_ENABLE_REG__PORT0) | (1 << XLPORT_ENABLE_REG__PORT2));
	xlport_reg32_write(XLPORT_ENABLE_REG, val);
	debug("%s XLPORT_ENABLE_REG = 0x%x\n",  __func__, xlport_reg32_read(XLPORT_ENABLE_REG));

	val = xlport_reg32_read(XLPORT_MAC_CONTROL);
	val &= ~(1 << XLPORT_MAC_CONTROL__XMAC0_RESET);
	xlport_reg32_write(XLPORT_MAC_CONTROL, val);

	/* xlport_intr_enable ? */

	/* Resetting MIB counter */
	xlport_reg32_write(XLPORT_MIB_RESET, 0xf);
	/* FIXME : delay? */
	/* udelay(10); */
	xlport_reg32_write(XLPORT_MIB_RESET, 0x0);

	/* Ethernet mode, 1G */
	val64 = xlmac_reg64_read(XLMAC_MODE(port));
	val64 &= ~(0x70);
	val64 |= SPEED_MODE_LINK_1G << XLMAC_MODE__SPEED_MODE_R;
	xlmac_reg64_write(XLMAC_MODE(port), val64);
	debug("%s XLMAC_MODE = 0x%llx\n",  __func__, xlmac_reg64_read(XLMAC_MODE(port)));

	val64 = xlmac_reg64_read(XLMAC_RX_CTRL(port));
	val64 &= ~(1 << XLMAC_RX_CTRL__STRIP_CRC);
	xlmac_reg64_write(XLMAC_RX_CTRL(port), val64);
	debug("%s XLMAC_RX_CTRL = 0x%llx\n",  __func__, xlmac_reg64_read(XLMAC_RX_CTRL(port)));

	val64 = xlmac_reg64_read(XLMAC_TX_CTRL(port));
	val64 &= ~(0x3 << XLMAC_TX_CTRL__CRC_MODE_R);
	val64 |= (CRC_MODE_REPLACE << XLMAC_TX_CTRL__CRC_MODE_R);
	val64 |= (1 << XLMAC_TX_CTRL__PAD_EN);
	xlmac_reg64_write(XLMAC_TX_CTRL(port), val64);
	debug("%s XLMAC_TX_CTRL = 0x%llx\n",  __func__, xlmac_reg64_read(XLMAC_TX_CTRL(port)));

#if 0
	val64 = xlmac_reg64_read(XLMAC_CTRL(port));
	val64 &= ~(1 << XLMAC_CTRL__SOFT_RESET);
	val64 |= ((1 << XLMAC_CTRL__RX_EN) | (1 << XLMAC_CTRL__TX_EN));
	xlmac_reg64_write(XLMAC_CTRL(port), val64);
	debug("%s XLMAC_CTRL = 0x%llx\n",  __func__, xlmac_reg64_read(XLMAC_CTRL(port)));
#endif
}
#endif /* 0 */
