// SPDX-License-Identifier: MIT

#include <baremetal/io.h>
#include <drivers/sifive_pwm.h>

#define SIFIVE_PWM_ZERO_COMPARE      (1U << 9)
#define SIFIVE_PWM_ENABLE_ALWAYS     (1U << 12)
#define SIFIVE_PWM_COMPARE_MASK      0xffffU

struct sifive_pwm_regs {
	bm_u32 config;
	bm_u32 reserved_04;
	bm_u32 count;
	bm_u32 reserved_0c;
	bm_u32 scaled_count;
	bm_u32 reserved_14[3];
	bm_u32 compare[SIFIVE_PWM_CHANNEL_COUNT];
};

void sifive_pwm_apply(bm_ulong base, const struct sifive_pwm_config *config)
{
	volatile struct sifive_pwm_regs *regs =
		(volatile struct sifive_pwm_regs *)base;
	bm_u32 control = SIFIVE_PWM_ENABLE_ALWAYS |
		(config->scale & SIFIVE_PWM_MAX_SCALE);
	bm_u32 channel;

	if (config->zero_compare)
		control |= SIFIVE_PWM_ZERO_COMPARE;

	/* Stop the shared counter before changing its period and thresholds. */
	regs->config = 0;
	bm_io_fence();
	regs->count = 0;

	for (channel = 0; channel < SIFIVE_PWM_CHANNEL_COUNT; channel++) {
		if (config->compare_mask & SIFIVE_PWM_CHANNEL(channel))
			regs->compare[channel] =
				config->compare[channel] & SIFIVE_PWM_COMPARE_MASK;
	}

	regs->config = control;
	bm_io_fence();
}
