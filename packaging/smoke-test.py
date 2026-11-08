#!/usr/bin/env python3
# smoke-test.py — run vted in a pseudo terminal, quit it with Ctrl-Q,
# and require a clean exit. The editor refuses to run without a real
# terminal, which makes a plain shell test awkward; python's pty
# module gives us exactly the terminal we need.
#
# A second run walks the word completion the way a hand would: two
# files open as buffers, a half word typed into the second, Alt-/
# listing the words the first one offers — each entry of the menu
# naming the buffer it came from — another Alt-/ stepping down the
# list the way dabbrev-expand does in Emacs, Enter taking the word
# and Ctrl-S saving it. The file left on disk is the proof the whole
# interactive path worked.
#
# A third run walks the regex search the way a hand would: Ctrl-F
# opening the search, Ctrl-T reading the query as a pattern, a bad
# one telling so in the bar, a good one landing the cursor and the
# count telling its place, Ctrl-R filling the escapes of a
# replacement with the groups of the match, and the file saved to
# disk carrying the proof the whole path worked.
#
#   python3 packaging/smoke-test.py ./vted
#   python3 packaging/smoke-test.py dist/vted_0.3.3_amd64.deb-extracted
#
# A fourth run walks the replace-all the way a hand would: Ctrl-R
# asking about one match at a time, n leaving the first, a taking the
# second and the third with it in the same pass — no ask for the
# third, no frame per match — and the file saved to disk carrying
# the proof the pass laid the rest in at once.
#
# A round-trip run walks the answers and the undo of them the way a
# hand would: y taking the first match, q stopping at the second, one
# Ctrl-Z taking the one replacement back; then the instant pass on
# the prompts as they stand, n leaving the first and a taking the
# second and the third in one pass, two Ctrl-Z walking the pass back
# one occurrence at a time and a third saying Nothing to undo — the
# file saved to disk at every stop of the way, carrying the whole
# journey out and back.
#
# A mouse run walks the double click the way a hand would: two presses
# of the left button inside the double click time, the word under them
# selected, a letter typed over it taking the word in its place, and
# the file saved to disk carrying the proof the click landed where
# the frame draws it. Another pastes with the middle button what the
# last copy kept: the word copied with Ctrl-C, the paste laid in at
# the far end of the row below, the selection giving way first the
# way every paste replaces one, and the bar saying what came in.
#
# A path run walks the Tab completion the way a hand would: Ctrl-O
# opening the prompt, a prefix with no answer told so in the bar, the
# directory of a fresh tree completed with its slash, a lone file
# landing whole, and the menu of several matches stepped down with an
# arrow and taken with Enter — the file on disk and the bar saying
# the open landed where the completion walked. A save-as run walks
# the other side of it: text typed into an unnamed buffer, Ctrl-S
# asking where, the directory completed the same way, and the file
# written to disk carrying the proof.
#
# A gf run walks the open of the file under the cursor the way a hand
# would: a file named at the head of another's first line opened with
# Alt-o, the name standing beside the file being edited rather than
# in the working directory, the open handed back to the first buffer
# when its own name is taken from the second, and a name that is no
# file at all told so in the bar.
#
# A history run walks the search history the way a hand would: two
# queries searched and kept, the search reopened on the last of them,
# an up arrow recalling the one before it — its matches lighting up
# the way a typed query's do — and a down arrow walking back to the
# newest, Enter keeping the recalled one.
#
# An electric run walks the bracket pairs the way a hand would: a
# paren typed at the end of a row closing itself right away, an x
# going inside it, the closer typed over the closer already there
# stepping over instead of doubling it, backspaces walking the whole
# thing back out — the empty pair going as one — and Alt-( turning
# the electricity off, a bracket typed after it standing alone.
#
# A wrap run walks the selection the pairs wrap the way a hand would:
# a word picked with a double click, a bracket typed over it wrapping
# it in its pair instead of taking it out, a letter typed over the
# wrap landing between the halves, and Ctrl-Z walking the whole thing
# back out with the pair as one step.
#
# A quote run walks the quote wrap the way a hand would: a word picked
# with a double click, a double quote typed over it wrapping it in its
# own pair — the same byte both halves — a single quote doing the
# same, Ctrl-Z walking each back out as one step, and Alt-( turning
# the electricity off so a quote typed over a word takes it out the
# way any letter does.
#
# A jump run walks the :line:column tails the way a hand would: a name
# carrying one under the cursor, Alt-o opening the file it names with
# the cursor landing on the spot the tail says — the bar counting the
# line and the column — and a tail on a name already open landing
# there just the same.
#
# A goto run walks the Ctrl-G prompt the way a hand would: a line with
# a :column tail behind it landing on the spot it names, a bare line
# landing the start of the line the way it always did, a line past the
# end clamped to the last one, an answer that spells no line told so
# in the bar, and ESC taking the prompt down with the cursor standing.
#
# A goto completion run walks the Alt-/ of the goto prompt the way a
# hand would: a lone name landing whole behind its directory, several
# landing the stem they agree on with the menu standing and a take
# from it landing behind the name part, the tail behind the name
# standing through a round and a take — the open landing on the spot
# it spells — and a prefix nothing answers told so in the bar.
#
# A browse run walks the directory browse the way a hand would: Alt-o
# on a directory opening the browse of it — the name standing beside
# the file being edited — the size and the time of a name said behind
# it the way ls lays its own columns, a take from the menu opening the
# file under the selection, an empty directory saying so with
# Backspace going up and the name walked from standing selected, a
# walk in from the listing above, a left click taking the line drawn
# under it, ESC cancelling with the editor standing where it was, and
# a name that is no regular file still told so in the bar.
#
# A prompt run walks the Alt-o of the message bar the way a hand
# would: a search whose query names a file with a :line:column tail
# opened with Alt-o and landing on the spot it says, a query that
# names no file told so in a notice while the search stands, the
# notice answered by the next key, and the replace's own prompt
# opening the same way with the bar saying what opened.
#
# A match run walks the quote match the way a hand would: the cursor
# landed on the quote that opens a span with the far end of it
# lighting up the way a bracket's partner does, Alt-% landing there
# with the bar counting the spot, the cursor resting past the closer
# still lighting the opener, and an escaped quote finding no partner
# at all — Alt-% saying so in the bar.
import os
import pty
import select
import struct
import sys
import termios
import fcntl
import time

CTRL_Q = b'\x11'
CTRL_S = b'\x13'
CTRL_C = b'\x03'
CTRL_F = b'\x06'
CTRL_R = b'\x12'
CTRL_T = b'\x14'
CTRL_O = b'\x0f'    # Ctrl-O: the open prompt
CTRL_G = b'\x07'    # Ctrl-G: the goto prompt
CTRL_Z = b'\x1a'
BACKSPACE = b'\x7f'
ENTER = b'\r'
ALT_DOT = b'\x1b.'    # Alt-.: the buffer after the current one
ALT_SLASH = b'\x1b/'  # Alt-/: complete the word left of the cursor
ESC = b'\x1b'         # ESC: cancel the prompt
ALT_O = b'\x1bo'    # Alt-o: the file the cursor sits on
ALT_LPAREN = b'\x1b('  # Alt-(: the electric bracket pairs on or off
ALT_PERCENT = b'\x1b%'  # Alt-%: the jump to the lit partner
END = b'\x1b[F'      # End: the cursor at the row's end

SMOKE = '/tmp/vted-smoke.txt'
WORDS = '/tmp/vted-smoke-words.txt'  # the buffer the answers come from
NOTE = '/tmp/vted-smoke-note.txt'    # the buffer the word is typed into
DATE = '/tmp/vted-smoke-date.txt'    # the buffer the pattern runs in
CONFIRM = '/tmp/vted-smoke-confirm.txt'  # the buffer the answers walk in
ALL = '/tmp/vted-smoke-all.txt'      # the buffer the one a takes in
ROUNDTRIP = '/tmp/vted-smoke-roundtrip.txt'  # the buffer the pass goes out and back in
DBLCLICK = '/tmp/vted-smoke-dblclick.txt'  # the buffer the clicks land in
MIDDLE = '/tmp/vted-smoke-middle.txt'  # the buffer the paste lands in
PATH_TREE = '/tmp/vted-smoke-path'      # the tree the Tab completion walks
PATH_SAVE = '/tmp/vted-smoke-saveas'  # the tree the save-as completion walks
GF_TREE = '/tmp/vted-smoke-gf'      # the tree the Alt-o walk opens in
JUMP_TREE = '/tmp/vted-smoke-jump'  # the tree the :line:column walk opens in
HISTORY = '/tmp/vted-smoke-history.txt'  # the buffer the queries walk
ELECTRIC = '/tmp/vted-smoke-electric.txt'  # the buffer the pairs land in
WRAP = '/tmp/vted-smoke-wrap.txt'  # the buffer the wrap wraps in
QUOTE = '/tmp/vted-smoke-quote.txt'  # the buffer the quotes wrap in
GOTO = '/tmp/vted-smoke-goto.txt'  # the buffer the jumps land in
GOTO_TREE = '/tmp/vted-smoke-goto-tree'  # the tree the goto's Alt-/ walks
BROWSE_TREE = '/tmp/vted-smoke-browse'  # the tree the Alt-o browse walks
PROMPT_TREE = '/tmp/vted-smoke-prompt'  # the tree the prompt's Alt-o opens in
MATCH = '/tmp/vted-smoke-match.txt'  # the buffer the partners light in


