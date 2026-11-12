/*
 * ============================================================================
 *  CUSTOM FILE ALLOCATION SYSTEM  -  Operating Systems Project Prototype
 * ============================================================================
 *  Simulates three classic file allocation methods on an in-memory "disk":
 *      1. Contiguous Allocation
 *      2. Linked Allocation
 *      3. Indexed Allocation
 *
 *  IMPORTANT: This is a SIMULATION. No real files, partitions, or operating
 *  system filesystem calls are used. The "disk" is just an array in RAM.
 *
 *  Build (GCC / MinGW):   gcc -std=c11 -Wall -Wextra -o fas.exe main.c
 *  Run:                   fas.exe            (interactive menu)
 *                         fas.exe --test     (automated self-tests)
 *                         fas.exe --nocolor  (disable ANSI colors)
 *
 *  Design notes
 *  ------------
 *  - One shared disk model (Disk / DiskBlock) is used by ALL three methods.
 *  - Every block records its owner (file slot) so a block can never be
 *    allocated twice and one file can never free another file's blocks.
 *  - Overhead accounting:
 *        Contiguous : 0 blocks (only start + length kept in the directory)
 *        Linked     : 0 extra blocks, but POINTER_BYTES per block are lost
 *                     inside each data block (reported in bytes)
 *        Indexed    : 1 extra index block per file (counted as a used block)
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* ----------------------------- Configuration ------------------------------ */
#define MAX_BLOCKS        256   /* fixed maximum disk capacity (blocks)        */
#define DEFAULT_BLOCKS    32    /* disk size used until the user changes it    */
#define MAX_FILES         64    /* fixed maximum number of files               */
#define MAX_NAME          20    /* file name buffer (19 chars + '\0')          */
#define INPUT_SIZE        128   /* size of every input buffer                  */
#define BLOCK_SIZE_BYTES  512   /* simulated bytes per block                   */
#define POINTER_BYTES     4     /* simulated bytes per block pointer           */
#define MAX_INDEX_ENTRIES (BLOCK_SIZE_BYTES / POINTER_BYTES) /* 128 pointers   */
#define FREE_BLOCK        (-1)  /* owner value of a free block                 */
#define END_OF_CHAIN      (-1)  /* "NULL" next pointer                         */

/* ------------------------------ ANSI colors ------------------------------- */
#define C_RESET   "\x1b[0m"
#define C_BOLD    "\x1b[1m"
#define C_RED     "\x1b[91m"
#define C_GREEN   "\x1b[92m"
#define C_YELLOW  "\x1b[93m"
#define C_BLUE    "\x1b[94m"
#define C_MAGENTA "\x1b[95m"
#define C_CYAN    "\x1b[96m"

static int useColor = 1;   /* cleared if the terminal does not support ANSI */

/* Returns the color code, or an empty string if colors are disabled. */
static const char *clr(const char *code)
{
    return useColor ? code : "";
}

/* ------------------------------- Data types ------------------------------- */
typedef enum {
    ALLOC_CONTIGUOUS = 1,
    ALLOC_LINKED     = 2,
    ALLOC_INDEXED    = 3
} AllocMethod;

typedef enum {
    RES_OK = 0,
    RES_BAD_NAME,        /* invalid file name                                  */
    RES_DUPLICATE,       /* file name already exists                           */
    RES_BAD_SIZE,        /* zero or negative size                              */
    RES_EXCEEDS_DISK,    /* size larger than the whole disk                    */
    RES_BAD_METHOD,      /* method not 1..3                                    */
    RES_TABLE_FULL,      /* no free file slot                                  */
    RES_NO_SPACE,        /* not enough free blocks (incl. index block)         */
    RES_FRAGMENTED,      /* enough free blocks but no contiguous run           */
    RES_INDEX_LIMIT,     /* file needs more pointers than one index block holds*/
    RES_CORRUPT          /* internal consistency problem (should never happen) */
} AllocResult;

/* One simulated disk block. */
typedef struct {
    int owner;     /* file slot that owns this block, or FREE_BLOCK            */
    int next;      /* linked allocation: next block of the chain, or END       */
    int isIndex;   /* 1 if this block is an index block (indexed allocation)   */
} DiskBlock;

/* The simulated disk. */
typedef struct {
    DiskBlock blocks[MAX_BLOCKS];
    int totalBlocks;
} Disk;

/* Directory entry / file control block. */
typedef struct {
    int         inUse;                 /* 1 if this slot holds a file          */
    char        name[MAX_NAME];
    int         sizeBlocks;            /* requested size in DATA blocks        */
    AllocMethod method;
    int         startBlock;            /* contiguous: first block              */
    int         endBlock;              /* contiguous: last block               */
    int         headBlock;             /* linked: first block of chain         */
    int         indexBlock;            /* indexed: block number of index block */
    int         blockCount;            /* number of DATA blocks recorded       */
    int         blocks[MAX_BLOCKS];    /* data blocks in logical order.
                                          For indexed files this array is the
                                          CONTENT of the index block.          */
} File;

/* Everything that makes up one simulated system (disk + directory + stats). */
typedef struct {
    Disk disk;
    File files[MAX_FILES];
    int  fileCount;
    int  allocFailures;          /* failed allocation attempts                 */
    int  fragmentationFailures;  /* failures caused by external fragmentation  */
} System;

/* Per-method usage summary. */
typedef struct {
    int files;
    int dataBlocks;
    int indexBlocks;
    int pointerBytes;   /* linked allocation pointer overhead in bytes         */
} MethodUsage;

/* ------------------------------ Small helpers ----------------------------- */
static const char *methodName(AllocMethod m)
{
    switch (m) {
        case ALLOC_CONTIGUOUS: return "Contiguous";
        case ALLOC_LINKED:     return "Linked";
        case ALLOC_INDEXED:    return "Indexed";
        default:               return "Unknown";
    }
}

static const char *methodColor(AllocMethod m)
{
    switch (m) {
        case ALLOC_CONTIGUOUS: return C_CYAN;
        case ALLOC_LINKED:     return C_YELLOW;
        case ALLOC_INDEXED:    return C_MAGENTA;
        default:               return C_RESET;
    }
}

static void printError(const char *msg)
{
    printf("%s[ERROR]%s %s\n", clr(C_RED), clr(C_RESET), msg);
}

static void printOk(const char *msg)
{
    printf("%s[OK]%s %s\n", clr(C_GREEN), clr(C_RESET), msg);
}

static void printInfo(const char *msg)
{
    printf("%s[INFO]%s %s\n", clr(C_CYAN), clr(C_RESET), msg);
}

/* Enable ANSI escape sequences on Windows 10/11 consoles. */
static void enableAnsi(void)
{
#ifdef _WIN32
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode) ||
        !SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        useColor = 0;   /* terminal cannot do colors: fall back to plain text */
    }
