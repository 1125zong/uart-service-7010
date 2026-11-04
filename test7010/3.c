#define _GNU_SOURCE
#include <linux/termios.h>  /* termios2, TCGETS2, TCSETS2, BOTHER, Bxxxx */
#include <sys/syscall.h>    /* syscall(SYS_ioctl, ...) avoids header conflicts */
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#define FRAME_SIZE  1u
#define HEADER_SIZE 12u
#define RX_BUF_SIZE (FRAME_SIZE * 8u)

/* linux/termios.h may conflict with sys/ioctl.h on some platforms. */
#define uart_ioctl(fd, req, arg) syscall(SYS_ioctl, (fd), (req), (arg))

static const uint8_t MAGIC[8] = {'U','A','R','T','T','E','S','T'};
static volatile sig_atomic_t stopping = 0;

static void stop_handler(int sig) { (void)sig; stopping = 1; }

/* Return 1 for a Linux standard baud, 0 for a custom baud (BOTHER). */
static int standard_baud_flag(unsigned int baud, unsigned int *flag)
{
    switch (baud) {
    case 50:      *flag = B50;      return 1;
    case 75:      *flag = B75;      return 1;
    case 110:     *flag = B110;     return 1;
    case 134:     *flag = B134;     return 1;
    case 150:     *flag = B150;     return 1;
    case 200:     *flag = B200;     return 1;
    case 300:     *flag = B300;     return 1;
    case 600:     *flag = B600;     return 1;
    case 1200:    *flag = B1200;    return 1;
    case 1800:    *flag = B1800;    return 1;
    case 2400:    *flag = B2400;    return 1;
    case 4800:    *flag = B4800;    return 1;
    case 9600:    *flag = B9600;    return 1;
    case 19200:   *flag = B19200;   return 1;
    case 38400:   *flag = B38400;   return 1;
#ifdef B57600
    case 57600:   *flag = B57600;   return 1;
#endif
#ifdef B115200
    case 115200:  *flag = B115200;  return 1;
#endif
#ifdef B230400
    case 230400:  *flag = B230400;  return 1;
#endif
#ifdef B460800
    case 460800:  *flag = B460800;  return 1;
#endif
#ifdef B500000
    case 500000:  *flag = B500000;  return 1;
#endif
#ifdef B576000
    case 576000:  *flag = B576000;  return 1;
#endif
#ifdef B921600
    case 921600:  *flag = B921600;  return 1;
#endif
#ifdef B1000000
    case 1000000: *flag = B1000000; return 1;
#endif
#ifdef B1152000
    case 1152000: *flag = B1152000; return 1;
#endif
#ifdef B1500000
    case 1500000: *flag = B1500000; return 1;
#endif
#ifdef B2000000
    case 2000000: *flag = B2000000; return 1;
#endif
#ifdef B2500000
    case 2500000: *flag = B2500000; return 1;
#endif
#ifdef B3000000
    case 3000000: *flag = B3000000; return 1;
#endif
#ifdef B3500000
    case 3500000: *flag = B3500000; return 1;
#endif
#ifdef B4000000
    case 4000000: *flag = B4000000; return 1;
#endif
    default: return 0;
    }
}