def spawn(binary, args):
    """the editor on a pseudo terminal of 24 rows and 80 columns"""
    pid, fd = pty.fork()
    if pid == 0:  # child: run the editor, far from anything it might touch
        os.chdir('/')
        os.execv(binary, ['vted'] + args)
        os._exit(127)

    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))
    return pid, fd


def drive(fd, keys, out, pause=0.25):
    """one round of keystrokes, then whatever frames they drew,
    drained into out until pause seconds passed without them"""
    try:
        os.write(fd, keys)
    except OSError:
        pass  # the editor may have left already; its exit status says

    deadline = time.time() + pause
    while time.time() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.05)
        if not ready:
            continue
        try:
            chunk = os.read(fd, 65536)
        except OSError:
            return
        if not chunk:
            return
        out.append(chunk)


def wait_exit(pid, fd, timeout=10):
    """the editor's exit status once it left on its own, or None when
    it had to be killed after timeout seconds"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        waited, status = os.waitpid(pid, os.WNOHANG)
        if waited:
            os.close(fd)
            return status
        time.sleep(0.05)

    os.kill(pid, 9)
    os.waitpid(pid, 0)
    os.close(fd)
    return None


def fail(message):
    print(f'smoke: FAIL - {message}', file=sys.stderr)
    return 1


def quit_smoke(binary):
    """the plain run: start, open a file, quit cleanly"""
    with open(SMOKE, 'w') as f:
        f.write('vted smoke test\n')

    pid, fd = spawn(binary, [SMOKE])
    time.sleep(0.3)
    for _ in range(3):  # Ctrl-Q three times: modified buffer guard
        drive(fd, CTRL_Q, [], pause=0.1)

    status = wait_exit(pid, fd)
    if status is None:
        return fail('the editor did not quit within 10s')
    if os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0:
        print('smoke: OK - started, opened a file and quit cleanly')
        return 0
    return fail(f'exit status {status:#x}')


def completion_smoke(binary):
    """the interactive run: a word completed from another buffer,
    stepped onto and saved"""
    with open(WORDS, 'w') as f:
        f.write('alpha alphabet\n')
    with open(NOTE, 'w') as f:
        f.write('\nx\n')  # an empty row to type on, one word off the way

    pid, fd = spawn(binary, [WORDS, NOTE])
    out = []
    time.sleep(0.3)       # the first frame, in the words buffer
    drive(fd, ALT_DOT, out)   # Alt-.: into the note buffer
    drive(fd, b'al', out)     # the half word to complete
    drive(fd, ALT_SLASH, out)  # Alt-/: the menu, its words named
    drive(fd, ALT_SLASH, out)  # Alt-/ again: step down the list
    drive(fd, b'\r', out)     # Enter: take the stepped-to word
    drive(fd, CTRL_S, out)    # Ctrl-S: save the note
    for _ in range(3):
        drive(fd, CTRL_Q, out, pause=0.1)

    status = wait_exit(pid, fd)
    if status is None:
        return fail('the editor did not quit within 10s')
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return fail(f'exit status {status:#x}')

    drew = b''.join(out)

    # the menu said where each answer came from: the words of the
    # other buffer, drawn with its name behind them
    for word in (b'alpha', b'alphabet'):
        if word + b' - ' + WORDS.encode() not in drew:
            return fail(f"the menu did not draw '{word.decode()}' "
                        f"named after the words buffer")

    # the second Alt-/ stepped onto the far word: Enter took it and
    # the saved note carries it
    with open(NOTE) as f:
        note = f.read()
    if note != 'alphabet\nx\n':
        return fail(f'the note reads {note!r}, '
                    'not the word the step landed on')

    print('smoke: OK - completed a word from another buffer, '
          'stepped down and saved')
    return 0


def regex_smoke(binary):
    """the regex run: a pattern searched with, a group replaced with,
    and the text it became saved to disk"""
    with open(DATE, 'w') as f:
        f.write('date 2026-11-05\n')

    pid, fd = spawn(binary, [DATE])
    out = []
    time.sleep(0.3)
    drive(fd, CTRL_F, out)          # the find prompt
    drive(fd, CTRL_T, out)          # Ctrl-T: read the query as a pattern
    drive(fd, b'a(', out)           # a pattern that cannot stand
    drive(fd, BACKSPACE * 2, out)   # take it apart again
    drive(fd, b'([0-9]+)-([0-9]+)-([0-9]+)', out)  # a pattern that can
    drive(fd, ENTER, out)           # Enter: keep the match it found
    drive(fd, CTRL_R, out)          # Ctrl-R: the query stands already
    drive(fd, ENTER, out)           # Enter: replace by the pattern
    drive(fd, b'\\2/\\3/\\1', out)   # the groups written where they go
    drive(fd, ENTER, out)           # Enter: lay it in
    drive(fd, b'a', out)            # a: take the match, the rest without asking
    drive(fd, CTRL_S, out)          # Ctrl-S: save the date
    for _ in range(3):
        drive(fd, CTRL_Q, out, pause=0.1)

    status = wait_exit(pid, fd)
    if status is None:
        return fail('the editor did not quit within 10s')
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return fail(f'exit status {status:#x}')

    drew = b''.join(out)

    # the prompt said what it reads: the bad pattern told its trouble
    # in the bar, the dash before the compiler's own words
    if b'Find (regex): a( \xe2\x80\x94 ' not in drew:
        return fail('the search did not tell the bad pattern')

    if b'1 of 1' not in drew:
        return fail('the pattern did not say where its match stands')

    if b'Replaced 1 occurrence' not in drew:
        return fail('the replace did not say what it laid in')

    # the groups of the match were written where their escapes stood
    with open(DATE) as f:
        date = f.read()
    if date != 'date 11/05/2026\n':
        return fail(f'the date reads {date!r}, '
                    'not the groups of the match written out')

    print('smoke: OK - searched a pattern, replaced by its groups '
          'and saved')
    return 0


def confirm_smoke(binary):
    """the confirmed run: a replace that asks about one match at a
    time, n leaving one, y taking the next, q stopping at the last,
    and the file saved to disk carrying the proof the answers landed
    where they were given"""
    with open(CONFIRM, 'w') as f:
        f.write('one red fox, two red hens, three red cars\n')

    pid, fd = spawn(binary, [CONFIRM])
    out = []
    time.sleep(0.3)
    drive(fd, CTRL_R, out)          # Ctrl-R: the replace prompt
    drive(fd, b'red', out)          # the query
    drive(fd, ENTER, out)           # Enter: the query stands
    drive(fd, b'green', out)        # the answer
    drive(fd, ENTER, out)           # Enter: the walk starts
    drive(fd, b'n', out)            # n: the first stays
    drive(fd, b'y', out)            # y: the second goes green
    drive(fd, b'q', out)            # q: the third is never asked in
    drive(fd, CTRL_S, out)          # Ctrl-S: save what was answered
    for _ in range(3):
        drive(fd, CTRL_Q, out, pause=0.1)

    status = wait_exit(pid, fd)
    if status is None:
        return fail('the editor did not quit within 10s')
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return fail(f'exit status {status:#x}')

    drew = b''.join(out)

    # the bar asked about each of them in turn, the count telling
    # where the match stood
    for ask in (b'Replace: red (1 of 3', b'Replace: red (2 of 3',
                 b'Replace: red (3 of 3'):
        if ask not in drew:
            return fail(f'the walk never asked {ask!r}')

    if b'Replaced 1 occurrence' not in drew:
        return fail('the answers did not say what they laid in')

    # the answers landed where they were given: the first kept, the
    # second taken, the third left alone by the stop
    with open(CONFIRM) as f:
        text = f.read()
    if text != 'one red fox, two green hens, three red cars\n':
        return fail(f'the text reads {text!r}, not the answers laid in')

    print('smoke: OK - answered a replace one match at a time and saved')
    return 0


def all_smoke(binary):
    """the replace-all run: n leaving the first of three, a taking the
    second and — the escape hatch of it — the third in the same pass,
    no ask for it and no frame per match, and the file saved to disk
    carrying the proof the pass laid the rest in at once"""
    with open(ALL, 'w') as f:
        f.write('one red fox, two red hens, three red cars\n')

    pid, fd = spawn(binary, [ALL])
    out = []
    time.sleep(0.3)
    drive(fd, CTRL_R, out)          # Ctrl-R: the replace prompt
    drive(fd, b'red', out)          # the query
    drive(fd, ENTER, out)           # Enter: the query stands
    drive(fd, b'green', out)        # the answer
    drive(fd, ENTER, out)           # Enter: the walk starts, the first asked
    drive(fd, b'n', out)            # n: the first stays, the second asked
    drive(fd, b'a', out)            # a: the second goes, the third with it
    drive(fd, CTRL_S, out)          # Ctrl-S: save what was answered
    for _ in range(3):
        drive(fd, CTRL_Q, out, pause=0.1)

    status = wait_exit(pid, fd)
    if status is None:
        return fail('the editor did not quit within 10s')
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return fail(f'exit status {status:#x}')

    drew = b''.join(out)

    # the walk asked about the first two; the third went in unasked,
    # the pass it was taken in never drawing an ask of its own
    for ask in (b'Replace: red (1 of 3', b'Replace: red (2 of 3'):
        if ask not in drew:
            return fail(f'the walk never asked {ask!r}')

    if b'Replace: red (3 of 3' in drew:
        return fail('the a answer stepped the rest instead of '
                    'taking them in one pass')

    if b'Replaced 2 occurrences' not in drew:
        return fail('the pass did not say what it laid in')

    # the answers landed where they were given: the first kept, the
    # second and the third taken by the one a
    with open(ALL) as f:
        text = f.read()
    if text != 'one red fox, two green hens, three green cars\n':
        return fail(f'the text reads {text!r}, not the pass laid in')

    print('smoke: OK - took the rest of the replace with one a '
          'and saved')
    return 0


def roundtrip_smoke(binary):
    """the round-trip run: y and q laying one replacement in, undo
    taking it back, then the instant pass — n leaving the first, a
    taking the second and the third in one pass — walked back by two
    undos one occurrence at a time, the file saved to disk carrying
    the proof at every stop of the way"""
    with open(ROUNDTRIP, 'w') as f:
        f.write('one red fox, two red hens, three red cars\n')

    pid, fd = spawn(binary, [ROUNDTRIP])
    out = []
    landings = []  # what the file on disk read at every stop of the way
    time.sleep(0.3)

    def land():
        """Ctrl-S saves what stands in the buffer, and the file on
        disk is read back: the landing the round trip makes there"""
        drive(fd, CTRL_S, out)
        with open(ROUNDTRIP) as f:
            landings.append(f.read())

    # the first walk: y takes the first match, q stops at the second
    drive(fd, CTRL_R, out)          # Ctrl-R: the replace prompt
    drive(fd, b'red', out)          # the query
    drive(fd, ENTER, out)           # Enter: the query stands
    drive(fd, b'green', out)        # the answer
    drive(fd, ENTER, out)           # Enter: the walk starts
    drive(fd, b'y', out)            # y: the first goes green
    drive(fd, b'q', out)            # q: the second is left alone
    land()                          # one green fox, the rest red

    drive(fd, CTRL_Z, out)          # Ctrl-Z: the one replacement comes back
    land()                          # the file back where it began

    # the second walk, on the prompts as they stand: the instant pass
    drive(fd, CTRL_R, out)          # Ctrl-R: the query stands already
    drive(fd, ENTER, out)           # Enter: 'red' keeps its place
    drive(fd, ENTER, out)           # Enter: 'green' too, the walk starts
    drive(fd, b'n', out)            # n: the first stays
    drive(fd, b'a', out)            # a: the second and the third in one pass
    land()                          # the hens and the cars green, the fox red

    drive(fd, CTRL_Z, out)          # Ctrl-Z: the third comes back first
    land()                          # the cars red again

    drive(fd, CTRL_Z, out)          # Ctrl-Z: the second comes back too
    land()                          # the whole pass walked back

    drive(fd, CTRL_Z, out)          # Ctrl-Z: nothing left to walk back
    for _ in range(3):
        drive(fd, CTRL_Q, out, pause=0.1)

    status = wait_exit(pid, fd)
    if status is None:
        return fail('the editor did not quit within 10s')
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return fail(f'exit status {status:#x}')

    drew = b''.join(out)

    # the walks asked about their first two; the pass took the third
    # without an ask of its own
    for ask in (b'Replace: red (1 of 3', b'Replace: red (2 of 3'):
        if ask not in drew:
            return fail(f'the walk never asked {ask!r}')

    if b'Replace: red (3 of 3' in drew:
        return fail('the a answer stepped the rest instead of '
                    'taking them in one pass')

    for said in (b'Replaced 1 occurrence', b'Replaced 2 occurrences'):
        if said not in drew:
            return fail(f'the bar never said {said!r}')

    if b'Nothing to undo' not in drew:
        return fail('the history was not walked back whole')

    # the landings: out through the answers and the pass, back one
    # occurrence at a time, ending where the round trip began
    want = ['one green fox, two red hens, three red cars\n',
            'one red fox, two red hens, three red cars\n',
            'one red fox, two green hens, three green cars\n',
            'one red fox, two green hens, three red cars\n',
            'one red fox, two red hens, three red cars\n']
    if landings != want:
        return fail(f'the landings read {landings!r}, '
                    'not the round trip out and back')

    print('smoke: OK - walked the instant pass out with one a '
          'and back with two undos')
    return 0


def dblclick_smoke(binary):
    """the double click run: two presses inside the double click time
    select the word under them, a letter typed over it takes the word,
    and the file saved to disk carrying the proof the click landed
    where the frame draws it"""
    with open(DBLCLICK, 'w') as f:
        f.write('one two three\nfour five six\n')

    pid, fd = spawn(binary, [DBLCLICK])
    out = []
    time.sleep(0.3)
    # 'w' of "two" is drawn at terminal column 10, row 1: the gutter
    # of a two-row file is four cells, and the w is the fifth text
    # cell of the first row. The three reports go in one write, so
    # the second press lands inside the double click time of the first
    drive(fd, b'\x1b[<0;10;1M\x1b[<0;10;1m\x1b[<0;10;1M', out)
    drive(fd, b'X', out)     # typing over the selection takes the word
    drive(fd, CTRL_S, out)   # Ctrl-S: save what the click selected
    for _ in range(3):
        drive(fd, CTRL_Q, out, pause=0.1)

    status = wait_exit(pid, fd)
    if status is None:
        return fail('the editor did not quit within 10s')
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return fail(f'exit status {status:#x}')

    # the word under the click is what the typing replaced: the
    # second row never touched, the first carrying the one letter
    with open(DBLCLICK) as f:
        text = f.read()
    if text != 'one X three\nfour five six\n':
        return fail(f'the text reads {text!r}, '
                    'not the word the double click took')

    print('smoke: OK - selected a word with a double click '
          'and typed over it')
    return 0


def middleclick_smoke(binary):
    """the middle click run: the word double-clicked and copied, the
    middle button pasting it at the click — the selection giving way
    first, the way every paste replaces one — and the file saved to
    disk carrying the proof"""
    with open(MIDDLE, 'w') as f:
        f.write('one two three\nfour five six\n')

    pid, fd = spawn(binary, [MIDDLE])
    out = []
    time.sleep(0.3)
    # the double click on 'w' of "two" as before, then Ctrl-C: the
    # copy goes out to the terminal and one is kept
    drive(fd, b'\x1b[<0;10;1M\x1b[<0;10;1m\x1b[<0;10;1M', out)
    drive(fd, CTRL_C, out)
    # the middle button at terminal column 71, row 2: past the last
    # glyph of "four five six", so the paste lands after "six". The
    # press and its release in one write
    drive(fd, b'\x1b[<1;71;2M\x1b[<1;71;2m', out)
    drive(fd, CTRL_S, out)
    for _ in range(3):
        drive(fd, CTRL_Q, out, pause=0.1)

    status = wait_exit(pid, fd)
    if status is None:
        return fail('the editor did not quit within 10s')
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return fail(f'exit status {status:#x}')

    drew = b''.join(out)

    # the bar said what the paste laid in
    if b'Pasted 3 bytes' not in drew:
        return fail('the bar did not say what the paste laid in')

    # the word came back at the click: the selection gave way first —
    # "two" left the first row — and the paste landed after "six"
    with open(MIDDLE) as f:
        text = f.read()
    if text != 'one  three\nfour five sixtwo\n':
        return fail(f'the text reads {text!r}, '
                    'not the paste laid in at the click')

    print('smoke: OK - copied a double-clicked word, '
          'pasted it with the middle button')
    return 0


def path_smoke(binary):
    """the path run: a prefix with no answer told so, a directory
    completed with its slash, a lone file landed whole, and the menu
    of several matches stepped down and taken with Enter"""
    import shutil

    shutil.rmtree(PATH_TREE, ignore_errors=True)
    os.makedirs(os.path.join(PATH_TREE, 'sub'))
    with open(os.path.join(PATH_TREE, 'alpha.txt'), 'w') as f:
        f.write('alpha text\n')
    with open(os.path.join(PATH_TREE, 'answer.txt'), 'w') as f:
        f.write('answer text\n')

    pid, fd = spawn(binary, [])
    out = []
    time.sleep(0.3)

    # a prefix nothing answers: the bar says so, the answer stands
    drive(fd, CTRL_O, out)
    drive(fd, (PATH_TREE + '/zz').encode(), out)
    drive(fd, b'\t', out)
    drive(fd, ESC, out)
    drew = b''.join(out)

    if b"No match for" not in drew:
        return fail('the bar did not tell the prefix with no answer')

    # the directory alone completes with its slash, and the lone file
    # behind it lands whole
    out.clear()
    drive(fd, CTRL_O, out)
    drive(fd, PATH_TREE.encode(), out)
    drive(fd, b'\t', out)
    drew = b''.join(out)

    if (PATH_TREE + '/').encode() not in drew:
        return fail('the directory did not complete with its slash')

    out.clear()
    drive(fd, b'al', out)
    drive(fd, b'\t', out)
    drive(fd, ENTER, out)          # Enter: open what completed
    drive(fd, CTRL_S, out)         # Ctrl-S: save it back, unmodified
    drew = b''.join(out)

    if b'Opened' not in drew or b'alpha.txt' not in drew:
        return fail('the lone file did not open whole')

    # several answer: the menu lists them, a directory with its slash,
    # and an arrow plus Enter takes the one stepped onto
    out.clear()
    drive(fd, CTRL_O, out)
    drive(fd, (PATH_TREE + '/').encode(), out)
    drive(fd, b'\t', out)
    drew = b''.join(out)

    for said in (b'alpha.txt', b'answer.txt', b'sub/'):
        if said not in drew:
            return fail(f"the menu did not draw '{said.decode()}'")

    out.clear()
    drive(fd, b'\x1b[B', out)     # ArrowDown: onto answer.txt
    drive(fd, ENTER, out)          # Enter: take it into the answer
    drive(fd, ENTER, out)          # Enter: open it
    drive(fd, CTRL_S, out)
    for _ in range(3):
        drive(fd, CTRL_Q, out, pause=0.1)

    status = wait_exit(pid, fd)
    if status is None:
        return fail('the editor did not quit within 10s')
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return fail(f'exit status {status:#x}')

    drew = b''.join(out)

    if b'Opened' not in drew or b'answer.txt' not in drew:
        return fail('the match stepped onto did not open')

    # the second open switched to the first buffer again: both files
    # stand as they were written, the completion walking only
    with open(os.path.join(PATH_TREE, 'alpha.txt')) as f:
        if f.read() != 'alpha text\n':
            return fail('the alpha file did not stand as written')

    with open(os.path.join(PATH_TREE, 'answer.txt')) as f:
        if f.read() != 'answer text\n':
            return fail('the answer file did not stand as written')

    print('smoke: OK - walked the Tab completion into an open, '
          'menu and all')
    return 0


def saveas_smoke(binary):
    """the save-as run: text typed into an unnamed buffer, Ctrl-S
    asking where, the directory completed with its slash, and the
    file written to disk carrying the proof"""
    import shutil

    shutil.rmtree(PATH_SAVE, ignore_errors=True)
    os.makedirs(PATH_SAVE)

    pid, fd = spawn(binary, [])
    out = []
    time.sleep(0.3)
    drive(fd, b'saved by completion', out)  # the unnamed buffer's text
    drive(fd, CTRL_S, out)                  # Ctrl-S: the save-as prompt
    drive(fd, PATH_SAVE.encode(), out)
    drive(fd, b'\t', out)                  # Tab: the directory completes
    drive(fd, b'saved.txt', out)            # the name of the new file
    drive(fd, ENTER, out)                   # Enter: write it
    for _ in range(3):
        drive(fd, CTRL_Q, out, pause=0.1)

    status = wait_exit(pid, fd)
    if status is None:
        return fail('the editor did not quit within 10s')
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return fail(f'exit status {status:#x}')

    drew = b''.join(out)

    if b'Saved' not in drew:
        return fail('the bar did not say the save landed')

    try:
        with open(os.path.join(PATH_SAVE, 'saved.txt')) as f:
            text = f.read()
    except OSError:
        return fail('the save-as completion wrote no file')

    if text != 'saved by completion\n':
        return fail(f'the file reads {text!r}, '
                    'not the text typed into the unnamed buffer')

    print('smoke: OK - saved an unnamed buffer through the '
          'completed directory')
    return 0


def gf_smoke(binary):
    """the gf run: the file named under the cursor opened with Alt-o,
    its name standing beside the file being edited, the open handed
    back when the first file's own name is taken, and a name that is
    no file told so in the bar"""
    import shutil

    shutil.rmtree(GF_TREE, ignore_errors=True)
    os.makedirs(GF_TREE)
    with open(os.path.join(GF_TREE, 'one.txt'), 'w') as f:
        f.write('two.txt is the file to open\n'
                'missing.txt is not there\n')
    with open(os.path.join(GF_TREE, 'two.txt'), 'w') as f:
        f.write('one.txt named me from its own text\n')

    # the editor runs in / (the spawn chdir), so a bare name answers
    # only beside the file being edited — the way an #include does
    pid, fd = spawn(binary, [os.path.join(GF_TREE, 'one.txt')])
    out = []
    time.sleep(0.3)

    # the cursor starts on the name at the head of the first line:
    # Alt-o opens the file it names as a buffer of its own
    drive(fd, ALT_O, out)
    drew = b''.join(out)

    if b'Opened' not in drew or b'two.txt' not in drew:
        return fail('the file under the cursor did not open')

    if b'named me from its own text' not in drew:
        return fail('the buffer opened is not the file the name stood for')

    # the second file names the first, which is still open: the open
    # comes back to it instead of a second buffer of the same file
    out.clear()
    drive(fd, ALT_O, out)
    drew = b''.join(out)

    if b'Opened' not in drew or b'one.txt' not in drew:
        return fail('the open file was not handed back')
    if b'buffer 1 of 2' not in drew:
        return fail('the handed-back buffer is not the first one')

    # a line down, a name that is no file at all: the bar says so and
    # the buffer stands where it is
    out.clear()
    drive(fd, b'\x1b[B', out)   # ArrowDown: onto the missing name
    drive(fd, ALT_O, out)
    for _ in range(3):
        drive(fd, CTRL_Q, out, pause=0.1)

    status = wait_exit(pid, fd)
    if status is None:
        return fail('the editor did not quit within 10s')
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return fail(f'exit status {status:#x}')

    drew = b''.join(out)

    if b'No such file: missing.txt' not in drew:
        return fail('the name that is no file was not told so in the bar')

    # neither file was touched by the walking
    with open(os.path.join(GF_TREE, 'one.txt')) as f:
        if f.read() != 'two.txt is the file to open\nmissing.txt is not there\n':
            return fail('the one file did not stand as written')
    with open(os.path.join(GF_TREE, 'two.txt')) as f:
        if f.read() != 'one.txt named me from its own text\n':
            return fail('the two file did not stand as written')

    print('smoke: OK - walked Alt-o into an open, a hand back and a miss')
    return 0


def jump_smoke(binary):
    """the jump run: a name carrying a :line:column tail under the
    cursor, Alt-o opening the file it names with the cursor landing
    on the spot the tail says — the bar counting the line and the
    column it landed on — and a tail on a name already open landing
    there just the same"""
    import shutil

    shutil.rmtree(JUMP_TREE, ignore_errors=True)
    os.makedirs(JUMP_TREE)
    with open(os.path.join(JUMP_TREE, 'one.txt'), 'w') as f:
        f.write('two.txt:3:7 is the spot to open\n')
    with open(os.path.join(JUMP_TREE, 'two.txt'), 'w') as f:
        f.write('first\nsecond\nthird one.txt:1:2 sits here\n')

    # the editor runs in / (the spawn chdir), so a bare name answers
    # only beside the file being edited — the way an #include does
    pid, fd = spawn(binary, [os.path.join(JUMP_TREE, 'one.txt')])
    out = []
    time.sleep(0.3)

    # the cursor starts on the name at the head of the first line:
    # Alt-o opens the file it names, and the tail lands the cursor on
    # its line 3, its column 7 — the bar counting the spot
    drive(fd, ALT_O, out)
    drew = b''.join(out)

    if b'Opened' not in drew or b'two.txt' not in drew:
        return fail('the name with its tail did not open')
    if b'Ln 3/3' not in drew:
        return fail('the open did not land on the line the tail named')
    if b'Col 7' not in drew:
        return fail('the open did not land on the column the tail named')

    # the cursor landed inside the name on the third line, its own
    # tail naming the first file: the open hands back to it and lands
    # where that tail says
    out.clear()
    drive(fd, ALT_O, out)
    drew = b''.join(out)

    if b'Opened' not in drew or b'one.txt' not in drew:
        return fail('the tailed name was not handed back')
    if b'buffer 1 of 2' not in drew:
        return fail('the handed-back buffer is not the first one')
    if b'Ln 1/1' not in drew or b'Col 2' not in drew:
        return fail('the hand back did not land where its tail said')

    for _ in range(3):
        drive(fd, CTRL_Q, out, pause=0.1)

    status = wait_exit(pid, fd)
    if status is None:
        return fail('the editor did not quit within 10s')
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return fail(f'exit status {status:#x}')

    # neither file was touched by the walking
    with open(os.path.join(JUMP_TREE, 'one.txt')) as f:
        if f.read() != 'two.txt:3:7 is the spot to open\n':
            return fail('the one file did not stand as written')
    with open(os.path.join(JUMP_TREE, 'two.txt')) as f:
        if f.read() != 'first\nsecond\nthird one.txt:1:2 sits here\n':
            return fail('the two file did not stand as written')

    print('smoke: OK - walked Alt-o into a :line:column jump, there and back')
    return 0


def goto_smoke(binary):
    """the goto run: Ctrl-G asked for a line with a :column tail
    behind it, the cursor landing on the spot it names — a bare line
    landing the start of the line the way it always did, a line past
    the end clamped to the last one, an answer that spells no line
    naming a file instead, one that is not there told so in the bar,
    and ESC taking the prompt down with the cursor standing"""
    with open(GOTO, 'w') as f:
        f.write('first\nsecond\nthird\n')

    pid, fd = spawn(binary, [GOTO])
    out = []
    time.sleep(0.3)

    # a line and a column: the cursor lands on the second row, its
    # third column — the bar counting the spot
    drive(fd, CTRL_G, out)
    drive(fd, b'2:3', out)
    drive(fd, ENTER, out)
    drew = b''.join(out)

    if b'Ln 2/3' not in drew:
        return fail('the goto did not land on the line the answer named')
    if b'Col 3' not in drew:
        return fail('the goto did not land on the column the tail named')

    # a bare line lands the start of the line, the way it always did
    out.clear()
    drive(fd, CTRL_G, out)
    drive(fd, b'1', out)
    drive(fd, ENTER, out)
    drew = b''.join(out)

    if b'Ln 1/3' not in drew or b'Col 1' not in drew:
        return fail('a bare line number did not land as it always did')

    # a line past the end is clamped to the last one
    out.clear()
    drive(fd, CTRL_G, out)
    drive(fd, b'99:2', out)
    drive(fd, ENTER, out)
    drew = b''.join(out)

    if b'Ln 3/3' not in drew:
        return fail('a line past the end did not land on the last one')

    # an answer that spells no line names a file instead — one that
    # is not there says so in the bar the way Alt-o does, and a
    # colon with no number behind it still spells no line
    out.clear()
    drive(fd, CTRL_G, out)
    drive(fd, b'two', out)
    drive(fd, ENTER, out)
    drew = b''.join(out)

    if b'No such file: two' not in drew:
        return fail('a name that is no file was not told so')

    out.clear()
    drive(fd, CTRL_G, out)
    drive(fd, b'2:', out)
    drive(fd, ENTER, out)
    drew = b''.join(out)

    if b'Not a line number: 2:' not in drew:
        return fail('a colon with no number behind it was not told so')

    # ESC takes the prompt down with the cursor standing where it was
    out.clear()
    drive(fd, CTRL_G, out)
    out.clear()
    drive(fd, ESC, out)
    drew = b''.join(out)

    if b'Go to line' in drew:
        return fail('ESC did not take the prompt down')
    if b'Ln 3/3' not in drew:
        return fail('ESC moved the cursor off the last line')

    for _ in range(3):
        drive(fd, CTRL_Q, out, pause=0.1)

    status = wait_exit(pid, fd)
    if status is None:
        return fail('the editor did not quit within 10s')
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return fail(f'exit status {status:#x}')

    with open(GOTO) as f:
        if f.read() != 'first\nsecond\nthird\n':
            return fail('the goto run did not leave the file as written')

    print('smoke: OK - walked Ctrl-G into a :line:column landing')
    return 0


def gocomplete_smoke(binary):
    """the goto completion run: Alt-/ over the name part of the
    goto answer — a lone name landing whole behind its directory,
    several landing the stem they agree on with the menu standing, a
    take from the menu landing behind the name part, the tail behind
    the name standing through a round and a take so the open lands
    on the spot it spells, and a prefix nothing answers told so in
    the bar"""
    import shutil

    shutil.rmtree(GOTO_TREE, ignore_errors=True)
    os.makedirs(GOTO_TREE)
    with open(os.path.join(GOTO_TREE, 'start.txt'), 'w') as f:
        f.write('here the walk starts\n')
    with open(os.path.join(GOTO_TREE, 'alpha.txt'), 'w') as f:
        f.write('one\ntwo\nthree\nfour\nfive\n')
    with open(os.path.join(GOTO_TREE, 'beta.c'), 'w') as f:
        f.write('a\nb\nc\nd\n')
    with open(os.path.join(GOTO_TREE, 'betty.txt'), 'w') as f:
        f.write('x\ny\nz\n')

    pid, fd = spawn(binary, [os.path.join(GOTO_TREE, 'start.txt')])
    out = []
    time.sleep(0.3)

    # a lone name lands whole: the bar spells the answer it completed
    drive(fd, CTRL_G, out)
    drive(fd, (GOTO_TREE + '/alp').encode(), out)
    drive(fd, ALT_SLASH, out)
    drew = b''.join(out)

    if (GOTO_TREE + '/alpha.txt').encode() not in drew:
        return fail('the lone name did not land whole behind its directory')

    # Enter takes the answer: the file opens with the cursor on its
    # first line
    drive(fd, ENTER, out)
    drew = b''.join(out)

    if b'Opened' not in drew or b'alpha.txt' not in drew:
        return fail('the completed name did not open')
    if b'Ln 1/5' not in drew:
        return fail('the open did not land on the first line')

    # several answer: the stem they agree on lands, and the menu the
    # Tab completion uses lists them
    drive(fd, CTRL_G, out)
    drive(fd, (GOTO_TREE + '/be').encode(), out)
    drive(fd, ALT_SLASH, out)
    drew = b''.join(out)

    for said in (b'beta.c', b'betty.txt'):
        if said not in drew:
            return fail(f"the menu did not draw '{said.decode()}'")

    if (GOTO_TREE + '/bet').encode() not in drew:
        return fail('the stem the names agree on did not land')

    # a take from the menu lands behind the name part, and Enter
    # opens it
    drive(fd, ENTER, out)   # take the selection into the answer
    drive(fd, ENTER, out)   # open what the answer names
    drew = b''.join(out)

    if b'Opened' not in drew or b'Ln 1/4' not in drew:
        return fail('the take from the menu did not open the file')

    # the tail stands through a round and a take: a second Alt-/
    # relists after the tail was typed, an arrow takes betty.txt
    # behind the name part with the tail behind it, and the open
    # lands on the spot it spells
    drive(fd, CTRL_G, out)
    drive(fd, (GOTO_TREE + '/be').encode(), out)
    drive(fd, ALT_SLASH, out)
    drive(fd, b':2', out)   # the tail: any other key takes the menu down
    drive(fd, ALT_SLASH, out)  # and a fresh round puts it back up
    drive(fd, b'\x1b[B', out)  # ArrowDown: onto betty.txt
    drive(fd, ENTER, out)   # take it, the tail behind the name
    drive(fd, ENTER, out)   # open it, landing on the tail's spot
    drew = b''.join(out)

    if b'betty.txt' not in drew:
        return fail('the take did not land betty.txt in the answer')
    if b'Ln 2/3' not in drew:
        return fail('the tail did not land the open on the second line')

    # a prefix nothing answers says so in the bar, the answer standing
    out.clear()
    drive(fd, CTRL_G, out)
    drive(fd, (GOTO_TREE + '/zz').encode(), out)
    drive(fd, ALT_SLASH, out)
    drive(fd, ESC, out)
    drew = b''.join(out)

    if b'No match for' not in drew:
        return fail('the prefix nothing answers was not told so')

    for _ in range(3):
        drive(fd, CTRL_Q, out, pause=0.1)

    status = wait_exit(pid, fd)
    if status is None:
        return fail('the editor did not quit within 10s')
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return fail(f'exit status {status:#x}')

    for name, said in (('start.txt', 'here the walk starts\n'),
                       ('alpha.txt', 'one\ntwo\nthree\nfour\nfive\n'),
                       ('beta.c', 'a\nb\nc\nd\n'),
                       ('betty.txt', 'x\ny\nz\n')):
        with open(os.path.join(GOTO_TREE, name)) as f:
            if f.read() != said:
                return fail(f'the goto completion run did not leave {name} as written')

    print('smoke: OK - walked the Alt-/ of the goto prompt')
    return 0


def browse_smoke(binary):
    """the browse run: Alt-o on a directory opening the browse of
    it — the name standing beside the file being edited, the way an
    #include does — the size and the time of a name said behind it
    the way ls lays its own columns, a take from the menu opening
    the file under the selection, a walk into a narrower directory
    painting the rows
    the wider listing covered, the hidden names held back until a
    dot takes them in and back out with the selection standing
    through it and a take from the hidden ones opening one, the
    bytes typed narrowing the listing with the filter carried in
    the bar, Backspace giving a byte back and only then going up,
    ESC taking the filter off with the browse standing, a take from
    the narrowed listing, the walk in taking the filter off, and a
    dot going into the filter instead of the hidden toggle, an
    empty directory saying so with Backspace going up and the name
    walked from standing selected, a walk in from the listing
    above, a left click taking the line drawn under it, ESC
    cancelling with the editor standing where it was, and a name
    that is no regular file still told so in the bar"""
    import shutil

    shutil.rmtree(BROWSE_TREE, ignore_errors=True)
    os.makedirs(os.path.join(BROWSE_TREE, 'sub', 'inner'))
    os.makedirs(os.path.join(BROWSE_TREE, 'empty'))

    # the file the browse opens over reaches the bottom of the
    # viewport, so the menu it draws covers text rows and not the
    # tildes below the text, which paint again every frame anyway
    start_text = ('sub\nempty\nstart again\n' +
                  ''.join('filler %02d\n' % i for i in range(1, 30)))
    with open(os.path.join(BROWSE_TREE, 'start.txt'), 'w') as f:
        f.write(start_text)
    with open(os.path.join(BROWSE_TREE, 'sub', 'deep.txt'), 'w') as f:
        f.write('deep one\ndeep two\ndeep three\n')
    # the moment the clock was set to, so the walk asks after a time
    # it knows — the size is the 29 bytes the file was written with
    os.utime(os.path.join(BROWSE_TREE, 'sub', 'deep.txt'),
             (1715000000, 1715000000))
    with open(os.path.join(BROWSE_TREE, 'sub', 'deeper.txt'), 'w') as f:
        f.write('far one\nfar two\n')
    with open(os.path.join(BROWSE_TREE, 'sub', '.secret'), 'w') as f:
        f.write('hidden one\nhidden two\n')
    with open(os.path.join(BROWSE_TREE, 'sub', 'inner', 'one.txt'), 'w') as f:
        f.write('innermost\n')

    pid, fd = spawn(binary, [os.path.join(BROWSE_TREE, 'start.txt')])
    out = []
    time.sleep(0.3)

    # the cursor starts on "sub" at the head of the first line: Alt-o
    # opens the browse of it, the relative name standing beside the
    # file being edited
    drive(fd, ALT_O, out)
    drew = b''.join(out)

    if b'Browse' not in drew or b'sub' not in drew:
        return fail('Alt-o on a directory did not open the browse of it')

    for said in (b'deep.txt', b'deeper.txt', b'inner/'):
        if said not in drew:
            return fail(f"the browse did not draw '{said.decode()}'")

    # the browse says the size and the time of every name: the bytes
    # the name holds and the moment its clock was set to, laid behind
    # the name the way ls lays its own columns, both ending at the
    # screen's right edge
    when = time.strftime('%Y-%m-%d %H:%M',
                         time.localtime(1715000000)).encode()
    if b'29 ' + when not in drew:
        return fail('the browse did not say the size and the time of deep.txt')

    # the walk into the narrower directory: the listing of it draws
    # fewer lines than the one above, and the rows the wider menu
    # covered paint as text again instead of staying as they were
    # drawn — the stale lines would be the wider listing standing
    drive(fd, b'\x1b[B', out)
    drive(fd, b'\x1b[B', out)
    out.clear()
    drive(fd, ENTER, out)
    drew = b''.join(out)

    if b'one.txt' not in drew:
        return fail('the walk in did not list the directory')
    if b'filler 17' not in drew:
        return fail('the rows the wider menu covered did not paint again')
    for stale in (b'deep.txt', b'deeper.txt'):
        if stale in drew:
            return fail(f"the walk in left '{stale.decode()}' standing")

    # Backspace goes up a level, the name walked from standing
    # selected in the listing above, and the arrows come back to the
    # head of it
    drive(fd, BACKSPACE, out)
    drew = b''.join(out)

    if b'inner/' not in drew:
        return fail('the walk up did not draw the name walked from')

    drive(fd, b'\x1b[A', out)
    drive(fd, b'\x1b[A', out)

    # Enter takes the selection: deep.txt opens on its first line
    drive(fd, ENTER, out)
    drew = b''.join(out)

    if b'Opened' not in drew or b'deep.txt' not in drew:
        return fail('the take from the browse did not open the file')
    if b'Ln 1/3' not in drew:
        return fail('the open did not land on the first line')

    # the goto names an empty directory: the browse says so, and
    # Backspace goes up a level with the name walked from standing
    # selected in the listing above
    drive(fd, CTRL_G, out)
    drive(fd, (BROWSE_TREE + '/empty').encode(), out)
    drive(fd, ENTER, out)
    drew = b''.join(out)

    if b'empty;' not in drew:
        return fail('an empty directory did not say so in the bar')

    drive(fd, BACKSPACE, out)
    drew = b''.join(out)

    if b'empty/' not in drew:
        return fail('the walk up did not draw the name walked from')

    # the arrows walk the listing: down twice onto sub, Enter walks
    # in, and the menu of it lists its files again
    drive(fd, b'\x1b[B', out)
    drive(fd, b'\x1b[B', out)
    drive(fd, ENTER, out)
    drew = b''.join(out)

    if b'deep.txt' not in drew or b'deeper.txt' not in drew:
        return fail('the walk in did not list the directory')

    # a left click takes the line drawn under it — the middle of the
    # three menu lines, deeper.txt — and the open lands on its first
    # line
    out.clear()
    drive(fd, b'\x1b[<0;1;21M\x1b[<0;1;21m', out)
    drew = b''.join(out)

    if b'Opened' not in drew or b'deeper.txt' not in drew:
        return fail('the click did not take the line drawn under it')
    if b'Ln 1/2' not in drew:
        return fail('the click did not open on the first line')

    # the bytes typed while the listing stands narrow it, the way
    # the goto prompt carries its answer in the bar: the filter
    # itself carried in the bar, the names that carry its bytes
    # anywhere in them staying standing, Backspace giving a byte
    # back and only then going up a level, a filter nothing carries
    # saying so, ESC taking the filter off with the browse standing
    # before it cancels, a take from the narrowed listing, the walk
    # in taking the filter off, and a dot going into the filter
    # instead of the hidden toggle
    drive(fd, CTRL_G, out)
    drive(fd, (BROWSE_TREE + '/sub').encode(), out)
    out.clear()
    drive(fd, ENTER, out)
    out.clear()

    # 'e' takes nothing out — every name of sub carries one — but
    # the bar carries the filter itself
    drive(fd, b'e', out)
    drew = b''.join(out)

    if b"(filter 'e')" not in drew:
        return fail("the bar did not carry the filter 'e'")
    if b'inner/' not in drew:
        return fail("the filter 'e' took a name that carries it out")

    # 'p' takes the names that carry 'ep' nowhere out
    out.clear()
    drive(fd, b'p', out)
    drew = b''.join(out)

    if b'inner/' in drew:
        return fail("the filter 'ep' left inner/ standing")
    if b'deep.txt' not in drew or b'deeper.txt' not in drew:
        return fail("the filter 'ep' took a name that carries it out")

    # Backspace gives the byte back — and a second one empties the
    # filter, the listing standing whole again with the bar saying
    # so the plain way
    out.clear()
    drive(fd, BACKSPACE, out)
    drew = b''.join(out)

    if b'inner/' not in drew:
        return fail('Backspace did not give the filter byte back')

    out.clear()
    drive(fd, BACKSPACE, out)
    drew = b''.join(out)

    if b'inner/' not in drew:
        return fail('an emptied filter did not leave the listing whole')
    if b'(filter' in drew:
        return fail('an emptied filter stayed carried in the bar')

    # a filter nothing carries says so, the menu coming down the way
    # an empty directory takes it down
    out.clear()
    drive(fd, b'z', out)
    drew = b''.join(out)

    if b'no name carries it' not in drew:
        return fail('a filter nothing carries did not say so')
    if b'deep.txt' in drew:
        return fail('a filter nothing carries left a name standing')

    # ESC takes the filter off with the browse standing — the whole
    # listing back, no cancel yet — and only the second one cancels
    # the browse, the editor standing where it was
    out.clear()
    drive(fd, ESC, out)
    drew = b''.join(out)

    if b'deep.txt' not in drew or b'inner/' not in drew:
        return fail('ESC did not take the filter off with the browse standing')
    if b'Cancelled' in drew:
        return fail('ESC cancelled the browse with a filter standing')

    out.clear()
    drive(fd, ESC, out)
    drew = b''.join(out)

    if b'Cancelled the browse' not in drew:
        return fail('the second ESC did not cancel the browse')
    if b'far one' not in drew:
        return fail('the cancel did not leave the editor where it was')

    # a take from the narrowed listing opens the file under it:
    # 'deepe' narrows to deeper.txt alone, the selection landing on
    # it through the relisting
    drive(fd, CTRL_G, out)
    drive(fd, (BROWSE_TREE + '/sub').encode(), out)
    drive(fd, ENTER, out)
    drive(fd, b'deep', out)
    out.clear()
    drive(fd, b'e', out)
    drew = b''.join(out)

    if b'deep.txt' in drew:
        return fail("the filter 'deepe' left deep.txt standing")
    if b'deeper.txt' not in drew:
        return fail("the filter 'deepe' took deeper.txt out")

    drive(fd, ENTER, out)
    drew = b''.join(out)

    if b'Opened' not in drew or b'deeper.txt' not in drew:
        return fail('the take from the narrowed listing did not open the file')
    if b'Ln 1/2' not in drew:
        return fail('the take from the narrowed listing did not open on the first line')

    # the walk in takes the filter off: 'in' narrows to inner/
    # alone, Enter walks in, the listing of it stands whole, and
    # Backspace goes up a level again — the filter gone, not edited
    drive(fd, CTRL_G, out)
    drive(fd, (BROWSE_TREE + '/sub').encode(), out)
    drive(fd, ENTER, out)
    out.clear()
    drive(fd, b'in', out)
    drew = b''.join(out)

    if b'deep.txt' in drew or b'deeper.txt' in drew:
        return fail("the filter 'in' left a name standing that carries it nowhere")
    if b'inner/' not in drew:
        return fail("the filter 'in' took inner/ out")

    drive(fd, ENTER, out)
    drew = b''.join(out)

    if b'one.txt' not in drew:
        return fail('the walk in from a narrowed listing did not list the directory')

    drive(fd, BACKSPACE, out)
    drew = b''.join(out)

    if b'deep.txt' not in drew or b'inner/' not in drew:
        return fail('Backspace after the walk in did not go up with the filter off')

    # a dot goes into the filter when one stands, instead of
    # toggling the hidden names: nothing of sub carries 'e.', and
    # .secret — which carries an 'e' — would stand were the dot a
    # toggle that took the hidden names in
    out.clear()
    drive(fd, b'e', out)
    drive(fd, b'.', out)
    drew = b''.join(out)

    if b"(filter 'e.')" not in drew:
        return fail('a dot did not go into the filter standing')
    if b'no name carries it' not in drew:
        return fail("the filter 'e.' did not say nothing carries it")
    if b'.secret' in drew:
        return fail('a dot with a filter standing toggled the hidden names')

    # the filter standing comes off and the browse with it, so the
    # rounds after this one start from the editor standing
    drive(fd, ESC, out)
    drive(fd, ESC, out)

    # the hidden names stay back the way the Tab completion holds
    # them, until a dot takes them in and back out again: the
    # selection stands through the relisting, and a take from the
    # hidden ones opens one
    drive(fd, CTRL_G, out)
    drive(fd, (BROWSE_TREE + '/sub').encode(), out)
    out.clear()
    drive(fd, ENTER, out)
    drew = b''.join(out)

    if b'.secret' in drew:
        return fail('the browse drew a hidden name without being asked to')
    if b'deep.txt' not in drew:
        return fail('the browse did not draw the plain names')

    drive(fd, b'.', out)
    drew = b''.join(out)

    if b'.secret' not in drew:
        return fail('the dot did not take the hidden names in')
    if b'The hidden names are shown' not in drew:
        return fail('the bar did not say the hidden names are shown')

    # a second dot holds them back again, the name the selection
    # stands on standing through both rounds
    out.clear()
    drive(fd, b'.', out)
    drew = b''.join(out)

    if b'.secret' in drew:
        return fail('the second dot did not hold the hidden names back')
    if b'The hidden names are held back' not in drew:
        return fail('the bar did not say the hidden names are held back')

    # a third takes them in again: the selection stood on deep.txt
    # through the rounds, so the up arrow steps onto the hidden file
    # and the take opens it
    drive(fd, b'.', out)
    drive(fd, b'\x1b[A', out)
    out.clear()
    drive(fd, ENTER, out)
    drew = b''.join(out)

    if b'Opened' not in drew or b'.secret' not in drew:
        return fail('the take did not open the hidden file')
    if b'Ln 1/2' not in drew:
        return fail('the hidden file did not open on its first line')

    # ESC cancels the browse, the editor standing where it was
    drive(fd, CTRL_G, out)
    drive(fd, (BROWSE_TREE + '/sub').encode(), out)
    drive(fd, ENTER, out)
    out.clear()
    drive(fd, ESC, out)
    drew = b''.join(out)

    if b'Cancelled the browse' not in drew:
        return fail('ESC did not cancel the browse')
    if b'hidden one' not in drew:
        return fail('the cancel did not leave the editor where it was')

    # a name that answers as no regular file — a fifo — is told so in
    # the bar, the way it always was
    os.mkfifo(os.path.join(BROWSE_TREE, 'pipe'))
    drive(fd, CTRL_G, out)
    drive(fd, (BROWSE_TREE + '/pipe').encode(), out)
    drive(fd, ENTER, out)
    drew = b''.join(out)

    if b'Not a file' not in drew:
        return fail('a name that is no regular file was not told so')

    for _ in range(3):
        drive(fd, CTRL_Q, out, pause=0.1)

    status = wait_exit(pid, fd)
    if status is None:
        return fail('the editor did not quit within 10s')
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return fail(f'exit status {status:#x}')

    for name, said in (('start.txt', start_text),
                       ('sub/deep.txt', 'deep one\ndeep two\ndeep three\n'),
                       ('sub/deeper.txt', 'far one\nfar two\n'),
                       ('sub/.secret', 'hidden one\nhidden two\n'),
                       ('sub/inner/one.txt', 'innermost\n')):
        with open(os.path.join(BROWSE_TREE, name)) as f:
            if f.read() != said:
                return fail(f'the browse run did not leave {name} as written')

    print('smoke: OK - walked the Alt-o directory browse')
    return 0


def prompt_smoke(binary):
    """the prompt run: a search whose query names a file with a tail,
    Alt-o opening it with the cursor landing on the spot the tail
    says — a query that names no file told so in a notice while the
    search stands, the notice answered by the next key, and the
    replace's own prompt opening the same way"""
    import shutil

    shutil.rmtree(PROMPT_TREE, ignore_errors=True)
    os.makedirs(PROMPT_TREE)
    with open(os.path.join(PROMPT_TREE, 'one.txt'), 'w') as f:
        f.write('alpha\nbeta\ngamma\n')
    with open(os.path.join(PROMPT_TREE, 'two.txt'), 'w') as f:
        f.write('first\nsecond\nthird\nfourth\n')

    pid, fd = spawn(binary, [os.path.join(PROMPT_TREE, 'one.txt')])
    out = []
    time.sleep(0.3)

    # the search opens on a query naming two.txt with a tail: Alt-o
    # opens it and the tail lands the cursor on its line 3, column 4,
    # the bar counting the spot
    drive(fd, CTRL_F, out)
    drive(fd, b'two.txt:3:4', out)
    drive(fd, ALT_O, out)
    drew = b''.join(out)

    if b'Opened' not in drew or b'two.txt' not in drew:
        return fail('the query with its tail did not open')
    if b'Ln 3/4' not in drew:
        return fail('the open did not land on the line the tail named')
    if b'Col 4' not in drew:
        return fail('the open did not land on the column the tail named')

    # a query that names no file says so in a notice while the search
    # stands, the query still in it
    out.clear()
    drive(fd, CTRL_F, out)
    for _ in range(11):   # the remembered query goes back out
        drive(fd, BACKSPACE, out, pause=0.02)
    drive(fd, b'nope.txt', out)
    drive(fd, ALT_O, out)
    drew = b''.join(out)

    if b'No such file: nope.txt' not in drew:
        return fail('a query that names no file was not told so')
    if b'Find:' not in drew or b'nope.txt' not in drew:
        return fail('the search did not stand behind its notice')

    # the next key answers the notice: the counting line comes back
    out.clear()
    drive(fd, b'!', out)
    drew = b''.join(out)

    if b'No such file' in drew:
        return fail('the notice stood past the key that answers it')

    drive(fd, ESC, out)   # the search ends

    # the replace's own prompt opens the same way: its Alt-o takes the
    # answer as the name, and the bar says what opened instead of
    # talking about a cancel
    out.clear()
    drive(fd, CTRL_R, out)
    for _ in range(9):    # the remembered query goes back out
        drive(fd, BACKSPACE, out, pause=0.02)
    drive(fd, b'one.txt:2', out)
    drive(fd, ALT_O, out)
    drew = b''.join(out)

    if b'Opened' not in drew or b'one.txt' not in drew:
        return fail('the replace prompt did not take the Alt-o')
    if b'Replace cancelled' in drew:
        return fail('the cancel-talk talked over the open')
    if b'Ln 2/3' not in drew:
        return fail('the open did not land where the tail said')

    for _ in range(3):
        drive(fd, CTRL_Q, out, pause=0.1)

    status = wait_exit(pid, fd)
    if status is None:
        return fail('the editor did not quit within 10s')
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return fail(f'exit status {status:#x}')

    # neither file was touched by the walking
    with open(os.path.join(PROMPT_TREE, 'one.txt')) as f:
        if f.read() != 'alpha\nbeta\ngamma\n':
            return fail('the one file did not stand as written')
    with open(os.path.join(PROMPT_TREE, 'two.txt')) as f:
        if f.read() != 'first\nsecond\nthird\nfourth\n':
            return fail('the two file did not stand as written')

    print('smoke: OK - walked Alt-o on the names the prompts spell')
    return 0


