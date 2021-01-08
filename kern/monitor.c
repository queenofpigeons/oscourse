// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/env.h>
#include <inc/x86.h>
#include <inc/error.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/tsc.h>
#include <kern/timer.h>
#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>

#define CMDBUF_SIZE 80 // enough for one VGA text line

extern int my_func(int a);

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
    {"memory", "List all physical pages", mon_memory},
    {"call", "Call a C function", mon_call}};
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
  //cprintf("%d\n", my_func(1));
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
  uint64_t rbp = tf ? tf->tf_regs.reg_rbp : read_rbp();
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
pass_arg(int32_t arg, int i) {
  asm volatile("movq %0, %%rax" : : "g" (arg) : "rax");
  switch (i) {
    case 0:
      asm volatile("movq %rax , %rdi");
      return;
    case 1:
      asm volatile("movq %rax , %rsi");
      return;
    case 2:
      asm volatile("movq %rax , %rdx");
      return;
    case 3:
      asm volatile("movq %rax , %rcx");
      return;
    case 4:
      asm volatile("movq %rax , %r8");
      return;
    case 5:
      asm volatile("movq %rax , %r9");
      return;
    default:
      asm volatile("push %rax");
  }
}
//itask
int
mon_call(int argc, char **argv, struct Trapframe *tf) {
  char *fname = argv[1];
  print_arguments(fname);
  return 0;

  /*cprintf("%d %s\n", argc, argv[1]);
  struct Call_type types[(argc - 2) / 2];
  asm volatile("pushq %rbp");
  //asm volatile("movq %rsp, %rbp");

  for (int i = 2; i < argc; i += 2) {
    asm volatile("xor %rax, %rax");

    // int is_int = 0;
    // int is_unsigned = 0;
    // int is_str = 0;

    char *type = argv[i];
    char *val = argv[i + 1];
    int type_i = (i - 2)/ 2;
    if (!strcmp(type, "string")) {
      strcpy (types[type_i].string, val);
    } else if (!strcmp(type, "uint8_t")) {
      return 0;
      //types[type_i].value.u8 = (uint8_t) strtol(val, NULL, 10);
    } else if (!strcmp(type, "uint16_t")) {
      return 0;
      //types[type_i].value.u16 = (uint16_t) strtol(val, NULL, 10);
    } else if (!strcmp(type, "uint32_t")) {
      return 0;
      //types[type_i].value.u32 = (uint32_t) strtol(val, NULL, 10);
    } else if (!strcmp(type, "int8_t") || !strcmp(type, "int16_t") || !strcmp(type, "int32_t")) {
      //types[type_i].value.i8 = (int8_t) strtol(val, NULL, 10);
      int64_t temp = strtol(val, NULL, 10);
      switch (type_i) {
      case 0:
        asm volatile("movq %0 , %%rdi" : : "g" (temp));
        break;
      case 1:
        asm volatile("movq %0 , %%rsi" : : "g" (temp));
        break;
      case 2:
        asm volatile("movq %0 , %%rdx" : : "g" (temp));
        break;
      case 3:
        asm volatile("movq %0 , %%rcx" : : "g" (temp));
        break;
      case 4:
        asm volatile("movq %0 , %%r8" : : "g" (temp));
        break;
      case 5:
        asm volatile("movq %0 , %%r9" : : "g" (temp));
        break;
      default:
        asm volatile("push %0" : : "g" (temp));
      }
    } else {
      return 0;
    }
  }

  uintptr_t addr = find_function(fname);
  uint64_t func_address;
  memcpy(&func_address, &addr, sizeof(void *));
  asm volatile("call %P0" : : "i"(func_address));

  asm volatile("popq %rbp");
  int regVal;
  asm("movl %%eax, %0" : "=r"(regVal) :);
  cprintf("%d\n", regVal);
  return 0; */
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
