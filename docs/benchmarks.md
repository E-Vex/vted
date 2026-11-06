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
process starting to the first painted frame — told from the setup
escapes by waiting for the bar of the opening view, or the welcome
banner of an empty buffer, so the open of the file is inside the
number — and the time to a clean exit after one Ctrl-Q. The median
of the runs is the answer, and the file is opened by its absolute
path.

`python3 bench/scroll.py ./vted FILE 3 30` starts the editor the same
way on the hundred-megabyte file and takes four clocks per run: the
first painted frame — told from the setup escapes by waiting for the
bar of the opening view, so the load of the whole file is inside it —
a go to line a screenful short of the million, timed to the frame
that carries the bar of the deep line, and thirty Page Downs and
thirty Page Ups from there, the walk crossing the million-line
boundary so the gutter grows a digit inside it, each page one key
timed from its write to the last byte of the frame it drew.

`python3 bench/search.py ./vted FILE 3` types two queries into the
search in one burst, the clock running from the write of the query to
the frame that carries the count in the bar, so the incremental
searches of its prefixes are inside the number: the seven-digit
suffix of the last line — one match, the deepest row of the file,
the search wrapping the whole buffer to land it — and "walrus", a
word the file never carries. The search keeps its query, so the
prompt is cleared with backspaces between the two, outside the
clock.

`python3 bench/wrap.py ./vted 3 30` starts the editor the same way
on a file the walk generates once and keeps — sixty-four rows,
sixteen kilobytes of words each, no line feed inside a row, so a row
wraps into a couple of hundred screen lines the way a line of
minified text or a CSV export does — and takes four clocks per run:
the first painted frame, one Ctrl-L timed to the frame that carries
the "Wrap on" in the bar, and thirty Page Downs and thirty Page Ups
through the wrapped layout, each page one key timed from its write
to the last byte of the frame it drew.

## The numbers (0.3.4, taken 2026-11-06)

| What                        | Result                                   |
| --------------------------- | ---------------------------------------- |
| startup, first frame        | 102.9 ms median (102.7–106.5), a 30 KB file open |
| startup, up and out         | 104.0 ms median (103.8–107.7)            |
| load the 100 MB file        | 345 ms (290 MB/s, 2,016,493 rows)        |
| save it back whole          | 105 ms (955 MB/s)                        |
| insert burst, 12,400 bytes  | under 1 ms (173,502 bytes/ms)            |
| undo the burst, 200 steps   | under 1 ms                               |
| peak RSS over all of it     | 261,680 kB (about 255 MiB)               |
| the binary, `-O2`           | 204 KB, 156 KB of it text                |
| the 100 MB file, first frame | 285.8 ms median (280.9–373.6), the load inside it |
| go to line, deep in it      | 0.2 ms median (0.2–0.3)                  |
| page down, a viewportful    | 0.2 ms median (0.1–0.4) over 90 pages    |
| page up, a viewportful      | 0.2 ms median (0.2–0.4) over 90 pages    |
| a search hit, one burst     | 214.1 ms median (205.1–214.9)            |
| a search miss, one burst    | 410.3 ms median (320.6–414.8)            |
| wrap on, the long row laid out | 3.0 ms median (3.0–3.0)               |
| page down, wrapped          | 9.6 ms median (4.1–16.9) over 90 pages   |
| page up, wrapped            | 9.4 ms median (3.3–15.9) over 90 pages   |

The startup walk opens the README, so the config parsing, the syntax
coloring and the first frame all happen inside the number; the median
over 15 runs is reported with the spread in the parentheses. The
first frame waits out the editor's idle read before it is drawn — no
key is typed at the editor while it comes up, so the 100 ms read
timeout is nearly all of the 103 ms, the work of the frame itself
the 3 ms left over — which is when the editor shows itself to
whoever launched it and has not typed yet. The binary measured is
the plain `make CFLAGS="-O2 ..."` one — the release
packages carry the same source linked against musl, statically.

The scroll and search walks run on the same hundred-megabyte file:
the pages cross the million-line boundary, so the gutter grows a
digit inside the walk — the page that grew it is the 0.4 ms max of
the page-down spread — and the search numbers carry the incremental
searches of every prefix of the query, the way the prompt runs them
while the query is typed.

The wrap walk runs on the file of its own: the pages cross three
row boundaries inside the walk and spend most of their keys deep in
a tall row, where every screen line the frame paints is laid out
from the row's own start — the deeper into a row the page sits, the
wider its cost, and the page whose viewport sits deepest inside a
single row is the 16.9 ms max of the page-down spread.

## The machine the walks ran on

A shared virtual machine: two cores of an Intel Xeon, 4 GB of memory,
Linux 5.10, gcc 14.2.0. The numbers are one take on one day; run the
same walks on your own machine and read your own — that is what the
page is for.

## Reproducing

```sh
make bench
python3 bench/startup.py ./vted "$(pwd)/README.md" 15
python3 bench/scroll.py ./vted /tmp/vted_bench_100mb.txt 3 30
python3 bench/search.py ./vted /tmp/vted_bench_100mb.txt 3
python3 bench/wrap.py ./vted 3 30
```

`make bench` writes `/tmp/vted_bench_100mb.txt` on its first run and
reuses it after, so the load and save numbers stay comparable between
runs. Delete the file to generate it afresh. The wrap walk keeps
`/tmp/vted_bench_longlines.txt` the same way.