def history_smoke(binary):
    """the history run: two queries searched and kept, the search
    reopened on the newest, an up arrow recalling the older one and a
    down arrow walking back, Enter keeping the recalled query"""
    with open(HISTORY, 'w') as f:
        f.write('one two one two\n')

    pid, fd = spawn(binary, [HISTORY])
    out = []
    time.sleep(0.3)

    # a first query, searched and kept
    drive(fd, CTRL_F, out)
    drive(fd, b'one', out)
    drive(fd, ENTER, out)

    # a second one: the seeded first is taken apart, the new typed and
    # kept — the history now holds them both, newest last
    drive(fd, CTRL_F, out)
    for _ in range(3):
        drive(fd, BACKSPACE, out, pause=0.1)
    drive(fd, b'two', out)
    drive(fd, ENTER, out)

    # the search opens on the newest of them
    out.clear()
    drive(fd, CTRL_F, out)
    drew = b''.join(out)

    if b'Find: two (' not in drew:
        return fail('the search did not open on the newest query')

    # an up arrow recalls the query before it, its matches lighting up
    # the way a typed query's do
    out.clear()
    drive(fd, b'\x1b[A', out)     # ArrowUp: onto the older query
    drew = b''.join(out)

    if b'Find: one (' not in drew:
        return fail('the up arrow did not recall the older query')
    if b'Find: one (1 of 2' not in drew:
        return fail('the recalled query did not count its matches')

    # a down arrow walks back to the newest, and Enter keeps it
    out.clear()
    drive(fd, b'\x1b[B', out)     # ArrowDown: back to the newest
    drive(fd, ENTER, out)         # Enter: keep the query
    for _ in range(3):
        drive(fd, CTRL_Q, out, pause=0.1)

    status = wait_exit(pid, fd)
    if status is None:
        return fail('the editor did not quit within 10s')
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return fail(f'exit status {status:#x}')

    drew = b''.join(out)

    if b'Find: two (' not in drew:
        return fail('the down arrow did not walk back to the newest query')

    # the file stands as written: the walk only ever read it
    with open(HISTORY) as f:
        if f.read() != 'one two one two\n':
            return fail('the file did not stand as written')

    print('smoke: OK - walked the search history up and back down')
    return 0


