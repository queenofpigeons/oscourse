// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/env.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/tsc.h>
#include <kern/timer.h>
#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>

#define CMDBUF_SIZE 80 // enough for one VGA text line

struct Command {
  const char *name;
  const char *desc;
  // return -1 to force monitor to exit
  int (*func)(int argc, char **argv, struct Trapframe *tf);
};

// LAB 5: Your code here.
// Implement timer_start (mon_start), timer_stop (mon_stop), timer_freq (mon_frequency) commands.
// LAB 6: Your code here.
// Implement memory (mon_memory) command.
static struct Command commands[] = {
    {"help", "Display this list of commands", mon_help},
    {"hello", "Display greeting message", mon_hello},
    {"text", "Display simple text", mon_text},
    {"kerninfo", "Display information about the kernel", mon_kerninfo},
    {"backtrace", "Print stack backtrace", mon_backtrace},
    {"timer_start", "Start timer", mon_start},
    {"timer_stop", "Stop timer", mon_stop},
    {"timer_freq", "Count processor frequency", mon_frequency},
    {"memory", "List all physical pages", mon_memory}};
#define NCOMMANDS (sizeof(commands) / sizeof(commands[0]))

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf) {
  int i;

  for (i = 0; i < NCOMMANDS; i++)
    cprintf("%s - %s\n", commands[i].name, commands[i].desc);
  return 0;
}

int
mon_hello(int argc, char **argv, struct Trapframe *tf) {
  cprintf("Hello!\n");
  return 0;
}

int
mon_text(int argc, char **argv, struct Trapframe *tf) {
  cprintf("Text!!!\n");
  return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf) {
  extern char _head64[], entry[], etext[], edata[], end[];

  cprintf("Special kernel symbols:\n");
  cprintf("  _head64                  %08lx (phys)\n",
          (unsigned long)_head64);
  cprintf("  entry  %08lx (virt)  %08lx (phys)\n",
          (unsigned long)entry, (unsigned long)entry - KERNBASE);
  cprintf("  etext  %08lx (virt)  %08lx (phys)\n",
          (unsigned long)etext, (unsigned long)etext - KERNBASE);
  cprintf("  edata  %08lx (virt)  %08lx (phys)\n",
          (unsigned long)edata, (unsigned long)edata - KERNBASE);
  cprintf("  end    %08lx (virt)  %08lx (phys)\n",
          (unsigned long)end, (unsigned long)end - KERNBASE);
  cprintf("Kernel executable memory footprint: %luKB\n",
          (unsigned long)ROUNDUP(end - entry, 1024) / 1024);
  return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf) {
  cprintf("Stack backtrace:\n");
  uint64_t rbp = read_rbp();
  while (rbp) {
    uint64_t rip = ((uint64_t *)rbp)[1];
    struct Ripdebuginfo info;

    cprintf("  rbp %016lx  rip %016lx\n", rbp, rip);
    debuginfo_rip(rip, &info);
    cprintf("         %s:%d: %*s+%ld\n", info.rip_file, info.rip_line, info.rip_fn_namelen, info.rip_fn_name, rip - info.rip_fn_addr);
    rbp = ((uint64_t *)rbp)[0];
  }
  return 0;
}
int mon_start(int argc, char **argv, struct Trapframe *tf) {
  if (argc != 2) {
    return 1;
  }
  timer_start(argv[1]);
  return 0;
}
int mon_stop(int argc, char **argv, struct Trapframe *tf) {
  timer_stop();
  return 0;
}
int mon_frequency(int argc, char **argv, struct Trapframe *tf) {
  if (argc != 2) {
    return 1;
  }
  timer_cpu_frequency(argv[1]);
  return 0;
}

// LAB 5: Your code here.
// Implement timer_start (mon_start), timer_stop (mon_stop), timer_freq (mon_frequency) commands.

// LAB 6: Your code here.
// Implement memory (mon_memory) commands.

int
mon_memory(int argc, char **argv, struct Trapframe *tf) {
  int allocated;

  for (size_t i = 0; i < npages; i++) {
    allocated = page_is_allocated(&pages[i]);
    cprintf("%lu", i + 1);
    size_t i_start = i;
    while (i + 1 < npages && page_is_allocated(&pages[i + 1]) == allocated) {
      i++;
    }
    if (i_start != i) {
      cprintf("..%lu", i + 1);
    }
    if (allocated) {
      cprintf(" ALLOCATED\n");
    } else {
      cprintf(" FREE\n");
    }
  }
  return 0;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS    16

static int
runcmd(char *buf, struct Trapframe *tf) {
  int argc;
  char *argv[MAXARGS];
  int i;

  // Parse the command buffer into whitespace-separated arguments
  argc       = 0;
  argv[argc] = 0;
  while (1) {
    // gobble whitespace
    while (*buf && strchr(WHITESPACE, *buf))
      *buf++ = 0;
    if (*buf == 0)
      break;

    // save and scan past next arg
    if (argc == MAXARGS - 1) {
      cprintf("Too many arguments (max %d)\n", MAXARGS);
      return 0;
    }
    argv[argc++] = buf;
    while (*buf && !strchr(WHITESPACE, *buf))
      buf++;
  }
  argv[argc] = 0;

  // Lookup and invoke the command
  if (argc == 0)
    return 0;
  for (i = 0; i < NCOMMANDS; i++) {
    if (strcmp(argv[0], commands[i].name) == 0)
      return commands[i].func(argc, argv, tf);
  }
  cprintf("Unknown command '%s'\n", argv[0]);
  return 0;
}

void
monitor(struct Trapframe *tf) {
  char *buf;

  cprintf("Welcome to the JOS kernel monitor!\n");
  cprintf("Type 'help' for a list of commands.\n");

  if (tf != NULL)
    print_trapframe(tf);

  while (1) {
    buf = readline("K> ");
    if (buf != NULL)
      if (runcmd(buf, tf) < 0)
        break;
  }
}
