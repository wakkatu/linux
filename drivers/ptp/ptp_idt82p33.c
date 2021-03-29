// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2018 Integrated Device Technology, Inc
//

#define pr_fmt(fmt) "IDT_82p33xxx: " fmt

#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/timekeeping.h>
#include <linux/bitops.h>
#include <linux/of.h>
#include <linux/mfd/rsmu.h>
#include <linux/mfd/idt82p33_reg.h>

#include "ptp_private.h"
#include "ptp_idt82p33.h"

MODULE_DESCRIPTION("Driver for IDT 82p33xxx clock devices");
MODULE_AUTHOR("IDT support-1588 <IDT-support-1588@lm.renesas.com>");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(FW_FILENAME);

#define EXTTS_PERIOD (HZ / 10)

/* Module Parameters */
static u32 phase_snap_threshold = SNAP_THRESHOLD_NS;
module_param(phase_snap_threshold, uint, 0);
MODULE_PARM_DESC(phase_snap_threshold,
"threshold (150000ns by default) below which adjtime would ignore");

static char *firmware;
module_param(firmware, charp, 0);

static inline int idt82p33_read(struct idt82p33 *idt82p33, u16 regaddr,
				u8 *buf, u16 count)
{
	return rsmu_read(idt82p33->mfd, regaddr, buf, count);
}

static inline int idt82p33_write(struct idt82p33 *idt82p33, u16 regaddr,
				 u8 *buf, u16 count)
{
	return rsmu_write(idt82p33->mfd, regaddr, buf, count);
}

static void idt82p33_byte_array_to_timespec(struct timespec64 *ts,
					    u8 buf[TOD_BYTE_COUNT])
{
	time64_t sec;
	s32 nsec;
	u8 i;

	nsec = buf[3];
	for (i = 0; i < 3; i++) {
		nsec <<= 8;
		nsec |= buf[2 - i];
	}

	sec = buf[9];
	for (i = 0; i < 5; i++) {
		sec <<= 8;
		sec |= buf[8 - i];
	}

	ts->tv_sec = sec;
	ts->tv_nsec = nsec;
}

static void idt82p33_timespec_to_byte_array(struct timespec64 const *ts,
					    u8 buf[TOD_BYTE_COUNT])
{
	time64_t sec;
	s32 nsec;
	u8 i;

	nsec = ts->tv_nsec;
	sec = ts->tv_sec;

	for (i = 0; i < 4; i++) {
		buf[i] = nsec & 0xff;
		nsec >>= 8;
	}

	for (i = 4; i < TOD_BYTE_COUNT; i++) {
		buf[i] = sec & 0xff;
		sec >>= 8;
	}
}

static int idt82p33_dpll_set_mode(struct idt82p33_channel *channel,
				  enum pll_mode mode)
{
	struct idt82p33 *idt82p33 = channel->idt82p33;
	u8 dpll_mode;
	int err;

	if (channel->pll_mode == mode)
		return 0;

	err = idt82p33_read(idt82p33, channel->dpll_mode_cnfg,
			    &dpll_mode, sizeof(dpll_mode));
	if (err)
		return err;

	dpll_mode &= ~(PLL_MODE_MASK << PLL_MODE_SHIFT);

	dpll_mode |= (mode << PLL_MODE_SHIFT);

	err = idt82p33_write(idt82p33, channel->dpll_mode_cnfg,
			     &dpll_mode, sizeof(dpll_mode));
	if (err)
		return err;

	channel->pll_mode = dpll_mode;

	return 0;
}

static int idt82p33_set_tod_trigger(struct idt82p33_channel *channel,
				    u8 trigger, bool write)
{
	struct idt82p33 *idt82p33 = channel->idt82p33;
	int err;
	u8 cfg;

	if (trigger > WR_TRIG_SEL_MAX)
		return -EINVAL;

	err = idt82p33_read(idt82p33, channel->dpll_tod_trigger,
			    &cfg, sizeof(cfg));

	if (err)
		return err;

	if (write == true)
		trigger = (trigger << WRITE_TRIGGER_SHIFT) |
			  (cfg & READ_TRIGGER_MASK);
	else
		trigger = (trigger << READ_TRIGGER_SHIFT) |
			  (cfg & WRITE_TRIGGER_MASK);

	return idt82p33_write(idt82p33, channel->dpll_tod_trigger,
			      &trigger, sizeof(trigger));
}

static int idt82p33_get_extts(struct idt82p33_channel *channel,
			      struct timespec64 *ts)
{
	struct idt82p33 *idt82p33 = channel->idt82p33;
	u8 buf[TOD_BYTE_COUNT];
	int err;

	err = idt82p33_read(idt82p33, channel->dpll_tod_sts, buf, sizeof(buf));

	if (err)
		return err;

	/* Since trigger is not self clearing itself, we have to poll tod_sts */
	if (memcmp(buf, channel->extts_tod_sts, TOD_BYTE_COUNT) == 0)
		return -EAGAIN;

	memcpy(channel->extts_tod_sts, buf, TOD_BYTE_COUNT);

	idt82p33_byte_array_to_timespec(ts, buf);

	return 0;
}