#endif
}

/* ------------------------------ Safe input -------------------------------- */

/* Called when stdin is closed (Ctrl+Z / Ctrl+D / end of piped input). */
static void handleEof(void)
{
    printf("\nInput closed. Exiting program.\n");
    exit(0);
}

/*
 * Reads one line using fgets().
 * Returns 0 = OK, 1 = line was too long (rest of line discarded), -1 = EOF.
 */
static int readLine(char *buf, size_t size)
{
    size_t len;
    int ch;

    if (fgets(buf, (int)size, stdin) == NULL)
        return -1;

    len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
        return 0;
    }
    if (feof(stdin))            /* last line without a trailing newline */
        return 0;

    /* Buffer overflow attempt: throw away the rest of the line. */
    while ((ch = getchar()) != '\n' && ch != EOF)
        ;
    return 1;
}

/* Removes leading and trailing whitespace in place. */
static void trim(char *s)
{
    size_t len = strlen(s);
    size_t start = 0;

    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';
    while (s[start] != '\0' && isspace((unsigned char)s[start]))
        start++;
    if (start > 0)
        memmove(s, s + start, strlen(s + start) + 1);
}

/* Prompts for a text line. Returns 1 on success, 0 on invalid input. */
static int promptString(const char *prompt, char *out, size_t size)
{
    int rc;
    printf("%s", prompt);
    rc = readLine(out, size);
    if (rc < 0) handleEof();
    if (rc > 0) {
        printError("Input too long. Extra characters were discarded.");
        return 0;
    }
    trim(out);
    return 1;
}

/* Prompts for an integer using strtol() validation. Returns 1 on success. */
static int promptInt(const char *prompt, int *out)
{
    char buf[INPUT_SIZE];
    char *end;
    long v;
    int rc;

    printf("%s", prompt);
    rc = readLine(buf, sizeof buf);
    if (rc < 0) handleEof();
    if (rc > 0) {
        printError("Input too long. Extra characters were discarded.");
        return 0;
    }
    trim(buf);
    if (buf[0] == '\0') {
        printError("Input cannot be empty.");
        return 0;
    }
    errno = 0;
    v = strtol(buf, &end, 10);
    if (end == buf || *end != '\0') {
        printError("Please enter a whole number (no letters or symbols).");
        return 0;
    }
    if (errno == ERANGE || v > INT_MAX || v < INT_MIN) {
        printError("Number is out of range.");
        return 0;
    }
    *out = (int)v;
    return 1;
}

/* Waits for Enter so the user can read the output. */
static void pauseForEnter(void)
{
    char buf[INPUT_SIZE];
    int rc;
    printf("\nPress Enter to continue...");
    rc = readLine(buf, sizeof buf);
    if (rc < 0) handleEof();
}

/* ------------------------------ Disk queries ------------------------------ */
static int countFreeBlocks(const Disk *d)
{
    int i, n = 0;
    for (i = 0; i < d->totalBlocks; i++)
        if (d->blocks[i].owner == FREE_BLOCK)
            n++;
    return n;
}

/* Length of the largest run of consecutive free blocks (and its start). */
static int largestFreeRun(const Disk *d, int *startOut)
{
    int i, run = 0, best = 0, bestStart = -1, runStart = -1;

    for (i = 0; i < d->totalBlocks; i++) {
        if (d->blocks[i].owner == FREE_BLOCK) {
            if (run == 0) runStart = i;
            run++;
            if (run > best) { best = run; bestStart = runStart; }
        } else {
            run = 0;
        }
    }
    if (startOut) *startOut = bestStart;
    return best;
}

static int namesEqual(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++; b++;
    }
    return *a == *b;
}

/* Returns the slot of the file with this name (case-insensitive) or -1. */
static int findFileByName(const System *s, const char *name)
{
    int i;
    for (i = 0; i < MAX_FILES; i++)
        if (s->files[i].inUse && namesEqual(s->files[i].name, name))
            return i;
    return -1;
}

static int findFreeSlot(const System *s)
{
    int i;
    for (i = 0; i < MAX_FILES; i++)
        if (!s->files[i].inUse)
            return i;
    return -1;
}

/* Valid names: 1..19 characters of letters, digits, '_', '-', '.'. */
static int validateFilename(const char *name)
{
    size_t i, len = strlen(name);
    if (len == 0 || len >= MAX_NAME)
        return 0;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.'))
            return 0;
    }
    return 1;
}

/* Number of separate fragments a file is split into (adjacent = same one). */
static int countFragments(const File *f)
{
    int i, frags = 0;
    for (i = 0; i < f->blockCount; i++)
        if (i == 0 || f->blocks[i] != f->blocks[i - 1] + 1)
            frags++;
    return frags;
}

/* ------------------------------ Disk setup -------------------------------- */

/* Resets the disk: every block free, directory empty, statistics zeroed. */
static void initializeDisk(System *s, int totalBlocks)
{
    int i;
    memset(s, 0, sizeof *s);
    s->disk.totalBlocks = totalBlocks;
    for (i = 0; i < MAX_BLOCKS; i++) {
        s->disk.blocks[i].owner   = FREE_BLOCK;
        s->disk.blocks[i].next    = END_OF_CHAIN;
        s->disk.blocks[i].isIndex = 0;
    }
}

/*
 * The ONLY function that marks a block as allocated. It refuses to touch a
 * block that is out of range or already owned, so double allocation is
 * impossible. Returns 1 on success, 0 on refusal.
 */
static int claimBlock(Disk *d, int block, int fileId)
{
    if (block < 0 || block >= d->totalBlocks)
        return 0;
    if (d->blocks[block].owner != FREE_BLOCK)
        return 0;
    d->blocks[block].owner   = fileId;
    d->blocks[block].next    = END_OF_CHAIN;
    d->blocks[block].isIndex = 0;
    return 1;
}

/* Checks that a linked file's chain is valid: right length, right owner, no cycle. */
static int validateLinkedChain(const System *s, int id)
{
    const File *f = &s->files[id];
    int visited[MAX_BLOCKS];
    int cur = f->headBlock, steps = 0;

    memset(visited, 0, sizeof visited);
    while (cur != END_OF_CHAIN) {
        if (cur < 0 || cur >= s->disk.totalBlocks) return 0;  /* wild pointer  */
        if (visited[cur])                          return 0;  /* cycle         */
        if (s->disk.blocks[cur].owner != id)       return 0;  /* foreign block */
        if (steps >= f->blockCount)                return 0;  /* chain too long*/
        if (f->blocks[steps] != cur)               return 0;  /* order mismatch*/
        visited[cur] = 1;
        cur = s->disk.blocks[cur].next;
        steps++;
    }
    return steps == f->blockCount;                            /* chain too short? */
}

