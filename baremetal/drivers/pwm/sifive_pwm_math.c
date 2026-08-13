// SPDX-License-Identifier: MIT

#include <drivers/sifive_pwm.h>

#define SIFIVE_PWM_COMPARE_MASK  0xffffU

static bm_ulong absolute_difference(bm_ulong left, bm_ulong right)
{
	return left > right ? left - right : right - left;
}

bm_u32 sifive_pwm_scale_for_frequency(bm_ulong input_hz,
					      bm_ulong target_hz)
{
	bm_ulong target_period;
	bm_ulong period_ticks = SIFIVE_PWM_NATURAL_PERIOD_TICKS;
	bm_ulong error;
	bm_u32 scale = 0;

	if (!target_hz)
		return SIFIVE_PWM_MAX_SCALE;

	target_period = input_hz / target_hz;
	error = absolute_difference(target_period, period_ticks);

	while (scale < SIFIVE_PWM_MAX_SCALE) {
		bm_ulong next_period = period_ticks << 1;
		bm_ulong next_error =
			absolute_difference(target_period, next_period);

		/* Compare frequency error without dividing, while avoiding overflow. */
		if (target_period < next_period && next_error >= error * 2)
			break;

		period_ticks = next_period;
		error = next_error;
		scale++;
	}

	return scale;
}

bm_u32 sifive_pwm_period_ticks_for_frequency(bm_ulong input_hz,
						     bm_ulong target_hz)
{
	bm_ulong ticks;
	bm_ulong remainder;

	if (!target_hz)
		return SIFIVE_PWM_NATURAL_PERIOD_TICKS;

	/* Round input_hz / target_hz without overflowing the addition. */
	ticks = input_hz / target_hz;
	remainder = input_hz % target_hz;
	if (remainder >= target_hz - target_hz / 2)
		ticks++;

	if (ticks < 2)
		return 2;
	if (ticks > SIFIVE_PWM_NATURAL_PERIOD_TICKS)
		return SIFIVE_PWM_NATURAL_PERIOD_TICKS;

	return (bm_u32)ticks;
}

bm_u32 sifive_pwm_compare_for_fraction(bm_u32 period_ticks,
					       bm_u32 numerator,
					       bm_u32 denominator)
{
	bm_ulong active_ticks;
	bm_ulong compare;

	if (!denominator)
		return SIFIVE_PWM_COMPARE_OFF;

	active_ticks =
		((bm_ulong)period_ticks * numerator + denominator / 2) /
		denominator;
	if (active_ticks > period_ticks)
		active_ticks = period_ticks;
	compare = period_ticks - active_ticks;

	return compare > SIFIVE_PWM_COMPARE_MASK ?
		SIFIVE_PWM_COMPARE_MASK : (bm_u32)compare;
}
