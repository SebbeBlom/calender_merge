#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_EVENTS 1000
#define MAX_LINE_LENGTH 1024
#define MAX_FIELD_LENGTH 32

typedef struct {
    int year;
    int month;
    int day;
} Date;

typedef struct {
    Date date;
    int start_minutes;
    int end_minutes;
} Event;

typedef struct {
    Event *events;
    int count;
    int capacity;
} EventList;

typedef struct {
    int window_start_minutes;
    int window_end_minutes;
    int minimum_slot_minutes;
} Config;

EventList *create_event_list() {
    EventList *list = malloc(sizeof(EventList));
    list->events = malloc(sizeof(Event) * 10);
    list->count = 0;
    list->capacity = 10;
    return list;
}

void add_event(EventList *list, Event event) {
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->events = realloc(list->events, sizeof(Event) * list->capacity);
    }
    list->events[list->count] = event;
    list->count++;
}

void free_event_list(EventList *list) {
    free(list->events);
    free(list);
}

int is_leap_year(int year) {
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

int days_in_month(int year, int month) {
    int days_per_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return days_per_month[month - 1];
}

int compare_dates(Date date1, Date date2) {
    if (date1.year != date2.year) {
        return date1.year - date2.year;
    }
    if (date1.month != date2.month) {
        return date1.month - date2.month;
    }
    return date1.day - date2.day;
}

Date add_days_to_date(Date date, int days) {
    Date result = date;
    result.day += days;

    while (result.day > days_in_month(result.year, result.month)) {
        result.day -= days_in_month(result.year, result.month);
        result.month++;

        if (result.month > 12) {
            result.month = 1;
            result.year++;
        }
    }

    while (result.day < 1) {
        result.month--;
        if (result.month < 1) {
            result.month = 12;
            result.year--;
        }
        result.day += days_in_month(result.year, result.month);
    }

    return result;
}

int parse_time_to_minutes(const char *time_str) {
    if (strlen(time_str) != 5 || time_str[2] != ':') {
        return -1;
    }

    if (!isdigit(time_str[0]) || !isdigit(time_str[1]) ||
        !isdigit(time_str[3]) || !isdigit(time_str[4])) {
        return -1;
    }

    int hours = (time_str[0] - '0') * 10 + (time_str[1] - '0');
    int minutes = (time_str[3] - '0') * 10 + (time_str[4] - '0');

    if (hours > 24 || minutes > 59 || (hours == 24 && minutes != 0)) {
        return -1;
    }

    return hours * 60 + minutes;
}

int parse_date(const char *date_str, Date *date) {
    if (strlen(date_str) != 10 || date_str[4] != '-' || date_str[7] != '-') {
        return 0;
    }

    char year_str[5] = {date_str[0], date_str[1], date_str[2], date_str[3], 0};
    char month_str[3] = {date_str[5], date_str[6], 0};
    char day_str[3] = {date_str[8], date_str[9], 0};

    date->year = atoi(year_str);
    date->month = atoi(month_str);
    date->day = atoi(day_str);

    if (date->month < 1 || date->month > 12 || date->day < 1 ||
        date->day > 31) {
        return 0;
    }

    return 1;
}

int parse_csv_line(const char *line, char fields[][MAX_FIELD_LENGTH],
                   int max_fields) {
    int field_count = 0;
    int field_pos = 0;
    int line_pos = 0;

    while (line[line_pos] && field_count < max_fields) {
        char c = line[line_pos];

        if (c == ',' || c == '\n' || c == '\r' || c == '\0') {
            fields[field_count][field_pos] = '\0';
            field_count++;
            field_pos = 0;

            if (c == '\r' && line[line_pos + 1] == '\n') {
                line_pos++;
            }

            if (c == '\n' || c == '\r') {
                break;
            }
        } else {
            if (field_pos < MAX_FIELD_LENGTH - 1) {
                fields[field_count][field_pos] = c;
                field_pos++;
            }
        }
        line_pos++;
    }

    if (field_pos > 0 && field_count < max_fields) {
        fields[field_count][field_pos] = '\0';
        field_count++;
    }

    return field_count;
}

void process_multi_day_event(EventList *event_list, Date start_date,
                             Date end_date, int start_minutes,
                             int end_minutes) {
    Date current_date = start_date;

    while (compare_dates(current_date, end_date) <= 0) {
        Event daily_event;
        daily_event.date = current_date;

        if (compare_dates(current_date, start_date) == 0) {
            daily_event.start_minutes = start_minutes;
        } else {
            daily_event.start_minutes = 0;
        }

        if (compare_dates(current_date, end_date) == 0) {
            daily_event.end_minutes = end_minutes;
        } else {
            daily_event.end_minutes = 24 * 60;
        }

        if (daily_event.end_minutes > daily_event.start_minutes) {
            add_event(event_list, daily_event);
        }

        current_date = add_days_to_date(current_date, 1);
    }
}

int load_events_from_csv(const char *filename, EventList *event_list) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Cannot open file: %s\n", filename);
        return 0;
    }

    char line[MAX_LINE_LENGTH];
    int line_number = 0;

    while (fgets(line, sizeof(line), file)) {
        line_number++;

        char fields[4][MAX_FIELD_LENGTH];
        int field_count = parse_csv_line(line, fields, 4);

        if (field_count < 4) {
            printf("Skipping line %d: insufficient fields\n", line_number);
            continue;
        }

        Date start_date, end_date;
        if (!parse_date(fields[0], &start_date)) {
            printf("Skipping line %d: invalid start date '%s'\n", line_number,
                   fields[0]);
            continue;
        }

        if (!parse_date(fields[2], &end_date)) {
            printf("Skipping line %d: invalid end date '%s'\n", line_number,
                   fields[2]);
            continue;
        }

        int start_minutes = parse_time_to_minutes(fields[1]);
        int end_minutes = parse_time_to_minutes(fields[3]);

        if (start_minutes < 0) {
            printf("Skipping line %d: invalid start time '%s'\n", line_number,
                   fields[1]);
            continue;
        }

        if (end_minutes < 0) {
            printf("Skipping line %d: invalid end time '%s'\n", line_number,
                   fields[3]);
            continue;
        }

        process_multi_day_event(event_list, start_date, end_date, start_minutes,
                                end_minutes);
    }

    fclose(file);
    return 1;
}