static int idt82p33_enable_extts(struct idt82p33_channel *channel, u8 todn,
				 u8 ref, bool enable)
{
	struct idt82p33 *idt82p33 = channel->idt82p33;
	u8 old_mask = idt82p33->extts_mask;
	struct timespec64 ts;
	u8 mask = 1 << todn;
	int err = 0;
	u8 trigger;

	if (todn >= MAX_PHC_PLL)
		return -EINVAL;

	if (enable) {
		if (idt82p33->extts_mask & mask)
			return 0;

		switch (ref) {
		case 12:
			trigger = HW_TOD_TRIG_SEL_IN12;
			break;
		case 13:
			trigger = HW_TOD_TRIG_SEL_IN13;
			break;
		case 14:
			trigger = HW_TOD_TRIG_SEL_IN14;
			break;
		default:
			return -EINVAL;
		}

		err = idt82p33_set_tod_trigger(&idt82p33->channel[todn], trigger,
					       false);
		if (err == 0) {
			idt82p33_get_extts(channel, &ts);
			idt82p33->extts_mask |= mask;
			idt82p33->channel[todn].refn = ref;
			idt82p33->event_channel[todn] = channel;
		}
	} else
		idt82p33->extts_mask &= ~mask;

	if (old_mask == 0 && idt82p33->extts_mask)
		schedule_delayed_work(&idt82p33->extts_work, EXTTS_PERIOD);
	else if (idt82p33->extts_mask == 0)
		cancel_delayed_work_sync(&idt82p33->extts_work);

	return err;
}

static void idt82p33_enable_extts_mask(struct idt82p33_channel *channel,
				       u8 extts_mask, bool enable)
{
	struct idt82p33 *idt82p33 = channel->idt82p33;
	int i;

	for (i = 0; i < MAX_PHC_PLL; i++) {
		u8 mask = 1 << i;
		u8 refn = idt82p33->channel[i].refn;

		if (extts_mask & mask) {
			(void)idt82p33_enable_extts(channel, i, refn, enable);
		}
	}	
}

static int _idt82p33_gettime(struct idt82p33_channel *channel,
			     struct timespec64 *ts)
{
	struct idt82p33 *idt82p33 = channel->idt82p33;
	u8 buf[TOD_BYTE_COUNT];
	u8 extts_mask = 0;
	int err;

	/* Disable extts */
	if (idt82p33->extts_mask) {
		extts_mask = idt82p33->extts_mask;
		idt82p33_enable_extts_mask(channel, extts_mask, false);
	}

	err = idt82p33_set_tod_trigger(channel, HW_TOD_RD_TRIG_SEL_LSB_TOD_STS,
				       false);
	if (err)
		return err;

	if (idt82p33->calculate_overhead_flag)
		idt82p33->start_time = ktime_get_raw();

	err = idt82p33_read(idt82p33, channel->dpll_tod_sts, buf, sizeof(buf));

	if (err)
		return err;

	/* Re-enable extts */
	if (extts_mask)
		idt82p33_enable_extts_mask(channel, extts_mask, true);

	idt82p33_byte_array_to_timespec(ts, buf);

	return 0;
}

/*
 *   TOD Trigger:
 *   Bits[7:4] Write 0x9, MSB write
 *   Bits[3:0] Read 0x9, LSB read
 */

static int _idt82p33_settime(struct idt82p33_channel *channel,
			     struct timespec64 const *ts)
{
	struct idt82p33 *idt82p33 = channel->idt82p33;
	struct timespec64 local_ts = *ts;
	char buf[TOD_BYTE_COUNT];
	s64 dynamic_overhead_ns;
	int err;
	u8 i;

	err = idt82p33_set_tod_trigger(channel, HW_TOD_WR_TRIG_SEL_MSB_TOD_CNFG,
				       true);
	if (err)
		return err;

	if (idt82p33->calculate_overhead_flag) {
		dynamic_overhead_ns = ktime_to_ns(ktime_get_raw())
					- ktime_to_ns(idt82p33->start_time);

		timespec64_add_ns(&local_ts, dynamic_overhead_ns);

		idt82p33->calculate_overhead_flag = 0;
	}

	idt82p33_timespec_to_byte_array(&local_ts, buf);

	/*
	 * Store the new time value.
	 */
	for (i = 0; i < TOD_BYTE_COUNT; i++) {
		err = idt82p33_write(idt82p33, channel->dpll_tod_cnfg + i,
				     &buf[i], sizeof(buf[i]));
		if (err)
			return err;
	}

	return err;
}

static int idt82p33_adjtime_second(struct idt82p33_channel *channel,
				   s64 delta_sec)
{
	struct idt82p33 *idt82p33 = channel->idt82p33;
	char buf[TOD_BYTE_COUNT];
	struct timespec64 ts;
	const u8 delay_ns = 32;
	s64 ns;
	int err;

	err = _idt82p33_gettime(channel, &ts);

	if (err)
		return err;

	if (ts.tv_nsec > (NSEC_PER_SEC - NSEC_PER_MSEC)) {
		/*  Too close to miss next trigger, so skip it */
		udelay(USEC_PER_MSEC + 100);
		ns = (ts.tv_sec + 2 + delta_sec) * NSEC_PER_SEC + delay_ns;
	} else
		ns = (ts.tv_sec + 1 + delta_sec) * NSEC_PER_SEC + delay_ns;

	ts = ns_to_timespec64(ns);

	idt82p33_timespec_to_byte_array(&ts, buf);

	/*
	 * Store the new time value.
	 */
	err = idt82p33_write(idt82p33, channel->dpll_tod_cnfg, buf, sizeof(buf));
	if (err)
		return err;

	return idt82p33_set_tod_trigger(channel, HW_TOD_TRIG_SEL_TOD_PPS, true);	
}

