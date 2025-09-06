# Example usage:

# Build
```
cc -o nob nob.c && ./nob
```

# Run (defaults: window 00:00–24:00, no min length)
```
./free_slots data/cal1.csv data/cal2.csv > free_slots.csv
```

# With a working window and minimum slot length (e.g., ≥ 30 min)
```
./free_slots -w 08:15-17:00 -m 30 cal1.csv cal2.csv cal3.csv > free_slots.csv
```