def electric_smoke(binary):
    """the electric run: a bracket typed closing itself, the closer
    already under the cursor stepped over, the empty pair taken back
    as one, and Alt-( turning the whole thing off"""
    with open(ELECTRIC, 'w') as f:
        f.write('row\n')

    pid, fd = spawn(binary, [ELECTRIC])
    out = []
    time.sleep(0.3)

    # End: the cursor at the row's end. A paren typed closes itself
    # right away, the cursor between the two; an x goes inside it; the
    # closer typed over the closer already there steps over instead of
    # doubling it
    drive(fd, END, out)
    drive(fd, b'(', out)
    drive(fd, b'x', out)
    drive(fd, b')', out)
    drive(fd, CTRL_S, out)         # Ctrl-S: the proof lands on disk

    with open(ELECTRIC) as f:
        if f.read() != 'row(x)\n':
            return fail('the pair did not close itself, or the closer '
                        'doubled instead of stepping over')

    # backspaces walk it back out: the closer the step went over, the
    # x inside, and the empty pair as one press
    drive(fd, BACKSPACE, out, pause=0.1)   # the closer
    drive(fd, BACKSPACE, out, pause=0.1)   # the x
    drive(fd, BACKSPACE, out, pause=0.1)   # the pair, as one
    drive(fd, CTRL_S, out)

    with open(ELECTRIC) as f:
        if f.read() != 'row\n':
            return fail('the empty pair did not go back out as one')

    # Alt-( turns the pairs off, the bar saying so: a bracket typed
    # after it stands alone
    out.clear()
    drive(fd, ALT_LPAREN, out)     # Alt-(: off
    drew = b''.join(out)

    if b'Electric brackets off' not in drew:
        return fail('the bar did not say the pairs turned off')

    drive(fd, b'(', out)           # no closer behind it now
    drive(fd, CTRL_S, out)
    for _ in range(3):
        drive(fd, CTRL_Q, out, pause=0.1)

    status = wait_exit(pid, fd)
    if status is None:
        return fail('the editor did not quit within 10s')
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return fail(f'exit status {status:#x}')

    with open(ELECTRIC) as f:
        if f.read() != 'row(\n':
            return fail('the bracket did not stand alone with the '
                        'pairs off')

    print('smoke: OK - walked an electric pair in, over and back out')
    return 0


