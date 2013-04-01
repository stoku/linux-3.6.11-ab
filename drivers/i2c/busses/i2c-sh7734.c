/*
 * SuperH SH7734 I2C Controller
 *
 * Copyright (C) 2008 Magnus Damm
 * Copyright (C) 2012 Nobuhiro Iwamatsu <nobuhiro.iwamatsu.yj@renesas.com>
 * Copyright (C) 2012 Renesas Solutions Corp.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Based on drivers/i2c/busses/i2c-sh_mobile.c.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/i2c/i2c-sh_mobile.h>

enum sh7734_i2c_op {
	OP_START_TRS = 0,
	OP_START_SCP,
	OP_TX_FIRST,
	OP_TX_FIRST_END,
	OP_TX,
	OP_TX_END,
	OP_TX_STOP,
	OP_TX_STOP_CHK,
	OP_TX_FINISH,
	OP_RX,
	OP_RX_BBSY_CLR,
	OP_RX_LAST_DATA,
	OP_RX_FINISH,
	OP_RX_STOP,
	OP_RX_STOP_CHK,
};

struct sh7734_i2c_data {
	struct device *dev;
	void __iomem *reg;
	struct i2c_adapter adap;
	unsigned long bus_speed;
	struct clk *clk;
	u_int8_t nf2cyc;
	u_int8_t nf2cyc_clk;
	u_int8_t iccr1_clk;
	spinlock_t lock;
	wait_queue_head_t wait;
	struct i2c_msg *msg;
	int pos;
	int sr;
	int irq;
	int ackbr;
	int state;
	int data_num;
};

#define NORMAL_SPEED	100000 /* FAST_SPEED 400000 */

/* Register offsets */
#define ICCR1	0x0
#define ICCR2	0x1
#define ICMR	0x2
#define ICIER	0x3
#define ICSR	0x4
#define SAR		0x5
#define ICDRT	0x6
#define ICDRR	0x7
#define NF2CYC	0x8

#define ICCR1_ICE		0x80
#define ICCR1_RCVD		0x40
#define ICCR1_MST		0x20
#define ICCR1_TRS		0x10

#define SW_DONE			0x100
#define SW_ERROR		0x200

#define ICCR2_BBSY		0x80
#define ICCR2_SCP		0x40
#define ICCR2_SDAO		0x20
#define ICCR2_SDAOP		0x10
#define ICCR2_SCLO		0x08
#define ICCR2_I2CRST	0x02

#define ICSR_TDRE		0x80
#define ICSR_TEND		0x40
#define ICSR_RDRF		0x20
#define ICSR_NACKF		0x10
#define ICSR_STOP		0x08
#define ICSR_AL			0x04
#define ICSR_AAS		0x02
#define ICSR_ADZ		0x01

#define ICIER_TIE	0x80	/* Transfer interrputs */
#define ICIER_TEIE	0x40	/* Transfer end interrupts */
#define ICIER_RIE	0x20	/* Receive interrupts */
#define ICIER_NAKIE	0x10	/* NACK interrupts */
#define ICIER_STIE	0x08	/* Stop interrupts */
#define ICIER_ACKE	0x04	/* ACKbit check*/
#define ICIER_ACKBR	0x02	/* set ACKbit / Receive */
#define ICIER_ACKBT	0x01	/* set ACKbit / Transfer */
#define ICIER_RX (ICIER_RIE|ICIER_TEIE|ICIER_TIE|ICIER_NAKIE|ICIER_STIE)
#define ICIER_TX (ICIER_TIE|ICIER_TEIE|ICIER_NAKIE|ICIER_STIE)

#define I2C0_INTMASK 0xFF804288
#define I2C1_INTMASK 0xFF80428C
#define I2C0_INTMASK_CLR 0xFF8042A8
#define I2C1_INTMASK_CLR 0xFF8042AC
#define I2C0_INT 0xFF8040AC
#define I2C1_INT 0xFF8040D8

