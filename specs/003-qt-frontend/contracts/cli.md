# Contract: The `cutenes` command line

**Feature**: 003 | **Authority**: spec FR-001, FR-004, FR-071

```text
cutenes [--fullscreen] [--library DIR] [--load-state SLOT|FILE]
        [--api [--api-port N]] [--mcp [--mcp-port N]]
        [--help] [--version] [ROM]
```

| Option | Effect |
|---|---|
| `ROM` | Boot this `.nes` file and skip the library browser. Any path works; the file need not be in the library |
| `--fullscreen` | Start in fullscreen |
| `--library DIR` | Use `DIR` as the library for this run only, without changing the setting |
| `--load-state SLOT\|FILE` | After booting `ROM`, load slot 1-10 or a state file. It needs `ROM` |
| `--api`, `--api-port N` | Turn the API server on for this run, on its port setting or `N` |
| `--mcp`, `--mcp-port N` | Turn the MCP server on for this run (which turns the API on too), on its port setting or `N` |
| `--help`, `--version` | Print and exit 0 |

- Without `ROM`, the application opens the library browser (spec US1).
- **Errors**:
  - An unknown option prints usage and exits with code 2. On Windows, where
    the GUI has no console, a message box shows the usage instead.
  - An invalid `ROM` (missing, unreadable or unsupported) opens the library
    browser and shows the error. It does not exit.
- **Run-only options.** The `--api` and `--mcp` options change the servers
  for this run only, and the saved settings are left alone.
- **File association.** The operating system's "open with" passes the ROM
  as `ROM`. On macOS, file-open events arrive as `QFileOpenEvent` and are
  handled the same way.
- **Token file.** The MCP token file and its location are the ones that
  `cutenes-server --attach` reads (002 transports.md, "GUI" run mode).
