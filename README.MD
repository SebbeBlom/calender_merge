# Free Time Slot Finder

calender_merge is a **C program** that merges busy time intervals from one or more CSV files and outputs all available free time slots within a specified daily window. It supports filtering by minimum free slot duration.

Basically it is a neat tool for coordinating with other students in a group project.
---

## CSV Format Input

The CSV Format is specified per TimeEdit (which my University uses).

Each line in the CSV must contain **four fields**:
- `start_date`: YYYY-MM-DD  
- `start_time`: HH:MM  
- `end_date`: YYYY-MM-DD  
- `end_time`: HH:MM  

### Example CSV
So each line should contain this:
```csv
2025-09-06,09:00,2025-09-06,11:00
2025-09-06,13:30,2025-09-06,15:00
2025-09-07,10:00,2025-09-07,12:00
```

# Build
```
cc -o nob nob.c && ./nob
```

# Find all free slots in default window (00:00–24:00)
```
./freeslots cal1.csv
```

# Only consider free slots longer than 30 minutes
```
./freeslots -m 30 cal1.csv
```

# Limit search to working hours (09:00–17:00)
```
./freeslots -w 09:00-17:00 cal1.csv
```

# Merge multiple schedules
```
./freeslots -w 09:00-17:00 -m 15 cal1.csv cal2.csv cal3.csv
```