static int _idt82p33_adjtime(struct idt82p33_channel *channel, s64 delta_ns)
{
	struct idt82p33 *idt82p33 = channel->idt82p33;
	struct timespec64 ts;
	s64 now_ns;
	int err;

	/* Use internal tod 1pps trigger for seconds adjustments*/
	if (delta_ns % NSEC_PER_SEC == 0)
		return idt82p33_adjtime_second(channel, delta_ns / NSEC_PER_SEC);

	idt82p33->calculate_overhead_flag = 1;

	err = _idt82p33_gettime(channel, &ts);

	if (err)
		return err;

	now_ns = timespec64_to_ns(&ts);
	now_ns += delta_ns + idt82p33->tod_write_overhead_ns;
	ts = ns_to_timespec64(now_ns);

	err = _idt82p33_settime(channel, &ts);

	return err;
}

static int _idt82p33_adjfreq(struct idt82p33_channel *channel, long ppb)
{
	struct idt82p33 *idt82p33 = channel->idt82p33;
	unsigned char buf[5] = {0};
	int err, i;
	s64 fcw;

	/*
	 * Frequency Control Word unit is: 1.68 * 10^-10 ppm
	 *
	 * adjfreq:
	 *       ppb * 10^9
	 * FCW = ----------
	 *          168
	 *
	 * adjfine:
	 *       scaled_ppm * 5^12
	 * FCW = -------------
	 *         168 * 2^4
	 */

	fcw = ppb * 1000000000ULL;
	fcw = div_s64(fcw, 168);

	for (i = 0; i < 5; i++) {
		buf[i] = fcw & 0xff;
		fcw >>= 8;
	}

	err = idt82p33_dpll_set_mode(channel, PLL_MODE_DCO);

	if (err)
		return err;

	err = idt82p33_write(idt82p33, channel->dpll_freq_cnfg,
			     buf, sizeof(buf));

	return err;
}

static int _idt82p33_adjfine(struct idt82p33_channel *channel, long scaled_ppm)
{
	struct idt82p33 *idt82p33 = channel->idt82p33;
	unsigned char buf[5] = {0};
	int err, i;
	s64 fcw;

	/*
	 * Frequency Control Word unit is: 1.68 * 10^-10 ppm
	 *
	 * adjfreq:
	 *       ppb * 10^9
	 * FCW = ----------
	 *          168
	 *
	 * adjfine:
	 *       scaled_ppm * 5^12
	 * FCW = -------------
	 *         168 * 2^4
	 */

	fcw = scaled_ppm * 244140625ULL;
	fcw = div_s64(fcw, 2688);

	for (i = 0; i < 5; i++) {
		buf[i] = fcw & 0xff;
		fcw >>= 8;
	}

	err = idt82p33_dpll_set_mode(channel, PLL_MODE_DCO);

	if (err)
		return err;

	err = idt82p33_write(idt82p33, channel->dpll_freq_cnfg,
			     buf, sizeof(buf));

	return err;
}

static int idt82p33_stop_ddco(struct idt82p33_channel *channel)
{
	int err;

	err = _idt82p33_adjfine(channel, channel->current_freq);
	if (err)
		return err;

	channel->ddco = false;

	return 0;
}

static int idt82p33_start_ddco(struct idt82p33_channel *channel, s32 delta_ns)
{
	s32 ppb = DDCO_MAX_PPB;
	u32 duration_ms;
	int err;

	/* more precise adjustment */
	if (abs(delta_ns) < DDCO_THRESHOLD_NS)
		ppb /= 2;

	duration_ms = abs(delta_ns) * MSEC_PER_SEC / ppb;

	if (duration_ms < 250) {
		duration_ms *= 2;
		ppb /= 2;
	}

	if (delta_ns < 0)
		ppb = -ppb;

	err = _idt82p33_adjfreq(channel, ppb);

	if (err)
		return err;

	/* schedule the worker to cancel ddco */
	ptp_schedule_worker(channel->ptp_clock, msecs_to_jiffies(duration_ms));
	channel->ddco = true;

	return 0;
}

static int idt82p33_measure_one_byte_write_overhead(
		struct idt82p33_channel *channel, s64 *overhead_ns)
{
	struct idt82p33 *idt82p33 = channel->idt82p33;
	ktime_t start, stop;
	u8 trigger = 0;
	s64 total_ns;
	int err;
	u8 i;

	total_ns = 0;
	*overhead_ns = 0;