#define I2C_INT_TXI	(1 << 4)
#define I2C_INT_TEI	(1 << 3)
#define I2C_INT_RXI	(1 << 2)
#define I2C_INT_NAKI	(1 << 1)
#define I2C_INT_STPI	(1 << 0)
#define I2C_INT_ALL	\
	(I2C_INT_TXI|I2C_INT_TEI|I2C_INT_RXI|I2C_INT_NAKI|I2C_INT_STPI)

static void
iic_wr(struct sh7734_i2c_data *pd, int offs, unsigned char data)
{
	iowrite8(data, pd->reg + offs);
}

static unsigned char iic_rd(struct sh7734_i2c_data *pd, int offs)
{
	return ioread8(pd->reg + offs);
}

static void iic_set_clr(struct sh7734_i2c_data *pd, int offs,
			unsigned char set, unsigned char clr)
{
	iic_wr(pd, offs, (iic_rd(pd, offs) | set) & ~clr);
}

static int clk_denom_tbl[] = {
	44, 52, 64, 72, 84, 92, 100, 108, 176,
	208, 256, 288, 336, 368, 400, 432, 352, 416,
	512, 576, 672, 736, 800, 864, 704, 832, 1024,
	1152, 1344, 1472, 1600, 1728 };

static void activate_ch(struct sh7734_i2c_data *pd, int num)
{
	unsigned long pclk;
	unsigned long mode, mode_tmp;
	unsigned int cks_bit;
	long denom, speed;
	unsigned int i;

	pclk = clk_get_rate(pd->clk) / 1000;
	mode = speed = pd->bus_speed / 1000;

	for (i = 0; i < ARRAY_SIZE(clk_denom_tbl); i++) {
		denom = (unsigned int)(pclk / clk_denom_tbl[i]);
		if (denom > (speed * 2))
			continue;
		else if ((speed - denom) > 100)
			continue;
		mode_tmp = (unsigned int)(denom % speed);

		if (mode_tmp < speed) {
			if (mode_tmp < mode) {
				mode = mode_tmp;
				cks_bit = i;
			}
		}
	}

	pd->nf2cyc = 1;
	pd->nf2cyc_clk = (u_int8_t)(cks_bit & 0x10);
	pd->iccr1_clk = (u_int8_t)(cks_bit & 0x0F);
	pd->state = OP_START_TRS;
	pd->ackbr = 0;
	pd->data_num = num;

	/* Mask clear interrupts */
	if (pd->adap.nr == 0)
		__raw_writel(I2C_INT_ALL, I2C0_INTMASK_CLR);
	else
		__raw_writel(I2C_INT_ALL, I2C1_INTMASK_CLR);
}

static void deactivate_ch(struct sh7734_i2c_data *pd)
{
	/* Clear/disable interrupts */
	iic_wr(pd, ICSR, 0);
	iic_wr(pd, ICIER, 0);

	/* Mask interrupts */
	if (pd->adap.nr == 0)
		__raw_writel(I2C_INT_ALL, I2C0_INTMASK);
	else
		__raw_writel(I2C_INT_ALL, I2C1_INTMASK);

	/* Disable channel */
	iic_set_clr(pd, ICCR1, 0, ICCR1_ICE);

	pd->ackbr = 0;
}

