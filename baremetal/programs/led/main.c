// SPDX-License-Identifier: MIT

#include <baremetal.h>
#include <fu740.h>
#include <sifive_pwm.h>

#define PWM_TARGET_BLINK_HZ         1UL
#define PWM_TARGET_LOW_HZ           100UL
#define PWM_TARGET_HIGH_HZ          10000000UL

enum led_component {
	LED_RED = 1U << 0,
	LED_GREEN = 1U << 1,
	LED_BLUE = 1U << 2,
};

enum led_result {
	LED_SUCCESS = 0,
	LED_INVALID_ARGUMENTS = 2,
	LED_UNKNOWN_COLOR = 3,
	LED_UNKNOWN_MODE = 4,
};

enum led_mode {
	LED_MODE_SOLID,
	LED_MODE_BLINK,
	LED_MODE_PWM_LOW,
	LED_MODE_PWM_HIGH,
};

enum d2_pwm_channel {
	/* D2 is wired to PWM0 channels 2, 1, and 3 rather than RGB order. */
	D2_PWM_GREEN = 1,
	D2_PWM_RED = 2,
	D2_PWM_BLUE = 3,
};

struct led_color {
	const char *name;
	bm_u32 components;
};

static const struct led_color colors[] = {
	{ "off", 0 },
	{ "red", LED_RED },
	{ "green", LED_GREEN },
	{ "blue", LED_BLUE },
	{ "yellow", LED_RED | LED_GREEN },
	{ "cyan", LED_GREEN | LED_BLUE },
	{ "magenta", LED_RED | LED_BLUE },
	{ "white", LED_RED | LED_GREEN | LED_BLUE },
};

static const struct led_color *find_color(const char *name)
{
	unsigned int i;

	for (i = 0; i < BM_ARRAY_SIZE(colors); i++) {
		if (bm_streq(name, colors[i].name))
			return &colors[i];
	}

	return 0;
}

static bm_u32 compare_value(bm_u32 components, bm_u32 component,
			    bm_u32 active_compare)
{
	if (!(components & component))
		return SIFIVE_PWM_COMPARE_OFF;

	return active_compare;
}

static void set_color(bm_u32 components, enum led_mode mode)
{
	struct sifive_pwm_config config = { 0 };
	bm_u32 active_compare = SIFIVE_PWM_COMPARE_FULL_ON;
	bm_u32 period_ticks = SIFIVE_PWM_NATURAL_PERIOD_TICKS;

	if (mode == LED_MODE_BLINK) {
		bm_ulong pclk = fu740_pclk_rate();

		config.scale =
			sifive_pwm_scale_for_frequency(pclk,
						       PWM_TARGET_BLINK_HZ);
		active_compare =
			sifive_pwm_compare_for_fraction(period_ticks, 1, 2);
	} else if (mode == LED_MODE_PWM_LOW) {
		bm_ulong pclk = fu740_pclk_rate();

		config.scale =
			sifive_pwm_scale_for_frequency(pclk, PWM_TARGET_LOW_HZ);
		active_compare =
			sifive_pwm_compare_for_fraction(period_ticks, 1, 2);
	} else if (mode == LED_MODE_PWM_HIGH) {
		bm_ulong pclk = fu740_pclk_rate();

		period_ticks =
			sifive_pwm_period_ticks_for_frequency(pclk,
							     PWM_TARGET_HIGH_HZ);
		active_compare =
			sifive_pwm_compare_for_fraction(period_ticks, 1, 2);
		config.zero_compare = 1;
		config.compare_mask |= SIFIVE_PWM_CHANNEL(0);
		config.compare[0] = period_ticks - 1;
	}

	config.compare_mask |= SIFIVE_PWM_CHANNEL(D2_PWM_RED) |
		SIFIVE_PWM_CHANNEL(D2_PWM_GREEN) |
		SIFIVE_PWM_CHANNEL(D2_PWM_BLUE);
	config.compare[D2_PWM_RED] =
		compare_value(components, LED_RED, active_compare);
	config.compare[D2_PWM_GREEN] =
		compare_value(components, LED_GREEN, active_compare);
	config.compare[D2_PWM_BLUE] =
		compare_value(components, LED_BLUE, active_compare);

	sifive_pwm_apply(FU740_PWM0_BASE, &config);
}

bm_ulong baremetal_main(int argc, char *const argv[])
{
	const struct led_color *color;
	const char *color_name;
	enum led_mode mode = LED_MODE_SOLID;

	if (!argv || (argc != 2 && argc != 3) || !argv[1])
		return LED_INVALID_ARGUMENTS;

	if (argc == 2) {
		color_name = argv[1];
	} else {
		if (!argv[2])
			return LED_INVALID_ARGUMENTS;

		if (bm_streq(argv[1], "solid"))
			mode = LED_MODE_SOLID;
		else if (bm_streq(argv[1], "blink"))
			mode = LED_MODE_BLINK;
		else if (bm_streq(argv[1], "pwm-low"))
			mode = LED_MODE_PWM_LOW;
		else if (bm_streq(argv[1], "pwm-high"))
			mode = LED_MODE_PWM_HIGH;
		else
			return LED_UNKNOWN_MODE;

		color_name = argv[2];
	}

	color = find_color(color_name);
	if (!color)
		return LED_UNKNOWN_COLOR;

	set_color(color->components, mode);
	return LED_SUCCESS;
}
