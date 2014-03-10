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
#include <linux/gpio.h>
#include <linux/mtd/physmap.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/sh_clk.h>
#include <linux/sh_intc.h>
#include <linux/videodev2.h>
#include <linux/usb/r8a66597.h>
#include <linux/usb/renesas_usbhs.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mobile_sdhi.h>
#include <net/ax88796.h>
#include <video/sh_mobile_lcdc.h>
#include <asm/machvec.h>
#include <asm/suspend.h>
#include <asm/sizes.h>
#include <cpu/sh7724.h>

/* NOR Flash */

static struct physmap_flash_data nor_flash_data = {
	.width		= 2,
};

static struct resource nor_flash_resources[] = {
	[0] = {
		.start	= 0x00000000,
		.end	= 0x00000000 + SZ_32M - 1,
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

static struct ax_plat_data ax88796_platdata = {
	.flags		= AXFLG_MAC_FROMDEV,
	.wordlength	= 2,
	.dcr_val	= 0x01,
	.rcr_val	= 0x00,
};

static struct resource ax88796_resources[] = {
	[0] = {
		.start	= 0x19000000,
		.end	= 0x19000020 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0x6a0),
		.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_LOW,
	},
};

static struct platform_device ax88796_device = {
	.name		= "ax88796",
	.id		= 0,
	.dev = {
		.platform_data = &ax88796_platdata,
	},
	.num_resources	= ARRAY_SIZE(ax88796_resources),
	.resource	= ax88796_resources,
};

/* USB0 host */

static void usb0_port_power(int port, int power)
{
	gpio_set_value(GPIO_PTX1, power ? 0 : 1);
}

static struct r8a66597_platdata usb0_host_data = {
	.on_chip = 1,
	.port_power = usb0_port_power,
};

static struct resource usb0_host_resources[] = {
	[0] = {
		.start	= 0xa4d80000,
		.end	= 0xa4d80124 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0xa20),
		.end	= evt2irq(0xa20),
		.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_LOW,
	},
};

static struct platform_device usb0_host_device = {
	.name	= "r8a66597_hcd",
	.id	= 0,
	.dev	= {
		.dma_mask		= NULL,         /*  not use dma */
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &usb0_host_data,
	},
	.num_resources	= ARRAY_SIZE(usb0_host_resources),
	.resource	= usb0_host_resources,
};

/* USB1 host */

static void usb1_port_power(int port, int power)
{
	gpio_set_value(GPIO_PTX7, power ? 0 : 1);
}

static struct r8a66597_platdata usb1_host_data = {
	.on_chip = 1,
	.port_power = usb1_port_power,
};

static struct resource usb1_host_resources[] = {
	[0] = {
		.start	= 0xa4d90000,
		.end	= 0xa4d90124 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0xa40),
		.end	= evt2irq(0xa40),
		.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_LOW,
	},
};

static struct platform_device usb1_host_device = {
	.name		= "r8a66597_hcd",
	.id		= 1,
	.dev = {
		.dma_mask		= NULL,         /*  not use dma */
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &usb1_host_data,
	},
	.num_resources	= ARRAY_SIZE(usb1_host_resources),
	.resource	= usb1_host_resources,
};

/* LCD */