/* ----------------------------- Allocation algorithms ---------------------- */

/*
 * CONTIGUOUS (first-fit): scan for the first run of 'size' consecutive free
 * blocks, then claim them all. The caller has already verified such a run.
 */
static AllocResult contiguousAllocation(System *s, int id)
{
    File *f = &s->files[id];
    Disk *d = &s->disk;
    int i, run = 0, start = -1;

    for (i = 0; i < d->totalBlocks; i++) {
        if (d->blocks[i].owner == FREE_BLOCK) {
            if (run == 0) start = i;
            run++;
            if (run == f->sizeBlocks) break;
        } else {
            run = 0;
        }
    }
    if (run < f->sizeBlocks)
        return RES_FRAGMENTED;

    for (i = 0; i < f->sizeBlocks; i++) {
        if (!claimBlock(d, start + i, id))
            return RES_CORRUPT;
        f->blocks[f->blockCount++] = start + i;
    }
    f->startBlock = start;
    f->endBlock   = start + f->sizeBlocks - 1;
    return RES_OK;
}

/*
 * LINKED: take ANY free blocks (lowest numbers first). Each block's 'next'
 * field points to the following block; the last one gets END_OF_CHAIN.
 */
static AllocResult linkedAllocation(System *s, int id)
{
    File *f = &s->files[id];
    Disk *d = &s->disk;
    int i, prev = END_OF_CHAIN;

    for (i = 0; i < d->totalBlocks && f->blockCount < f->sizeBlocks; i++) {
        if (d->blocks[i].owner != FREE_BLOCK)
            continue;
        if (!claimBlock(d, i, id))
            return RES_CORRUPT;
        f->blocks[f->blockCount++] = i;
        if (prev != END_OF_CHAIN)
            d->blocks[prev].next = i;      /* link previous block to this one  */
        else
            f->headBlock = i;              /* first block becomes the head     */
        prev = i;
    }
    if (f->blockCount != f->sizeBlocks)
        return RES_CORRUPT;
    d->blocks[prev].next = END_OF_CHAIN;   /* terminate the chain              */

    return validateLinkedChain(s, id) ? RES_OK : RES_CORRUPT;
}

/*
 * INDEXED: first claim ONE index block, then claim 'size' data blocks.
 * f->blocks[] holds the data-block addresses stored in the index block.
 */
static AllocResult indexedAllocation(System *s, int id)
{
    File *f = &s->files[id];
    Disk *d = &s->disk;
    int i, idx = -1;

    for (i = 0; i < d->totalBlocks; i++) {      /* 1) find the index block     */
        if (d->blocks[i].owner == FREE_BLOCK) { idx = i; break; }
    }
    if (idx < 0 || !claimBlock(d, idx, id))
        return RES_CORRUPT;
    d->blocks[idx].isIndex = 1;
    f->indexBlock = idx;

    for (i = 0; i < d->totalBlocks && f->blockCount < f->sizeBlocks; i++) {
        if (d->blocks[i].owner != FREE_BLOCK)   /* 2) claim the data blocks    */
            continue;
        if (!claimBlock(d, i, id))
            return RES_CORRUPT;
        f->blocks[f->blockCount++] = i;         /* store pointer in index      */
    }
    return (f->blockCount == f->sizeBlocks) ? RES_OK : RES_CORRUPT;
}

/*
 * Releases every block recorded for a file and clears its directory entry.
 * A block is only freed if the disk says THIS file owns it, so a file can never
 * free another file's blocks. Returns the number of blocks freed.
 */
static int freeAllocatedBlocks(System *s, int id)
{
    File *f = &s->files[id];
    Disk *d = &s->disk;
    int i, freed = 0;

    for (i = 0; i < f->blockCount; i++) {
        int b = f->blocks[i];
        if (b >= 0 && b < d->totalBlocks && d->blocks[b].owner == id) {
            d->blocks[b].owner   = FREE_BLOCK;
            d->blocks[b].next    = END_OF_CHAIN;
            d->blocks[b].isIndex = 0;
            freed++;
        } else {
            printf("%s[WARNING]%s block %d is not owned by file '%s'; not freed.\n",
                   clr(C_YELLOW), clr(C_RESET), b, f->name);
        }
    }
    if (f->method == ALLOC_INDEXED && f->indexBlock >= 0 &&
        f->indexBlock < d->totalBlocks && d->blocks[f->indexBlock].owner == id) {
        d->blocks[f->indexBlock].owner   = FREE_BLOCK;
        d->blocks[f->indexBlock].next    = END_OF_CHAIN;
        d->blocks[f->indexBlock].isIndex = 0;
        freed++;
    }
    memset(f, 0, sizeof *f);        /* wipe metadata so nothing stale remains  */
    if (s->fileCount > 0) s->fileCount--;
    return freed;
}

/*
 * Common entry point used by the menu, the comparison demo and the self-tests.
 * Performs ALL validation, then calls one of the three allocators.
 */
static AllocResult allocateFile(System *s, const char *name, int size,
                                AllocMethod method, int *outId)
{
    int slot, needed;
    AllocResult res;
    File *f;

    if (outId) *outId = -1;

    if (!validateFilename(name))             return RES_BAD_NAME;
    if (findFileByName(s, name) >= 0)        return RES_DUPLICATE;
    if (size <= 0)                           return RES_BAD_SIZE;
    if (method < ALLOC_CONTIGUOUS || method > ALLOC_INDEXED)
                                             return RES_BAD_METHOD;
    if (size > s->disk.totalBlocks) { s->allocFailures++; return RES_EXCEEDS_DISK; }

    slot = findFreeSlot(s);
    if (slot < 0) { s->allocFailures++; return RES_TABLE_FULL; }

    if (method == ALLOC_INDEXED && size > MAX_INDEX_ENTRIES) {
        s->allocFailures++;
        return RES_INDEX_LIMIT;
    }

    needed = size + (method == ALLOC_INDEXED ? 1 : 0);   /* index block counts */
    if (countFreeBlocks(&s->disk) < needed) {
        s->allocFailures++;
        return RES_NO_SPACE;
    }
    if (method == ALLOC_CONTIGUOUS && largestFreeRun(&s->disk, NULL) < size) {
        s->allocFailures++;
        s->fragmentationFailures++;
        return RES_FRAGMENTED;
    }

    /* Build the directory entry first so a rollback can use freeAllocatedBlocks */
    f = &s->files[slot];
    memset(f, 0, sizeof *f);
    f->inUse      = 1;
    snprintf(f->name, sizeof f->name, "%s", name);
    f->sizeBlocks = size;
    f->method     = method;
    f->startBlock = f->endBlock = f->headBlock = f->indexBlock = -1;
    s->fileCount++;

    switch (method) {
        case ALLOC_CONTIGUOUS: res = contiguousAllocation(s, slot); break;
        case ALLOC_LINKED:     res = linkedAllocation(s, slot);     break;
        default:               res = indexedAllocation(s, slot);    break;
    }

    if (res != RES_OK) {                  /* roll back any partial allocation  */
        freeAllocatedBlocks(s, slot);
        s->allocFailures++;
        return res;
    }
    if (outId) *outId = slot;
    return RES_OK;
}

