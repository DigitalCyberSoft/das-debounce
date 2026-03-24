/*
 * das-debounce — Fix Das Keyboard 4 Professional volume knob bounce on Linux
 *
 * Problem: The volume knob's rotary encoder produces spurious direction
 * reversals at high rotation speeds (alternating VOLUMEUP/VOLUMEDOWN every
 * 8-16ms). The keyboard firmware performs no debounce.
 *
 * Solution: Grab the Consumer Control input device, suppress direction
 * reversals within a debounce window, and re-emit clean events via uinput.
 *
 * Build: gcc -Wall -Wextra -O2 -o das-debounce das-debounce.c $(pkg-config --cflags --libs libevdev)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <time.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <libevdev/libevdev.h>

#define DAS_VENDOR  0x24F0
#define DAS_PRODUCT 0x204A
#define DEFAULT_DEBOUNCE_MS 100

static volatile sig_atomic_t running = 1;
static struct libevdev *g_dev = NULL;
static int g_input_fd = -1;
static int g_uinput_fd = -1;

static void cleanup(void)
{
	if (g_uinput_fd >= 0) {
		ioctl(g_uinput_fd, UI_DEV_DESTROY);
		close(g_uinput_fd);
		g_uinput_fd = -1;
	}
	if (g_dev) {
		libevdev_grab(g_dev, LIBEVDEV_UNGRAB);
		libevdev_free(g_dev);
		g_dev = NULL;
	}
	if (g_input_fd >= 0) {
		close(g_input_fd);
		g_input_fd = -1;
	}
}

static void handle_signal(int sig)
{
	running = 0;
	/* For fatal signals, clean up and re-raise to get default behavior */
	if (sig == SIGHUP || sig == SIGQUIT) {
		cleanup();
		signal(sig, SIG_DFL);
		raise(sig);
	}
}

static long elapsed_ms(struct timeval *a, struct timeval *b)
{
	return (b->tv_sec - a->tv_sec) * 1000 +
	       (b->tv_usec - a->tv_usec) / 1000;
}

/*
 * Scan /dev/input/event* for the Das Keyboard Consumer Control interface.
 * Returns an open fd or -1.
 */
static int find_device(struct libevdev **dev)
{
	DIR *dir;
	struct dirent *ent;
	char path[512];

	dir = opendir("/dev/input");
	if (!dir) {
		perror("opendir /dev/input");
		return -1;
	}

	while ((ent = readdir(dir)) != NULL) {
		if (strncmp(ent->d_name, "event", 5) != 0)
			continue;

		snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);

		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;

		struct libevdev *d = NULL;
		if (libevdev_new_from_fd(fd, &d) < 0) {
			close(fd);
			continue;
		}

		int vid = libevdev_get_id_vendor(d);
		int pid = libevdev_get_id_product(d);
		const char *name = libevdev_get_name(d);

		if (vid == DAS_VENDOR && pid == DAS_PRODUCT &&
		    name && strstr(name, "Consumer Control")) {
			fprintf(stderr, "das-debounce: found %s at %s\n", name, path);
			closedir(dir);
			*dev = d;
			return fd;
		}

		libevdev_free(d);
		close(fd);
	}

	closedir(dir);
	fprintf(stderr, "das-debounce: Das Keyboard Consumer Control not found\n");
	return -1;
}

/*
 * Create a uinput virtual device that mirrors the capabilities of the
 * grabbed physical device.
 */
static int create_uinput(struct libevdev *dev)
{
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (fd < 0) {
		perror("open /dev/uinput");
		return -1;
	}

	struct uinput_setup setup;
	memset(&setup, 0, sizeof(setup));
	snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "Das Keyboard Volume (debounced)");
	setup.id.bustype = BUS_VIRTUAL;
	setup.id.vendor = DAS_VENDOR;
	setup.id.product = DAS_PRODUCT;
	setup.id.version = 1;

	/* Enable EV_SYN */
	ioctl(fd, UI_SET_EVBIT, EV_SYN);

	/* Enable EV_KEY + the volume keys */
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_KEYBIT, KEY_VOLUMEUP);
	ioctl(fd, UI_SET_KEYBIT, KEY_VOLUMEDOWN);
	ioctl(fd, UI_SET_KEYBIT, KEY_MUTE);

	/* Enable EV_MSC if the device has it */
	if (libevdev_has_event_type(dev, EV_MSC)) {
		ioctl(fd, UI_SET_EVBIT, EV_MSC);
		if (libevdev_has_event_code(dev, EV_MSC, MSC_SCAN))
			ioctl(fd, UI_SET_MSCBIT, MSC_SCAN);
	}

	if (ioctl(fd, UI_DEV_SETUP, &setup) < 0) {
		perror("UI_DEV_SETUP");
		close(fd);
		return -1;
	}

	if (ioctl(fd, UI_DEV_CREATE) < 0) {
		perror("UI_DEV_CREATE");
		close(fd);
		return -1;
	}

	fprintf(stderr, "das-debounce: virtual device created\n");
	return fd;
}

