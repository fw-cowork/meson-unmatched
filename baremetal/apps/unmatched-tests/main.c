// SPDX-License-Identifier: MIT

#include <baremetal/app.h>
#include <baremetal/io.h>
#include <baremetal/string.h>
#include <baremetal/types.h>
#include <drivers/sifive_pwm.h>
#include <soc/fu740.h>

#define TEST_PCLK_MIN_HZ               1000000UL
#define TEST_PCLK_MAX_HZ               1000000000UL

#define SIFIVE_PWM_CONFIG_OFFSET       0x00UL
#define SIFIVE_PWM_COUNT_OFFSET        0x08UL
#define SIFIVE_PWM_COMPARE_OFFSET      0x20UL
#define SIFIVE_PWM_COMPARE_STRIDE      0x04UL
#define SIFIVE_PWM_ZERO_COMPARE        (1U << 9)
#define SIFIVE_PWM_ENABLE_ALWAYS       (1U << 12)
#define SIFIVE_PWM_TEST_CONTROL_MASK   \
	(SIFIVE_PWM_MAX_SCALE | SIFIVE_PWM_ZERO_COMPARE | \
	 SIFIVE_PWM_ENABLE_ALWAYS)

enum unmatched_test_result {
	UNMATCHED_TEST_PASS = 0,
	UNMATCHED_TEST_INVALID_ARGUMENTS = 2,
	UNMATCHED_TEST_UNKNOWN = 3,
	UNMATCHED_TEST_CLOCK_PCLK_FAILED = 0x10,
	UNMATCHED_TEST_PWM_REGISTERS_FAILED = 0x11,
};

struct unmatched_test_case {
	const char *name;
	bm_ulong (*run)(void);
};

struct pwm_state {
	bm_u32 config;
	bm_u32 count;
	bm_u32 compare[SIFIVE_PWM_CHANNEL_COUNT];
};

static bm_ulong test_clock_pclk(void)
{
	bm_ulong pclk = fu740_pclk_rate();

	/* Reject a failed or clearly implausible live PRCI calculation. */
	if (pclk < TEST_PCLK_MIN_HZ || pclk > TEST_PCLK_MAX_HZ)
		return UNMATCHED_TEST_CLOCK_PCLK_FAILED;

	return UNMATCHED_TEST_PASS;
}

static bm_ulong pwm_compare_address(bm_u32 channel)
{
	return FU740_PWM0_BASE + SIFIVE_PWM_COMPARE_OFFSET +
		channel * SIFIVE_PWM_COMPARE_STRIDE;
}

static void pwm_state_save(struct pwm_state *state)
{
	bm_u32 channel;

	bm_io_fence();
	state->config = bm_read32(FU740_PWM0_BASE + SIFIVE_PWM_CONFIG_OFFSET);
	state->count = bm_read32(FU740_PWM0_BASE + SIFIVE_PWM_COUNT_OFFSET);
	for (channel = 0; channel < SIFIVE_PWM_CHANNEL_COUNT; channel++)
		state->compare[channel] = bm_read32(pwm_compare_address(channel));
}

static void pwm_state_restore(const struct pwm_state *state)
{
	bm_u32 channel;

	/* Stop the shared counter while restoring all PWM0 channels. */
	bm_write32(FU740_PWM0_BASE + SIFIVE_PWM_CONFIG_OFFSET, 0);
	bm_io_fence();
	for (channel = 0; channel < SIFIVE_PWM_CHANNEL_COUNT; channel++)
		bm_write32(pwm_compare_address(channel), state->compare[channel]);
	bm_write32(FU740_PWM0_BASE + SIFIVE_PWM_COUNT_OFFSET, state->count);
	bm_write32(FU740_PWM0_BASE + SIFIVE_PWM_CONFIG_OFFSET, state->config);
	bm_io_fence();
}

static bm_ulong test_pwm_registers(void)
{
	struct sifive_pwm_config config = {
		.scale = 0,
		.zero_compare = 0,
		.compare_mask = SIFIVE_PWM_CHANNEL(0) |
			SIFIVE_PWM_CHANNEL(1) |
			SIFIVE_PWM_CHANNEL(2) |
			SIFIVE_PWM_CHANNEL(3),
		.compare = { 0x1357U, 0x2468U, 0x5aa5U, 0xa55aU },
	};
	struct pwm_state saved;
	bm_ulong result = UNMATCHED_TEST_PASS;
	bm_u32 channel;
	bm_u32 control;

	pwm_state_save(&saved);
	sifive_pwm_apply(FU740_PWM0_BASE, &config);

	control = bm_read32(FU740_PWM0_BASE + SIFIVE_PWM_CONFIG_OFFSET);
	if ((control & SIFIVE_PWM_TEST_CONTROL_MASK) !=
	    SIFIVE_PWM_ENABLE_ALWAYS)
		result = UNMATCHED_TEST_PWM_REGISTERS_FAILED;

	for (channel = 0; channel < SIFIVE_PWM_CHANNEL_COUNT; channel++) {
		if ((bm_read32(pwm_compare_address(channel)) &
		     SIFIVE_PWM_COMPARE_OFF) != config.compare[channel])
			result = UNMATCHED_TEST_PWM_REGISTERS_FAILED;
	}

	/* Always restore PWM0, including after a failed readback. */
	pwm_state_restore(&saved);
	return result;
}

static const struct unmatched_test_case unmatched_tests[] = {
	{ "clock-pclk", test_clock_pclk },
	{ "pwm-registers", test_pwm_registers },
};

static bm_ulong run_named_test(const char *name)
{
	bm_u32 i;

	for (i = 0; i < BM_ARRAY_SIZE(unmatched_tests); i++) {
		if (bm_streq(name, unmatched_tests[i].name))
			return unmatched_tests[i].run();
	}

	return UNMATCHED_TEST_UNKNOWN;
}

static bm_ulong run_all_tests(void)
{
	bm_u32 i;

	for (i = 0; i < BM_ARRAY_SIZE(unmatched_tests); i++) {
		bm_ulong result = unmatched_tests[i].run();

		if (result != UNMATCHED_TEST_PASS)
			return result;
	}

	return UNMATCHED_TEST_PASS;
}

bm_ulong baremetal_main(int argc, char *const argv[])
{
	if (!argv || argc != 2 || !argv[1])
		return UNMATCHED_TEST_INVALID_ARGUMENTS;

	if (bm_streq(argv[1], "all"))
		return run_all_tests();

	return run_named_test(argv[1]);
}
