# myfancyeditor

A ncurses-based text editor for Ubuntu/Linux, modelled after Emacs. Written in C.

## Features

- **Emacs-style key bindings** for comfortable editing
- **Multiple buffers** — open as many files or shells as you need and switch between them
- **File I/O** — open, edit and save files
- **Shell buffers** — host a live `bash` session inside a buffer (via PTY)
- **JavaScript scripting** — built-in [Duktape](https://duktape.org/) engine lets you write macros and automate editing tasks
- **Coloured modeline** and minibuffer command area

## Dependencies

| Library | Ubuntu package |
|---------|----------------|
| ncursesw | `libncurses-dev` |
| duktape  | `duktape-dev` |
| libutil (forkpty) | *(part of glibc)* |

```
sudo apt-get install libncurses-dev duktape-dev
```

## Building

```
make
```

The binary `myfancyeditor` is placed in the project root.

## Usage

```
./myfancyeditor
```

Press **F1** at any time to toggle the key-binding help overlay.

## Key Bindings

### Cursor movement
| Key | Action |
|-----|--------|
| `C-f` / `→` | Forward char |
| `C-b` / `←` | Backward char |
| `C-n` / `↓` | Next line |
| `C-p` / `↑` | Previous line |
| `C-a` / `Home` | Beginning of line |
| `C-e` / `End` | End of line |
| `M-f` | Forward word |
| `M-b` | Backward word |
| `M-<` | Beginning of buffer |
| `M->` | End of buffer |
| `PgUp` / `PgDn` | Scroll page |

### Editing
| Key | Action |
|-----|--------|
| `C-d` / `Del` | Delete forward |
| `Backspace` | Delete backward |
| `C-k` | Kill line (to end) |
| `C-y` | Yank (paste) |
| `M-d` | Kill word forward |
| `Enter` | New line |
| `Tab` | Insert tab |

### C-x commands
| Key | Action |
|-----|--------|
| `C-x C-s` | Save current buffer |
| `C-x C-f` | Find (open) file |
| `C-x C-c` | Quit |
| `C-x b` | Switch buffer (by name) |
| `C-x k` | Kill buffer |
| `C-x s` | Open a shell buffer |

### M-x commands (execute via minibuffer)
| Command | Action |
|---------|--------|
| `list-buffers` | Show all open buffers |
| `open-shell` | Open a bash shell buffer |
| `eval-js <code>` | Evaluate JavaScript |

### Other
| Key | Action |
|-----|--------|
| `C-g` | Cancel / quit prefix |
| `C-l` | Redraw display |
| `F1` | Toggle help overlay |

## JavaScript Scripting

Run JavaScript via `M-x eval-js <code>` or from a script file.

### `editor` API

```javascript
editor.message(str)             // display a message in the minibuffer
editor.getCurrentBufferName()   // → name of the current buffer
editor.listBuffers()            // → array of buffer names
editor.switchBuffer(name)       // switch to a named buffer
editor.newBuffer(name)          // create a new buffer
editor.insertText(str)          // insert text at the cursor
editor.getBufferContent()       // → full text of the current buffer
editor.setBufferContent(str)    // replace the current buffer content
editor.openFile(filename)       // open a file into a buffer
editor.saveFile()               // save the current buffer
editor.getCurrentLine()         // → 1-based line number
editor.getCurrentCol()          // → 1-based column number
```

### Example macros

```javascript
// Insert a timestamp
editor.insertText(new Date().toISOString());

// Duplicate the buffer content
var c = editor.getBufferContent(); editor.setBufferContent(c + "\n" + c);

// Show all open buffers
editor.message(editor.listBuffers().join(", "));
```

## Shell Buffers

Press `C-x s` (or `M-x open-shell`) to open a live bash shell. All keystrokes
are forwarded to the shell. Use `C-x b` to switch back to a file buffer.
The `C-x` prefix always works even inside a shell buffer so you can manage
buffers without leaving the editor.

## Project Structure

```
src/
  main.c        — entry point, signal handlers, main loop
  editor.{h,c}  — editor state, buffer pool, minibuffer FSM
  buffer.{h,c}  — line-array text buffer operations
  ui.{h,c}      — ncursesw UI: edit window, modeline, minibuffer
  keys.{h,c}    — key dispatch and Emacs key bindings
  file_ops.{h,c}— file open/save helpers
  shell_buf.{h,c}— PTY-based shell buffer support
  script.{h,c}  — Duktape JavaScript scripting engine
Makefile
```
