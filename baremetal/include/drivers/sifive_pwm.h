/* SPDX-License-Identifier: MIT */
#ifndef DRIVERS_SIFIVE_PWM_H
#define DRIVERS_SIFIVE_PWM_H

#include <baremetal/types.h>

#define SIFIVE_PWM_CHANNEL_COUNT          4U
#define SIFIVE_PWM_CHANNEL(channel)       (1U << (channel))
#define SIFIVE_PWM_NATURAL_PERIOD_TICKS  (1UL << 16)
#define SIFIVE_PWM_MAX_SCALE              15U

/* SiFive PWM outputs are inverted on FU740: a lower compare means more on-time. */
#define SIFIVE_PWM_COMPARE_FULL_ON        0x0000U
#define SIFIVE_PWM_COMPARE_OFF            0xffffU

struct sifive_pwm_config {
	bm_u32 scale;
	bm_u32 zero_compare;
	bm_u32 compare_mask;
	bm_u32 compare[SIFIVE_PWM_CHANNEL_COUNT];
};

/* Select the closest natural-wrap scale; target_hz == 0 selects the slowest. */
bm_u32 sifive_pwm_scale_for_frequency(bm_ulong input_hz,
					      bm_ulong target_hz);

/*
 * Convert target_hz to a rounded period for pwmzerocmp mode.  The result is
 * clamped to 2..65536 input ticks; target_hz == 0 selects the longest period.
 */
bm_u32 sifive_pwm_period_ticks_for_frequency(bm_ulong input_hz,
						     bm_ulong target_hz);

/* Convert an active-duty fraction to pwmcmp; denominator == 0 selects off. */
bm_u32 sifive_pwm_compare_for_fraction(bm_u32 period_ticks,
					       bm_u32 numerator,
					       bm_u32 denominator);

/* Stop, update the selected channels, reset the counter, and run continuously. */
void sifive_pwm_apply(bm_ulong base, const struct sifive_pwm_config *config);

#endif