const static struct fb_videomode tfp410_modes[] = {
	{
		.name		= "1024x768@60",
		.xres		= 1024,
		.yres		= 768,
		.pixclock	= 15384,
		.left_margin	= 168,
		.right_margin	= 8,
		.upper_margin	= 29,
		.lower_margin	= 3,
		.hsync_len	= 144,
		.vsync_len	= 4,
		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
	{
		.name		= "800x600@60",
		.xres		= 800,
		.yres		= 600,
		.pixclock	= 27778,
		.left_margin	= 64,
		.right_margin	= 24,
		.upper_margin	= 22,
		.lower_margin	= 1,
		.hsync_len	= 72,
		.vsync_len	= 2,
		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
};

static struct sh_mobile_lcdc_info lcdc_info = {
	.clock_source	= LCDC_CLK_BUS,
	.ch[0] = {
		.chan			= LCDC_CHAN_MAINLCD,
		.fourcc			= V4L2_PIX_FMT_RGB565,
		.interface_type		= RGB18,
		.clock_divider		= 60,
		.flags			= 0u,
		.lcd_modes		= tfp410_modes,
		.num_modes		= ARRAY_SIZE(tfp410_modes),
	},
};

static struct resource lcdc_resources[] = {
	[0] = {
		.name	= "LCDC",
		.start	= 0xfe940000,
		.end	= 0xfe942fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= evt2irq(0xf40),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device lcdc_device = {
	.name		= "sh_mobile_lcdc_fb",
	.num_resources	= ARRAY_SIZE(lcdc_resources),
	.resource	= lcdc_resources,
	.dev		= {
		.platform_data	= &lcdc_info,
	},
};

/* SDHI0 */

static struct sh_mobile_sdhi_info sdhi0_info = {
	.dma_slave_tx	= SHDMA_SLAVE_SDHI0_TX,
	.dma_slave_rx	= SHDMA_SLAVE_SDHI0_RX,
	.tmio_caps      = MMC_CAP_SDIO_IRQ,
};

static struct resource sdhi0_resources[] = {
	[0] = {
		.name	= "SDHI0",
		.start  = 0x04ce0000,
		.end    = 0x04ce00ff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = evt2irq(0xe80),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device sdhi0_device = {
	.name           = "sh_mobile_sdhi",
	.num_resources  = ARRAY_SIZE(sdhi0_resources),
	.resource       = sdhi0_resources,
	.id             = 0,
	.dev	= {
		.platform_data	= &sdhi0_info,
	},
};

/* SDHI1 */

static void sdhi1_set_pwr(struct platform_device *pdev, int state)
{
	gpio_set_value(GPIO_PTM0, state);
};

static struct sh_mobile_sdhi_info sdhi1_info = {
	.dma_slave_tx	= SHDMA_SLAVE_SDHI1_TX,
	.dma_slave_rx	= SHDMA_SLAVE_SDHI1_RX,
	.tmio_caps      = MMC_CAP_SDIO_IRQ | MMC_CAP_POWER_OFF_CARD,
	.set_pwr	= sdhi1_set_pwr,
};

static struct resource sdhi1_resources[] = {
	[0] = {
		.name	= "SDHI1",
		.start  = 0x04cf0000,
		.end    = 0x04cf00ff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = evt2irq(0x4e0),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device sdhi1_device = {
	.name           = "sh_mobile_sdhi",
	.num_resources  = ARRAY_SIZE(sdhi1_resources),
	.resource       = sdhi1_resources,
	.id             = 1,
	.dev	= {
		.platform_data	= &sdhi1_info,
	},
};

static struct platform_device *actlinux_beta_devices[] __initdata = {
	&nor_flash_device,
	&ax88796_device,
	&usb0_host_device,
	&usb1_host_device,
	&lcdc_device,
#if defined(CONFIG_MMC_SDHI) || defined(CONFIG_MMC_SDHI_MODULE)
	&sdhi0_device,
	&sdhi1_device,
#endif
};

/* I2C devices */
static struct i2c_board_info actlinux_beta_i2c_devices[] __initdata = {
	/* RTC */
	{
		I2C_BOARD_INFO("rx8025", 0x32)
	}
};

#define PORT_HIZA	0xA4050158
#define MSELCRB		0xA4050182
#define IODRIVEA	0xA405018A

extern char actlinux_beta_sdram_enter_start;
extern char actlinux_beta_sdram_enter_end;
extern char actlinux_beta_sdram_leave_start;
extern char actlinux_beta_sdram_leave_end;

static void enable_module(const char *name)
{
	struct clk *clk = clk_get(NULL, name);

	if (IS_ERR(clk)) {
		pr_err("Failed to enable %s\n", name);
	} else {
		clk_enable(clk);
		clk_put(clk);
	}
}

static int __init actlinux_beta_arch_init(void)
{
	/* register board specific self-refresh code */
	sh_mobile_register_self_refresh(SUSP_SH_STANDBY | SUSP_SH_SF |
					SUSP_SH_RSTANDBY,
					&actlinux_beta_sdram_enter_start,
					&actlinux_beta_sdram_enter_end,
					&actlinux_beta_sdram_leave_start,
					&actlinux_beta_sdram_leave_end);

	/* enable STATUS0, STATUS2 and PDSTATUS */
	gpio_request(GPIO_FN_STATUS0, NULL);
	gpio_request(GPIO_FN_STATUS2, NULL);
	gpio_request(GPIO_FN_PDSTATUS, NULL);

	/* enable SCIF */
	//gpio_request(GPIO_FN_SCIF0_SCK, NULL); // PTM0 is used
	gpio_request(GPIO_FN_SCIF0_TXD, NULL);
	gpio_request(GPIO_FN_SCIF0_RXD, NULL);
	gpio_request(GPIO_FN_SCIF1_SCK, NULL);
	gpio_request(GPIO_FN_SCIF1_TXD, NULL);
	gpio_request(GPIO_FN_SCIF1_RXD, NULL);
	gpio_request(GPIO_FN_SCIF2_V_SCK, NULL);
	gpio_request(GPIO_FN_SCIF2_V_TXD, NULL);
	gpio_request(GPIO_FN_SCIF2_V_RXD, NULL);
	gpio_request(GPIO_FN_SCIF3_V_SCK, NULL);
	gpio_request(GPIO_FN_SCIF3_V_TXD, NULL);
	gpio_request(GPIO_FN_SCIF3_V_RXD, NULL);
	gpio_request(GPIO_FN_SCIF3_V_CTS, NULL);
	gpio_request(GPIO_FN_SCIF3_V_RTS, NULL);
	gpio_request(GPIO_FN_SCIF5_SCK, NULL);
	gpio_request(GPIO_FN_SCIF5_TXD, NULL);
	gpio_request(GPIO_FN_SCIF5_RXD, NULL);
	/* select SCIF2_V and SCIF3_V */
	__raw_writew((__raw_readw(MSELCRB) & ~0x0020) | 0x0080, MSELCRB);

	/* enable USB */
	__raw_writew(0x0000, 0xA4D80000);
	__raw_writew(0x0000, 0xA4D90000);
	gpio_request_one(GPIO_PTX1, GPIOF_OUT_INIT_HIGH, NULL);
	gpio_request_one(GPIO_PTX7, GPIOF_OUT_INIT_HIGH, NULL);
	__raw_writew(0x0600, 0xa40501d4);
	__raw_writew(0x0600, 0xa4050192);

	/* enable LCDC */
	//gpio_request(GPIO_FN_LCDD23,   NULL);
	//gpio_request(GPIO_FN_LCDD22,   NULL);
	//gpio_request(GPIO_FN_LCDD21,   NULL);
	//gpio_request(GPIO_FN_LCDD20,   NULL);
	//gpio_request(GPIO_FN_LCDD19,   NULL);
	//gpio_request(GPIO_FN_LCDD18,   NULL);
	gpio_request(GPIO_FN_LCDD17,   NULL);
	gpio_request(GPIO_FN_LCDD16,   NULL);
	gpio_request(GPIO_FN_LCDD15,   NULL);
	gpio_request(GPIO_FN_LCDD14,   NULL);
	gpio_request(GPIO_FN_LCDD13,   NULL);
	gpio_request(GPIO_FN_LCDD12,   NULL);
	gpio_request(GPIO_FN_LCDD11,   NULL);
	gpio_request(GPIO_FN_LCDD10,   NULL);
	gpio_request(GPIO_FN_LCDD9,    NULL);
	gpio_request(GPIO_FN_LCDD8,    NULL);
	gpio_request(GPIO_FN_LCDD7,    NULL);
	gpio_request(GPIO_FN_LCDD6,    NULL);
	gpio_request(GPIO_FN_LCDD5,    NULL);
	gpio_request(GPIO_FN_LCDD4,    NULL);
	gpio_request(GPIO_FN_LCDD3,    NULL);
	gpio_request(GPIO_FN_LCDD2,    NULL);
	gpio_request(GPIO_FN_LCDD1,    NULL);
	gpio_request(GPIO_FN_LCDD0,    NULL);
	gpio_request(GPIO_FN_LCDDISP,  NULL);
	gpio_request(GPIO_FN_LCDHSYN,  NULL);
	gpio_request(GPIO_FN_LCDDCK,   NULL);
	gpio_request(GPIO_FN_LCDVSYN,  NULL);
	//gpio_request(GPIO_FN_LCDDON,   NULL);
	//gpio_request(GPIO_FN_LCDLCLK,  NULL);
	//__raw_writew((__raw_readw(PORT_HIZA) & ~0x0001), PORT_HIZA);

	/* I/O buffer drive ability is high */
	__raw_writew((__raw_readw(IODRIVEA) & ~0x00c0) | 0x0080 , IODRIVEA);

#ifdef CONFIG_MMC_SDHI
	gpio_request(GPIO_FN_SDHI0CD,  NULL);
	gpio_request(GPIO_FN_SDHI0WP,  NULL);
	gpio_request(GPIO_FN_SDHI0CMD, NULL);
	gpio_request(GPIO_FN_SDHI0CLK, NULL);
	gpio_request(GPIO_FN_SDHI0D3,  NULL);
	gpio_request(GPIO_FN_SDHI0D2,  NULL);
	gpio_request(GPIO_FN_SDHI0D1,  NULL);
	gpio_request(GPIO_FN_SDHI0D0,  NULL);

	gpio_request(GPIO_FN_SDHI1CD,  NULL);
	gpio_request(GPIO_FN_SDHI1WP,  NULL);
	gpio_request(GPIO_FN_SDHI1CMD, NULL);
	gpio_request(GPIO_FN_SDHI1CLK, NULL);
	gpio_request(GPIO_FN_SDHI1D3,  NULL);
	gpio_request(GPIO_FN_SDHI1D2,  NULL);
	gpio_request(GPIO_FN_SDHI1D1,  NULL);
	gpio_request(GPIO_FN_SDHI1D0,  NULL);
	gpio_request_one(GPIO_PTM0, GPIOF_OUT_INIT_LOW, NULL);

	/* I/O buffer drive ability is high for SDHI1 */
	__raw_writew((__raw_readw(IODRIVEA) & ~0x3000) | 0x2000 , IODRIVEA);
#endif

	/* enable IRQ */
	gpio_request(GPIO_FN_INTC_IRQ0, NULL);
	gpio_request(GPIO_FN_INTC_IRQ1, NULL);
	gpio_request(GPIO_FN_INTC_IRQ2, NULL);
	gpio_request(GPIO_FN_INTC_IRQ3, NULL);
	gpio_request(GPIO_FN_INTC_IRQ4, NULL);
	/* AX88796B */
	gpio_request(GPIO_FN_INTC_IRQ5, NULL);
	irq_set_irq_type(evt2irq(0x6a0), IRQ_TYPE_LEVEL_LOW);
	gpio_request(GPIO_FN_INTC_IRQ6, NULL);
	gpio_request(GPIO_FN_INTC_IRQ7, NULL);

	i2c_register_board_info(0, actlinux_beta_i2c_devices,
				ARRAY_SIZE(actlinux_beta_i2c_devices));

	enable_module("2dg0");
	enable_module("beu0");
	enable_module("veu0");
	enable_module("jpu0");

	return platform_add_devices(actlinux_beta_devices,
				ARRAY_SIZE(actlinux_beta_devices));
}
arch_initcall(actlinux_beta_arch_init);

static int __init actlinux_beta_clk_init(void)
{
	struct clk *clk = clk_get_sys("sh_mobile_lcdc_fb.0", NULL);

	if (clk && ((ioread32(clk->mapped_reg) & (1 << clk->enable_bit)) == 0))
		/* enabled by U-boot -> don't disable on init */
		clk->flags |= CLK_ENABLE_ON_INIT;

	return 0;
}

/* Machine Vector */
static struct sh_machine_vector mv_actlinux_beta __initmv = {
	.mv_name	= "Actlinux-Beta",
	.mv_clk_init	= actlinux_beta_clk_init,
};