def wrap_smoke(binary):
    """the wrap run: a word selected with a double click, a bracket
    typed over it wrapping it in its pair instead of taking it out,
    the selection riding the text — a letter typed over the wrap
    landing between the halves — and Ctrl-Z walking the whole thing
    back out a step at a time, the pair as one of them"""
    with open(WRAP, 'w') as f:
        f.write('one two three\nfour five six\n')

    pid, fd = spawn(binary, [WRAP])
    out = []
    time.sleep(0.3)

    # the double click on 'w' of "two" as the double click run: the
    # word stands selected, and a ( typed over it wraps it
    drive(fd, b'\x1b[<0;10;1M\x1b[<0;10;1m\x1b[<0;10;1M', out)
    drive(fd, b'(', out)
    drive(fd, CTRL_S, out)

    with open(WRAP) as f:
        if f.read() != 'one (two) three\nfour five six\n':
            return fail('the bracket did not wrap the selected word')

    # the selection rode the text: the word, not the pair, is what the
    # typing over it replaces
    drive(fd, b'x', out)
    drive(fd, CTRL_S, out)

    with open(WRAP) as f:
        if f.read() != 'one (x) three\nfour five six\n':
            return fail('the wrapped word did not answer the typing over it')

    # three undos walk it back: the letter, the word the typing took,
    # and the pair as one step
    for _ in range(3):
        drive(fd, CTRL_Z, out, pause=0.1)
    drive(fd, CTRL_S, out)

    with open(WRAP) as f:
        if f.read() != 'one two three\nfour five six\n':
            return fail('the wrap did not go back out with the pair as one step')

    for _ in range(3):
        drive(fd, CTRL_Q, out, pause=0.1)

    status = wait_exit(pid, fd)
    if status is None:
        return fail('the editor did not quit within 10s')
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return fail(f'exit status {status:#x}')

    print('smoke: OK - wrapped a double-clicked word in an electric pair')
    return 0


