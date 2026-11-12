# 💾 File Allocation Studio

<p align="center">
  **An Interactive File Allocation Simulator**
  <br>
  Exploring Operating System file allocation techniques through a real-time disk visualization interface.
</p>

---

## 📌 About the Project

File Allocation Studio is an interactive simulation of three fundamental file allocation techniques used in Operating Systems: **Contiguous Allocation, Linked Allocation, and Indexed Allocation**.

The project demonstrates how files are stored and managed within disk blocks, allowing users to create and delete files, experiment with different allocation strategies, and observe how disk space is utilized.

It combines a C-based allocation engine with a browser-based interface to make file system concepts easier to understand through practical visualization.

**Note:** This is an educational simulation using an in-memory disk model. It does not modify actual files or disk partitions.

## ✨ Features

- **Three Allocation Techniques:** Simulates contiguous, linked, and indexed file allocation.
- **Interactive Disk Visualization:** Displays allocated and free disk blocks in real time.
- **File Creation:** Create files with custom names, sizes, and allocation methods.
- **File Deletion:** Delete existing files and automatically release their allocated blocks.
- **Disk Management:** Configure disk capacity from 16 to 256 blocks.
- **Live Statistics:** Monitor total capacity, used blocks, free blocks, active files, and allocation failures.
- **Block Inspection:** Select individual blocks to inspect their allocation status.
- **Allocation Validation:** Handles insufficient disk space, fragmentation, duplicate filenames, and other allocation errors.
- **Disk Reset:** Reset the simulation to start a fresh allocation experiment.

## 🧠 Allocation Techniques Implemented

### 1. Contiguous Allocation
- Stores file blocks in consecutive disk locations.
- Provides simple and fast sequential access.
- Requires a sufficiently large continuous free space.

### 2. Linked Allocation
- Stores file blocks at different disk locations.
- Connects blocks using pointers to form a linked chain.
- Eliminates the requirement for continuous free space.

### 3. Indexed Allocation
- Uses a separate index block to store the addresses of a file's data blocks.
- Supports direct access to file blocks.
- Requires additional storage for the index block.

## 🛠️ Tech Stack

| Technology | Purpose |
|---|---|
| C | Core file allocation algorithms and disk simulation |
| Python | Backend server and C integration |
| HTML5 | Web interface structure |
| CSS3 | Styling and responsive layout |
| JavaScript | Interactive UI and real-time updates |
| GCC / MinGW | Compiling the C backend |

## 🏗️ Project Architecture

The project uses a modular architecture:

- **main.c:** Implements the core allocation algorithms, disk management, file operations, and validation.
- **fas_web.c:** Connects the C allocation engine to the web interface.
- **server.py:** Runs the local HTTP server and handles communication between the frontend and C backend.
- **index.html:** Defines the application interface.
- **style.css:** Handles the visual design and layout.
- **app.js:** Manages user interactions, API requests, and dynamic disk visualization.

## 🚀 Getting Started

### Prerequisites

Make sure you have installed:

- Python 3
- GCC / MinGW GCC
- Visual Studio Code (recommended)

### Installation

**1. Clone the repository**

```bash
git clone https://github.com/YOUR-USERNAME/YOUR-REPOSITORY.git
```

**2. Navigate to the project directory**

```bash
cd "custom file allocation system"
```

**3. Start the application**

```bash
python server.py
```

**4. Open the application**

Visit the following URL in your browser:

```text
http://127.0.0.1:8000
```

The Python server automatically compiles the C backend on its first run.

## 💻 Running the C Program Independently

The allocation engine can also run as a standalone terminal application.

Compile:

```bash
gcc -std=c11 -Wall -Wextra -o fas.exe main.c
```

Run:

```bash
.\fas.exe
```

Run automated self-tests:

```bash
.\fas.exe --test
```

Disable ANSI colors:

```bash
.\fas.exe --nocolor
```

## 📚 Operating Systems Concepts

This project explores several important Operating Systems concepts:

- File allocation methods
- Disk space management
- Free space management
- File directory structures
- Memory and block management
- Disk fragmentation
- Allocation overhead
- Data structures and linked lists
- File creation and deletion operations

## 🎯 Project Objective

The primary objective is to understand how different file allocation strategies organize data on disk, compare their advantages and limitations, and explore how operating systems manage available storage efficiently.

## 👩‍💻 Author

**Akshaya Shree S**

Computer Science and Engineering Student

---

*Built as an Operating Systems Mini Project.*