	for (i = 0; i < MAX_MEASURMENT_COUNT; i++) {

		start = ktime_get_raw();

		err = idt82p33_write(idt82p33, channel->dpll_tod_trigger,
				     &trigger, sizeof(trigger));

		stop = ktime_get_raw();

		if (err)
			return err;

		total_ns += ktime_to_ns(stop) - ktime_to_ns(start);
	}

	*overhead_ns = div_s64(total_ns, MAX_MEASURMENT_COUNT);

	return err;
}

static int idt82p33_measure_one_byte_read_overhead(
		struct idt82p33_channel *channel, s64 *overhead_ns)
{
	struct idt82p33 *idt82p33 = channel->idt82p33;
	ktime_t start, stop;
	u8 trigger = 0;
	s64 total_ns;
	int err;
	u8 i;

	total_ns = 0;
	*overhead_ns = 0;

	for (i = 0; i < MAX_MEASURMENT_COUNT; i++) {

		start = ktime_get_raw();

		err = idt82p33_read(idt82p33, channel->dpll_tod_trigger,
				    &trigger, sizeof(trigger));

		stop = ktime_get_raw();

		if (err)
			return err;

		total_ns += ktime_to_ns(stop) - ktime_to_ns(start);
	}

	*overhead_ns = div_s64(total_ns, MAX_MEASURMENT_COUNT);

	return err;
}

static int idt82p33_measure_tod_write_9_byte_overhead(
			struct idt82p33_channel *channel)
{
	struct idt82p33 *idt82p33 = channel->idt82p33;
	u8 buf[TOD_BYTE_COUNT];
	ktime_t start, stop;
	s64 total_ns;
	int err = 0;
	u8 i, j;

	total_ns = 0;
	idt82p33->tod_write_overhead_ns = 0;

	for (i = 0; i < MAX_MEASURMENT_COUNT; i++) {

		start = ktime_get_raw();

		/* Need one less byte for applicable overhead */
		for (j = 0; j < (TOD_BYTE_COUNT - 1); j++) {
			err = idt82p33_write(idt82p33,
					     channel->dpll_tod_cnfg + i,
					     &buf[i], sizeof(buf[i]));
			if (err)
				return err;
		}

		stop = ktime_get_raw();

		total_ns += ktime_to_ns(stop) - ktime_to_ns(start);
	}

	idt82p33->tod_write_overhead_ns = div_s64(total_ns,
						  MAX_MEASURMENT_COUNT);

	return err;
}

static int idt82p33_measure_settime_gettime_gap_overhead(
		struct idt82p33_channel *channel, s64 *overhead_ns)
{
	struct timespec64 ts1 = {0, 0};
	struct timespec64 ts2;
	int err;

	*overhead_ns = 0;

	err = _idt82p33_settime(channel, &ts1);

	if (err)
		return err;

	err = _idt82p33_gettime(channel, &ts2);

	if (!err)
		*overhead_ns = timespec64_to_ns(&ts2) - timespec64_to_ns(&ts1);

	return err;
}

static int idt82p33_measure_tod_write_overhead(struct idt82p33_channel *channel)
{
	s64 trailing_overhead_ns, one_byte_write_ns, gap_ns, one_byte_read_ns;
	struct idt82p33 *idt82p33 = channel->idt82p33;
	int err;

	idt82p33->tod_write_overhead_ns = 0;

	err = idt82p33_measure_settime_gettime_gap_overhead(channel, &gap_ns);

	if (err) {
		dev_err(idt82p33->dev,
			"Failed in %s with err %d!\n", __func__, err);
		return err;
	}

	err = idt82p33_measure_one_byte_write_overhead(channel,
						       &one_byte_write_ns);

	if (err)
		return err;

	err = idt82p33_measure_one_byte_read_overhead(channel,
						      &one_byte_read_ns);

	if (err)
		return err;

	err = idt82p33_measure_tod_write_9_byte_overhead(channel);

	if (err)
		return err;

	trailing_overhead_ns = gap_ns - 2 * one_byte_write_ns
			       - one_byte_read_ns;

	idt82p33->tod_write_overhead_ns -= trailing_overhead_ns;

	return err;
}

static int idt82p33_check_and_set_masks(struct idt82p33 *idt82p33,
					u8 page,
					u8 offset,
					u8 val)
{
	int err = 0;

	if (page == PLLMASK_ADDR_HI && offset == PLLMASK_ADDR_LO) {
		if ((val & 0xfc) || !(val & 0x3)) {
			dev_err(idt82p33->dev,
				"Invalid PLL mask 0x%hhx\n", val);
			err = -EINVAL;
		} else {
			idt82p33->pll_mask = val;
		}
	} else if (page == PLL0_OUTMASK_ADDR_HI &&
		offset == PLL0_OUTMASK_ADDR_LO) {
		idt82p33->channel[0].output_mask = val;
	} else if (page == PLL1_OUTMASK_ADDR_HI &&
		offset == PLL1_OUTMASK_ADDR_LO) {
		idt82p33->channel[1].output_mask = val;
	}

	return err;
}

static void idt82p33_display_masks(struct idt82p33 *idt82p33)
{
	u8 mask, i;

	dev_info(idt82p33->dev,
		 "pllmask = 0x%02x\n", idt82p33->pll_mask);

	for (i = 0; i < MAX_PHC_PLL; i++) {
		mask = 1 << i;

		if (mask & idt82p33->pll_mask)
			dev_info(idt82p33->dev,
				 "PLL%d output_mask = 0x%04x\n",
				 i, idt82p33->channel[i].output_mask);
	}
}