static int i2c_op(struct sh7734_i2c_data *pd)
{
	int ret = 0;
	unsigned char data;
	unsigned long flags;

	spin_lock_irqsave(&pd->lock, flags);

	switch (pd->state) {
	case OP_START_TRS:
		if (!(iic_rd(pd, ICCR2) & ICCR2_BBSY)) {
			/* set to master transfer mode */
			iic_set_clr(pd,	ICCR1, ICCR1_MST | ICCR1_TRS, 0);
			pd->state = OP_TX_FIRST;
		} else {
			dev_dbg(pd->dev, "Device busy\n");
			pd->state = OP_TX_STOP;
			break;
		}

		iic_set_clr(pd,	ICCR2, ICCR2_BBSY, ICCR2_SCP);
		break;

	case OP_START_SCP:
		/* Send TIE, Set TDRE */
		iic_set_clr(pd,	ICCR2, ICCR2_BBSY, ICCR2_SCP);
		pd->state = OP_TX_FIRST;
		break;

	case OP_TX_FIRST:
		if (iic_rd(pd, ICSR) & ICSR_TDRE) {
			/* Create command */
			data = (pd->msg->addr & 0x7f) << 1;
			data |= (pd->msg->flags & I2C_M_RD) ? 1 : 0;

			/* write data with clear TDRE bit */
			iic_wr(pd, ICDRT, data);
			dev_dbg(pd->dev, "Write data (ICDRT): %02X\n", data);
			pd->state = OP_TX_FIRST_END;
		} else
			dev_dbg(pd->dev, "ICSR/TDRE is not set\n");

		break;

	case OP_TX_FIRST_END:
		if (!(iic_rd(pd, ICSR) & ICSR_TEND)) {
			dev_dbg(pd->dev, "ICSR/TEND is not set\n");
			break;
		}

		/* Don't clear TEND */
		if (pd->ackbr) {
			if (pd->msg->flags & I2C_M_RD) {
				iic_set_clr(pd, ICSR, 0, ICSR_TEND);
				iic_set_clr(pd, ICCR1, ICCR1_MST, ICCR1_TRS);
				iic_set_clr(pd, ICSR, 0, ICSR_TDRE);

				if (pd->msg->len == 1) {
					iic_set_clr(pd,	ICIER, ICIER_ACKBT, 0);
					iic_set_clr(pd,	ICCR1, ICCR1_RCVD, 0);

					data = iic_rd(pd, ICDRR);
					/* If pd->msg->len == 1,
					   this read data is dummy. */
					dev_dbg(pd->dev, "Last - 1 read data %02X\n",
								data);

					pd->state = OP_RX_STOP_CHK;
				} else {
					iic_set_clr(pd, ICIER, 0, ICIER_ACKBT);
					data = iic_rd(pd, ICDRR);
					dev_dbg(pd->dev,
						"Dummy read data %02X\n", data);

					pd->state = OP_RX;
				}
			} else
				pd->state = OP_TX_STOP;

			break;
		}

		if (iic_rd(pd, ICIER) & ICIER_ACKBR) {
			dev_err(pd->dev, "ACKBR Error...\n");
			pd->sr |= SW_ERROR; /* Save Error state */

			pd->state = OP_TX_STOP;
			break;
		} else {
			pd->ackbr = 1;
			pd->state = OP_TX;
		}

		/* Through to OP_TX */

	case OP_TX: /* send data */
		if (iic_rd(pd, ICSR) & ICSR_TDRE) {
			data = pd->msg->buf[pd->pos];
			/* write data with clear TDRE bit */
			iic_wr(pd, ICDRT, data);
			dev_dbg(pd->dev, "Write data (ICDRT): %02X\n", data);
			pd->pos++;

			pd->state = OP_TX_END;
		} else
			dev_dbg(pd->dev, "Retry Data translate\n");

		break;

	case OP_TX_END:
		if (!(iic_rd(pd, ICSR) & ICSR_TEND)) {
			dev_dbg(pd->dev, "TEND bit was not set (%02X)\n",
					iic_rd(pd, ICSR));
			break;
		}

		dev_dbg(pd->dev, "pd->pos %d, pd->msg->len %d, pd->data_num %d\n",
			pd->pos, pd->msg->len, pd->data_num);
		if (pd->pos == pd->msg->len) {
			/* If last data, need check ICSR_TDRE and ICSR_TEND. */
			if (!(iic_rd(pd, ICSR) & ICSR_TDRE))
				break;

			/* Last data / Last Packet */
			if (pd->data_num == 1) {
				pd->state = OP_TX_STOP;
				/* Through to OP_TX_STOP */
			} else if (pd->data_num > 1) {
				/* Change Transfer to Receive */
				iic_wr(pd, ICIER, 0x0); /* Interrupts disable */
				pd->state = OP_TX_FINISH;
				ret = 1;
				break;
			}
		} else {
			pd->state = OP_TX;
			break;
		}
		/* Through to OP_TX_STOP */

	case OP_TX_STOP:
		iic_set_clr(pd, ICSR, 0, ICSR_TEND);
		iic_set_clr(pd, ICSR, 0, ICSR_STOP);
		iic_set_clr(pd, ICCR2, 0, ICCR2_BBSY | ICCR2_SCP);
		udelay(10);
		pd->state = OP_TX_STOP_CHK;
		/* Through to OP_TX_STOP_CHK */

	case OP_TX_STOP_CHK:
		if (!(iic_rd(pd, ICSR) & ICSR_STOP)) {
			dev_dbg(pd->dev, "ICSR/STOP is not set\n");
			break;
		}
		iic_set_clr(pd,	ICCR1, 0, ICCR1_MST | ICCR1_TRS);
		iic_set_clr(pd,	ICSR, 0, ICSR_TDRE);

		/* Interrupts disable */
		iic_wr(pd, ICIER, 0x0);
		pd->state = OP_TX_FINISH;
		ret = 1;

		break;

	case OP_RX:
		if (!(iic_rd(pd, ICSR) & ICSR_RDRF))
			break;

		data = iic_rd(pd, ICDRR);
		pd->msg->buf[pd->pos] = data;
		pd->pos++;

		dev_dbg(pd->dev, "Read data %02X\n", data);

		if (pd->pos == pd->msg->len)
			pd->state = OP_RX_STOP;

		break;

	case OP_RX_STOP: /* enable DTE interrupt, issue stop */
		iic_set_clr(pd,	ICIER, ICIER_ACKBT, 0);
		iic_set_clr(pd,	ICCR1, ICCR1_RCVD, 0);
		pd->state = OP_RX_BBSY_CLR;

		break;

	case OP_RX_BBSY_CLR:
		data = iic_rd(pd, ICDRR);
		dev_dbg(pd->dev, "Last - 1 read data %02X\n", data);
		pd->msg->buf[pd->pos] = data;
		pd->pos++;
		pd->state = OP_RX_STOP_CHK;

		break;

	case OP_RX_STOP_CHK:
		if (!(iic_rd(pd, ICSR) & ICSR_RDRF)) {
			dev_dbg(pd->dev, "ICSR/RDRF bit 0\n");
			break;
		}

		iic_set_clr(pd, ICSR, 0, ICSR_STOP);
		iic_set_clr(pd,	ICCR2, 0, ICCR2_BBSY|ICCR2_SCP);

		pd->state = OP_RX_LAST_DATA;
		/* Through to OP_RX_LAST_DATA */

	case OP_RX_LAST_DATA:
		if (iic_rd(pd, ICCR2) && ICCR2_BBSY)
			iic_set_clr(pd,	ICCR2, 0, ICCR2_BBSY|ICCR2_SCP);

		if (!(iic_rd(pd, ICSR) & ICSR_STOP)) {
			dev_dbg(pd->dev, "ICSR/STOP is not set\n");
			break;
		}

		iic_set_clr(pd, ICSR, 0, ICSR_STOP);

		data = iic_rd(pd, ICDRR);
		pd->msg->buf[pd->pos] = data;

		dev_dbg(pd->dev, "Last read data %02X\n", data);

		pd->pos++;

		iic_set_clr(pd,	ICCR1, 0, ICCR1_MST);
		iic_set_clr(pd,	ICSR, 0, ICSR_TDRE);
		iic_set_clr(pd,	ICCR1, 0, ICCR1_RCVD);

		iic_wr(pd, ICIER, 0x00);
		pd->state = OP_RX_FINISH;
		ret = 1;

		break;
	}

	spin_unlock_irqrestore(&pd->lock, flags);

	return ret;
}