/* Deletes by name. Returns 1 if deleted, 0 if no such file. */
static int deleteFileByName(System *s, const char *name, int *freedOut)
{
    int id = findFileByName(s, name);
    int freed;
    if (id < 0) return 0;
    freed = freeAllocatedBlocks(s, id);
    if (freedOut) *freedOut = freed;
    return 1;
}

/* --------------------------- Integrity verification ----------------------- */

/*
 * Cross-checks the directory against the disk. Returns 1 if everything is
 * consistent: each used block has exactly one owner that lists it, each file's
 * blocks are owned by it, chains are valid, and counters match.
 */
static int verifyConsistency(const System *s)
{
    int owned[MAX_BLOCKS];
    int i, j, live = 0;

    memset(owned, 0, sizeof owned);

    for (i = 0; i < MAX_FILES; i++) {
        const File *f = &s->files[i];
        if (!f->inUse) continue;
        live++;

        for (j = 0; j < f->blockCount; j++) {
            int b = f->blocks[j];
            if (b < 0 || b >= s->disk.totalBlocks) return 0;
            if (s->disk.blocks[b].owner != i)      return 0;
            owned[b]++;
        }
        if (f->method == ALLOC_INDEXED) {
            int ib = f->indexBlock;
            if (ib < 0 || ib >= s->disk.totalBlocks)  return 0;
            if (s->disk.blocks[ib].owner != i)        return 0;
            if (!s->disk.blocks[ib].isIndex)          return 0;
            owned[ib]++;
        }
        if (f->method == ALLOC_LINKED && !validateLinkedChain(s, i))
            return 0;
        if (f->method == ALLOC_CONTIGUOUS) {
            for (j = 1; j < f->blockCount; j++)
                if (f->blocks[j] != f->blocks[j - 1] + 1) return 0;
        }
    }
    if (live != s->fileCount) return 0;

    for (i = 0; i < s->disk.totalBlocks; i++) {
        int o = s->disk.blocks[i].owner;
        if (o == FREE_BLOCK) {
            if (owned[i] != 0) return 0;
        } else {
            if (o < 0 || o >= MAX_FILES || !s->files[o].inUse) return 0;
            if (owned[i] != 1) return 0;
        }
    }
    return 1;
}

/* Collects per-method usage numbers. */
static MethodUsage getMethodUsage(const System *s, AllocMethod m)
{
    MethodUsage u;
    int i;
    memset(&u, 0, sizeof u);
    for (i = 0; i < MAX_FILES; i++) {
        const File *f = &s->files[i];
        if (!f->inUse || f->method != m) continue;
        u.files++;
        u.dataBlocks += f->blockCount;
        if (m == ALLOC_INDEXED) u.indexBlocks++;
        if (m == ALLOC_LINKED)  u.pointerBytes += f->blockCount * POINTER_BYTES;
    }
    return u;
}

/* ------------------------------- Display ---------------------------------- */

/* Prints the block map with colors and (optionally) a legend. */
static void displayDiskMap(const System *s, const char *title, int showLegend)
{
    const Disk *d = &s->disk;
    int i, w = (d->totalBlocks > 100) ? 3 : 2;

    printf("\n%s%s--- %s ---%s\n", clr(C_BOLD), clr(C_CYAN), title, clr(C_RESET));
    for (i = 0; i < d->totalBlocks; i++) {
        const DiskBlock *b = &d->blocks[i];
        if (b->owner == FREE_BLOCK) {
            printf("%s[%0*d:FREE]%s ", clr(C_GREEN), w, i, clr(C_RESET));
        } else {
            const File *f = &s->files[b->owner];
            printf("%s[%0*d:%c%02d]%s ",
                   clr(b->isIndex ? C_RED : methodColor(f->method)),
                   w, i, b->isIndex ? 'I' : 'F', b->owner + 1, clr(C_RESET));
        }
        if ((i + 1) % 8 == 0 || i == d->totalBlocks - 1)
            printf("\n");
    }
    if (showLegend) {
        printf("\nLegend: %sFREE%s = free block   %sFnn%s = contiguous file   "
               "%sFnn%s = linked file\n",
               clr(C_GREEN), clr(C_RESET), clr(C_CYAN), clr(C_RESET),
               clr(C_YELLOW), clr(C_RESET));
        printf("        %sFnn%s = indexed data block   %sInn%s = index block of file nn\n",
               clr(C_MAGENTA), clr(C_RESET), clr(C_RED), clr(C_RESET));
        printf("        nn = file label (see 'List Files'). Without colors, the "
               "method is shown in the file list.\n");
    }
}

static void displayFileDetails(const System *s, int id)
{
    const File *f = &s->files[id];
    int i;

    printf("\n%s%s=== File Details ===%s\n", clr(C_BOLD), clr(C_CYAN), clr(C_RESET));
    printf("  Label            : F%02d\n", id + 1);
    printf("  Name             : %s\n", f->name);
    printf("  Method           : %s\n", methodName(f->method));
    printf("  File size        : %d block(s) = %d bytes (block = %d bytes)\n",
           f->sizeBlocks, f->sizeBlocks * BLOCK_SIZE_BYTES, BLOCK_SIZE_BYTES);
    printf("  Blocks consumed  : %d (%d data + %d index)\n",
           f->blockCount + (f->method == ALLOC_INDEXED ? 1 : 0),
           f->blockCount, f->method == ALLOC_INDEXED ? 1 : 0);

    printf("  Data blocks      : ");
    for (i = 0; i < f->blockCount; i++)
        printf("%d%s", f->blocks[i], i + 1 < f->blockCount ? ", " : "\n");

    printf("  Fragments        : %d (separate groups of adjacent blocks)\n",
           countFragments(f));

    if (f->method == ALLOC_CONTIGUOUS) {
        printf("  Start block      : %d\n", f->startBlock);
        printf("  End block        : %d\n", f->endBlock);
        printf("  Allocated range  : %d..%d\n", f->startBlock, f->endBlock);
        printf("  Access           : direct (address of block i = start + i)\n");
    } else if (f->method == ALLOC_LINKED) {
        printf("  Head block       : %d\n", f->headBlock);
        printf("  Linked sequence  : ");
        for (i = 0; i < f->blockCount; i++)
            printf("%d -> ", f->blocks[i]);
        printf("NULL\n");
        printf("  Pointer overhead : %d bytes (%d bytes x %d blocks, stored inside data blocks)\n",
               f->blockCount * POINTER_BYTES, POINTER_BYTES, f->blockCount);
        printf("  Chain check      : %s\n",
               validateLinkedChain(s, id) ? "VALID (correct length, no cycles)"
                                          : "INVALID");
        printf("  Access           : sequential (reaching block i needs i pointer hops)\n");
    } else {
        printf("  Index block      : %d\n", f->indexBlock);
        printf("  Index contents   : (logical block -> disk block)\n");
        for (i = 0; i < f->blockCount; i++) {
            printf("    [%3d] -> %-4d", i, f->blocks[i]);
            if ((i + 1) % 4 == 0 || i + 1 == f->blockCount) printf("\n");
        }
        printf("  Index usage      : %d of %d pointer slots (%d bytes)\n",
               f->blockCount, MAX_INDEX_ENTRIES, f->blockCount * POINTER_BYTES);
        printf("  Access           : direct (read index block, then read data block)\n");
    }
}

