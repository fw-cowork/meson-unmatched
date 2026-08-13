// SPDX-License-Identifier: MIT

#include <sifive_pwm.h>

#define SIFIVE_PWM_SCALE_MASK    0xfU
#define SIFIVE_PWM_COMPARE_MASK  0xffffU

static bm_ulong absolute_difference(bm_ulong left, bm_ulong right)
{
	return left > right ? left - right : right - left;
}

bm_u32 sifive_pwm_scale_for_frequency(bm_ulong input_hz,
				      bm_ulong target_hz)
{
	bm_ulong target_period = input_hz / target_hz;
	bm_ulong period_ticks = SIFIVE_PWM_NATURAL_PERIOD_TICKS;
	bm_ulong error = absolute_difference(target_period, period_ticks);
	bm_u32 scale = 0;

	while (scale < SIFIVE_PWM_SCALE_MASK) {
		bm_ulong next_period = period_ticks << 1;
		bm_ulong next_error =
			absolute_difference(target_period, next_period);

		/* Compare frequency error; the next candidate has a 2x period. */
		if (next_error >= error * 2)
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
	bm_ulong ticks = (input_hz + target_hz / 2) / target_hz;

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
	bm_ulong active_ticks =
		((bm_ulong)period_ticks * numerator + denominator / 2) /
		denominator;
	bm_ulong compare;

	if (active_ticks > period_ticks)
		active_ticks = period_ticks;
	compare = period_ticks - active_ticks;

	return compare > SIFIVE_PWM_COMPARE_MASK ?
		SIFIVE_PWM_COMPARE_MASK : (bm_u32)compare;
}