static irqreturn_t sh7734_i2c_isr(int irq, void *dev_id)
{
	struct platform_device *dev = dev_id;
	struct sh7734_i2c_data *pd = platform_get_drvdata(dev);
	unsigned char sr;
	int wakeup;

	sr = iic_rd(pd, ICSR);
	pd->sr |= sr; /* remember state */

	dev_dbg(pd->dev, "%s ICSR: 0x%02x sr: 0x%02x %s %d %d!\n",
		__func__, sr, pd->sr,
		(pd->msg->flags & I2C_M_RD) ? "read" : "write",
		pd->pos, pd->msg->len);

	if (sr & (ICSR_AL)) {
		/* error / abitration */
		iic_wr(pd, ICSR, sr & ~ICSR_AL);
		wakeup = 0;
	} else {
		wakeup = i2c_op(pd);
	}

	if (wakeup) {
		pd->sr |= SW_DONE;
		wake_up(&pd->wait);
	}

	return IRQ_HANDLED;
}

static int start_ch(struct sh7734_i2c_data *pd, struct i2c_msg *usr_msg)
{
	unsigned long flags;

	if (usr_msg->len == 0 && (usr_msg->flags & I2C_M_RD)) {
		dev_err(pd->dev, "Unsupported zero length i2c read\n");
		return -EIO;
	}

	if (pd->ackbr == 0) {
		iic_wr(pd, ICCR1, pd->iccr1_clk | ICCR1_ICE);
		iic_wr(pd, NF2CYC, pd->nf2cyc | pd->nf2cyc_clk);
	}

	spin_lock_irqsave(&pd->lock, flags);

	pd->msg = usr_msg;
	pd->pos = 0;
	pd->sr = 0;

	/* Enable interrupts */
	if (usr_msg->flags & I2C_M_RD) {
		if ((pd->ackbr) && (pd->state == OP_TX_FINISH))
			pd->state = OP_START_SCP;

		iic_wr(pd, ICIER, ICIER_RX);
	} else {
		if (pd->state == OP_RX)
			pd->state = OP_TX;

		iic_wr(pd, ICIER, ICIER_TX);
	}
	spin_unlock_irqrestore(&pd->lock, flags);

	return 0;
}

