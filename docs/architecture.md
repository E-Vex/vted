
            
THIS IS THE PROJECT LIFECYCLE.
THE PROJECT IS CURRENTLY UNDER DEVELOPMENT,
AND THIS IS NOT THE FINAL ARCHITECTURE.
               
               
               
                 PROGRAM START                               vted docs                    
                       │
                       ▼
          SaveTerminalCurrentSettings()
                       │
                       ├── tcgetattr()
                       │       │
                       │       ▼
                       │   old_terminal_settings
                       │
                       └── atexit(RestoreTerminalSettings)
                       │
                       ▼
             EnableTerminalRawMode()
                       │
                       ▼
                terminal = raw
                       │
                       ▼
             EnterToAlternateScreenBuffer()
                       │
                       ▼
                  InitBuffer()
                       │
                       ├── argv[1] exists ──► OpenFile() ──► rows in the buffer
                       │
                       ▼
                InitVtedEditor()
                       │
                       ▼
                   Editor runs
                       │
                 ┌─────┴─────────────────────────────┐
                 │            main loop              │
                 │                                   │
                 │   ReadKeyPress() ──► key dispatch │
                 │        │                          │
                 │        ├── movement ──► MoveCursor │
                 │        ├── typing ───► row ops     │
                 │        ├── Ctrl-S ───► SaveFile()  │
                 │        ├── Ctrl-G ───► go to line  │
                 │        └── Ctrl-F ───► search      │
                 │                                   │
                 │        RefreshScreen()             │
                 │        (buffer + viewport + bars) │
                 │                                   │
                 └─────┬─────────────────────────────┘
                       │
                       ▼
                  program exit
                       │
                       ▼
          RestoreTerminalSettings()
                       │
                       ▼
              original terminal
                       │
                       ▼
                      END

modules:
  vted_terminal.c  raw mode, settings save/restore, KillApp
  vted_keyboard.c  byte reading, escape sequence parsing
  vted_screen.c    viewport, scrolling, rendering, bars
  vted_row.c       text buffer, row editing primitives
  vted_undo.c      the undo and redo history
  vted_file.c      OpenFile / ReadStream / SaveFile
  vted_syntax.c    coloring text by the language of the file
  vted_color.c     colors: hex, names, palette numbers, what a terminal shows
  vted_theme.c     color themes: built-in ones and theme files
  vted_config.c    the vtedrc settings file
  vted_buffers.c   the open files, switching between them
  vted_cli.c       the command line: options, usage, version
  vted.c           editor core: key dispatch, editing, search

by E-Vex                      
