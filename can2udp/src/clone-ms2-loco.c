/* ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <info@gerhard-bertelsmann.de> wrote this file. As long as you retain this
 * notice you can do whatever you want with this stuff. If we meet some day,
 * and you think this stuff is worth it, you can buy me a beer in return
 * Gerhard Bertelsmann
 * ----------------------------------------------------------------------------
 */

/*  LED		PI14	270
 *  bush button PI10	266
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <net/if.h>
#include <netinet/in.h>
#include <linux/can.h>

#define BIT(x)		(1<<x)
#define MINDELAY	1000000	/* min delay in usec */
#define MAXLEN		64	/* maximum string length */
#define MAX_BUFFER	8
#define MAX_SYSFS_LEN	256
#define DEF_INTERVAL	300
#define MAX(a,b)	((a) > (b) ? (a) : (b))

unsigned int led_period;
pthread_mutex_t lock;

unsigned char CLONE_CONFIG_REQUEST[] = { 0x00, 0x40, 0xaf, 0x7e, 0x00 };

static char *F_CAN_FORMAT_STRG = "   -> CAN     0x%08X   [%d]";
static char *T_CAN_FORMAT_STRG = "      CAN ->  0x%08X   [%d]";

enum gpio_edges {
    EDGE_NONE,
    EDGE_RISING,
    EDGE_FALLING,
    EDGE_BOTH
};

struct trigger_t {
    struct sockaddr_can caddr;
    int socket;
    int background;
    int verbose;
    int interval;
    int led_pin;
    int pb_pin;
    int pb_fd;
    uint16_t hash;
    uint16_t hw_id;
};

void usage(char *prg) {
    fprintf(stderr, "\nUsage: %s -vf [-i <can int>][-t <sec>] \n", prg);
    fprintf(stderr, "   Version 1.0\n\n");
    fprintf(stderr, "         -i <can interface>   broadcast address or interface - default 255.255.255.255/br-lan\n");
    fprintf(stderr, "         -t <interval in sec> using timer in sec / 0 -> only once - default %d\n", DEF_INTERVAL);
    fprintf(stderr, "         -l <led pin>         LED pin (e.g. BPi PI14 -> 270)\n");
    fprintf(stderr, "         -p <push button>     push button (e.g. BPi PI10 -> 266)\n");
    fprintf(stderr, "         -f                   run in foreground (for debugging)\n");
    fprintf(stderr, "         -v                   be verbose\n\n");
}

void usec_sleep(int usec) {
    struct timespec to_wait;

    to_wait.tv_sec = 0;
    to_wait.tv_nsec = usec * 1000;
    nanosleep(&to_wait, NULL);
}

int time_stamp() {
    struct timeval tv;
    struct tm *tm;

    gettimeofday(&tv, NULL);
    tm = localtime(&tv.tv_sec);

    printf("%02d:%02d:%02d.%06d ", tm->tm_hour, tm->tm_min, tm->tm_sec, (int)tv.tv_usec);
    return 0;
}

void print_can_frame(char *format_string, struct can_frame *frame) {
    int i;
    time_stamp();
    printf(format_string, frame->can_id & CAN_EFF_MASK, frame->can_dlc);
    for (i = 0; i < frame->can_dlc; i++) {
	printf(" %02x", frame->data[i]);
    }
    if (frame->can_dlc < 8) {
	for (i = frame->can_dlc; i < 8; i++) {
	    printf("   ");
	}
    }
    printf("  ");
    for (i = 0; i < frame->can_dlc; i++) {
	if (isprint(frame->data[i]))
	    printf("%c", frame->data[i]);
	else
	    putchar(46);
    }
    printf("\n");
}

int send_can_frame(int can_socket, struct can_frame *frame, int verbose) {

    frame->can_id &= CAN_EFF_MASK;
    frame->can_id |= CAN_EFF_FLAG;
    /* send CAN frame */
    if (write(can_socket, frame, sizeof(*frame)) != sizeof(*frame)) {
	fprintf(stderr, "error writing CAN frame: %s\n", strerror(errno));
	return -1;
    }
    if (verbose)
	print_can_frame(T_CAN_FORMAT_STRG, frame);
    return 0;
}

int gpio_export(int pin) {
    char buffer[MAX_BUFFER];
    ssize_t bytes_written;
    int fd;

    fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0) {
	fprintf(stderr, "%s: Failed to open export for writing: %s\n", __func__, strerror(errno));
	return (EXIT_FAILURE);
    }

    bytes_written = snprintf(buffer, MAX_BUFFER, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return (0);
}