static void listFiles(const System *s)
{
    int i, any = 0;

    printf("\n%s%s=== File List ===%s\n", clr(C_BOLD), clr(C_CYAN), clr(C_RESET));
    printf("%-6s %-20s %-11s %-6s %-9s\n", "Label", "Name", "Method", "Size", "Consumed");
    printf("------ -------------------- ----------- ------ ---------\n");
    for (i = 0; i < MAX_FILES; i++) {
        const File *f = &s->files[i];
        if (!f->inUse) continue;
        any = 1;
        printf("%s F%02d   %-20s %-11s %-6d %-9d%s\n",
               clr(methodColor(f->method)), i + 1, f->name, methodName(f->method),
               f->sizeBlocks,
               f->blockCount + (f->method == ALLOC_INDEXED ? 1 : 0), clr(C_RESET));
    }
    if (!any)
        printInfo("No files exist yet.");
    else
        printf("\nTotal files: %d\n", s->fileCount);
}

static void displayDiskStatistics(const System *s)
{
    int total = s->disk.totalBlocks;
    int freeB = countFreeBlocks(&s->disk);
    int used  = total - freeB;
    int m;

    printf("\n%s%s=== Disk Statistics ===%s\n", clr(C_BOLD), clr(C_CYAN), clr(C_RESET));
    printf("  Total blocks      : %d\n", total);
    printf("  Used blocks       : %d\n", used);
    printf("  Free blocks       : %d\n", freeB);
    printf("  Utilization       : %.1f%%\n", total > 0 ? (used * 100.0) / total : 0.0);
    printf("  Largest free run  : %d block(s)\n", largestFreeRun(&s->disk, NULL));
    printf("  Number of files   : %d\n", s->fileCount);
    printf("  Alloc. failures   : %d (of which external fragmentation: %d)\n",
           s->allocFailures, s->fragmentationFailures);
    printf("  Integrity check   : %s\n",
           verifyConsistency(s) ? "PASSED (disk and directory agree)"
                                : "FAILED (inconsistent metadata!)");

    printf("\n  Space consumed by allocation method:\n");
    printf("  %-11s %-6s %-12s %-13s %-9s\n",
           "Method", "Files", "Data blocks", "Index blocks", "Total");
    for (m = ALLOC_CONTIGUOUS; m <= ALLOC_INDEXED; m++) {
        MethodUsage u = getMethodUsage(s, (AllocMethod)m);
        printf("  %s%-11s%s %-6d %-12d %-13d %-9d",
               clr(methodColor((AllocMethod)m)), methodName((AllocMethod)m),
               clr(C_RESET), u.files, u.dataBlocks, u.indexBlocks,
               u.dataBlocks + u.indexBlocks);
        if (m == ALLOC_LINKED)
            printf(" (+%d bytes of pointers inside blocks)", u.pointerBytes);
        printf("\n");
    }
}

static const char *resultMessage(AllocResult r)
{
    switch (r) {
        case RES_OK:           return "Success.";
        case RES_BAD_NAME:     return "Invalid file name. Use 1-19 letters, digits, '_', '-' or '.'.";
        case RES_DUPLICATE:    return "A file with this name already exists.";
        case RES_BAD_SIZE:     return "File size must be a positive number of blocks.";
        case RES_EXCEEDS_DISK: return "File is larger than the entire disk.";
        case RES_BAD_METHOD:   return "Invalid allocation method (choose 1, 2 or 3).";
        case RES_TABLE_FULL:   return "File table is full (maximum number of files reached).";
        case RES_NO_SPACE:     return "Insufficient free disk space.";
        case RES_FRAGMENTED:   return "No contiguous region is large enough (external fragmentation).";
        case RES_INDEX_LIMIT:  return "File needs more pointers than a single index block can hold.";
        case RES_CORRUPT:      return "Internal error: allocation was rolled back.";
        default:               return "Unknown error.";
    }
}

/* ------------------------------ Menu actions ------------------------------ */

static void menuInitializeDisk(System *s)
{
    int n;
    char ans[INPUT_SIZE];

    printf("\n--- Initialize Disk ---\n");
    if (s->fileCount > 0) {
        printf("%sWarning:%s re-initializing erases all %d file(s).\n",
               clr(C_YELLOW), clr(C_RESET), s->fileCount);
        if (!promptString("Continue? (y/N): ", ans, sizeof ans)) return;
        if (!(ans[0] == 'y' || ans[0] == 'Y')) {
            printInfo("Initialization cancelled.");
            return;
        }
    }
    printf("Enter number of blocks (1-%d): ", MAX_BLOCKS);
    {
        char buf[INPUT_SIZE];
        char *end;
        long v;
        int rc = readLine(buf, sizeof buf);
        if (rc < 0) handleEof();
        if (rc > 0) { printError("Input too long. Extra characters were discarded."); return; }
        trim(buf);
        errno = 0;
        v = strtol(buf, &end, 10);
        if (buf[0] == '\0' || end == buf || *end != '\0' || errno == ERANGE) {
            printError("Please enter a whole number (no letters or symbols).");
            return;
        }
        if (v < 1 || v > MAX_BLOCKS) {
            printf("%s[ERROR]%s Disk capacity must be between 1 and %d blocks.\n",
                   clr(C_RED), clr(C_RESET), MAX_BLOCKS);
            return;
        }
        n = (int)v;
    }
    initializeDisk(s, n);
    printOk("Disk initialized. All blocks are free (numbered from 0).");
    printf("  Total: %d   Used: 0   Free: %d\n", n, n);
}