static int idt82p33_sync_tod(struct idt82p33_channel *channel, bool enable)
{
	struct idt82p33 *idt82p33 = channel->idt82p33;
	u8 sync_cnfg;
	int err;

	err = idt82p33_read(idt82p33, channel->dpll_sync_cnfg,
			    &sync_cnfg, sizeof(sync_cnfg));
	if (err)
		return err;

	sync_cnfg &= ~SYNC_TOD;
	if (enable)
		sync_cnfg |= SYNC_TOD;

	return idt82p33_write(idt82p33, channel->dpll_sync_cnfg,
			      &sync_cnfg, sizeof(sync_cnfg));
}

static long idt82p33_work_handler(struct ptp_clock_info *ptp)
{
	struct idt82p33_channel *channel =
			container_of(ptp, struct idt82p33_channel, caps);
	struct idt82p33 *idt82p33 = channel->idt82p33;

	mutex_lock(idt82p33->lock);
	(void)idt82p33_stop_ddco(channel);
	mutex_unlock(idt82p33->lock);

	/* Return a negative value here to not reschedule */
	return -1;
}

static int idt82p33_output_enable(struct idt82p33_channel *channel,
				  bool enable, unsigned int outn)
{
	struct idt82p33 *idt82p33 = channel->idt82p33;
	int err;
	u8 val;

	err = idt82p33_read(idt82p33, OUT_MUX_CNFG(outn), &val, sizeof(val));
	if (err)
		return err;
	if (enable)
		val &= ~SQUELCH_ENABLE;
	else
		val |= SQUELCH_ENABLE;

	return idt82p33_write(idt82p33, OUT_MUX_CNFG(outn), &val, sizeof(val));
}

static int idt82p33_output_mask_enable(struct idt82p33_channel *channel,
				       bool enable)
{
	u16 mask;
	int err;
	u8 outn;

	mask = channel->output_mask;
	outn = 0;

	while (mask) {
		if (mask & 0x1) {
			err = idt82p33_output_enable(channel, enable, outn);
			if (err)
				return err;
		}

		mask >>= 0x1;
		outn++;
	}

	return 0;
}

static int idt82p33_perout_enable(struct idt82p33_channel *channel,
				  bool enable,
				  struct ptp_perout_request *perout)
{
	unsigned int flags = perout->flags;

	/* Enable/disable output based on output_mask */
	if (flags == PEROUT_ENABLE_OUTPUT_MASK)
		return idt82p33_output_mask_enable(channel, enable);

	/* Enable/disable individual output instead */
	return idt82p33_output_enable(channel, enable, perout->index);
}

static int idt82p33_enable_tod(struct idt82p33_channel *channel)
{
	struct idt82p33 *idt82p33 = channel->idt82p33;
	struct timespec64 ts = {0, 0};
	int err;
	u8 val;

	val = 0;
	err = idt82p33_write(idt82p33, channel->dpll_input_mode_cnfg,
			     &val, sizeof(val));
	if (err)
		return err;

	/* STEELAI-366 - Temporary workaround for ts2phc compatibility */
	if (0)
		err = idt82p33_output_mask_enable(channel, false);

	err = idt82p33_measure_tod_write_overhead(channel);

	if (err) {
		dev_err(idt82p33->dev,
			"Failed in %s with err %d!\n", __func__, err);
		return err;
	}

	err = _idt82p33_settime(channel, &ts);

	if (err)
		return err;

	return idt82p33_sync_tod(channel, true);
}

static void idt82p33_ptp_clock_unregister_all(struct idt82p33 *idt82p33)
{
	struct idt82p33_channel *channel;
	u8 i;

	for (i = 0; i < MAX_PHC_PLL; i++) {

		channel = &idt82p33->channel[i];

		if (channel->ptp_clock)
			ptp_clock_unregister(channel->ptp_clock);
	}
}

static int idt82p33_enable(struct ptp_clock_info *ptp,
			   struct ptp_clock_request *rq, int on)
{
	struct idt82p33_channel *channel =
			container_of(ptp, struct idt82p33_channel, caps);
	struct idt82p33 *idt82p33 = channel->idt82p33;
	int err = -EOPNOTSUPP;

	mutex_lock(idt82p33->lock);

	if (rq->type == PTP_CLK_REQ_PEROUT) {
		if (!on)
			err = idt82p33_perout_enable(channel, false,
						     &rq->perout);
		/* Only accept a 1-PPS aligned to the second. */
		else if (rq->perout.start.nsec || rq->perout.period.sec != 1 ||
			 rq->perout.period.nsec)
			err = -ERANGE;
		else
			err = idt82p33_perout_enable(channel, true,
						     &rq->perout);
	} else if (rq->type == PTP_CLK_REQ_EXTTS) {
		err = idt82p33_enable_extts(channel, rq->extts.index,
					    rq->extts.rsv[0], on);
	}

	mutex_unlock(idt82p33->lock);

