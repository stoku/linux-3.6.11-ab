/*
 * Copyright (C) 2013 Sosuke Tokunaga <sosuke.tokunaga@courier-systems.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the Lisence, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be helpful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MARCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
 * GNU General Public License for more details.
 *
 */

#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/platform_data/ehci-sh.h>
#include <linux/mtd/physmap.h>
#include <linux/sh_eth.h>
#include <linux/i2c.h>
#include <linux/sh7734-hpbdma.h>
#include <cpu/sh7734.h>
#include <cpu/dma-register.h>
#include <asm/machvec.h>
#include <asm/sizes.h>

/* NOR Flash */

static struct physmap_flash_data nor_flash_data = {
	.width		= 2,
};

static struct resource nor_flash_resources[] = {
	[0] = {
		.start	= 0x00000000,
		.end	= 0x00000000 + SZ_16M - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device nor_flash_device = {
	.name		= "physmap-flash",
	.dev = {
		.platform_data	= &nor_flash_data,
	},
	.num_resources	= ARRAY_SIZE(nor_flash_resources),
	.resource	= nor_flash_resources,
};

/* Ethernet */

static struct resource sh_eth_resources[] = {
	[0] = {
		.start	= 0xFEE00000,
		.end	= 0xFEE007FF,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 0xFEE01800,
		.end	= 0xFEE01FFF,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= evt2irq(0xCA0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct sh_eth_plat_data sh_eth_pdata = {
	.phy			= 0,
	.edmac_endian		= EDMAC_LITTLE_ENDIAN,
	.register_type		= SH_ETH_REG_GIGABIT,
	.phy_interface		= PHY_INTERFACE_MODE_GMII,
	.ether_link_active_low	= 1,
};

static struct platform_device sh_eth_device = {
	.name		= "sh-eth",
	.id		= 0,
	.dev = {
		.platform_data	= &sh_eth_pdata,
	},
	.num_resources	= ARRAY_SIZE(sh_eth_resources),
	.resource	= sh_eth_resources,
};

/* USB Common */

#define USBPCTRL0	(0xFFE70800)
#define USBPCTRL1	(0xFFE70804)
#define USBST		(0xFFE70808)
#define USBEH0		(0xFFE7080C)
#define USBOH0		(0xFFE7081C)
#define USBCTL0		(0xFFE70858)
#define USB_MAGIC_REG0	(0xFFE70094)
#define USB_MAGIC_REG1	(0xFFE7009C)

#define USBPCTRL1_RST		(1 << 31)
#define USBPCTRL1_PHYRST	(1 << 2)
#define USBPCTRL1_PLLENB	(1 << 1)
#define USBPCTRL1_PHYENB	(1 << 0)
#define USBST_ACT		(1 << 31)
#define USBST_PLL		(1 << 30)


static void usb_phy_init(void)
{
	u32 data;

	/* Init check */
	if (__raw_readl(USBPCTRL1) & USBPCTRL1_PHYRST)
		return;

	__raw_writel(USBPCTRL1_RST, USBPCTRL1);
	__raw_writel(0,	USBPCTRL1);

	/* see SH7734 hardware manual 23.4 */

	data = USBPCTRL1_PHYENB;
	__raw_writel(data, USBPCTRL1);
	data |= USBPCTRL1_PLLENB;
	__raw_writel(data, USBPCTRL1);

	while (__raw_readl(USBST) != (USBST_ACT | USBST_PLL))
		cpu_relax();

	data |= USBPCTRL1_PHYRST;
	__raw_writel(data, USBPCTRL1);

	__raw_writel(0x00FF0040, USB_MAGIC_REG0);
	__raw_writel(0x00000001, USB_MAGIC_REG1);
}

/* USB 2.0 */

static struct ehci_sh_platdata usb_ehci_pdata = {
	.phy_init	= &usb_phy_init,
};

static struct resource usb_ehci_resources[] = {
	[0] = {
		.start	= 0xFFE70000,
		.end	= 0xFFE70058 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x580),
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 usb_ehci_dma_mask = 0xffffffffUL;
static struct platform_device usb_ehci_device = {
	.name	= "sh_ehci",
	.id	= -1,
	.dev = {
		.dma_mask 		= &usb_ehci_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &usb_ehci_pdata,
	},
	.num_resources	= ARRAY_SIZE(usb_ehci_resources),
	.resource	= usb_ehci_resources,
};

/* USB 1.1 */

static struct resource usb_ohci_resources[] = {
	[0] = {
		.start	= 0xFFE70400,
		.end	= 0xFFE7045C - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x580),
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 usb_ohci_dma_mask = 0xffffffffUL;
static struct platform_device usb_ohci_device = {
	.name	= "sh_ohci",
	.id	= -1,
	.dev = {
		.dma_mask		= &usb_ohci_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(usb_ohci_resources),
	.resource	= usb_ohci_resources,
};

/* HPB-DMAC */
static const struct hpb_dmae_slave_config hpb_dmae_slaves[] = {
	[0] = {
		.id	= SHDMA_SLAVE_SDHI1_TX,
		.addr	= 0xFFE4D030,
		.dcr	= SPDS_32BIT | DMDL | DPDS_32BIT,
		.port	= 0x1312,
		.dma_ch	= 23,
	},
	[1] = {
		.id	= SHDMA_SLAVE_SDHI1_RX,
		.addr	= 0xFFE4D030,
		.dcr	= SMDL | SPDS_32BIT | DPDS_32BIT,
		.port	= 0x1312,
		.dma_ch	= 23,
	},
	[2] = {
		.id	= SHDMA_SLAVE_SDHI2_TX,
		.addr	= 0xFFE4E030,
		.dcr	= SPDS_32BIT | DMDL | DPDS_32BIT,
		.port	= 0x1514,
		.dma_ch	= 24,
	},
	[3] = {
		.id	= SHDMA_SLAVE_SDHI2_RX,
		.addr	= 0xFFE4E030,
		.dcr	= SMDL | SPDS_32BIT | DPDS_32BIT,
		.port	= 0x1514,
		.dma_ch	= 24,
	},
};

static const struct hpb_dmae_channel hpb_dmae_channels[] = {
	[0] = {
		.offset	= 0x40 * 23,
		.ch_irq	= evt2irq(0xBE0),
		.s_id	= SHDMA_SLAVE_SDHI1_TX,
	},
	[1] = {
		.offset = 0x40 * 23,
		.ch_irq	= evt2irq(0xBE0),
		.s_id	= SHDMA_SLAVE_SDHI1_RX,
	},
	[2] = {
		.offset = 0x40 * 24,
		.ch_irq	= evt2irq(0xBE0),
		.s_id	= SHDMA_SLAVE_SDHI2_TX,
	},
	[3] = {
		.offset = 0x40 * 24,
		.ch_irq	= evt2irq(0xBE0),
		.s_id	= SHDMA_SLAVE_SDHI2_RX,
	},
};

static const unsigned int ts_shift[] = TS_SHIFT;

static struct hpb_dmae_pdata hpb_dmae_platform_data = {
	.slave		= hpb_dmae_slaves,
	.slave_num	= ARRAY_SIZE(hpb_dmae_slaves),
	.channel	= hpb_dmae_channels,
	.channel_num	= ARRAY_SIZE(hpb_dmae_channels),
	.ts_shift	= ts_shift,
	.ts_shift_num	= ARRAY_SIZE(ts_shift),
};

static struct resource hpb_dmae_resources[] = {
	[0] = {
		/* channel registers */
		.start	= 0xFFC08000,
		.end	= 0xFFC08000 + (0x40 * HPB_DMA_MAXCH) - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		/* common registers */
		.start 	= 0xFFC08800,
		.end	= 0xFFC088A4 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= evt2irq(0xB60),
		.end	= evt2irq(0xBE0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device hpb_dmae_device = {
	.name		= "hpb-dma-engine",
	.id		= -1,
	.resource	= hpb_dmae_resources,
	.num_resources	= ARRAY_SIZE(hpb_dmae_resources),
	.dev = {
		.platform_data	= &hpb_dmae_platform_data,
	},
};

/* DU */
static struct resource sh_du_resources[] = {
	[0] = {
		.start	= 0xFFF80000,
		.end	= 0xFFF9304C - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x3E0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sh_du_device = {
	.name		= "sh_du",
	.id		= -1,
	.resource	= sh_du_resources,
	.num_resources	= ARRAY_SIZE(sh_du_resources),
};

/* 2DG */
static struct resource sh_2dg_resources[] = {
	[0] = {
		.start	= 0xFFE80000,
		.end	= 0xFFE800FC - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x780),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sh_2dg_device = {
	.name		= "sh_2dg",
	.id		= -1,
	.resource	= sh_2dg_resources,
	.num_resources	= ARRAY_SIZE(sh_2dg_resources),
};

static struct platform_device *actlinux_alpha_devices[] __initdata = {
	&nor_flash_device,
	&sh_eth_device,
	&usb_ehci_device,
	&usb_ohci_device,
	&hpb_dmae_device,
	&sh_du_device,
	&sh_2dg_device,
};

/* I2C devices */
static struct i2c_board_info actlinux_alpha_i2c_devices[] __initdata = {
	/* RTC */
	{
		I2C_BOARD_INFO("rx8025", 0x32)
	}
};

static int __init actlinux_alpha_arch_init(void)
{
	i2c_register_board_info(0, actlinux_alpha_i2c_devices,
				ARRAY_SIZE(actlinux_alpha_i2c_devices));

	return platform_add_devices(actlinux_alpha_devices,
				ARRAY_SIZE(actlinux_alpha_devices));
}
arch_initcall(actlinux_alpha_arch_init);

static void __init actlinux_alpha_init_irq(void)
{
	int i;

	pr_info("INTC2 status:\n");
	pr_info("\tINT2MSKRG\t= 0x%08x\n", __raw_readl(0xFF804040));
	for (i = 0; i <= 11; i++) {
		pr_info("\tINT2PRI%d\t= 0x%08x\n", i,
			__raw_readl(0xFF804000 + (4 * i)));
	}
}

/* Machine Vector */
static struct sh_machine_vector mv_actlinux_alpha __initmv = {
	.mv_name	= "Actlinux-Alpha",
	.mv_init_irq	= actlinux_alpha_init_irq,
};