static void menuCreateFile(System *s)
{
    char name[INPUT_SIZE];
    int size, method, id;
    AllocResult r;

    printf("\n--- Create File ---\n");
    printf("Disk: %d free of %d blocks, largest contiguous run: %d\n",
           countFreeBlocks(&s->disk), s->disk.totalBlocks,
           largestFreeRun(&s->disk, NULL));

    if (!promptString("File name (letters, digits, _ - . ; max 19 chars): ", name, sizeof name))
        return;
    if (!validateFilename(name)) { printError(resultMessage(RES_BAD_NAME)); return; }
    if (findFileByName(s, name) >= 0) { printError(resultMessage(RES_DUPLICATE)); return; }

    if (!promptInt("File size in blocks: ", &size)) return;
    if (size <= 0) { printError(resultMessage(RES_BAD_SIZE)); return; }

    printf("Allocation method:  1) Contiguous   2) Linked   3) Indexed\n");
    if (!promptInt("Choose method (1-3): ", &method)) return;
    if (method < 1 || method > 3) { printError(resultMessage(RES_BAD_METHOD)); return; }

    r = allocateFile(s, name, size, (AllocMethod)method, &id);
    if (r != RES_OK) {
        printError(resultMessage(r));
        if (r == RES_FRAGMENTED)
            printf("  %d free blocks exist, but the largest contiguous region is only %d (need %d).\n",
                   countFreeBlocks(&s->disk), largestFreeRun(&s->disk, NULL), size);
        else if (r == RES_NO_SPACE)
            printf("  Needed %d block(s)%s, only %d free.\n",
                   size + (method == ALLOC_INDEXED ? 1 : 0),
                   method == ALLOC_INDEXED ? " (data + 1 index block)" : "",
                   countFreeBlocks(&s->disk));
        else if (r == RES_INDEX_LIMIT)
            printf("  One index block can hold at most %d pointers.\n", MAX_INDEX_ENTRIES);
        return;
    }
    printOk("File created.");
    displayFileDetails(s, id);
    displayDiskMap(s, "Disk Map", 0);
}

static void menuDeleteFile(System *s)
{
    char name[INPUT_SIZE];
    int freed = 0;

    printf("\n--- Delete File ---\n");
    if (s->fileCount == 0) { printInfo("There are no files to delete."); return; }
    listFiles(s);
    if (!promptString("\nName of the file to delete: ", name, sizeof name)) return;
    if (name[0] == '\0') { printError("File name cannot be empty."); return; }
    if (!deleteFileByName(s, name, &freed)) {
        printError("File not found. Nothing was deleted.");
        return;
    }
    printf("%s[OK]%s File '%s' deleted. %d block(s) released.\n",
           clr(C_GREEN), clr(C_RESET), name, freed);
    displayDiskMap(s, "Disk Map", 0);
}

static void menuViewFileDetails(const System *s)
{
    char name[INPUT_SIZE];
    int id;

    printf("\n--- View File Details ---\n");
    if (s->fileCount == 0) { printInfo("There are no files yet."); return; }
    listFiles(s);
    if (!promptString("\nName of the file to view: ", name, sizeof name)) return;
    id = findFileByName(s, name);
    if (id < 0) { printError("File not found."); return; }
    displayFileDetails(s, id);
}

/* ---------------------------- Allocation comparison ----------------------- */

typedef struct {
    int         isDelete;   /* 0 = create, 1 = delete */
    const char *name;
    int         size;
} DemoOp;

static const char *shortResult(AllocResult r)
{
    switch (r) {
        case RES_OK:           return "OK";
        case RES_FRAGMENTED:   return "FAIL(frag)";
        case RES_NO_SPACE:     return "FAIL(space)";
        case RES_INDEX_LIMIT:  return "FAIL(index)";
        default:               return "FAIL";
    }
}

/*
 * Runs the SAME workload on three identical empty disks, one per method, and
 * compares the actual simulated results. Uses its own private disks, so the
 * user's real session is not touched.
 */