	if (err)
		dev_err(idt82p33->dev,
			"Failed in %s with err %d!\n", __func__, err);
	return err;
}

static int idt82p33_adjwritephase(struct ptp_clock_info *ptp, s32 offset_ns)
{
	struct idt82p33_channel *channel =
		container_of(ptp, struct idt82p33_channel, caps);
	struct idt82p33 *idt82p33 = channel->idt82p33;
	s64 offset_regval, offset_fs;
	u8 val[4] = {0};
	int err;

	offset_fs = (s64)(-offset_ns) * 1000000;

	if (offset_fs > WRITE_PHASE_OFFSET_LIMIT)
		offset_fs = WRITE_PHASE_OFFSET_LIMIT;
	else if (offset_fs < -WRITE_PHASE_OFFSET_LIMIT)
		offset_fs = -WRITE_PHASE_OFFSET_LIMIT;

	/* Convert from phaseoffset_fs to register value */
	offset_regval = div_s64(offset_fs * 1000, IDT_T0DPLL_PHASE_RESOL);

	val[0] = offset_regval & 0xFF;
	val[1] = (offset_regval >> 8) & 0xFF;
	val[2] = (offset_regval >> 16) & 0xFF;
	val[3] = (offset_regval >> 24) & 0x1F;
	val[3] |= PH_OFFSET_EN;

	mutex_lock(idt82p33->lock);

	err = idt82p33_dpll_set_mode(channel, PLL_MODE_WPH);
	if (err) {
		dev_err(idt82p33->dev,
			"Failed in %s with err %d!\n", __func__, err);
		goto out;
	}

	err = idt82p33_write(idt82p33, channel->dpll_phase_cnfg, val,
			     sizeof(val));

out:
	mutex_unlock(idt82p33->lock);
	return err;
}

static int idt82p33_adjfine(struct ptp_clock_info *ptp, long scaled_ppm)
{
	struct idt82p33_channel *channel =
			container_of(ptp, struct idt82p33_channel, caps);
	struct idt82p33 *idt82p33 = channel->idt82p33;
	int err;

	if (channel->ddco == true)
		return 0;

	if (scaled_ppm == channel->current_freq)
		return 0;

	mutex_lock(idt82p33->lock);
	err = _idt82p33_adjfine(channel, scaled_ppm);

	if (err == 0)
		channel->current_freq = scaled_ppm;
	mutex_unlock(idt82p33->lock);

	if (err)
		dev_err(idt82p33->dev,
			"Failed in %s with err %d!\n", __func__, err);
	return err;
}

static int idt82p33_adjtime(struct ptp_clock_info *ptp, s64 delta_ns)
{
	struct idt82p33_channel *channel =
			container_of(ptp, struct idt82p33_channel, caps);
	struct idt82p33 *idt82p33 = channel->idt82p33;
	int err;

	if (channel->ddco == true)
		return 0;

	mutex_lock(idt82p33->lock);

	if (abs(delta_ns) < phase_snap_threshold) {
		err = idt82p33_start_ddco(channel, delta_ns);
		mutex_unlock(idt82p33->lock);
		return err;
	}

	err = _idt82p33_adjtime(channel, delta_ns);

	mutex_unlock(idt82p33->lock);

	if (err)
		dev_err(idt82p33->dev,
			"Failed in %s with err %d!\n", __func__, err);
	return err;
}

static int idt82p33_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct idt82p33_channel *channel =
			container_of(ptp, struct idt82p33_channel, caps);
	struct idt82p33 *idt82p33 = channel->idt82p33;
	int err;

	mutex_lock(idt82p33->lock);
	err = _idt82p33_gettime(channel, ts);
	mutex_unlock(idt82p33->lock);

	if (err)
		dev_err(idt82p33->dev,
			"Failed in %s with err %d!\n", __func__, err);
	return err;
}

static int idt82p33_settime(struct ptp_clock_info *ptp,
			const struct timespec64 *ts)
{
	struct idt82p33_channel *channel =
			container_of(ptp, struct idt82p33_channel, caps);
	struct idt82p33 *idt82p33 = channel->idt82p33;
	int err;

	mutex_lock(idt82p33->lock);
	err = _idt82p33_settime(channel, ts);
	mutex_unlock(idt82p33->lock);

	if (err)
		dev_err(idt82p33->dev,
			"Failed in %s with err %d!\n", __func__, err);
	return err;
}