int gpio_unexport(int pin) {
    char buffer[MAX_BUFFER];
    ssize_t bytes_written;
    int fd;

    fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (fd < 0) {
	fprintf(stderr, "%s: Failed to open unexport for writing %s\n", __func__, strerror(errno));
	return (EXIT_FAILURE);
    }

    bytes_written = snprintf(buffer, MAX_BUFFER, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return (0);
}

int gpio_direction(int pin, int dir) {
    char path[MAX_SYSFS_LEN];
    int fd, ret;

    snprintf(path, MAX_SYSFS_LEN, "/sys/class/gpio/gpio%d/direction", pin);
    fd = open(path, O_WRONLY);
    if (fd < 0) {
	fprintf(stderr, "%s: Failed to open gpio direction for writing: %s\n", __func__, strerror(errno));
	return (-1);
    }

    if (dir)
	ret = write(fd, "in", 3);
    else
	ret = write(fd, "out", 4);

    if (ret == -1) {
	fprintf(stderr, "%s: Failed to set direction: %s\n", __func__, strerror(errno));
	return (-1);
    }

    close(fd);
    return (0);
}

int gpio_open(int pin) {
    char path[MAX_SYSFS_LEN];
    int fd;

    snprintf(path, MAX_SYSFS_LEN, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_RDONLY);
    if (fd < 0) {
	fprintf(stderr, "%s: Failed to open gpio value for reading: %s\n", __func__, strerror(errno));
	return (EXIT_FAILURE);
    }

    return fd;
}

int gpio_set(int pin, int value) {
    char path[MAX_SYSFS_LEN];
    int fd, ret;

    snprintf(path, MAX_SYSFS_LEN, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_WRONLY);
    if (fd < 0) {
	fprintf(stderr, "%s: Failed to open gpio value for writing: %s\n", __func__, strerror(errno));
	return (EXIT_FAILURE);
    }

    if (value)
	ret = write(fd, "1", 2);
    else
	ret = write(fd, "0", 2);

    if (ret != 2) {
	fprintf(stderr, "%s: Failed to write value!\n", __func__);
	return (EXIT_FAILURE);
    }

    close(fd);
    return (0);
}

int gpio_edge(int pin, int value) {
    char path[MAX_SYSFS_LEN];
    int fd, ret;

    snprintf(path, MAX_SYSFS_LEN, "/sys/class/gpio/gpio%d/edge", pin);
    fd = open(path, O_WRONLY);
    if (fd < 0) {
	fprintf(stderr, "%s: Failed to open gpio value for writing: %s\n", __func__, strerror(errno));
	return (EXIT_FAILURE);
    }

    switch (value) {
    case EDGE_RISING:
	ret = write(fd, "rising", 7);
	break;
    case EDGE_FALLING:
	ret = write(fd, "falling", 8);
	break;
    case EDGE_BOTH:
	ret = write(fd, "both", 5);
	break;
    case EDGE_NONE:
	ret = write(fd, "none", 5);
	break;
    default:
	fprintf(stderr, "%s: gpio edge type unknown\n", __func__);
	return (EXIT_FAILURE);
    }
    return ret;
}

int get_data(struct trigger_t *trigger, struct can_frame *frame) {
    /* TODO */
    if (trigger->socket)
	frame->can_dlc = 0;
    return 0;
}

/* Blink LED */
void *LEDMod(void *ptr) {
    int fd;
    char path[MAX_SYSFS_LEN];
    struct trigger_t *trigger = (struct trigger_t *)ptr;

#if 1
    snprintf(path, MAX_SYSFS_LEN, "/sys/class/gpio/gpio%d/value", trigger->led_pin);
    fd = open(path, O_WRONLY);
    if (fd < 0) {
	fprintf(stderr, "Failed to open gpio value for writing: %s\n", strerror(errno));
	exit(1);
    }

    while (1) {
	write(fd, "1", 2);
	usec_sleep(led_period);
	write(fd, "0", 2);
	usec_sleep(led_period);
    }
#else
    while (1) {
	usec_sleep(led_period);
	printf("*");
	usec_sleep(led_period);
	printf("-");
    }
#endif
}

int main(int argc, char **argv) {
    pthread_t pth;
    int opt;
    struct ifreq ifr;
    struct trigger_t trigger_data;
    struct sockaddr_can caddr;
    fd_set read_fds;
    struct can_frame frame;

    memset(&trigger_data, 0, sizeof(trigger_data));
    memset(ifr.ifr_name, 0, sizeof(ifr.ifr_name));
    strcpy(ifr.ifr_name, "can0");

    trigger_data.led_pin = -1;
    trigger_data.pb_pin = -1;

    trigger_data.interval = DEF_INTERVAL;

    while ((opt = getopt(argc, argv, "i:l:p:t:fvh?")) != -1) {
	switch (opt) {
	case 'i':
	    strncpy(ifr.ifr_name, optarg, sizeof(ifr.ifr_name));
	    break;
	case 't':
	    trigger_data.interval = atoi(optarg);
	    break;
	case 'l':
	    trigger_data.led_pin = atoi(optarg);
	    break;
	case 'p':
	    trigger_data.pb_pin = atoi(optarg);
	    break;
	case 'v':
	    trigger_data.verbose = 1;
	    break;
	case 'f':
	    trigger_data.background = 0;
	    break;
	case 'h':
	case '?':
	    usage(basename(argv[0]));
	    exit(EXIT_SUCCESS);
	default:
	    usage(basename(argv[0]));
	    exit(EXIT_FAILURE);
	}
    }

    /* prepare CAN socket */
    if ((trigger_data.socket = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
	fprintf(stderr, "error creating CAN socket: %s\n", strerror(errno));
	exit(EXIT_FAILURE);
    }

    memset(&caddr, 0, sizeof(caddr));
    caddr.can_family = AF_CAN;
    if (ioctl(trigger_data.socket, SIOCGIFINDEX, &ifr) < 0) {
	fprintf(stderr, "setup CAN socket error: %s %s\n", strerror(errno), ifr.ifr_name);
	exit(EXIT_FAILURE);
    }
    caddr.can_ifindex = ifr.ifr_ifindex;

    if (bind(trigger_data.socket, (struct sockaddr *)&caddr, sizeof(caddr)) < 0) {
	fprintf(stderr, "error binding CAN socket: %s\n", strerror(errno));
	exit(EXIT_FAILURE);
    }

    trigger_data.caddr = caddr;

    /* Create thread if LED pin defined */
    if ((trigger_data.led_pin) > 0) {
	led_period = 200000;
	gpio_export(trigger_data.led_pin);
	gpio_direction(trigger_data.led_pin, 0);

	if (pthread_mutex_init(&lock, NULL)) {
	    fprintf(stderr, "can't nit mutex %s\n", strerror(errno));
	    exit(EXIT_FAILURE);
	}
	if (pthread_create(&pth, NULL, LEDMod, &trigger_data)) {
	    fprintf(stderr, "can't create pthread %s\n", strerror(errno));
	    exit(EXIT_FAILURE);
	}
	if (!trigger_data.background && trigger_data.verbose)
	    printf("created LED thread\n");
    }

    if (trigger_data.background) {
	pid_t pid;

	/* fork off the parent process */
	pid = fork();
	if (pid < 0) {
	    exit(EXIT_FAILURE);
	}
	/* if we got a good PID, then we can exit the parent process. */
	if (pid > 0) {
	    if (trigger_data.verbose)
		printf("Going into background ...\n");
	    exit(EXIT_SUCCESS);
	}
    }

    /* initialize push button */
    if ((trigger_data.pb_pin) > 0) {
	gpio_export(trigger_data.pb_pin);
	gpio_direction(trigger_data.pb_pin, 1);
	trigger_data.pb_fd = gpio_open(trigger_data.pb_pin);
	gpio_edge(trigger_data.pb_pin, EDGE_RISING);
	read(trigger_data.pb_fd, NULL, 100);	/* won't work without this read ? */
    }

    FD_ZERO(&read_fds);

    /* loop forever TODO: if interval is set */
    while (1) {
	FD_SET(trigger_data.socket, &read_fds);
	FD_SET(trigger_data.pb_fd, &read_fds);
	if (select(MAX(trigger_data.socket, trigger_data.pb_fd) + 1, &read_fds, NULL, NULL, NULL) < 0) {
	    fprintf(stderr, "select error: %s\n", strerror(errno));
	    exit(EXIT_FAILURE);
	}
	/* CAN frame event */
	if (FD_ISSET(trigger_data.socket, &read_fds)) {
	    if (read(trigger_data.socket, &frame, sizeof(struct can_frame)) < 0)
		fprintf(stderr, "error reading CAN frame: %s\n", strerror(errno));

	    if (frame.can_id & CAN_EFF_FLAG) {
		switch ((frame.can_id & 0x00FF0000UL) >> 16) {
		case 0x41:
		case 0x42:
		    get_data(&trigger_data, &frame);
		    break;
		default:
		    if (trigger_data.verbose)
			print_can_frame(F_CAN_FORMAT_STRG, &frame);
		    break;
		}
	    }
	}
	/* push button event */
	if (FD_ISSET(trigger_data.pb_fd, &read_fds)) {
	    printf("push button event\n");
	}
    }

    /* TODO : wait until copy is done */

    if ((trigger_data.pb_pin) > 0)
	gpio_unexport(trigger_data.pb_pin);
    if ((trigger_data.led_pin) > 0) {
	gpio_unexport(trigger_data.led_pin);
	pthread_join(pth, (void *)&trigger_data);
	pthread_mutex_unlock(&lock);
    }
    return 0;
}