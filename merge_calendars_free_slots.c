#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_WINDOW_START_MIN 0
#define DEFAULT_WINDOW_END_MIN (24 * 60)
#define DEFAULT_MIN_SLOT_MIN 0

typedef struct {
    int y, m, d;
} Date;

typedef struct {
    int ymd;
    int start_min;
    int end_min;
} Event;

typedef struct {
    Event *data;
    size_t len;
    size_t cap;
} EventVec;

static void ev_push(EventVec *v, Event e) {
    if (v->len == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 256;
        v->data = (Event *)realloc(v->data, v->cap * sizeof(Event));
        if (!v->data) {
            fprintf(stderr, "Out of memory\n");
            exit(1);
        }
    }
    v->data[v->len++] = e;
}

static int is_leap(int y) {
    return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}
static int days_in_month(int y, int m) {
    static const int dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2) return dim[m - 1] + is_leap(y);
    return dim[m - 1];
}

static int ymd_key(int y, int m, int d) { return y * 10000 + m * 100 + d; }

static void add_days(Date *dt, int delta) {
    int y = dt->y, m = dt->m, d = dt->d + delta;
    while (d > days_in_month(y, m)) {
        d -= days_in_month(y, m);
        if (++m > 12) {
            m = 1;
            y++;
        }
    }
    while (d < 1) {
        if (--m < 1) {
            m = 12;
            y--;
        }
        d += days_in_month(y, m);
    }
    dt->y = y;
    dt->m = m;
    dt->d = d;
}

static int parse_hhmm(const char *s) {
    if (!isdigit((unsigned char)s[0]) || !isdigit((unsigned char)s[1]) ||
        s[2] != ':' || !isdigit((unsigned char)s[3]) ||
        !isdigit((unsigned char)s[4]))
        return -1;
    int h = (s[0] - '0') * 10 + (s[1] - '0');
    int m = (s[3] - '0') * 10 + (s[4] - '0');
    if (h < 0 || h > 24 || m < 0 || m > 59) return -1;
    if (h == 24 && m != 0) return -1;
    return h * 60 + m;
}

static int parse_yyyy_mm_dd(const char *s, Date *out) {
    if (!(isdigit((unsigned char)s[0]) && isdigit((unsigned char)s[1]) &&
          isdigit((unsigned char)s[2]) && isdigit((unsigned char)s[3]) &&
          s[4] == '-' && isdigit((unsigned char)s[5]) &&
          isdigit((unsigned char)s[6]) && s[7] == '-' &&
          isdigit((unsigned char)s[8]) && isdigit((unsigned char)s[9])))
        return 0;
    int y = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 +
            (s[3] - '0');
    int m = (s[5] - '0') * 10 + (s[6] - '0');
    int d = (s[8] - '0') * 10 + (s[9] - '0');
    if (m < 1 || m > 12) return 0;
    if (d < 1 || d > 31) return 0;
    out->y = y;
    out->m = m;
    out->d = d;
    return 1;
}

static int csv_first_n_fields(const char *line, int max_fields, char **buffers,
                              size_t *buf_sizes) {
    int count = 0;
    int in_quotes = 0;
    const char *p = line;
    while (*p && count < max_fields) {
        char *out = buffers[count];
        size_t cap = buf_sizes[count];
        size_t len = 0;
        in_quotes = 0;
        if (*p == '"') {
            in_quotes = 1;
            p++;
        }
        while (*p) {
            if (in_quotes) {
                if (*p == '"') {
                    if (p[1] == '"') {
                        if (len + 1 < cap) out[len++] = '"';
                        p += 2;
                    } else {
                        p++;
                        in_quotes = 0;
                        break;
                    }
                } else {
                    if (len + 1 < cap) out[len++] = *p;
                    p++;
                }
            } else {
                if (*p == ',') {
                    p++;
                    break;
                }
                if (*p == '\r' || *p == '\n') {
                    break;
                }
                if (len + 1 < cap) out[len++] = *p;
                p++;
            }
        }
        out[len] = '\0';
        count++;

        if (in_quotes) {
            while (*p && *p != ',') {
                if (*p == '\n' || *p == '\r') break;
                p++;
            }
            if (*p == ',') p++;
        }

        if (*p == '\r') {
            p++;
            if (*p == '\n') p++;
            break;
        }
        if (*p == '\n') {
            p++;
            break;
        }
    }
    return count;
}