static int idt82p33_channel_init(struct idt82p33 *idt82p33, u32 index)
{
	struct idt82p33_channel *channel = &idt82p33->channel[index];

	switch (index) {
	case 0:
		channel->dpll_tod_cnfg = DPLL1_TOD_CNFG;
		channel->dpll_tod_trigger = DPLL1_TOD_TRIGGER;
		channel->dpll_tod_sts = DPLL1_TOD_STS;
		channel->dpll_mode_cnfg = DPLL1_OPERATING_MODE_CNFG;
		channel->dpll_freq_cnfg = DPLL1_HOLDOVER_FREQ_CNFG;
		channel->dpll_phase_cnfg = DPLL1_PHASE_OFFSET_CNFG;
		channel->dpll_sync_cnfg = DPLL1_SYNC_EDGE_CNFG;
		channel->dpll_input_mode_cnfg = DPLL1_INPUT_MODE_CNFG;
		break;
	case 1:
		channel->dpll_tod_cnfg = DPLL2_TOD_CNFG;
		channel->dpll_tod_trigger = DPLL2_TOD_TRIGGER;
		channel->dpll_tod_sts = DPLL2_TOD_STS;
		channel->dpll_mode_cnfg = DPLL2_OPERATING_MODE_CNFG;
		channel->dpll_freq_cnfg = DPLL2_HOLDOVER_FREQ_CNFG;
		channel->dpll_phase_cnfg = DPLL2_PHASE_OFFSET_CNFG;
		channel->dpll_sync_cnfg = DPLL2_SYNC_EDGE_CNFG;
		channel->dpll_input_mode_cnfg = DPLL2_INPUT_MODE_CNFG;
		break;
	default:
		return -EINVAL;
	}

	channel->current_freq = 0;
	channel->idt82p33 = idt82p33;

	return 0;
}

static void idt82p33_caps_init(struct ptp_clock_info *caps)
{
	caps->owner = THIS_MODULE;
	caps->max_adj = DCO_MAX_PPB;
	caps->n_per_out = 11;
	caps->n_ext_ts = MAX_PHC_PLL,
	caps->adjphase = idt82p33_adjwritephase,
	caps->adjfine = idt82p33_adjfine;
	caps->adjtime = idt82p33_adjtime;
	caps->gettime64 = idt82p33_gettime;
	caps->settime64 = idt82p33_settime;
	caps->enable = idt82p33_enable;
	caps->do_aux_work = idt82p33_work_handler;
}

static int idt82p33_enable_channel(struct idt82p33 *idt82p33, u32 index)
{
	struct idt82p33_channel *channel;
	int err;

	if (!(index < MAX_PHC_PLL))
		return -EINVAL;

	channel = &idt82p33->channel[index];

	err = idt82p33_channel_init(idt82p33, index);
	if (err) {
		dev_err(idt82p33->dev,
			"Channel_init failed in %s with err %d!\n",
			__func__, err);
		return err;
	}

	idt82p33_caps_init(&channel->caps);
	snprintf(channel->caps.name, sizeof(channel->caps.name),
		 "IDT 82P33 PLL%u", index);

	channel->ptp_clock = ptp_clock_register(&channel->caps, NULL);

	if (IS_ERR(channel->ptp_clock)) {
		err = PTR_ERR(channel->ptp_clock);
		channel->ptp_clock = NULL;
		return err;
	}

	if (!channel->ptp_clock)
		return -ENOTSUPP;

	err = idt82p33_dpll_set_mode(channel, PLL_MODE_DCO);
	if (err) {
		dev_err(idt82p33->dev,
			"Dpll_set_mode failed in %s with err %d!\n",
			__func__, err);
		return err;
	}

	err = idt82p33_enable_tod(channel);
	if (err) {
		dev_err(idt82p33->dev,
			"Enable_tod failed in %s with err %d!\n",
			__func__, err);
		return err;
	}

	dev_info(idt82p33->dev, "PLL%d registered as ptp%d\n",
		 index, channel->ptp_clock->index);

	return 0;
}

static int idt82p33_reset(struct idt82p33 *idt82p33, bool cold)
{
	int err;
	u8 cfg = SOFT_RESET_EN;

	if (cold == true)
		goto cold_reset;

	err = idt82p33_read(idt82p33, REG_SOFT_RESET, &cfg, sizeof(cfg));
	if (err) {
		dev_err(idt82p33->dev,
			"Soft reset failed with err %d!\n", err);
		return err;
	}

	cfg |= SOFT_RESET_EN;

cold_reset:
	err = idt82p33_write(idt82p33, REG_SOFT_RESET, &cfg, sizeof(cfg));
	if (err)
		dev_err(idt82p33->dev,
			"Cold reset failed with err %d!\n", err);
	return err;
}

static int idt82p33_load_firmware(struct idt82p33 *idt82p33)
{
	char fname[128] = FW_FILENAME;
	const struct firmware *fw;
	struct idt82p33_fwrc *rec;
	u8 loaddr, page, val;
	int err;
	s32 len;

	if (firmware) /* module parameter */
		snprintf(fname, sizeof(fname), "%s", firmware);

	dev_dbg(idt82p33->dev, "requesting firmware '%s'\n", fname);

	err = request_firmware(&fw, fname, idt82p33->dev);

	if (err) {
		dev_err(idt82p33->dev,
			"Failed in %s with err %d!\n", __func__, err);
		return err;
	}

	dev_dbg(idt82p33->dev, "firmware size %zu bytes\n", fw->size);

	rec = (struct idt82p33_fwrc *) fw->data;

	for (len = fw->size; len > 0; len -= sizeof(*rec)) {

		if (rec->reserved) {
			dev_err(idt82p33->dev,
				"bad firmware, reserved field non-zero\n");
			err = -EINVAL;
		} else {
			val = rec->value;
			loaddr = rec->loaddr;
			page = rec->hiaddr;

			rec++;

			err = idt82p33_check_and_set_masks(idt82p33, page,
							   loaddr, val);
		}

		if (err == 0) {
			/* Page size 128, last 4 bytes of page skipped */
			if (loaddr > 0x7b)
				continue;

			err = idt82p33_write(idt82p33, REG_ADDR(page, loaddr),
					     &val, sizeof(val));
		}

		if (err)
			goto out;
	}

	idt82p33_display_masks(idt82p33);
out:
	release_firmware(fw);
	return err;
}