int compare_events(const void *a, const void *b) {
    const Event *event1 = (const Event *)a;
    const Event *event2 = (const Event *)b;

    int date_comparison = compare_dates(event1->date, event2->date);
    if (date_comparison != 0) {
        return date_comparison;
    }

    if (event1->start_minutes != event2->start_minutes) {
        return event1->start_minutes - event2->start_minutes;
    }

    return event1->end_minutes - event2->end_minutes;
}

void merge_intervals_for_day(Event *day_events, int event_count,
                             int window_start, int window_end,
                             int *merged_starts, int *merged_ends,
                             int *merged_count) {
    *merged_count = 0;

    if (event_count == 0) {
        return;
    }

    int current_start = day_events[0].start_minutes;
    int current_end = day_events[0].end_minutes;

    if (current_start < window_start) current_start = window_start;
    if (current_end > window_end) current_end = window_end;

    if (current_end <= window_start || current_start >= window_end) {
        current_start = -1;
    }

    for (int i = 1; i < event_count; i++) {
        int next_start = day_events[i].start_minutes;
        int next_end = day_events[i].end_minutes;

        if (next_start < window_start) next_start = window_start;
        if (next_end > window_end) next_end = window_end;

        if (next_end <= window_start || next_start >= window_end) {
            continue;
        }

        if (current_start == -1) {
            current_start = next_start;
            current_end = next_end;
        } else if (next_start <= current_end) {
            if (next_end > current_end) {
                current_end = next_end;
            }
        } else {
            merged_starts[*merged_count] = current_start;
            merged_ends[*merged_count] = current_end;
            (*merged_count)++;

            current_start = next_start;
            current_end = next_end;
        }
    }

    if (current_start != -1) {
        merged_starts[*merged_count] = current_start;
        merged_ends[*merged_count] = current_end;
        (*merged_count)++;
    }
}

void print_time_slot(Date date, int start_minutes, int end_minutes) {
    int start_hours = start_minutes / 60;
    int start_mins = start_minutes % 60;
    int end_hours = end_minutes / 60;
    int end_mins = end_minutes % 60;
    int duration = end_minutes - start_minutes;

    printf("%04d-%02d-%02d   %02d:%02d   %02d:%02d   %d\n", date.year,
           date.month, date.day, start_hours, start_mins, end_hours, end_mins,
           duration);
}

void print_free_slots_for_day(Date date, Event *day_events, int event_count,
                              Config config) {
    int merged_starts[100];
    int merged_ends[100];
    int merged_count;

    merge_intervals_for_day(
        day_events, event_count, config.window_start_minutes,
        config.window_end_minutes, merged_starts, merged_ends, &merged_count);

    int last_end = config.window_start_minutes;

    for (int i = 0; i < merged_count; i++) {
        int gap_start = last_end;
        int gap_end = merged_starts[i];

        if (gap_end - gap_start >= config.minimum_slot_minutes) {
            print_time_slot(date, gap_start, gap_end);
        }

        last_end = merged_ends[i];
    }

    if (config.window_end_minutes - last_end >= config.minimum_slot_minutes) {
        print_time_slot(date, last_end, config.window_end_minutes);
    }
}