static void compareAllocationMethods(void)
{
    static System sim[3];
    static const DemoOp ops[] = {
        {0, "A", 6}, {0, "B", 4}, {0, "C", 8}, {0, "D", 3}, {0, "E", 5},
        {1, "B", 0}, {1, "D", 0}, {0, "F", 7}, {0, "G", 6}
    };
    const int nOps = (int)(sizeof ops / sizeof ops[0]);
    const int capacity = 32;
    const char *res[3][16];
    int m, i;

    printf("\n%s%s=== Allocation Method Comparison ===%s\n",
           clr(C_BOLD), clr(C_CYAN), clr(C_RESET));
    printf("Same workload and same %d-block disk for every method (simulated results).\n",
           capacity);
    printf("Workload: create A(6) B(4) C(8) D(3) E(5), delete B and D, then create F(7), G(6).\n\n");

    for (m = 0; m < 3; m++) {
        initializeDisk(&sim[m], capacity);
        for (i = 0; i < nOps; i++) {
            if (ops[i].isDelete) {
                res[m][i] = deleteFileByName(&sim[m], ops[i].name, NULL) ? "deleted" : "n/a";
            } else {
                AllocResult r = allocateFile(&sim[m], ops[i].name, ops[i].size,
                                             (AllocMethod)(m + 1), NULL);
                res[m][i] = shortResult(r);
            }
        }
    }

    printf("%-4s %-14s %-12s %-12s %-12s\n", "Step", "Operation", "Contiguous", "Linked", "Indexed");
    printf("---- -------------- ------------ ------------ ------------\n");
    for (i = 0; i < nOps; i++) {
        char op[32];
        if (ops[i].isDelete) snprintf(op, sizeof op, "delete %s", ops[i].name);
        else                 snprintf(op, sizeof op, "create %s(%d)", ops[i].name, ops[i].size);
        printf("%-4d %-14s %-12s %-12s %-12s\n", i + 1, op, res[0][i], res[1][i], res[2][i]);
    }

    printf("\n%-34s %-12s %-12s %-12s\n", "Final result", "Contiguous", "Linked", "Indexed");
    printf("---------------------------------- ------------ ------------ ------------\n");
    {
        int files[3], data[3], idx[3], used[3], freeB[3], run[3], fail[3], frag[3], ptr[3];
        double util[3];
        for (m = 0; m < 3; m++) {
            MethodUsage u = getMethodUsage(&sim[m], (AllocMethod)(m + 1));
            files[m] = sim[m].fileCount;
            data[m]  = u.dataBlocks;
            idx[m]   = u.indexBlocks;
            used[m]  = capacity - countFreeBlocks(&sim[m].disk);
            freeB[m] = capacity - used[m];
            run[m]   = largestFreeRun(&sim[m].disk, NULL);
            fail[m]  = sim[m].allocFailures;
            frag[m]  = sim[m].fragmentationFailures;
            ptr[m]   = u.pointerBytes;
            util[m]  = used[m] * 100.0 / capacity;
        }
        printf("%-34s %-12d %-12d %-12d\n", "Files stored", files[0], files[1], files[2]);
        printf("%-34s %-12d %-12d %-12d\n", "Data blocks in use", data[0], data[1], data[2]);
        printf("%-34s %-12d %-12d %-12d\n", "Index-block overhead (blocks)", idx[0], idx[1], idx[2]);
        printf("%-34s %-12d %-12d %-12d\n", "Pointer overhead (bytes)", 0, ptr[1], 0);
        printf("%-34s %-12d %-12d %-12d\n", "Total blocks used", used[0], used[1], used[2]);
        printf("%-34s %-12d %-12d %-12d\n", "Free blocks", freeB[0], freeB[1], freeB[2]);
        printf("%-34s %-12d %-12d %-12d\n", "Largest free contiguous run", run[0], run[1], run[2]);
        {
            char u0[16], u1[16], u2[16];
            snprintf(u0, sizeof u0, "%.1f%%", util[0]);
            snprintf(u1, sizeof u1, "%.1f%%", util[1]);
            snprintf(u2, sizeof u2, "%.1f%%", util[2]);
            printf("%-34s %-12s %-12s %-12s\n", "Disk utilization", u0, u1, u2);
        }
        printf("%-34s %-12d %-12d %-12d\n", "Failed requests", fail[0], fail[1], fail[2]);
        printf("%-34s %-12d %-12d %-12d\n", "  ...due to external fragmentation", frag[0], frag[1], frag[2]);
    }

    for (m = 0; m < 3; m++) {
        char title[64];
        snprintf(title, sizeof title, "Final map: %s", methodName((AllocMethod)(m + 1)));
        displayDiskMap(&sim[m], title, m == 2);
    }

    printf("\n%s--- Theoretical properties (true in general, not only in this run) ---%s\n",
           clr(C_BOLD), clr(C_RESET));
    printf("Space utilization:\n");
    printf("  Contiguous: no per-file overhead, but free space can become unusable.\n");
    printf("  Linked    : any free block is usable; each block loses %d bytes to a pointer.\n", POINTER_BYTES);
    printf("  Indexed   : any free block is usable; one extra index block per file.\n");
    printf("External fragmentation:\n");
    printf("  Contiguous suffers from it; Linked and Indexed do not (but files can\n");
    printf("  become scattered, which hurts real disk speed - not modeled here).\n");
    printf("Access:\n");
    printf("  Contiguous: direct and sequential are both fast (address = start + i).\n");
    printf("  Linked    : sequential is natural; direct access to block i needs i hops.\n");
    printf("  Indexed   : direct access via the index block; sequential also works.\n");
    printf("Overhead:\n");
    printf("  Contiguous: only start+length in the directory. Linked: one pointer per\n");
    printf("  block, and a lost pointer breaks the chain. Indexed: a whole block per\n");
    printf("  file, which is costly for very small files, and a size cap of %d blocks\n", MAX_INDEX_ENTRIES);
    printf("  in this single-level model.\n");
    printf("No method is best everywhere: the right choice depends on file sizes,\n");
    printf("access patterns, and how often files are created and deleted.\n");
}

/* ------------------------------- Self-tests ------------------------------- */
static int testsRun = 0, testsFailed = 0;

#define CHECK(cond, desc)                                              \
    do {                                                               \
        testsRun++;                                                    \
        if (cond) { printf("  [PASS] %s\n", desc); }                   \
        else      { testsFailed++; printf("  [FAIL] %s\n", desc); }    \
    } while (0)