static void emit(int uinput_fd, unsigned short type, unsigned short code, int value)
{
	struct input_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = type;
	ev.code = code;
	ev.value = value;
	/* Let the kernel fill in the timestamp */
	write(uinput_fd, &ev, sizeof(ev));
}

static void emit_syn(int uinput_fd)
{
	emit(uinput_fd, EV_SYN, SYN_REPORT, 0);
}

int main(int argc, char *argv[])
{
	int last_direction = 0; /* KEY_VOLUMEUP or KEY_VOLUMEDOWN */
	struct timeval last_change_time = {0, 0};
	int debounce_ms = DEFAULT_DEBOUNCE_MS;
	int dropped = 0;

	if (argc > 1) {
		debounce_ms = atoi(argv[1]);
		if (debounce_ms < 10 || debounce_ms > 500) {
			fprintf(stderr, "usage: %s [debounce_ms (10-500, default %d)]\n",
				argv[0], DEFAULT_DEBOUNCE_MS);
			return 1;
		}
	}

	atexit(cleanup);

	/* Use sigaction without SA_RESTART so blocking reads get interrupted */
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handle_signal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0; /* no SA_RESTART */
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);

	g_input_fd = find_device(&g_dev);
	if (g_input_fd < 0)
		return 1;

	if (libevdev_grab(g_dev, LIBEVDEV_GRAB) < 0) {
		fprintf(stderr, "das-debounce: failed to grab device: %s\n",
			strerror(errno));
		return 1;
	}

	g_uinput_fd = create_uinput(g_dev);
	if (g_uinput_fd < 0)
		return 1;

	fprintf(stderr, "das-debounce: running (debounce=%dms)\n", debounce_ms);

	/* Switch to blocking reads */
	int flags = fcntl(g_input_fd, F_GETFL);
	fcntl(g_input_fd, F_SETFL, flags & ~O_NONBLOCK);

	while (running) {
		struct input_event ev;
		int rc = libevdev_next_event(g_dev,
			LIBEVDEV_READ_FLAG_NORMAL | LIBEVDEV_READ_FLAG_BLOCKING,
			&ev);

		if (rc == -EAGAIN || rc == -EINTR)
			continue;
		if (rc < 0) {
			if (rc == -ENODEV) {
				fprintf(stderr, "das-debounce: device disconnected\n");
				break;
			}
			fprintf(stderr, "das-debounce: read error: %s\n",
				strerror(-rc));
			break;
		}

		/* Only debounce volume key press events */
		if (ev.type == EV_KEY &&
		    (ev.code == KEY_VOLUMEUP || ev.code == KEY_VOLUMEDOWN) &&
		    ev.value == 1) {
			int direction = ev.code;

			if (direction != last_direction && last_direction != 0) {
				long dt = elapsed_ms(&last_change_time, &ev.time);
				/* Reset the change timer on every reversal attempt,
				 * so rapid bouncing keeps the window open */
				last_change_time = ev.time;
				if (dt >= 0 && dt < debounce_ms) {
					dropped++;
					if (dropped % 10 == 1)
						fprintf(stderr,
							"das-debounce: suppressed bounce (%ld ms, total %d)\n",
							dt, dropped);
					continue;
				}
			}

			last_direction = direction;
			last_change_time = ev.time;
		}

		/* Drop the release of a bounced press */
		if (ev.type == EV_KEY &&
		    (ev.code == KEY_VOLUMEUP || ev.code == KEY_VOLUMEDOWN) &&
		    ev.value == 0) {
			if ((int)ev.code != last_direction && last_direction != 0)
				continue;
		}

		/* Drop MSC_SCAN events for bounced keys */
		if (ev.type == EV_MSC && ev.code == MSC_SCAN) {
			unsigned int scan = (unsigned int)ev.value;
			/* c00e9 = vol up, c00ea = vol down */
			if (scan == 0xc00e9 && last_direction == KEY_VOLUMEDOWN)
				continue;
			if (scan == 0xc00ea && last_direction == KEY_VOLUMEUP)
				continue;
		}

		/* Pass through */
		emit(g_uinput_fd, ev.type, ev.code, ev.value);
		if (ev.type != EV_SYN)
			emit_syn(g_uinput_fd);
	}

	fprintf(stderr, "das-debounce: shutting down (suppressed %d bounces)\n", dropped);
	/* cleanup() runs via atexit */
	return 0;
}
