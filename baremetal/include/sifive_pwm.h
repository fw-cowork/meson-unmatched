/* SPDX-License-Identifier: MIT */
#ifndef SIFIVE_PWM_H
#define SIFIVE_PWM_H

#include <baremetal.h>

#define SIFIVE_PWM_CHANNEL_COUNT          4U
#define SIFIVE_PWM_CHANNEL(channel)       (1U << (channel))
#define SIFIVE_PWM_NATURAL_PERIOD_TICKS  (1UL << 16)

/* SiFive PWM outputs are inverted on FU740: a lower compare means more on-time. */
#define SIFIVE_PWM_COMPARE_FULL_ON        0x0000U
#define SIFIVE_PWM_COMPARE_OFF            0xffffU

struct sifive_pwm_config {
	bm_u32 scale;
	bm_u32 zero_compare;
	bm_u32 compare_mask;
	bm_u32 compare[SIFIVE_PWM_CHANNEL_COUNT];
};

/* Select the natural-wrap pwmscale closest to nonzero target_hz. */
bm_u32 sifive_pwm_scale_for_frequency(bm_ulong input_hz,
				      bm_ulong target_hz);

/*
 * Convert nonzero target_hz to a rounded period for pwmzerocmp mode.  The
 * result is clamped to the hardware range of 2..65536 input ticks.
 */
bm_u32 sifive_pwm_period_ticks_for_frequency(bm_ulong input_hz,
					     bm_ulong target_hz);

/* Convert an active-duty fraction with a nonzero denominator to pwmcmp. */
bm_u32 sifive_pwm_compare_for_fraction(bm_u32 period_ticks,
				       bm_u32 numerator,
				       bm_u32 denominator);

/* Stop, update the selected channels, reset the counter, and run continuously. */
void sifive_pwm_apply(bm_ulong base, const struct sifive_pwm_config *config);

#endif
