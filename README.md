# memtrack
A simple memory leak detector tool. On program exit it displays the memory usage by line and filename of each original malloc call.

Usage:

1. Run make clean, make all, and sudo make install.

2. Run any program as such: LD_PRELOAD=libmemtrack [program command as usual]

3. On program exit by Ctrl+C or normal termination, a list of memory usage in kB will appear by file, line number. If there are no debug symbols, the hexadecimal location will be displayed instead along with the binary file.

Note: you can also use the environment variable, MEMCHECK_THRESHOLD, to show values of higher or lower amounts. The value is in bytes, and by default is akin to: LD_PRELOAD=libmemtrack MEMCHECK_THRESHOLD=1024 [program command as usual]