int parse_window_argument(const char *arg, int *start_minutes,
                          int *end_minutes) {
    const char *dash = strchr(arg, '-');
    if (!dash) {
        return 0;
    }

    char start_str[6] = {0};
    char end_str[6] = {0};

    int start_len = dash - arg;
    if (start_len != 5) {
        return 0;
    }

    if (strlen(dash + 1) != 5) {
        return 0;
    }

    strncpy(start_str, arg, 5);
    strcpy(end_str, dash + 1);

    *start_minutes = parse_time_to_minutes(start_str);
    *end_minutes = parse_time_to_minutes(end_str);

    return (*start_minutes >= 0 && *end_minutes >= 0 &&
            *start_minutes <= *end_minutes);
}

void print_usage(const char *program_name) {
    fprintf(
        stderr,
        "Usage: %s [-w HH:MM-HH:MM] [-m MINUTES] file1.csv [file2.csv ...]\n\n"
        "Finds free time slots by analyzing busy times from CSV files.\n\n"
        "CSV format: start_date,start_time,end_date,end_time\n"
        "Date format: YYYY-MM-DD\n"
        "Time format: HH:MM\n\n"
        "Options:\n"
        "  -w HH:MM-HH:MM  Daily time window (default: 00:00-24:00)\n"
        "  -m MINUTES      Minimum free slot length in minutes (default: 0)\n\n"
        "Examples:\n"
        "  %s calendar.csv\n"
        "  %s -w 09:00-17:00 -m 30 cal1.csv cal2.csv\n",
        program_name, program_name, program_name);
}

int main(int argc, char *argv[]) {
    Config config = {.window_start_minutes = 0,
                     .window_end_minutes = 24 * 60,
                     .minimum_slot_minutes = 0};

    int arg_index = 1;
    while (arg_index < argc && argv[arg_index][0] == '-') {
        if (strcmp(argv[arg_index], "-w") == 0) {
            if (arg_index + 1 >= argc) {
                fprintf(stderr, "Error: -w option requires an argument\n");
                print_usage(argv[0]);
                return 1;
            }

            if (!parse_window_argument(argv[arg_index + 1],
                                       &config.window_start_minutes,
                                       &config.window_end_minutes)) {
                fprintf(stderr,
                        "Error: Invalid window format. Use HH:MM-HH:MM\n");
                return 1;
            }
            arg_index += 2;

        } else if (strcmp(argv[arg_index], "-m") == 0) {
            if (arg_index + 1 >= argc) {
                fprintf(stderr, "Error: -m option requires an argument\n");
                print_usage(argv[0]);
                return 1;
            }

            config.minimum_slot_minutes = atoi(argv[arg_index + 1]);
            if (config.minimum_slot_minutes < 0) {
                config.minimum_slot_minutes = 0;
            }
            arg_index += 2;

        } else {
            fprintf(stderr, "Error: Unknown option %s\n", argv[arg_index]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (arg_index >= argc) {
        fprintf(stderr, "Error: No CSV files specified\n");
        print_usage(argv[0]);
        return 1;
    }

    EventList *event_list = create_event_list();

    for (int i = arg_index; i < argc; i++) {
        printf("Loading events from: %s\n", argv[i]);
        if (!load_events_from_csv(argv[i], event_list)) {
            fprintf(stderr, "Error loading file: %s\n", argv[i]);
            free_event_list(event_list);
            return 1;
        }
    }

    printf("Loaded %d events total\n\n", event_list->count);

    if (event_list->count == 0) {
        printf("No events found. All time is free!\n");
        free_event_list(event_list);
        return 0;
    }

    qsort(event_list->events, event_list->count, sizeof(Event), compare_events);

    printf("Free Time Slots:\n");
    printf("Date         Start   End     Duration(min)\n");
    printf("-------------------------------------------\n");

    int current_event_index = 0;
    while (current_event_index < event_list->count) {
        Date current_date = event_list->events[current_event_index].date;

        int day_event_count = 0;
        while (
            current_event_index + day_event_count < event_list->count &&
            compare_dates(
                event_list->events[current_event_index + day_event_count].date,
                current_date) == 0) {
            day_event_count++;
        }

        print_free_slots_for_day(current_date,
                                 &event_list->events[current_event_index],
                                 day_event_count, config);

        current_event_index += day_event_count;
    }

    free_event_list(event_list);
    return 0;
}