static void idt82p33_extts_check(struct work_struct *work)
{
	struct idt82p33 *idt82p33 = container_of(work, struct idt82p33,
						 extts_work.work);
	struct idt82p33_channel *event_channel;
	struct ptp_clock_event event;
	struct timespec64 ts;
	int err, i;

	if (idt82p33->extts_mask == 0)
		return;

	mutex_lock(idt82p33->lock);
	for (i = 0; i < MAX_PHC_PLL; i++) {
		u8 mask = 1 << i;

		if (idt82p33->extts_mask & mask) {
			err = idt82p33_get_extts(&idt82p33->channel[i], &ts);
			if (err == 0) {
				event_channel = idt82p33->event_channel[i];
				event.type = PTP_CLOCK_EXTTS;
				event.index = i;
				event.timestamp = timespec64_to_ns(&ts);
				ptp_clock_event(event_channel->ptp_clock,
						&event);
			}
		}
	}
	mutex_unlock(idt82p33->lock);

	if (idt82p33->extts_mask)
		schedule_delayed_work(&idt82p33->extts_work, EXTTS_PERIOD);
}

static int idt82p33_probe(struct platform_device *pdev)
{
	struct rsmu_pdata *pdata = dev_get_platdata(&pdev->dev);
	struct idt82p33 *idt82p33;
	int err;
	u8 i;

	idt82p33 = devm_kzalloc(&pdev->dev,
				sizeof(struct idt82p33), GFP_KERNEL);
	if (!idt82p33)
		return -ENOMEM;

	idt82p33->dev = &pdev->dev;
	idt82p33->mfd = pdev->dev.parent;
	idt82p33->lock = pdata->lock;
	idt82p33->tod_write_overhead_ns = 0;
	idt82p33->calculate_overhead_flag = 0;
	idt82p33->pll_mask = DEFAULT_PLL_MASK;
	idt82p33->channel[0].output_mask = DEFAULT_OUTPUT_MASK_PLL0;
	idt82p33->channel[1].output_mask = DEFAULT_OUTPUT_MASK_PLL1;
	idt82p33->extts_mask = 0;
	INIT_DELAYED_WORK(&idt82p33->extts_work, idt82p33_extts_check);

	mutex_lock(idt82p33->lock);

	/* cold reset before loading firmware */
	idt82p33_reset(idt82p33, true);

	err = idt82p33_load_firmware(idt82p33);
	if (err)
		dev_warn(idt82p33->dev,
			 "loading firmware failed with %d\n", err);

	/* soft reset after loading firmware */
	idt82p33_reset(idt82p33, false);

	if (idt82p33->pll_mask) {
		for (i = 0; i < MAX_PHC_PLL; i++) {
			if (idt82p33->pll_mask & (1 << i))
				err = idt82p33_enable_channel(idt82p33, i);
			else
				err = idt82p33_channel_init(idt82p33, i);
			if (err) {
				dev_err(idt82p33->dev,
					"Failed in %s with err %d!\n",
					__func__, err);
				break;
			}
		}
	} else {
		dev_err(idt82p33->dev,
			"no PLLs flagged as PHCs, nothing to do\n");
		err = -ENODEV;
	}

	mutex_unlock(idt82p33->lock);

	if (err) {
		idt82p33_ptp_clock_unregister_all(idt82p33);
		return err;
	}

	platform_set_drvdata(pdev, idt82p33);

	return 0;
}

static int idt82p33_remove(struct platform_device *pdev)
{
	struct idt82p33 *idt82p33 = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&idt82p33->extts_work);

	idt82p33_ptp_clock_unregister_all(idt82p33);

	return 0;
}

static const struct platform_device_id idt82p33_id_table[] = {
	{ "idt82p33-ptp0", },
	{ "idt82p33-ptp1", },
	{ "idt82p33-ptp2", },
	{ "idt82p33-ptp3", },
	{},
};
MODULE_DEVICE_TABLE(platform, idt82p33_id_table);

#ifdef CONFIG_OF
static const struct of_device_id idt82p33_of_match[] = {
	{ .compatible = "renesas,idt82p33-ptp0", },
	{ .compatible = "renesas,idt82p33-ptp1", },
	{ .compatible = "renesas,idt82p33-ptp2", },
	{ .compatible = "renesas,idt82p33-ptp3", },
	{},
};
MODULE_DEVICE_TABLE(of, idt82p33_of_match);
#endif

static struct platform_driver idt82p33_driver = {
	.driver = {
		.of_match_table	= of_match_ptr(idt82p33_of_match),
		.name = "idt82p33-phc",
	},
	.probe = idt82p33_probe,
	.remove	= idt82p33_remove,
	.id_table = idt82p33_id_table,
};

module_platform_driver(idt82p33_driver);