static int cmp_event(const void *a, const void *b) {
    const Event *ea = (const Event *)a, *eb = (const Event *)b;
    if (ea->ymd != eb->ymd) return ea->ymd - eb->ymd;
    if (ea->start_min != eb->start_min) return ea->start_min - eb->start_min;
    return ea->end_min - eb->end_min;
}

static void usage(const char *prog) {
    fprintf(
        stderr,
        "Usage: %s [-w HH:MM-HH:MM] [-m MINUTES] file1.csv [file2.csv ...]\n"
        "\n"
        "Merges busy times across all CSVs (TimeEdit-like format) and prints\n"
        "the time slots when EVERYONE is free within the chosen daily window.\n"
        "\n"
        "Options:\n"
        "  -w HH:MM-HH:MM  Daily window to consider (default 00:00-24:00).\n"
        "  -m MINUTES      Minimum free-slot length to report (default 0).\n",
        prog);
}

int main(int argc, char **argv) {
    int day_start_min = DEFAULT_WINDOW_START_MIN;
    int day_end_min = DEFAULT_WINDOW_END_MIN;
    int min_slot_min = DEFAULT_MIN_SLOT_MIN;

    int argi = 1;
    while (argi < argc && argv[argi][0] == '-') {
        if (strcmp(argv[argi], "-w") == 0) {
            if (argi + 1 >= argc) {
                usage(argv[0]);
                return 1;
            }
            const char *w = argv[++argi];
            const char *dash = strchr(w, '-');
            if (!dash) {
                fprintf(stderr, "Bad -w format. Use HH:MM-HH:MM\n");
                return 1;
            }
            char a[6] = {0}, b[6] = {0};
            if ((dash - w) != 5 || strlen(dash + 1) != 5) {
                fprintf(stderr, "Bad -w format. Use HH:MM-HH:MM\n");
                return 1;
            }
            memcpy(a, w, 5);
            memcpy(b, dash + 1, 5);
            int s = parse_hhmm(a), e = parse_hhmm(b);
            if (s < 0 || e < 0 || s > e) {
                fprintf(stderr, "Invalid window times\n");
                return 1;
            }
            day_start_min = s;
            day_end_min = e;
            argi++;
            continue;
        } else if (strcmp(argv[argi], "-m") == 0) {
            if (argi + 1 >= argc) {
                usage(argv[0]);
                return 1;
            }
            min_slot_min = atoi(argv[++argi]);
            if (min_slot_min < 0) min_slot_min = 0;
            argi++;
            continue;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (argi >= argc) {
        usage(argv[0]);
        return 1;
    }

    EventVec ev = {0};

    for (; argi < argc; ++argi) {
        const char *fname = argv[argi];
        FILE *f = fopen(fname, "r");
        if (!f) {
            fprintf(stderr, "Cannot open %s\n", fname);
            return 1;
        }
        char line[8192];
        while (fgets(line, sizeof(line), f)) {
            Date sd = {0}, ed = {0};
            char *fields[4];
            char b0[32], b1[32], b2[32], b3[32];
            size_t sz[4] = {sizeof(b0), sizeof(b1), sizeof(b2), sizeof(b3)};
            fields[0] = b0;
            fields[1] = b1;
            fields[2] = b2;
            fields[3] = b3;
            int got = csv_first_n_fields(line, 4, fields, sz);
            if (got < 4) continue;
            if (!parse_yyyy_mm_dd(fields[0], &sd)) continue;
            if (!parse_yyyy_mm_dd(fields[2], &ed)) continue;
            int st = parse_hhmm(fields[1]);
            int et = parse_hhmm(fields[3]);
            if (st < 0 || et < 0) continue;

            Date cur = sd;
            while (1) {
                int cur_ymd = ymd_key(cur.y, cur.m, cur.d);
                int cur_start =
                    (cur.y == sd.y && cur.m == sd.m && cur.d == sd.d) ? st : 0;
                int cur_end = (cur.y == ed.y && cur.m == ed.m && cur.d == ed.d)
                                  ? et
                                  : 24 * 60;
                if (cur_end < cur_start) cur_end = cur_start;

                Event e = {
                    .ymd = cur_ymd, .start_min = cur_start, .end_min = cur_end};
                ev_push(&ev, e);

                if (cur.y == ed.y && cur.m == ed.m && cur.d == ed.d) break;
                add_days(&cur, 1);
            }
        }
        fclose(f);
    }

    if (ev.len == 0) {
        printf("Date,Start,End,Duration_min\n");
        return 0;
    }

    qsort(ev.data, ev.len, sizeof(Event), cmp_event);

    printf("Date,Start,End,Duration_min\n");

    size_t i = 0;
    while (i < ev.len) {
        int ymd = ev.data[i].ymd;

        int merged_cap = 16, merged_len = 0;
        int *ms = (int *)malloc(sizeof(int) * merged_cap * 2);
        if (!ms) {
            fprintf(stderr, "Out of memory\n");
            return 1;
        }

        int cur_s = -1, cur_e = -1;
        int have_cur = 0;

        for (; i < ev.len && ev.data[i].ymd == ymd; ++i) {
            int s = ev.data[i].start_min;
            int e = ev.data[i].end_min;

            if (e <= day_start_min || s >= day_end_min) continue;
            if (s < day_start_min) s = day_start_min;
            if (e > day_end_min) e = day_end_min;
            if (s >= e) continue;

            if (!have_cur) {
                cur_s = s;
                cur_e = e;
                have_cur = 1;
            } else if (s <= cur_e) {
                if (e > cur_e) cur_e = e;
            } else {
                if (merged_len + 1 >= merged_cap) {
                    merged_cap *= 2;
                    ms = (int *)realloc(ms, sizeof(int) * merged_cap * 2);
                    if (!ms) {
                        fprintf(stderr, "OOM\n");
                        exit(1);
                    }
                }
                ms[2 * merged_len] = cur_s;
                ms[2 * merged_len + 1] = cur_e;
                merged_len++;
                cur_s = s;
                cur_e = e;
            }
        }
        if (have_cur) {
            if (merged_len + 1 >= merged_cap) {
                merged_cap *= 2;
                ms = (int *)realloc(ms, sizeof(int) * merged_cap * 2);
                if (!ms) {
                    fprintf(stderr, "OOM\n");
                    exit(1);
                }
            }
            ms[2 * merged_len] = cur_s;
            ms[2 * merged_len + 1] = cur_e;
            merged_len++;
        }

        int last = day_start_min;
        for (int k = 0; k < merged_len; k++) {
            int s = ms[2 * k], e = ms[2 * k + 1];
            if (s > last) {
                int dur = s - last;
                if (dur >= min_slot_min) {
                    int Y = ymd / 10000;
                    int M = (ymd / 100) % 100;
                    int D = ymd % 100;
                    printf("%04d-%02d-%02d,%02d:%02d,%02d:%02d,%d\n", Y, M, D,
                           last / 60, last % 60, s / 60, s % 60, dur);
                }
            }
            if (e > last) last = e;
        }
        if (last < day_end_min) {
            int dur = day_end_min - last;
            if (dur >= min_slot_min) {
                int Y = ymd / 10000;
                int M = (ymd / 100) % 100;
                int D = ymd % 100;
                printf("%04d-%02d-%02d,%02d:%02d,%02d:%02d,%d\n", Y, M, D,
                       last / 60, last % 60, day_end_min / 60, day_end_min % 60,
                       dur);
            }
        }

        free(ms);
    }

    free(ev.data);
    return 0;
}
