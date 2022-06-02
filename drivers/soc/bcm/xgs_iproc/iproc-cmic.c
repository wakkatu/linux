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
#include <asm/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/soc/bcm/iproc-cmic.h>


extern const struct sbus_ops cmicx_sbus_ops;
extern const struct sbus_ops cmicd_sbus_ops;

static struct iproc_cmic *cmic;

int iproc_cmic_schan_reg32_write(u32 blk_type, u32 addr, u32 val)
{
	if (cmic && cmic->sbus_ops) {
		if (cmic->sbus_ops->reg32_write) {
			return cmic->sbus_ops->reg32_write(cmic, blk_type, addr, val);
		}
	}
	return -EINVAL;
}

u32 iproc_cmic_schan_reg32_read(u32 blk_type, u32 addr)
{
	if (cmic && cmic->sbus_ops) {
		if (cmic->sbus_ops->reg32_read) {
			return cmic->sbus_ops->reg32_read(cmic, blk_type, addr);
		}
	}
	return 0;
}

int iproc_cmic_schan_reg64_write(u32 blk_type, u32 addr, u64 val)
{
	if (cmic && cmic->sbus_ops) {
		if (cmic->sbus_ops->reg64_write) {
			return cmic->sbus_ops->reg64_write(cmic, blk_type, addr, val);
		}
	}
	return -EINVAL;
}

u64 iproc_cmic_schan_reg64_read(u32 blk_type, u32 addr)
{
	if (cmic && cmic->sbus_ops) {
		if (cmic->sbus_ops->reg64_read) {
			return cmic->sbus_ops->reg64_read(cmic, blk_type, addr);
		}
	}
	return 0;
}

int iproc_cmic_schan_ucmem_write(u32 blk_type, u32 *mem)
{
	if (cmic && cmic->sbus_ops) {
		if (cmic->sbus_ops->ucmem_write) {
			return cmic->sbus_ops->ucmem_write(cmic, blk_type, mem);
		}
	}
	return -EINVAL;
}

int iproc_cmic_schan_ucmem_read(u32 blk_type, u32 *mem)
{
	if (cmic && cmic->sbus_ops) {
		if (cmic->sbus_ops->ucmem_read) {
			return cmic->sbus_ops->ucmem_read(cmic, blk_type, mem);
		}
	}
	return -EINVAL;
}

void inline __iomem *iproc_cmic_base_get(void)
{
	if (cmic && cmic->base) {
		return cmic->base;
	}
	return NULL;
}

/****************************************************************************
 ***************************************************************************/
static int cmic_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;

	cmic = devm_kzalloc(&pdev->dev, sizeof(*cmic), GFP_KERNEL);
	if (!cmic) {
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, cmic);
	cmic->dev = &pdev->dev;

	cmic->base = (void *)of_iomap(np, 0);
	if (IS_ERR(cmic->base)) {
		dev_err(&pdev->dev, "Unable to iomap CMIC resource\n");
		return PTR_ERR(cmic->base);
	}

	if (of_device_is_compatible(np, "brcm,iproc-cmicx")) {
		cmic->sbus_ops = &cmicx_sbus_ops;
	} else if (of_device_is_compatible(np, "brcm,iproc-cmicd")) {
		cmic->sbus_ops = &cmicd_sbus_ops;
	}

	if (cmic->sbus_ops) {
		if (cmic->sbus_ops->init) {
			/* Initial cmic */
			cmic->sbus_ops->init(cmic);
		}
	}

	return 0;
}

static int cmic_remove(struct platform_device *pdev)
{
	if (cmic->base) {
		iounmap(cmic->base);
	}

	devm_kfree(&pdev->dev, cmic);
	cmic = NULL;

	return 0;
}

static const struct of_device_id iproc_cmic_of_match[] = {
	{.compatible = "brcm,iproc-cmicx",},
	{.compatible = "brcm,iproc-cmicd",},
	{},
};
MODULE_DEVICE_TABLE(of, iproc_cmic_of_match);

static struct platform_driver iproc_cmic_driver = {
	.driver = {
		.name  = "iproc_cmic",
		.of_match_table = iproc_cmic_of_match,
	},
	.probe = cmic_probe,
	.remove = cmic_remove,
};

module_platform_driver(iproc_cmic_driver);
MODULE_LICENSE("GPL");