static int setup_serial(const char *path, unsigned int baud)
{
    int fd = open(path, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    struct termios2 tio;
    if (uart_ioctl(fd, TCGETS2, &tio) < 0) {
        perror("TCGETS2");
        close(fd);
        return -1;
    }

    unsigned int requested_flag = BOTHER;
    int standard = standard_baud_flag(baud, &requested_flag);

    /* 8N1, raw mode, no software/hardware flow control. */
    tio.c_iflag = 0;
    tio.c_oflag = 0;
    tio.c_lflag = 0;
    tio.c_cflag &= ~(CBAUD | CSIZE | PARENB | CSTOPB | CRTSCTS);
#ifdef CIBAUD
    tio.c_cflag &= ~CIBAUD; /* clear separate input baud bits */
#endif
    tio.c_cflag |= (requested_flag | CS8 | CLOCAL | CREAD);

    if (!standard) {
        /* BOTHER requires explicit speed values. */
        tio.c_ispeed = baud;
        tio.c_ospeed = baud;
    }
    /* For standard Bxxxx, the kernel derives baud from c_cflag. */

    /* Blocking read until at least 1 byte, with no inter-byte timer. */
    tio.c_cc[VMIN] = 1;
    tio.c_cc[VTIME] = 0;

    if (uart_ioctl(fd, TCSETS2, &tio) < 0) {
        perror("TCSETS2");
        close(fd);
        return -1;
    }
    if (uart_ioctl(fd, TCGETS2, &tio) < 0) {
        perror("verify TCGETS2");
        close(fd);
        return -1;
    }

    unsigned int actual_flag = tio.c_cflag & CBAUD;
    printf("port=%s, requested=%u, mode=%s, 8N1\n",
           path, baud, standard ? "standard Bxxxx" : "custom BOTHER");
    printf("baud bits: requested=0x%x, readback=0x%x\n",
           requested_flag, actual_flag);
    printf("reported RX=%u, TX=%u\n", tio.c_ispeed, tio.c_ospeed);

    if (actual_flag != requested_flag) {
        fprintf(stderr, "ERROR: driver changed the baud-selection bits!\n");
        close(fd);
        return -1;
    }
    if (tio.c_ispeed != baud || tio.c_ospeed != baud) {
        fprintf(stderr,
                "WARNING: readback speed fields differ from requested %u.\n"
                "For standard Bxxxx, check c_cflag baud bits above;\n"
                "only hardware measurement can confirm actual line speed.\n", baud);
        if (!standard) {
            fprintf(stderr, "ERROR: custom BOTHER speed was not accepted.\n");
            close(fd);
            return -1;
        }
    }
    return fd;
}

static void make_frame(uint8_t frame[FRAME_SIZE], uint32_t seq)
{
    memcpy(frame, MAGIC, sizeof(MAGIC));
    for (unsigned int i = 0; i < 4; ++i)
        frame[8 + i] = (uint8_t)(seq >> (i * 8));
    for (size_t i = HEADER_SIZE; i < FRAME_SIZE; ++i)
        frame[i] = (uint8_t)(seq * 131u + (uint32_t)i * 17u);
}

static uint32_t get_seq(const uint8_t *frame)
{
    uint32_t n = 0;
    for (unsigned int i = 0; i < 4; ++i)
        n |= (uint32_t)frame[8 + i] << (8 * i);
    return n;
}

static int frame_valid(const uint8_t *frame, uint32_t seq)
{
    for (size_t i = HEADER_SIZE; i < FRAME_SIZE; ++i)
        if (frame[i] != (uint8_t)(seq * 131u + (uint32_t)i * 17u))
            return 0;
    return 1;
}

static unsigned int frame_period_ms(unsigned int baud)
{
    switch (baud) {
    case 115200:  return 103;
    case 921600:  return 16;
    case 1843200: return 10;
    case 3686400: return 7;
    case 7372800: return 5;
    default:
        /* 1024 bytes, 8N1: 10240 bits/frame, with a 20% margin. */
        return (unsigned int)((12288000ULL + baud - 1u) / baud);
    }
}

static void add_ms(struct timespec *time, unsigned int ms)
{
    time->tv_sec += (time_t)(ms / 1000u);
    time->tv_nsec += (long)(ms % 1000u) * 1000000L;
    if (time->tv_nsec >= 1000000000L) {
        ++time->tv_sec;
        time->tv_nsec -= 1000000000L;
    }
}

static int tx_loop(int fd, uint64_t count, unsigned int baud)
{
    uint8_t frame[FRAME_SIZE];
    uint64_t sent = 0;
    int rc = 0;
    const unsigned int period_ms = frame_period_ms(baud);

    printf("TX target frame period: %u ms (start-to-start)\n", period_ms);

    for (uint64_t f = 0; f < count && !stopping; ++f) {
        make_frame(frame, (uint32_t)f);
        struct timespec next_start;
        if (clock_gettime(CLOCK_MONOTONIC, &next_start) < 0) {
            perror("clock_gettime");
            rc = 1;
            goto out;
        }
        add_ms(&next_start, period_ms);

        size_t offset = 0;
        while (offset < sizeof(frame) && !stopping) {
            ssize_t n = write(fd, frame + offset, sizeof(frame) - offset);
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
            if (n <= 0) {
                if (n == 0) fprintf(stderr, "write returned 0\n");
                else perror("write");
                rc = 1;
                goto out;
            }
            offset += (size_t)n;
        }
        if (offset != sizeof(frame)) break;
        ++sent;
        if (sent % 1000 == 0)
            printf("TX: %" PRIu64 " frames (%" PRIu64 " bytes)\n",
                   sent, sent * FRAME_SIZE);

        if (f + 1u < count && !stopping) {
            /* Wait until this frame has physically left the UART. */
            if (uart_ioctl(fd, TCSBRK, 1) < 0) {
                perror("drain TX");
                rc = 1;
                goto out;
            }

            int err;
            do {
                err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
                                      &next_start, NULL);
            } while (err == EINTR && !stopping);
            if (err != 0 && err != EINTR) {
                fprintf(stderr, "clock_nanosleep: %s\n", strerror(err));
                rc = 1;
                goto out;
            }
        }
    }
out:
    /* Linux TCSBRK with nonzero arg drains queued TX before returning. */
    if (uart_ioctl(fd, TCSBRK, 1) < 0) perror("drain TX");
    printf("TX done: %" PRIu64 " frames (%" PRIu64 " bytes)\n",
           sent, sent * FRAME_SIZE);
    return rc;
}

