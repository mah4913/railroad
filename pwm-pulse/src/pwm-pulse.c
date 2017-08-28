#include "allwinner.h"
#include <libgen.h>

#define AW_IO_BASE_ADDR		(0x01C20000)
#define AW_PWM_CTRL_REG		(0x00000E00)
#define AW_PWM_CH0_PERIOD	(0x00000E04)
#define AW_PWM_CH1_PERIOD	(0x00000E08)

void print_usage(char *prg) {
    fprintf(stderr, "\nUsage: %s \n", prg);
    fprintf(stderr, "   Version 0.1\n\n");
    fprintf(stderr, "         -p toggle pin [e.g. PH2 -> (7*32+2) 226]\n");
    fprintf(stderr, "         -t period time in ns\n");
    fprintf(stderr, "         -d duty cycle in ns\n");
}

int main(int argc, char **argv) {
    int opt, pwm_period, pwm_duty_cycle, toggle_pin;

    pwm_period = 0;
    pwm_duty_cycle = 0;
    toggle_pin = 0;

    if (io_aw_open("/dev/mem") < 0) {
	fprintf(stderr, "Can't open IO mem: %s\n", strerror(errno));
	exit(EXIT_FAILURE);
    }

    while ((opt = getopt(argc, argv, "p:t:d:h?")) != -1) {
	switch (opt) {
	case 'p':
	    toggle_pin = strtoul(optarg, (char **)NULL, 10);
	    break;
	case 't':
	    pwm_period = strtoul(optarg, (char **)NULL, 10);
	    break;
	case 'd':
	    pwm_duty_cycle = strtoul(optarg, (char **)NULL, 10);
	    break;
	case 'h':
	case '?':
	    print_usage(basename(argv[0]));
	    exit(EXIT_SUCCESS);
	}
    }

    if (pwm_duty_cycle > pwm_period) {
	fprintf(stderr, "pwm_duty_cycle [%d] larger then pwm_period [%d]\n", pwm_duty_cycle, pwm_period);
	io_aw_close();
	exit(EXIT_FAILURE);
    }

    if (toggle_pin)
	gpio_aw_select_output(toggle_pin);
    else
	return 0;

    printf("toggling port %d ...\n", toggle_pin);

    while (1) {
	gpio_aw_set(toggle_pin, HIGH);
	gpio_aw_set(toggle_pin, LOW);
    }

    io_aw_close();
    return 0;
}
