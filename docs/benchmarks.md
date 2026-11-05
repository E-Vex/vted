# Benchmarks

vted measured on itself. The point of the page is to tell what to
expect from this editor — how fast it comes up, what it does with a
hundred megabytes of text, and what it holds in memory while it works
— and to keep those answers honest as the editor grows. Nothing here
lines it up against another editor: the numbers are vted's own, and
the walks that take them can be run anywhere the editor builds.

## How the numbers are taken

`make bench` builds the bench harness the way the release builds the
editor (`-O2`) and walks it over a file of about a hundred megabytes
of ordinary text lines, generated once and kept, so repeated runs are
comparable. It times the load and a save of the whole file, an insert
burst at the end of the last row, the undo of the burst, and reports
the peak resident set of the whole run.

`python3 bench/startup.py ./vted FILE 15` starts the editor on a
pseudo terminal (24 rows, 80 columns) the way `packaging/
smoke-test.py` does, and takes two clocks per run: the time from the
process starting to the first painted frame, and the time to a clean
exit after one Ctrl-Q. The median of the runs is the answer, and the
file is opened by its absolute path.

## The numbers (0.3.4, taken 2026-11-06)

| What                        | Result                                   |
| --------------------------- | ---------------------------------------- |
| startup, first frame        | 1.0 ms median (0.9–1.2), a 30 KB file open |
| startup, up and out         | 2.1 ms median (2.0–2.3)                  |
| load the 100 MB file        | 238 ms (421 MB/s, 2,016,493 rows)        |
| save it back whole          | 109 ms (919 MB/s)                        |
| insert burst, 12,400 bytes  | under 1 ms (156,847 bytes/ms)            |
| undo the burst, 200 steps   | under 1 ms                               |
| peak RSS over all of it     | 261,712 kB (about 255 MiB)               |
| the binary, `-O2`           | 205 KB, 158 KB of it text                |

The startup walk opens the README, so the config parsing, the syntax
coloring and the first frame all happen inside the number; the median
over 15 runs is reported with the spread in the parentheses. The
binary measured is the plain `make CFLAGS="-O2 ..."` one — the release
packages carry the same source linked against musl, statically.

## The machine the walks ran on

A shared virtual machine: two cores of an Intel Xeon, 4 GB of memory,
Linux 5.10, gcc 14.2.0. The numbers are one take on one day; run the
same walks on your own machine and read your own — that is what the
page is for.

## Reproducing

```sh
make bench
python3 bench/startup.py ./vted "$(pwd)/README.md" 15
```

`make bench` writes `/tmp/vted_bench_100mb.txt` on its first run and
reuses it after, so the load and save numbers stay comparable between
runs. Delete the file to generate it afresh.
