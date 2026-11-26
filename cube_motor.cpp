#include <gpiod.h>
#include <sched.h>
#include <time.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static const int    FULL_STEPS_PER_REV = 200;
static const int    MICROSTEPS         = 8;
static const double GEAR_RATIO         = 1.0;

static const int    START_SPS   = 400;
static const int    MAX_SPS     = 1500;
static const int    ACCEL_STEPS = 100;

static const char*  EN_PIN = "GPIO22";

struct MotorPins { char face; const char* step; const char* dir; };
static const MotorPins PINS[] = {
    {'U', "GPIO17", "GPIO27"},
    {'D', "GPIO23", "GPIO24"},
    {'F', "GPIO5",  "GPIO6"},
    {'B', "GPIO13", "GPIO19"},
    {'L', "GPIO26", "GPIO16"},
    {'R', "GPIO20", "GPIO21"},
};

struct Pin {
    gpiod_line_request* req = nullptr;
    unsigned int offset = 0;
    void set(int v) {
        gpiod_line_request_set_value(req, offset,
            v ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
    }
};

static Pin openPin(const char* spec, int initial) {
    gpiod_chip* chip = nullptr;
    int off = -1;

    const char* colon = strchr(spec, ':');
    if (colon) {
        std::string path = "/dev/gpiochip" + std::string(spec, colon - spec);
        chip = gpiod_chip_open(path.c_str());
        off = atoi(colon + 1);
    } else {
        for (int i = 0; i < 16 && !chip; i++) {
            std::string path = "/dev/gpiochip" + std::to_string(i);
            gpiod_chip* c = gpiod_chip_open(path.c_str());
            if (!c) continue;
            int o = gpiod_chip_get_line_offset_from_name(c, spec);
            if (o >= 0) { chip = c; off = o; }
            else gpiod_chip_close(c);
        }
    }
    if (!chip || off < 0) {
        fprintf(stderr, "Cannot find GPIO '%s'. Check names with: gpioinfo | grep -i gpio\n", spec);
        exit(1);
    }

    gpiod_line_settings* s = gpiod_line_settings_new();
    gpiod_line_settings_set_direction(s, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_output_value(s,
        initial ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
    gpiod_line_config* lc = gpiod_line_config_new();
    unsigned int offs[1] = { (unsigned int)off };
    gpiod_line_config_add_line_settings(lc, offs, 1, s);
    gpiod_request_config* rc = gpiod_request_config_new();
    gpiod_request_config_set_consumer(rc, "cube");

    Pin p;
    p.req = gpiod_chip_request_lines(chip, rc, lc);
    p.offset = (unsigned int)off;
    gpiod_request_config_free(rc);
    gpiod_line_config_free(lc);
    gpiod_line_settings_free(s);
    gpiod_chip_close(chip);
    if (!p.req) {
        fprintf(stderr, "Failed to request '%s' (run with sudo? pin in use?)\n", spec);
        exit(1);
    }
    return p;
}

static void sleepUntil(timespec& t, long ns) {
    t.tv_nsec += ns;
    while (t.tv_nsec >= 1000000000L) { t.tv_nsec -= 1000000000L; t.tv_sec++; }
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, nullptr);
}

class Motor {
public:
    char face;
    Motor(const MotorPins& p) : face(p.face) {
        step_ = openPin(p.step, 0);
        dir_  = openPin(p.dir, 0);
    }
    void move(long steps, bool cw) {
        dir_.set(cw ? 1 : 0);
        timespec ts{0, 5000}; nanosleep(&ts, nullptr);

        timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
        for (long i = 0; i < steps; i++) {
            long toEnd = steps - i;
            long ramp  = (i < toEnd) ? i : toEnd;
            double sps = (ramp < ACCEL_STEPS)
                ? START_SPS + (double)(MAX_SPS - START_SPS) * ramp / ACCEL_STEPS
                : MAX_SPS;
            long period_ns = (long)(1e9 / sps);

            step_.set(1);
            sleepUntil(t, period_ns / 2);
            step_.set(0);
            sleepUntil(t, period_ns - period_ns / 2);
        }
    }
private:
    Pin step_, dir_;
};

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "Usage: %s U F2 R- ...\n", argv[0]); return 1; }

    sched_param sp{}; sp.sched_priority = 80;
    if (sched_setscheduler(0, SCHED_FIFO, &sp) < 0)
        fprintf(stderr, "Warning: no realtime scheduling (run with sudo)\n");

    Pin en = openPin(EN_PIN, 1);
    std::vector<Motor> motors;
    for (auto& p : PINS) motors.emplace_back(p);

    const long stepsPerQuarter =
        (long)(FULL_STEPS_PER_REV * MICROSTEPS * GEAR_RATIO / 4.0 + 0.5);

    en.set(0);
    for (int a = 1; a < argc; a++) {
        std::string m = argv[a];
        char face = m[0];
        int quarters = 1; bool cw = true;
        if (m.size() > 1) {
            if (m[1] == '2') quarters = 2;
            else if (m[1] == '-' || m[1] == '\'') cw = false;
        }
        bool found = false;
        for (auto& mo : motors) if (mo.face == face) {
            printf("%s: %d x 90deg %s (%ld steps)\n", m.c_str(), quarters,
                   cw ? "CW" : "CCW", stepsPerQuarter * quarters);
            mo.move(stepsPerQuarter * quarters, cw);
            found = true;
        }
        if (!found) fprintf(stderr, "Unknown face '%c'\n", face);
        timespec ts{0, 150 * 1000000L}; nanosleep(&ts, nullptr);
    }
    en.set(1);
    return 0;
}