static int rx_loop(int fd)
{
    uint8_t buf[RX_BUF_SIZE];
    size_t len = 0;
    uint64_t ok = 0, bad = 0, lost = 0, bytes = 0, skipped = 0;
    uint32_t last_seq = 0;
    int have_last = 0;
    int status = 0;

    printf("RX: receiving; press Ctrl+C for summary...\n");
    while (!stopping) {
        ssize_t n = read(fd, buf + len, sizeof(buf) - len);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("read");
            status = 1;
            break;
        }
        if (n == 0) continue;
        bytes += (uint64_t)n;
        len += (size_t)n;

        while (len >= sizeof(MAGIC)) {
            /* Search for the magic header; allow starting mid-frame. */
            size_t pos = 0;
            while (pos + sizeof(MAGIC) <= len &&
                   memcmp(buf + pos, MAGIC, sizeof(MAGIC)) != 0)
                ++pos;
            if (pos + sizeof(MAGIC) > len) {
                const size_t keep = sizeof(MAGIC) - 1;
                skipped += len - keep;
                memmove(buf, buf + len - keep, keep);
                len = keep;
                break;
            }
            if (pos != 0) {
                skipped += pos;
                memmove(buf, buf + pos, len - pos);
                len -= pos;
            }
            if (len < FRAME_SIZE) break;

            uint32_t seq = get_seq(buf);
            if (!frame_valid(buf, seq)) {
                ++bad;
                ++skipped;
                memmove(buf, buf + 1, --len);
                continue;
            }

            if (have_last && seq != (uint32_t)(last_seq + 1u)) {
                uint32_t gap = seq - (last_seq + 1u);
                if (gap < 0x80000000u) lost += gap;
                printf("RX seq discontinuity: last=%" PRIu32
                       ", now=%" PRIu32 "\n", last_seq, seq);
            }
            last_seq = seq;
            have_last = 1;
            ++ok;
            if (ok % 1000 == 0)
                printf("RX: ok=%" PRIu64 ", bad=%" PRIu64
                       ", missing=%" PRIu64 ", skipped=%" PRIu64 "\n",
                       ok, bad, lost, skipped);
            memmove(buf, buf + FRAME_SIZE, len - FRAME_SIZE);
            len -= FRAME_SIZE;
        }
    }
    printf("RX summary: bytes=%" PRIu64 ", valid_frames=%" PRIu64
           ", bad_frames=%" PRIu64 ", missing_frames=%" PRIu64
           ", skipped_bytes=%" PRIu64 ", trailing_bytes=%zu\n",
           bytes, ok, bad, lost, skipped, len);
    return status;
}

static void usage(const char *name)
{
    fprintf(stderr,
        "Usage: %s tx|rx DEVICE BAUD [TX_FRAMES]\n"
        "Standard examples:\n"
        "  %s rx /dev/ttyw01 115200\n"
        "  %s tx /dev/ttyw00 115200 10000\n"
        "  %s rx /dev/ttyw01 921600\n"
        "  %s tx /dev/ttyw00 921600 10000\n"
        "Custom BOTHER is also supported, e.g. 1843200, 3686400, 7372800.\n",
        name, name, name, name, name);
}

int main(int argc, char **argv)
{
    if (argc < 4 || argc > 5 ||
        (strcmp(argv[1], "tx") != 0 && strcmp(argv[1], "rx") != 0)) {
        usage(argv[0]);
        return 2;
    }

    char *end;
    errno = 0;
    unsigned long baud = strtoul(argv[3], &end, 10);
    if (errno || *end || baud == 0 || baud > UINT32_MAX) {
        usage(argv[0]);
        return 2;
    }
    uint64_t count = 10000;
    if (strcmp(argv[1], "tx") == 0 && argc == 5) {
        errno = 0;
        unsigned long long x = strtoull(argv[4], &end, 10);
        if (errno || *end || x == 0) {
            usage(argv[0]);
            return 2;
        }
        count = (uint64_t)x;
    } else if (strcmp(argv[1], "rx") == 0 && argc != 4) {
        usage(argv[0]);
        return 2;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = stop_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    int fd = setup_serial(argv[2], (unsigned int)baud);
    if (fd < 0) return 1;
    int rc = strcmp(argv[1], "tx") == 0
             ? tx_loop(fd, count, (unsigned int)baud)
             : rx_loop(fd);
    close(fd);
    return rc;
}
