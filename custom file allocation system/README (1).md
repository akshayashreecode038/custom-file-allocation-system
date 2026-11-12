# File Allocation Studio

A simple browser interface for the C file allocation simulation. The page uses HTML, CSS, and JavaScript. The Python server hosts the page and connects browser actions to functions in main.c through a compiled C library.

## Run

1. Install Python 3 and MinGW GCC, with gcc available on PATH.
2. Open this folder in VS Code.
3. In the VS Code terminal, run: python server.py
4. Open http://127.0.0.1:8000 in your browser.

On first run, Python compiles fas_web.c into fas_backend.dll. The simulated disk starts with 32 blocks. Its contents live in C memory and reset when the server restarts.

main.c remains the standalone terminal program. Build and run it as before:

    gcc -std=c11 -Wall -Wextra -o fas.exe main.c
    .\fas.exe

This educational simulation does not create, change, or delete real files on your computer.