static int sh7734_i2c_xfer(struct i2c_adapter *adapter,
			      struct i2c_msg *msgs,
			      int num)
{
	struct sh7734_i2c_data *pd = i2c_get_adapdata(adapter);
	struct i2c_msg	*msg;
	int err = 0, i, k;
	u_int8_t val;

	activate_ch(pd, num);

	/* Process all messages */
	for (i = 0; i < num; i++) {
		msg = &msgs[i];

		err = start_ch(pd, msg);
		if (err)
			break;

		i2c_op(pd);

		k = wait_event_timeout(pd->wait,
				       pd->sr & (SW_DONE|SW_ERROR),
				       5 * HZ);
		if (!k) {
			dev_err(pd->dev, "Transfer request timed out\n");
			err = -EIO;
			dev_err(pd->dev, "Polling timed out\n");
			break;
		}

		val = iic_rd(pd, ICSR);
		/* handle missing acknowledge and arbitration lost */
		if (((val | pd->sr) & (ICSR_AL | SW_ERROR))) {
			dev_err(pd->dev, "I2C I/O error\n");
			err = -EIO;
			break;
		}
	}

	deactivate_ch(pd);

	if (!err)
		err = num;

	return err;
}

static u32 sh7734_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static struct i2c_algorithm sh7734_i2c_algorithm = {
	.functionality	= sh7734_i2c_func,
	.master_xfer	= sh7734_i2c_xfer,
};

