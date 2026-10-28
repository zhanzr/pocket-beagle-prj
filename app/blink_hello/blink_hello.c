#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#define LED_DIR  "/sys/class/leds/beaglebone:green:usr3"
#define BLINK_US 500000

static volatile sig_atomic_t g_run = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_run = 0;
}

static int write_file(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY);
    ssize_t n;

    if (fd < 0) {
        return -1;
    }
    n = write(fd, value, strlen(value));
    close(fd);
    return (n < 0) ? -1 : 0;
}

static int read_active_trigger(char *buf, size_t size)
{
    char raw[512];
    char *start, *end;
    int fd = open(LED_DIR "/trigger", O_RDONLY);
    ssize_t n;

    if (fd < 0) {
        return -1;
    }
    n = read(fd, raw, sizeof(raw) - 1);
    close(fd);
    if (n <= 0) {
        return -1;
    }
    raw[n] = '\0';
    start = strchr(raw, '[');
    end = (start != NULL) ? strchr(start, ']') : NULL;
    if (start == NULL || end == NULL) {
        return -1;
    }
    *end = '\0';
    start++;
    strncpy(buf, start, size - 1);
    buf[size - 1] = '\0';
    return 0;
}

int main(void)
{
    char old_trigger[128] = "none";
    int have_trigger = read_active_trigger(old_trigger, sizeof(old_trigger)) == 0;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    printf("hello world\n");

    if (have_trigger && strcmp(old_trigger, "none") != 0) {
        if (write_file(LED_DIR "/trigger", "none") != 0) {
            fprintf(stderr, "warn: cannot detach trigger '%s' "
                            "(no write access), blinking may be overridden\n",
                    old_trigger);
        }
    }

    while (g_run) {
        if (write_file(LED_DIR "/brightness", "1") != 0) {
            perror(LED_DIR "/brightness");
            fprintf(stderr, "hint: the debian user must be in the 'gpio' "
                            "group (or use sudo)\n");
            return 1;
        }
        usleep(BLINK_US);
        if (!g_run) {
            break;
        }
        if (write_file(LED_DIR "/brightness", "0") != 0) {
            perror(LED_DIR "/brightness");
            return 1;
        }
        usleep(BLINK_US);
    }

    if (have_trigger && strcmp(old_trigger, "none") != 0) {
        write_file(LED_DIR "/trigger", old_trigger);
    }
    printf("bye\n");
    return 0;
}
