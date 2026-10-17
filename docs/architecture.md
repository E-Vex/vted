
            
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
  vted_file.c      OpenFile / SaveFile
  vted.c           editor core: key dispatch, editing, search

by E-Vex                      