def quote_smoke(binary):
    """the quote run: a word selected with a double click, a double
    quote typed over it wrapping it in its own pair — the same byte
    both halves — a single quote doing the same, Ctrl-Z walking each
    back out as one step, and the pairs off so a quote typed over a
    word takes it out the way any letter does"""
    with open(QUOTE, 'w') as f:
        f.write('one two three\nfour five six\n')

    pid, fd = spawn(binary, [QUOTE])
    out = []
    time.sleep(0.3)

    # the double click on 'w' of "two" as the wrap run: the word
    # stands selected, and a double quote typed over it wraps it
    drive(fd, b'\x1b[<0;10;1M\x1b[<0;10;1m\x1b[<0;10;1M', out)
    drive(fd, b'"', out)
    drive(fd, CTRL_S, out)

    with open(QUOTE) as f:
        if f.read() != 'one "two" three\nfour five six\n':
            return fail('the double quote did not wrap the selected word')

    # the wrap is one undo step, the pair going as one
    drive(fd, CTRL_Z, out, pause=0.1)
    drive(fd, CTRL_S, out)

    with open(QUOTE) as f:
        if f.read() != 'one two three\nfour five six\n':
            return fail('the quote wrap did not go back out as one step')

    # the single quote over 'five' on the row below does the same
    drive(fd, b'\x1b[<0;10;2M\x1b[<0;10;2m\x1b[<0;10;2M', out)
    drive(fd, b"'", out)
    drive(fd, CTRL_S, out)

    with open(QUOTE) as f:
        if f.read() != "one two three\nfour 'five' six\n":
            return fail('the single quote did not wrap the selected word')

    drive(fd, CTRL_Z, out, pause=0.1)
    drive(fd, CTRL_S, out)

    # Alt-( turns the electricity off: a quote typed over a selection
    # takes it out the way any letter does
    out.clear()
    drive(fd, ALT_LPAREN, out)     # Alt-(: off
    drive(fd, b'\x1b[<0;10;1M\x1b[<0;10;1m\x1b[<0;10;1M', out)
    drive(fd, b'"', out)
    drive(fd, CTRL_S, out)

    with open(QUOTE) as f:
        if f.read() != 'one " three\nfour five six\n':
            return fail('the quote stood in for the word with the '
                        'pairs off')

    for _ in range(3):
        drive(fd, CTRL_Q, out, pause=0.1)

    status = wait_exit(pid, fd)
    if status is None:
        return fail('the editor did not quit within 10s')
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return fail(f'exit status {status:#x}')

    print('smoke: OK - walked the quote wrap in, both quotes and back out')
    return 0


