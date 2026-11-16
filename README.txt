======================================================================
MU-QTBROWSER v0.1.2 - Lightweight IPC-coupled & Tabbed Web Container
======================================================================

https://forum.puppylinux.com/viewtopic.php?t=17682
--------------------------------------------------------------------------------------------------------------------------------

1. WHAT IS IT AND WHAT IS IT GOOD FOR?
----------------------------------------------------------------------
mu-qtbrowser is a modular, high-performance web container architecture 
specifically designed for lightweight Linux desktop environments like IceWM, 
Openbox, or Fluxbox (such as Linux Mint, antiX, MX Linux, or Puppy Linux).

It offers two core paradigms tailored for low-RAM systems (e.g., 2 GB RAM devices):
1. **IPC-Coupled Suite (`mu-browser` + `mu-controller`)**: Decouples the heavy 
   Chromium rendering engine from a lightweight Vala/GTK3 toolbar via UNIX domain sockets.
2. **Low-RAM Tabbed Variant (`mu-browser-tabs`)**: A pure, lightning-fast C++/Qt6 
   multi-tab browser supporting up to 10 tabs, automated session restore, and minimal 
   memory overhead without IPC communication overhead.

Additionally, `mu-browser` can be run standalone as a single-window web container.


2. ARCHITECTURE & TECHNOLOGIES
----------------------------------------------------------------------
- Backend (`mu-browser`): C++ / Qt6 (QWebEngineView) with internal QLocalServer 
  for multi-instance IPC, custom context menu, X11 window layer positioning, and an 
  interactive mini-editor to generate `.desktop` launchers on-the-fly.
- Tabbed Browser (`mu-browser-tabs`): C++ / Qt6 multi-tab container (max 10 tabs) 
  with automatic `session.json` persistence, integrated bookmark manager, and low-RAM footprint.
- Controller (`mu-controller`): Vala / GTK3 native toolbar communicating via UNIX sockets.
- Launcher Wrapper (`launch-mu-browser.sh`): Manages socket cleanup and startup sequence.


3. MAJOR ACHIEVEMENTS & FEATURES (v0.1.2)
----------------------------------------------------------------------
- Default Homepage: DuckDuckGo search (`https://duckduckgo.com`) configured as default homepage.
- Sidebar Bookmark Button (`★`): Added hover tooltip ("Bookmarks Menu") and full settings visibility control (`chkSideBookmarks`).
- Unicode System Menu Icons: Window system menus feature clean Unicode icons (Minimize `🗕`, Maximize/Restore `🗖`/`🗗`, About `ℹ`, Close `🗙`).
- About Dialog In-App Tab Opening: Clicking links in the About dialog opens them in a new tab of the current browser instance.
- Icon Installation: SVG application icon strictly installed to `/usr/share/` (`/usr/share/pixmaps/` and `/usr/share/icons/hicolor/scalable/apps/`).
- Link Navigation & History Fix: Automatic JavaScript injection strips `target`
  attributes (`_blank`, `_new`) on load, preventing broken history states.
- Custom Qt Context Menu: Replaced default Chromium context menu with a reliable 
  custom menu offering Back, Forward, Reload, Toolbar Toggle, and Exit.
- Low-RAM Tabbed Variant (`mu-browser-tabs`): Supports up to 10 tabs, shortcut keys
  (`Ctrl+T`, `Ctrl+W`), and automated session state recovery (`session.json`).
- Persistent Bookmark Management: Full Qt-based GUI to add, edit, and delete 
  bookmarks stored as clean JSON (`bookmarks.json`).
- Multi-Instance Support: Dynamic unique UNIX domain socket naming (`--socket`) 
  preventing IPC collision issues.
- X11 Window Layers: Full support for desktop background, below, and above window types.


4. SHELL SCRIPT SUPPORT & COMMAND-LINE OPTIONS
----------------------------------------------------------------------
Both `mu-browser` and `mu-browser-tabs` feature extensive command-line argument 
support for fine-grained window control, positioning, and layering:

* `--url <URL>` or `-u <URL>`:
  Set the initial web address (default: `https://duckduckgo.com`).
* `--pos <X,Y>` or `-p <X,Y>`:
  Specify window screen coordinates (e.g., `100,50`).
* `--size <WxH>` or `-s <WxH>`:
  Set initial window dimensions (e.g., `1280x720`).
* `--frameless`:
  Launch in borderless/frameless window mode (`Qt::FramelessWindowHint`).
* `--hide-toolbar`:
  Start with the top control toolbar hidden.
* `--layer <desktop|below|above|normal>`:
  Set X11 window manager stacking layer hints.
* `--socket <NAME>` (mu-browser only):
  Custom UNIX domain socket path or name for isolated IPC communication.

### Wrapper Script (`launch-mu-browser.sh`)
The provided shell script automates the full IPC suite startup:
1. Cleans up stale UNIX domain socket files from `/tmp`.
2. Launches the C++/Qt6 backend (`mu-browser`) with background process management.
3. Waits for socket readiness.
4. Spawns the Vala/GTK3 frontend controller (`mu-controller`), ensuring that 
   terminating either process gracefully closes the entire session.


5. BUILD AND INSTALLATION COMMANDS
----------------------------------------------------------------------
To compile all components (`mu-browser`, `mu-browser-tabs`, `mu-controller`):
  make

To install binaries, wrapper script, icon, and desktop entries system-wide (requires root):
  sudo make install

To build a Debian package (`.deb`):
  make deb

To build a Puppy Linux PET package (`.pet`):
  make pet

To clean up build files:
  make clean


6. USAGE EXAMPLES
----------------------------------------------------------------------
Launch complete browser suite (Backend + GTK Controller):
   ./launch-mu-browser.sh

Launch standalone Qt6 browser without GTK Controller:
   ./build/mu-browser --url "https://duckduckgo.com"

Launch low-RAM tabbed browser variant:
   ./build/mu-browser-tabs --url "https://duckduckgo.com"

Launch isolated frameless desktop layer instance (Backend):
   ./build/mu-browser --socket /tmp/browser1.sock --url "https://example.com" --pos 100,100 --size 800x600 --frameless --hide-toolbar --layer desktop

Connect Vala controller to specific instance socket:
   ./mu-controller /tmp/browser1.sock <BACKEND_PID>