static int runSelfTests(void)
{
    static System t;
    int id, id2, freed = 0;
    AllocResult r;

    printf("=== Automated self-tests ===\n");

    printf("\nTest 1: successful allocation with each method\n");
    initializeDisk(&t, 16);
    r = allocateFile(&t, "c1", 4, ALLOC_CONTIGUOUS, &id);
    CHECK(r == RES_OK && t.files[id].startBlock == 0 && t.files[id].endBlock == 3,
          "contiguous file occupies blocks 0..3");
    r = allocateFile(&t, "l1", 3, ALLOC_LINKED, &id);
    CHECK(r == RES_OK && validateLinkedChain(&t, id), "linked chain is valid (no cycles)");
    r = allocateFile(&t, "i1", 3, ALLOC_INDEXED, &id);
    CHECK(r == RES_OK && countFreeBlocks(&t.disk) == 16 - 4 - 3 - 4,
          "indexed file consumes 3 data + 1 index block");
    CHECK(verifyConsistency(&t), "metadata consistent after allocations");

    printf("\nTest 2: duplicate file name rejected\n");
    {
        int before = countFreeBlocks(&t.disk);
        r = allocateFile(&t, "C1", 2, ALLOC_LINKED, NULL);
        CHECK(r == RES_DUPLICATE && countFreeBlocks(&t.disk) == before,
              "duplicate (case-insensitive) rejected, no blocks consumed");
    }

    printf("\nTest 3: invalid input rejected\n");
    CHECK(allocateFile(&t, "z0", 0, ALLOC_LINKED, NULL) == RES_BAD_SIZE, "size 0 rejected");
    CHECK(allocateFile(&t, "zn", -5, ALLOC_LINKED, NULL) == RES_BAD_SIZE, "negative size rejected");
    CHECK(allocateFile(&t, "bad name!", 2, ALLOC_LINKED, NULL) == RES_BAD_NAME, "illegal characters rejected");
    CHECK(allocateFile(&t, "big", 99, ALLOC_LINKED, NULL) == RES_EXCEEDS_DISK, "file larger than disk rejected");
    CHECK(allocateFile(&t, "m9", 2, (AllocMethod)9, NULL) == RES_BAD_METHOD, "invalid method rejected");

    printf("\nTest 4: disk full\n");
    initializeDisk(&t, 8);
    r = allocateFile(&t, "full", 8, ALLOC_CONTIGUOUS, NULL);
    CHECK(r == RES_OK && countFreeBlocks(&t.disk) == 0, "8-block file fills 8-block disk");
    r = allocateFile(&t, "more", 1, ALLOC_LINKED, NULL);
    CHECK(r == RES_NO_SPACE && t.allocFailures == 1, "next allocation fails with no space");
    CHECK(verifyConsistency(&t), "metadata consistent when disk is full");

    printf("\nTest 5: external fragmentation\n");
    initializeDisk(&t, 10);
    allocateFile(&t, "a", 3, ALLOC_CONTIGUOUS, NULL);
    allocateFile(&t, "b", 2, ALLOC_CONTIGUOUS, NULL);
    allocateFile(&t, "c", 3, ALLOC_CONTIGUOUS, NULL);
    allocateFile(&t, "d", 2, ALLOC_CONTIGUOUS, NULL);
    deleteFileByName(&t, "b", NULL);
    deleteFileByName(&t, "d", NULL);
    CHECK(countFreeBlocks(&t.disk) == 4 && largestFreeRun(&t.disk, NULL) == 2,
          "4 free blocks but largest run is 2");
    r = allocateFile(&t, "e", 4, ALLOC_CONTIGUOUS, NULL);
    CHECK(r == RES_FRAGMENTED && t.fragmentationFailures == 1,
          "contiguous 4-block request fails: external fragmentation");
    r = allocateFile(&t, "e", 4, ALLOC_LINKED, &id);
    CHECK(r == RES_OK && validateLinkedChain(&t, id),
          "same request succeeds with linked allocation");
    CHECK(verifyConsistency(&t), "metadata consistent after fragmentation test");

    printf("\nTest 6: deletion followed by reallocation\n");
    initializeDisk(&t, 10);
    allocateFile(&t, "x", 4, ALLOC_CONTIGUOUS, &id);
    allocateFile(&t, "y", 4, ALLOC_CONTIGUOUS, NULL);
    CHECK(deleteFileByName(&t, "x", &freed) && freed == 4, "delete releases 4 blocks");
    CHECK(t.files[id].inUse == 0 && t.fileCount == 1, "directory entry cleared");
    r = allocateFile(&t, "z", 4, ALLOC_CONTIGUOUS, &id2);
    CHECK(r == RES_OK && t.files[id2].startBlock == 0, "freed region is reused (starts at block 0)");
    CHECK(verifyConsistency(&t), "metadata consistent after delete + reallocate");

    printf("\nTest 7: indexed allocation counts the index block\n");
    initializeDisk(&t, 5);
    r = allocateFile(&t, "idx", 5, ALLOC_INDEXED, NULL);
    CHECK(r == RES_NO_SPACE, "5 data blocks + index needs 6 > 5, rejected");
    r = allocateFile(&t, "idx", 4, ALLOC_INDEXED, &id);
    CHECK(r == RES_OK && countFreeBlocks(&t.disk) == 0, "4 data + 1 index exactly fills disk");
    deleteFileByName(&t, "idx", &freed);
    CHECK(freed == 5 && countFreeBlocks(&t.disk) == 5, "deleting frees data AND index block");

    printf("\nTest 8: deleting a nonexistent file\n");
    CHECK(deleteFileByName(&t, "ghost", NULL) == 0, "nonexistent file reported, nothing deleted");

    printf("\nTest 9: utilization calculation\n");
    initializeDisk(&t, 10);
    allocateFile(&t, "u", 5, ALLOC_LINKED, NULL);
    CHECK((10 - countFreeBlocks(&t.disk)) * 100.0 / 10 == 50.0, "5 of 10 blocks used = 50%");

    printf("\nTest 10: foreign blocks are never freed\n");
    initializeDisk(&t, 10);
    allocateFile(&t, "p", 3, ALLOC_CONTIGUOUS, &id);
    allocateFile(&t, "q", 3, ALLOC_CONTIGUOUS, &id2);
    t.files[id].blocks[0] = t.files[id2].blocks[0];   /* simulate corruption */
    freed = freeAllocatedBlocks(&t, id);
    CHECK(freed == 2 && t.disk.blocks[t.files[id2].blocks[0]].owner == id2,
          "file p could not free q's block");

    printf("\n=== %d tests run, %d failed ===\n", testsRun, testsFailed);
    return testsFailed == 0 ? 0 : 1;
}

/* ---------------------------------- main ---------------------------------- */

static void printMenu(void)
{
    printf("\n%s%s========== CUSTOM FILE ALLOCATION SYSTEM ==========%s\n",
           clr(C_BOLD), clr(C_CYAN), clr(C_RESET));
    printf("%s1.%s Initialize Disk\n",            clr(C_YELLOW), clr(C_RESET));
    printf("%s2.%s Create File\n",                clr(C_YELLOW), clr(C_RESET));
    printf("%s3.%s Display Disk Map\n",          clr(C_YELLOW), clr(C_RESET));
    printf("%s4.%s List Files\n",                 clr(C_YELLOW), clr(C_RESET));
    printf("%s5.%s View File Details\n",          clr(C_YELLOW), clr(C_RESET));
    printf("%s6.%s Delete File\n",                clr(C_YELLOW), clr(C_RESET));
    printf("%s7.%s Disk Statistics\n",            clr(C_YELLOW), clr(C_RESET));
    printf("%s8.%s Compare Allocation Methods\n", clr(C_YELLOW), clr(C_RESET));
    printf("%s9.%s Exit\n",                       clr(C_YELLOW), clr(C_RESET));
    printf("%s===================================================%s\n",
           clr(C_CYAN), clr(C_RESET));
}

int main(int argc, char *argv[])
{
    static System sys;      /* static: keeps the large structure off the stack */
    int i, choice;

    for (i = 1; i < argc; i++)
        if (strcmp(argv[i], "--nocolor") == 0)
            useColor = 0;

    enableAnsi();

    if (argc > 1 && strcmp(argv[1], "--test") == 0)
        return runSelfTests();

    initializeDisk(&sys, DEFAULT_BLOCKS);

    printf("%s%sCustom File Allocation System (simulation only - no real files are touched)%s\n",
           clr(C_BOLD), clr(C_GREEN), clr(C_RESET));
    printf("A %d-block disk was initialized. Use option 1 to change its size.\n", DEFAULT_BLOCKS);

    for (;;) {
        printMenu();
        choice = 0;
        if (!promptInt("Enter your choice (1-9): ", &choice))
            continue;                      /* promptInt already printed the error */
        if (choice < 1 || choice > 9) {
            printError("Invalid menu choice. Please enter a number from 1 to 9.");
            continue;
        }
        switch (choice) {
            case 1: menuInitializeDisk(&sys); break;
            case 2: menuCreateFile(&sys);     break;
            case 3: displayDiskMap(&sys, "Disk Map", 1);
                    printf("\nTotal: %d   Used: %d   Free: %d\n", sys.disk.totalBlocks,
                           sys.disk.totalBlocks - countFreeBlocks(&sys.disk),
                           countFreeBlocks(&sys.disk));
                    break;
            case 4: listFiles(&sys);          break;
            case 5: menuViewFileDetails(&sys);break;
            case 6: menuDeleteFile(&sys);     break;
            case 7: displayDiskStatistics(&sys); break;
            case 8: compareAllocationMethods(); break;
            case 9: printf("\nGoodbye!\n"); return 0;
        }
        pauseForEnter();
    }
}
