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
#include <linux_platform_data/echi_sh.h>
#include <linux/mtd/physmap.h>
#include <linux/sh_eth.h>
#include <cpu/sh7734.h>
#include <asm/machvec.h>
#include <asm/sizes.h>

/* NOR Flash */

static struct mtd_partition nor_flash_partitions[] = {
	{
		.name		= "boot loader",
		.offset		= 0x00000000,
		.size		= SZ_256K,
		.mask_flags	= MTD_WRITEABLE, /* read-only */
	},
	{
		.name		= "boot env",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_256K,
		.mask_flags	= MTD_WRITEABLE, /* read-only */
	},
	{
		.name		= "splash image",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_1M + SZ_512K,
	},
	{
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_8M,
	}
	{
		.name		= "user",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
	},
};

static struct physmap_flash_data nor_flash_data = {
	.width		= 2,
	.parts		= nor_flash_partitions,
	.nr_pats	= ARRAY_SIZE(nor_flash_partitions),
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
		.start	= 0xFFE00000,
		.end	= 0xFFE007FF,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 0xFFE01800,
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
		.platform_data	= &sh_eth_pdata;
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
	.num_resources	= ARRAY_SIZE(usb_ohci_resouces),
	.resource	= usb_ohci_resources,
};

static struct platform_device *actlinux_alpha_devices[] __initdata = {
	&nor_flash_device,
	&sh_eth_device,
	&usb_ehci_device,
	&usb_ohci_device,
};

static int __init actlinux_alpha_arch_init(void)
{
	return platform_add_devices(actlinux_alpha_devices,
				ARRAY_SIZE(actlinux_alpha_devices));
}
arch_initcall(actlinux_arch_init);

static int __init actlinux_alpha_setup(char **cmdline_p)
{
	pr_info("Act Brain Actlinux-Alpha support:\n");
}

/* Machine Vector */
static struct sh_machine_vector mv_actlinux_alpha __initmv = {
	.mv_name	= "Actlinux-Alpha",
	.mv_setup	= actlinux_alpha_setup,
};

