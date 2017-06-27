/* ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <info@gerhard-bertelsmann.de> wrote this file. As long as you retain this
 * notice you can do whatever you want with this stuff. If we meet some day,
 * and you think this stuff is worth it, you can buy me a beer in return
 * Gerhard Bertelsmann
 * ----------------------------------------------------------------------------
 */

/* Thanks to Stefan Krauss and the SocketCAN team
 */

#include "can2serial.h"

static char *CAN_FORMAT_STRG =     "      CAN->  CANID 0x%06X R [%d]";
static char *TCP_FORMAT_STRG =     "->TCP>CAN    CANID 0x%06X   [%d]";
static char *TCP_FORMATS_STRG =    "->TCP>CAN*   CANID 0x%06X   [%d]";
static char *CAN_TCP_FORMAT_STRG = "->CAN>TCP    CANID 0x%06X   [%d]";

char config_dir[MAXLINE];
char config_file[MAXLINE];
char **page_name;
struct timeval last_sent;

void print_usage(char *prg) {
    fprintf(stderr, "\nUsage: %s -u <udp_port> -t <tcp_port> -d <udp_dest_port> -i <can interface>\n",
	    prg);
    fprintf(stderr, "   Version 0.9\n\n");
    fprintf(stderr, "         -u <port>           listening UDP port for the server - default 15731\n");
    fprintf(stderr, "         -t <port>           listening TCP port for the server - default 15731\n");
    fprintf(stderr, "         -d <port>           destination UDP port for the server - default 15730\n");
    fprintf(stderr, "         -a <IP addr>        IP address - default 255.255.255.255\n");
    fprintf(stderr, "         -i <can int>        CAN interface - default /dev/ttyUSB0\n");
    fprintf(stderr, "         -f                  running in foreground\n\n");
    fprintf(stderr, "         -v                  verbose output (in foreground)\n\n");
}

void usec_sleep(int usec) {
    struct timespec to_wait;

    to_wait.tv_sec = 0;
    to_wait.tv_nsec = usec * 1000;
    nanosleep(&to_wait, NULL);
}

int time_stamp(char *timestamp) {
    /* char *timestamp = (char *)malloc(sizeof(char) * 16); */
    struct timeval tv;
    struct tm *tm;

    gettimeofday(&tv, NULL);
    tm = localtime(&tv.tv_sec);

    sprintf(timestamp, "%02d:%02d:%02d.%03d", tm->tm_hour, tm->tm_min, tm->tm_sec, (int)tv.tv_usec / 1000);
    return 0;
}

void print_can_frame(char *format_string, unsigned char *netframe, int verbose) {
    uint32_t canid;
    int i, dlc;
    char timestamp[16];

    if (!verbose)
        return;

    memcpy(&canid, netframe, 4);
    dlc = netframe[4];
    time_stamp(timestamp);
    printf("%s   ", timestamp);
    printf(format_string, ntohl(canid) & CAN_EFF_MASK, netframe[4]);
    for (i = 5; i < 5 + dlc; i++) {
        printf(" %02x", netframe[i]);
    }
    if (dlc < 8) {
        printf("(%02x", netframe[i]);
        for (i = 6 + dlc; i < CAN_ENCAP_SIZE; i++) {
            printf(" %02x", netframe[i]);
        }
        printf(")");
    } else {
        printf(" ");
    }
    printf("  ");
    for (i = 5; i < CAN_ENCAP_SIZE; i++) {
        if (isprint(netframe[i]))
            printf("%c", netframe[i]);
        else
            putchar('.');
    }

    printf("\n");
}

int frame_to_can(int can_socket, unsigned char *netframe) {
    uint32_t canid;
    struct can_frame frame;
    struct timespec to_wait;
    struct timeval actual_time;
    long usec;

    /* Maerklin TCP/UDP Format: always 13 (CAN_ENCAP_SIZE) bytes
     *   byte 0 - 3  CAN ID
     *   byte 4      DLC
     *   byte 5 - 12 CAN data
     */
    memset(&frame, 0, sizeof(frame));
    memcpy(&canid, netframe, 4);
    /* CAN uses (network) big endian format */
    frame.can_id = ntohl(canid);
    frame.can_id &= CAN_EFF_MASK;
    frame.can_id |= CAN_EFF_FLAG;
    frame.can_dlc = netframe[4];
    memcpy(&frame.data, &netframe[5], 8);

    /* we calculate the difference between the actual time and the time the last command was sent */
    /* probably we don't need to wait anymore before putting next CAN frame on the wire */
    gettimeofday(&actual_time, NULL);
    usec = (actual_time.tv_sec - last_sent.tv_sec) * 1000000;
    usec += (actual_time.tv_usec - last_sent.tv_usec);
    if (usec < TIME_WAIT_US) {
	to_wait.tv_sec = 0;
	to_wait.tv_nsec = (TIME_WAIT_US - usec) * 1000;
	nanosleep(&to_wait, NULL);
    }

    /* send CAN frame */
    if (write(can_socket, &frame, sizeof(frame)) != sizeof(frame)) {
	fprint_syslog_wc(stderr, LOG_ERR, "error CAN frame:", strerror(errno));
	return -1;
    }

    gettimeofday(&last_sent, NULL);
    return 0;
}

