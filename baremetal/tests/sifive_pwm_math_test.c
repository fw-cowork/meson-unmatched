// SPDX-License-Identifier: MIT

#include <stdio.h>

#include <drivers/sifive_pwm.h>

static int failures;

static void expect_equal(const char *name, bm_u32 actual, bm_u32 expected)
{
	if (actual == expected)
		return;

	fprintf(stderr, "%s: got %u, expected %u\n", name, actual, expected);
	failures++;
}

int main(void)
{
	/* Natural-wrap frequency selection, including both scale limits. */
	expect_equal("scale 260 MHz to 10 MHz",
		     sifive_pwm_scale_for_frequency(260000000UL, 10000000UL),
		     0);
	expect_equal("scale 125 MHz to 100 Hz",
		     sifive_pwm_scale_for_frequency(125000000UL, 100UL),
		     4);
	expect_equal("scale 125 MHz to 1 Hz",
		     sifive_pwm_scale_for_frequency(125000000UL, 1UL),
		     11);
	expect_equal("scale zero target",
		     sifive_pwm_scale_for_frequency(125000000UL, 0),
		     SIFIVE_PWM_MAX_SCALE);

	/* pwmzerocmp periods are rounded and clamped to the hardware range. */
	expect_equal("period 260 MHz to 10 MHz",
		     sifive_pwm_period_ticks_for_frequency(260000000UL,
							      10000000UL),
		     26);
	expect_equal("period round half up",
		     sifive_pwm_period_ticks_for_frequency(100UL, 40UL), 3);
	expect_equal("period minimum",
		     sifive_pwm_period_ticks_for_frequency(1UL, 100UL), 2);
	expect_equal("period maximum",
		     sifive_pwm_period_ticks_for_frequency(1000000UL, 1UL),
		     SIFIVE_PWM_NATURAL_PERIOD_TICKS);
	expect_equal("period zero target",
		     sifive_pwm_period_ticks_for_frequency(1000000UL, 0),
		     SIFIVE_PWM_NATURAL_PERIOD_TICKS);

	/* SiFive compare values are inverted: lower compare means more on-time. */
	expect_equal("duty off",
		     sifive_pwm_compare_for_fraction(
			     SIFIVE_PWM_NATURAL_PERIOD_TICKS, 0, 1),
		     SIFIVE_PWM_COMPARE_OFF);
	expect_equal("duty 25 percent",
		     sifive_pwm_compare_for_fraction(
			     SIFIVE_PWM_NATURAL_PERIOD_TICKS, 1, 4),
		     0xc000);
	expect_equal("duty 50 percent",
		     sifive_pwm_compare_for_fraction(
			     SIFIVE_PWM_NATURAL_PERIOD_TICKS, 1, 2),
		     0x8000);
	expect_equal("duty full on",
		     sifive_pwm_compare_for_fraction(
			     SIFIVE_PWM_NATURAL_PERIOD_TICKS, 1, 1),
		     SIFIVE_PWM_COMPARE_FULL_ON);
	expect_equal("duty invalid denominator",
		     sifive_pwm_compare_for_fraction(
			     SIFIVE_PWM_NATURAL_PERIOD_TICKS, 1, 0),
		     SIFIVE_PWM_COMPARE_OFF);

	return failures != 0;
}
