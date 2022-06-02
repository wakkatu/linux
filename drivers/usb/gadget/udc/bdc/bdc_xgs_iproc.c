/*
 * bdc_xgs_iproc.c - BDC platform driver for Broadco XGS IPROC platforms.
 *
 * Copyright (C) 2014-2016, Broadcom Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

#include "bdc.h"

struct bdc_xgs_iproc {
	struct device *dev;
	struct platform_device *bdc;
};

static int bdc_xgs_iproc_probe(struct platform_device *pdev)
{
	struct resource res[2];
	struct platform_device *bdc;
	struct bdc_xgs_iproc *data;
    struct device_node *np = pdev->dev.of_node;
	int irq;
	int ret = -ENOMEM;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		return -ENOMEM;
	}
	data->dev = &pdev->dev;

	bdc = platform_device_alloc(BRCM_BDC_NAME, PLATFORM_DEVID_AUTO);
	if (!bdc) {
		return -ENOMEM;
	}

	memset(res, 0x00, sizeof(struct resource) * ARRAY_SIZE(res));

	if (of_address_to_resource(np, 0, &res[0]) < 0) {
		dev_err(&pdev->dev,
			"failed to get BDC registers\n");
		of_node_put(np);
		return -ENXIO;
	}
	res[0].name	= BRCM_BDC_NAME;
	res[0].flags = IORESOURCE_MEM;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev,
			"failed to get BDC IRQ\n");
		return -ENXIO;
	}
	res[1].start = irq;
	res[1].name	= BRCM_BDC_NAME;
	res[1].flags = IORESOURCE_IRQ;

	ret = platform_device_add_resources(bdc, res, ARRAY_SIZE(res));
	if (ret) {
		dev_err(&pdev->dev,
			"couldn't add resources to bdc device\n");
		return ret;
	}

	platform_set_drvdata(pdev, data);

	dma_set_coherent_mask(&bdc->dev, pdev->dev.coherent_dma_mask);
	bdc->dev.dma_mask = pdev->dev.dma_mask;
	bdc->dev.dma_parms = pdev->dev.dma_parms;
	bdc->dev.parent = &pdev->dev;
	data->bdc = bdc;

	ret = platform_device_add(bdc);
	if (ret) {
		dev_err(&pdev->dev, "failed to register bdc device\n");
		platform_device_put(bdc);
		return ret;
	}

	return 0;
}

static int bdc_xgs_iproc_remove(struct platform_device *pdev)
{
	struct bdc_xgs_iproc *data = platform_get_drvdata(pdev);

	platform_device_unregister(data->bdc);
	devm_kfree(&pdev->dev, data);

	return 0;
}

static const struct of_device_id bdc_xgs_iproc_ids[] = {
    { .compatible = "brcm,bdc,hx5", },
    { }
};
MODULE_DEVICE_TABLE(of, bdc_xgs_iproc_ids);

static struct platform_driver bdc_xgs_iproc_driver = {
	.probe = bdc_xgs_iproc_probe,
	.remove = bdc_xgs_iproc_remove,
    .driver = {
        .name   = "bdc-xgs-iproc",
        .owner  = THIS_MODULE,
        .of_match_table = of_match_ptr(bdc_xgs_iproc_ids),
    },
};

module_platform_driver(bdc_xgs_iproc_driver);

MODULE_AUTHOR("Broadcom");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("BDC platform driver for Broadcom XGS IPROC platforms");