int rawframe_to_net(int net_socket, struct sockaddr *net_addr, unsigned char *netframe, int length) {
    int s;
    s = sendto(net_socket, netframe, length, 0, net_addr, sizeof(*net_addr));
    if (s != length) {
        fprintf(stderr, "%s: error sending TCP/UDP data; %s\n", __func__, strerror(errno));
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    pid_t pid;
    int n, i, max_fds, opt, nready;
    int background, verbose, ec_index, on;
    char timestamp[16];
    struct termios term_attr;
    int sc, se, sb, st;		/* CAN socket, TCP socket */
    struct sockaddr_in baddr, tcp_addr;
    struct sockaddr_can caddr;
    socklen_t caddrlen = sizeof(caddr);
    char if_name[MAXSTRING];
    char udp_dst_address[16];
    struct ifreq ifr;
    /* socklen_t tcp_client_length = sizeof(tcp_addr); */
    fd_set all_fds, read_fds;
    uint32_t canid;
    int eci, s, ret;
    int tcp_port, destination_port;
    char buffer[64];
    unsigned char rawframe[64];
    struct can_frame frame;

    verbose = 0;
    background = 1;
    on = 1;
    strcpy(udp_dst_address, "255.255.255.255");
    ec_index = 0;
    page_name = calloc(64, sizeof(char *));

    memset(ifr.ifr_name, 0, sizeof(ifr.ifr_name));
    strcpy(ifr.ifr_name, "can0");
    tcp_port = 15731;
    destination_port = 15730;
    verbose = 0;
    strcpy(if_name, "/dev/ttyGS0");

    config_file[0] = '\0';

    while ((opt = getopt(argc, argv, "u:t:d:b:i:vhf?")) != -1) {
	switch (opt) {
	case 't':
	    tcp_port = strtoul(optarg, (char **)NULL, 10);
	    break;
	case 'd':
	    destination_port = strtoul(optarg, (char **)NULL, 10);
	    break;
	case 'b':
	    if (strlen(optarg) <= 15) {
		strncpy(udp_dst_address, optarg, sizeof(udp_dst_address) - 1);
	    } else {
		fprintf(stderr, "UDP broadcast address error: %s\n", optarg);
		exit(EXIT_FAILURE);
	    }
	    break;
	case 'i':
            strncpy(ifr.ifr_name, optarg, sizeof(ifr.ifr_name) - 1);
            break;
	case 'v':
	    verbose = 1;
	    break;
	case 'f':
	    background = 0;
	    break;
	case 'h':
	case '?':
	    print_usage(basename(argv[0]));
	    exit(EXIT_SUCCESS);
	default:
	    fprintf(stderr, "Unknown option %c\n", opt);
	    print_usage(basename(argv[0]));
	    exit(EXIT_FAILURE);
	}
    }

    /* prepare CAN socket */
    memset(&caddr, 0, sizeof(caddr));
    sc = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sc < 0) {
        fprintf(stderr, "creating CAN socket error: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    caddr.can_family = AF_CAN;
    if (ioctl(sc, SIOCGIFINDEX, &ifr) < 0) {
        fprintf(stderr, "setup CAN error: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    caddr.can_ifindex = ifr.ifr_ifindex;

    if (bind(sc, (struct sockaddr *)&caddr, caddrlen) < 0) {
        fprintf(stderr, "binding CAN socket error: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* prepare TCP socket */
    if ((st = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
	fprintf(stderr, "creating TCP socket error: %s\n", strerror(errno));
	exit(EXIT_FAILURE);
    }
    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    tcp_addr.sin_port = htons(tcp_port);
    if (bind(st, (struct sockaddr *)&tcp_addr, sizeof(tcp_addr)) < 0) {
	fprintf(stderr, "binding TCP socket error: %s\n", strerror(errno));
	exit(EXIT_FAILURE);
    }
    if (listen(st, MAXPENDING) < 0) {
	fprintf(stderr, "starting TCP listener error: %s\n", strerror(errno));
	exit(EXIT_FAILURE);
    }

    /* prepare simple CAN interface aka Schnitte */
    if ((se = open(if_name, O_RDWR | O_TRUNC | O_NONBLOCK | O_NOCTTY)) < 0) {
	fprintf(stderr, "opening CAN interface error: %s\n", strerror(errno));
	exit(EXIT_FAILURE);
    } else {
	memset(&term_attr, 0, sizeof(term_attr));
	if (tcgetattr(sc, &term_attr) < 0) {
	    fprintf(stderr, "can't get terminal settings error: %s\n", strerror(errno));
	    exit(EXIT_FAILURE);
	}
	term_attr.c_cflag = CS8 | CRTSCTS | CLOCAL | CREAD;
	term_attr.c_iflag = 0;
	term_attr.c_oflag = 0;
	term_attr.c_lflag = NOFLSH;
	if (cfsetospeed(&term_attr, TERM_SPEED) < 0) {
	    fprintf(stderr, "CAN interface ospeed error: %s\n", strerror(errno));
	    exit(EXIT_FAILURE);
	}
	if (cfsetispeed(&term_attr, TERM_SPEED) < 0) {
	    fprintf(stderr, "CAN interface ispeed error: %s\n", strerror(errno));
	    exit(EXIT_FAILURE);
	}
	if (tcsetattr(sc, TCSANOW, &term_attr) < 0) {
	    fprintf(stderr, "CAN interface set error: %s\n", strerror(errno));
	    exit(EXIT_FAILURE);
	}
    }

    /* daemonize the process if requested */
    if (background) {
	/* fork off the parent process */
	pid = fork();
	if (pid < 0) {
	    exit(EXIT_FAILURE);
	}
	/* if we got a good PID, then we can exit the parent process */
	if (pid > 0) {
	    printf("Going into background ...\n");
	    exit(EXIT_SUCCESS);
	}
    }

    FD_ZERO(&all_fds);
    FD_SET(sc, &all_fds);
    FD_SET(se, &all_fds);
    FD_SET(st, &all_fds);
    max_fds = MAX(se, MAX(sc, st));

    while (1) {
	read_fds = all_fds;
	if ((nready = select(max_fds + 1, &read_fds, NULL, NULL, NULL)) < 0) {
	    fprintf(stderr, "select error: %s\n", strerror(errno));
	}

	if (FD_ISSET(se, &read_fds)) {
	    while ((ret = read(se, buffer, sizeof(buffer))) > 0) {
		for (eci = 0; eci < ret; eci++) {
		    ec_frame[ec_index++] = (unsigned char)buffer[eci];
		    if (ec_index == 13) {
			/* we got a complete CAN frame */
			ec_index = 0;
			/* TODO */
			/* if (frame_to_net(sb, (struct sockaddr *)&baddr, ec_frame, 13)) {
			    fprintf(stderr, "sending UDP data error:%s \n", strerror(errno));
			} else if (!background) {
			    print_can_frame(UDP_FORMAT_STRG, ec_frame, verbose);
			} */
			/* send CAN frame to TCP socket */
			rawframe_to_net(st, (struct sockaddr *)&tcp_addr, ec_frame, 13);
			if (!background)
			    print_can_frame(CAN_TCP_FORMAT_STRG, ec_frame, verbose);
		    }
		}
	    }
	}

	/* received a CAN frame */
        if (FD_ISSET(sc, &read_fds)) {
            if (read(sc, &frame, sizeof(struct can_frame)) < 0) {
                fprintf(stderr, "CAN read error: %s\n", strerror(errno));
                break;
            } else {
                /* prepare packet */
                memset(rawframe, 0, 13);
                canid = htonl(frame.can_id);
                memcpy(rawframe, &canid, 4);
                rawframe[4] = frame.can_dlc;
                memcpy(&rawframe[5], &frame.data, frame.can_dlc);

                /* send TCP packet */
                if (sendto(st, rawframe, 13, 0, (struct sockaddr *)&tcp_addr, sizeof(tcp_addr)) != 13) {
                    fprintf(stderr, "UDP write error: %s\n", strerror(errno));
                    break;
                }
                print_can_frame(CAN_FORMAT_STRG, rawframe, verbose);
            }
        }

	/* received a TCP packet */
	if (FD_ISSET(st, &read_fds)) {
	    if (verbose && !background) {
		time_stamp(timestamp);
		printf("%s packet from: %s\n", timestamp,
		       inet_ntop(AF_INET, &tcp_addr.sin_addr, buffer, sizeof(buffer)));
	    }
	    if ((n = read(st, netframe, MAXDG)) == 0) {
		/* connection closed by client */
		if (verbose && !background) {
		    time_stamp(timestamp);
		    printf("%s client %s closed connection\n", timestamp,
			   inet_ntop(AF_INET, &tcp_addr.sin_addr, buffer, sizeof(buffer)));
		}
		/* tcp close  TODO */
		close(st);
		FD_CLR(st, &all_fds);
	    } else {
		/* check the whole TCP packet, if there are more than one CAN frame included */
		/* TCP packets with size modulo 13 !=0 are ignored though */
		if (n % 13) {
		    time_stamp(timestamp);
		    fprintf(stderr, "%s received packet %% 13 : length %d\n", timestamp, n);
		} else {
		    for (i = 0; i < n; i += 13) {
			ret = frame_to_can(sc, &netframe[i]);
			if ((ret == 0) && (verbose && !background)) {
			    if (i > 0)
				print_can_frame(TCP_FORMATS_STRG, &netframe[i], verbose);
			    else
				print_can_frame(TCP_FORMAT_STRG, &netframe[i], verbose);
			}
		    }
		}
	    }
	    if (--nready <= 0)
		break;		/* no more readable descriptors */
	}
    }
    close(sb);
    close(sc);
    close(se);
    close(st);
    return 0;
}
