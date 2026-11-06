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
CTRL_Z = b'\x1a'
BACKSPACE = b'\x7f'
ENTER = b'\r'
ALT_DOT = b'\x1b.'    # Alt-.: the buffer after the current one
ALT_SLASH = b'\x1b/'  # Alt-/: complete the word left of the cursor
ESC = b'\x1b'         # ESC: cancel the prompt
ALT_O = b'\x1bo'    # Alt-o: the file the cursor sits on

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
        or gf_smoke(binary)


if __name__ == '__main__':
    sys.exit(main())