def match_smoke(binary):
    """the match run: the cursor landed on the quote that opens a
    span, the far end of it lighting up inverted the way a bracket's
    partner does; Alt-% landing there with the bar counting the spot,
    the cursor resting past the closer still lighting its opener, and
    an escaped quote finding no partner at all"""
    with open(MATCH, 'w') as f:
        f.write('say "hello" now\nan \\" escaped one\n')

    pid, fd = spawn(binary, [MATCH])
    out = []
    time.sleep(0.3)

    # the cursor on the opening quote: the closing one lights up
    drive(fd, CTRL_G, out)
    drive(fd, b'1:5', out)
    drive(fd, ENTER, out)
    drew = b''.join(out)

    if b'\x1b[7m"' not in drew:
        return fail('the closing quote did not light up')

    # Alt-% lands on the lit partner, the bar counting the spot
    out.clear()
    drive(fd, ALT_PERCENT, out)
    drew = b''.join(out)

    if b'Ln 1/2' not in drew or b'Col 11' not in drew:
        return fail('Alt-% did not land on the quote partner')

    # the cursor rests right past the closer: the opener still lights
    out.clear()
    drive(fd, CTRL_G, out)
    drive(fd, b'1:12', out)
    drive(fd, ENTER, out)
    drew = b''.join(out)

    if b'\x1b[7m"' not in drew:
        return fail('the quote past the cursor did not light its partner')

    # an escaped quote is no delimiter: nothing lights, and Alt-%
    # says so in the bar
    out.clear()
    drive(fd, CTRL_G, out)
    drive(fd, b'2:4', out)
    drive(fd, ENTER, out)
    drew = b''.join(out)

    if b'\x1b[7m"' in drew:
        return fail('the escaped quote lit a partner up')

    out.clear()
    drive(fd, ALT_PERCENT, out)
    drew = b''.join(out)

    if b'No bracket or quote to jump to' not in drew:
        return fail('Alt-% did not say the escaped quote has no partner')

    for _ in range(3):
        drive(fd, CTRL_Q, out, pause=0.1)

    status = wait_exit(pid, fd)
    if status is None:
        return fail('the editor did not quit within 10s')
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        return fail(f'exit status {status:#x}')

    with open(MATCH) as f:
        if f.read() != 'say "hello" now\nan \\" escaped one\n':
            return fail('the match run did not leave the file as written')

    print('smoke: OK - walked the quote match and its Alt-% jump')
    return 0


def main() -> int:
    if len(sys.argv) != 2:
        print(f'usage: {sys.argv[0]} <path-to-vted>', file=sys.stderr)
        return 2
    binary = os.path.abspath(sys.argv[1])
    if not os.access(binary, os.X_OK):
        print(f'smoke: {binary} is not executable', file=sys.stderr)
        return 2

    return quit_smoke(binary) or completion_smoke(binary) \
        or regex_smoke(binary) or confirm_smoke(binary) \
        or all_smoke(binary) or roundtrip_smoke(binary) \
        or dblclick_smoke(binary) or middleclick_smoke(binary) \
        or path_smoke(binary) or saveas_smoke(binary) \
        or gf_smoke(binary) or history_smoke(binary) \
        or electric_smoke(binary) or wrap_smoke(binary) \
        or quote_smoke(binary) or jump_smoke(binary) \
        or goto_smoke(binary) or gocomplete_smoke(binary) \
        or browse_smoke(binary) or prompt_smoke(binary) or match_smoke(binary)


if __name__ == '__main__':
    sys.exit(main())