static int sh7734_i2c_probe(struct platform_device *pdev)
{
	struct i2c_sh_mobile_platform_data *pdata = pdev->dev.platform_data;
	struct sh7734_i2c_data *pd;
	struct i2c_adapter *adap;
	struct resource *res;
	int size;
	int ret = 0;

	pd = kzalloc(sizeof(struct sh7734_i2c_data), GFP_KERNEL);
	if (pd == NULL) {
		dev_err(&pdev->dev, "cannot allocate private data\n");
		return -ENOMEM;
	}

	/* I2C of SH7734 base clock is pll clock */
	pd->clk = clk_get(NULL, "peripheral_clk");
	if (IS_ERR(pd->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		ret = PTR_ERR(pd->clk);
		goto err;
	}

	pd->irq = platform_get_irq(pdev, 0);
	if (pd->irq < 0) {
		dev_err(&pdev->dev, "failed to get irq\n");
		goto err_clk;
	}

	ret = request_irq(pd->irq, sh7734_i2c_isr,
					IRQF_SHARED, pdev->name, pdev);
	if (ret) {
		dev_err(&pdev->dev, "cannot request IRQ\n");
		goto err_clk;
	}

	pd->dev = &pdev->dev;
	platform_set_drvdata(pdev, pd);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "cannot find IO resource\n");
		ret = -ENOENT;
		goto err_irq;
	}

	size = resource_size(res);

	pd->reg = ioremap(res->start, size);
	if (pd->reg == NULL) {
		dev_err(&pdev->dev, "cannot map IO\n");
		ret = -ENXIO;
		goto err_irq;
	}

	/* Use platformd data bus speed or NORMAL_SPEED */
	pd->bus_speed = NORMAL_SPEED;
	if (pdata && pdata->bus_speed)
		pd->bus_speed = pdata->bus_speed;

	/* setup the private data */
	adap = &pd->adap;
	i2c_set_adapdata(adap, pd);

	adap->owner = THIS_MODULE;
	adap->algo = &sh7734_i2c_algorithm;
	adap->dev.parent = &pdev->dev;
	adap->retries = 5;
	adap->nr = pdev->id;

	strlcpy(adap->name, pdev->name, sizeof(adap->name));

	spin_lock_init(&pd->lock);
	init_waitqueue_head(&pd->wait);

	ret = i2c_add_numbered_adapter(adap);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot add numbered adapter\n");
		goto err_all;
	}

	dev_info(&pdev->dev, "I2C adapter %d with bus speed %lu Hz\n",
		 adap->nr, pd->bus_speed);

	/* Reset controller */
	iic_set_clr(pd, ICCR2, ICCR2_I2CRST, 0);
	udelay(10);
	iic_set_clr(pd, ICCR2, 0, ICCR2_I2CRST);

	return 0;

err_all:
	iounmap(pd->reg);
err_irq:
	free_irq(pd->irq, pd);
err_clk:
	clk_put(pd->clk);
err:
	kfree(pd);

	return ret;
}

static int sh7734_i2c_remove(struct platform_device *pdev)
{
	struct sh7734_i2c_data *pd = platform_get_drvdata(pdev);

	i2c_del_adapter(&pd->adap);
	iounmap(pd->reg);
	free_irq(pd->irq, pdev);
	clk_put(pd->clk);
	kfree(pd);
	return 0;
}

static struct platform_driver sh7734_i2c_driver = {
	.driver		= {
		.name		= "i2c-sh7734",
		.owner		= THIS_MODULE,
	},
	.probe		= sh7734_i2c_probe,
	.remove		= sh7734_i2c_remove,
};

static int __init sh7734_i2c_adap_init(void)
{
	return platform_driver_register(&sh7734_i2c_driver);
}

static void __exit sh7734_i2c_adap_exit(void)
{
	platform_driver_unregister(&sh7734_i2c_driver);
}

subsys_initcall(sh7734_i2c_adap_init);
module_exit(sh7734_i2c_adap_exit);

MODULE_DESCRIPTION("SuperH SH7734 I2C Bus Controller driver");
MODULE_AUTHOR("Nobuhiro Iwamatsu <nobuhiro.iwamatsu.yj@renesas.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:i2c-sh7734");
