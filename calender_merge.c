#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_EVENTS 10000
#define MAX_MERGED_INTERVALS 1000
#define MAX_FILES 100
#define MAX_LINE_LENGTH 8192
#define MAX_FIELD_SIZE 32

#define MAX_DAYS_SPAN 3650
#define MAX_ITERATIONS_PER_DAY 1000

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
    Event data[MAX_EVENTS];
    size_t len;
} EventVec;

typedef struct {
    int intervals[MAX_MERGED_INTERVALS * 2];
    int count;
} MergedIntervals;

static EventVec g_events = {0};
static MergedIntervals g_merged = {0};

static int ev_push(EventVec *v, Event e) {
    assert(v != NULL && v->len < MAX_EVENTS);
    if (v->len >= MAX_EVENTS) return 0;
    v->data[v->len++] = e;
    return 1;
}

static int is_leap_year(int year) {
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

static int get_days_in_month(int year, int month) {
    static const int dpm[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (month == 2) ? dpm[1] + is_leap_year(year) : dpm[month - 1];
}

static int create_ymd_key(int y, int m, int d) {
    return y * 10000 + m * 100 + d;
}

static int add_days_to_date(Date *date, int days) {
    int iters = 0;
    int y = date->y, m = date->m, d = date->d + days;

    while (d > get_days_in_month(y, m) && iters++ < MAX_DAYS_SPAN) {
        d -= get_days_in_month(y, m);
        if (++m > 12) {
            m = 1;
            y++;
        }
    }
    while (d < 1 && iters++ < MAX_DAYS_SPAN) {
        if (--m < 1) {
            m = 12;
            y--;
        }
        d += get_days_in_month(y, m);
    }
    if (iters >= MAX_DAYS_SPAN) return 0;

    date->y = y;
    date->m = m;
    date->d = d;
    return 1;
}

static int parse_time_hhmm(const char *s) {
    if (!s || strlen(s) != 5 || s[2] != ':') return -1;
    if (!isdigit(s[0]) || !isdigit(s[1]) || !isdigit(s[3]) || !isdigit(s[4]))
        return -1;
    int h = (s[0] - '0') * 10 + (s[1] - '0');
    int m = (s[3] - '0') * 10 + (s[4] - '0');
    if (h > 24 || m > 59 || (h == 24 && m != 0)) return -1;
    return h * 60 + m;
}

static int parse_date_yyyy_mm_dd(const char *s, Date *out) {
    if (!s || strlen(s) != 10 || s[4] != '-' || s[7] != '-') return 0;
    int y = atoi((char[]){s[0], s[1], s[2], s[3], 0});
    int m = atoi((char[]){s[5], s[6], 0});
    int d = atoi((char[]){s[8], s[9], 0});
    if (m < 1 || m > 12 || d < 1 || d > 31) return 0;
    out->y = y;
    out->m = m;
    out->d = d;
    return 1;
}

static int parse_csv_fields(const char *line, char fields[][MAX_FIELD_SIZE],
                            int max_fields) {
    int count = 0, pos = 0, fpos = 0, in_q = 0;
    char c;
    while ((c = line[pos]) && count < max_fields && pos < MAX_LINE_LENGTH) {
        if (!in_q && fpos == 0 && c == '"') {
            in_q = 1;
            pos++;
            continue;
        }
        if (in_q) {
            if (c == '"' && line[pos + 1] == '"') {
                fields[count][fpos++] = '"';
                pos += 2;
            } else if (c == '"') {
                in_q = 0;
                pos++;
            } else {
                if (fpos < MAX_FIELD_SIZE - 1) fields[count][fpos++] = c;
                pos++;
            }
        } else if (c == ',' || c == '\n' || c == '\r') {
            fields[count][fpos] = '\0';
            count++;
            fpos = 0;
            if (c == '\r' && line[pos + 1] == '\n') pos++;
            pos++;
            if (c != ',') continue;
        } else {
            if (fpos < MAX_FIELD_SIZE - 1) fields[count][fpos++] = c;
            pos++;
        }
    }
    if (fpos > 0 && count < max_fields) {
        fields[count][fpos] = '\0';
        count++;
    }
    return count;
}

static int compare_events(const void *a, const void *b) {
    const Event *ea = a, *eb = b;
    if (ea->ymd != eb->ymd) return ea->ymd - eb->ymd;
    if (ea->start_min != eb->start_min) return ea->start_min - eb->start_min;
    return ea->end_min - eb->end_min;
}

static void push_interval(int *count, int start, int end) {
    if (*count < MAX_MERGED_INTERVALS) {
        g_merged.intervals[(*count) * 2] = start;
        g_merged.intervals[(*count) * 2 + 1] = end;
        (*count)++;
    }
}

static int merge_day_intervals(int ymd, int start, int end) {
    int cur_s = -1, cur_e = -1, has = 0, count = 0;
    g_merged.count = 0;

    for (int i = 0; i < (int)g_events.len && i < MAX_ITERATIONS_PER_DAY; i++) {
        Event *ev = &g_events.data[i];
        if (ev->ymd != ymd) continue;
        int s = ev->start_min < start ? start : ev->start_min;
        int e = ev->end_min > end ? end : ev->end_min;
        if (e <= start || s >= end || s >= e) continue;

        if (!has) {
            cur_s = s;
            cur_e = e;
            has = 1;
        } else if (s <= cur_e)
            cur_e = (e > cur_e ? e : cur_e);
        else {
            push_interval(&count, cur_s, cur_e);
            cur_s = s;
            cur_e = e;
        }
    }
    if (has) push_interval(&count, cur_s, cur_e);
    return g_merged.count = count;
}

static void print_slot(int ymd, int s, int e) {
    int y = ymd / 10000, m = (ymd / 100) % 100, d = ymd % 100;
    printf("%04d-%02d-%02d   %02d:%02d   %02d:%02d   %d\n", y, m, d, s / 60,
           s % 60, e / 60, e % 60, e - s);
}

static void print_free_slots(int ymd, int start, int end, int minlen) {
    int last = start;
    for (int i = 0; i < g_merged.count; i++) {
        int s = g_merged.intervals[i * 2], e = g_merged.intervals[i * 2 + 1];
        if (s > last && s - last >= minlen) print_slot(ymd, last, s);
        if (e > last) last = e;
    }
    if (end > last && end - last >= minlen) print_slot(ymd, last, end);
}

static int process_event_span(Date sdate, Date edate, int st, int et) {
    Date cur = sdate;
    int days = 0;
    while (days++ < MAX_DAYS_SPAN) {
        int ymd = create_ymd_key(cur.y, cur.m, cur.d);
        int cs =
            (cur.y == sdate.y && cur.m == sdate.m && cur.d == sdate.d) ? st : 0;
        int ce = (cur.y == edate.y && cur.m == edate.m && cur.d == edate.d)
                     ? et
                     : 24 * 60;
        if (ce < cs) ce = cs;
        if (!ev_push(&g_events, (Event){ymd, cs, ce})) return 0;
        if (cur.y == edate.y && cur.m == edate.m && cur.d == edate.d) break;
        if (!add_days_to_date(&cur, 1)) return 0;
    }
    return 1;
}

static int process_csv_file(const char *fname) {
    FILE *f = fopen(fname, "r");
    char buf[MAX_LINE_LENGTH];
    char fields[4][MAX_FIELD_SIZE];
    if (!f) return 0;
    while (fgets(buf, sizeof(buf), f)) {
        if (parse_csv_fields(buf, fields, 4) < 4) continue;
        Date sd, ed;
        if (!parse_date_yyyy_mm_dd(fields[0], &sd) ||
            !parse_date_yyyy_mm_dd(fields[2], &ed))
            continue;
        int st = parse_time_hhmm(fields[1]), et = parse_time_hhmm(fields[3]);
        if (st < 0 || et < 0) continue;
        if (!process_event_span(sd, ed, st, et)) {
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return 1;
}

static void print_usage(const char *prog) {
    fprintf(
        stderr,
        "Usage: %s [-w HH:MM-HH:MM] [-m MINUTES] file1.csv [file2.csv ...]\n\n"
        "Merges busy times across all CSVs and prints free time slots.\n\n"
        "Options:\n"
        "  -w HH:MM-HH:MM  Daily window (default 00:00-24:00)\n"
        "  -m MINUTES      Minimum free slot length (default 0)\n",
        prog);
}

static int parse_window_argument(const char *arg, int *s, int *e) {
    char st[6] = {0}, et[6] = {0};
    const char *dash = strchr(arg, '-');
    if (!dash || dash - arg != 5 || strlen(dash + 1) != 5) return 0;
    memcpy(st, arg, 5);
    memcpy(et, dash + 1, 5);
    *s = parse_time_hhmm(st);
    *e = parse_time_hhmm(et);
    return (*s >= 0 && *e >= 0 && *s <= *e);
}

int main(int argc, char *argv[]) {
    int start = DEFAULT_WINDOW_START_MIN, end = DEFAULT_WINDOW_END_MIN,
        minlen = DEFAULT_MIN_SLOT_MIN;
    int i = 1, files = 0;

    while (i < argc && argv[i][0] == '-') {
        if (!strcmp(argv[i], "-w") && i + 1 < argc) {
            if (!parse_window_argument(argv[i + 1], &start, &end)) {
                fprintf(stderr, "Invalid -w\n");
                return 1;
            }
            i += 2;
        } else if (!strcmp(argv[i], "-m") && i + 1 < argc) {
            minlen = atoi(argv[i + 1]);
            if (minlen < 0) minlen = 0;
            i += 2;
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }
    if (i >= argc) {
        print_usage(argv[0]);
        return 1;
    }

    for (; i < argc && files < MAX_FILES; i++, files++)
        if (!process_csv_file(argv[i])) {
            fprintf(stderr, "Error file %s\n", argv[i]);
            return 1;
        }

    printf("Date         Start   End     Durmation(min)\n");
    printf("-------------------------------------------\n");
    if (!g_events.len) return 0;

    qsort(g_events.data, g_events.len, sizeof(Event), compare_events);

    for (size_t idx = 0; idx < g_events.len;) {
        int ymd = g_events.data[idx].ymd;
        merge_day_intervals(ymd, start, end);
        print_free_slots(ymd, start, end, minlen);
        while (idx < g_events.len && g_events.data[idx].ymd == ymd) idx++;
    }
    return 0;
}
