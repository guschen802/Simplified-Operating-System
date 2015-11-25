#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "lib/debug.h"
#include "devices/shutdown.h"
#include "lib/stdio.h"
#include "lib/kernel/stdio.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

static void syscall_handler (struct intr_frame *);
static void get_arg(int* arg, int count, void* esp);
static int get_user (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);
static void copy_data(uint8_t *uaddr, uint8_t *dst, int size);
static void sys_halt(void);
static void sys_exit(int status);
static int sys_write(int fd, void *buffer, unsigned size);


struct opened_file
  {
    struct list_elem elem;	/* List element. */
    int fd;			/* File descripter. */
    struct File *file;		/* Associated file. */
  };

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int syscall_nr = *(int*)f->esp;
  int arg[3];

  switch (syscall_nr)
  {
    case SYS_HALT:
      sys_halt();
      break;
    case SYS_EXIT:
      get_arg(arg,1, f->esp);
      sys_exit(arg[0]);
      break;
    case SYS_EXEC:
    case SYS_WAIT:
    case SYS_CREATE:
    case SYS_REMOVE:
    case SYS_OPEN:
    case SYS_FILESIZE:
    case SYS_READ:
      printf("SYSCALL:%d NOT IMPLEMENTED YET!\n", syscall_nr);
      sys_exit(-1);
      break;
    case SYS_WRITE:
      get_arg(arg,3, f->esp);
      f->eax = sys_write(arg[0], arg[1], arg[2]);
      break;
    case SYS_SEEK:
    case SYS_TELL:
    case SYS_CLOSE:
      printf("SYSCALL: %d NOT IMPLEMENTED YET!\n", syscall_nr);
      sys_exit(-1);
      break;
    default:
      printf ("PANIC! UNKNOWN SYSCALL NUMBER: %d\n", syscall_nr);
      sys_exit(-1);
  }

}

static void
get_arg(int* arg, int count, void* esp)
{
  int i;
  for (i = 0; i < count; i++)
    {
      uint8_t* ptr = (uint8_t*)((int*)esp +1 +i);
      copy_data(ptr, (uint8_t *)&arg[i], sizeof arg[0]);
    }
}

/* Reads a byte at user virtual address UADDR.
UADDR must be below PHYS_BASE.
Returns the byte value if successful, -1 if a segfault
occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
  : "=&a" (result) : "m" (*uaddr));
  ASSERT(result!=-1);
  return result;
}

/* Writes BYTE to user address UDST.
UDST must be below PHYS_BASE.
Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
  : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

static void
copy_data(uint8_t *uaddr, uint8_t *dst, int size)
{
  int i;
  for (i = 0; i < size; i++, dst++, uaddr++)
    {
      ASSERT(is_user_vaddr((const void *)uaddr));
      *dst = (uint8_t)get_user((uint8_t*)uaddr);
    }
}
static void
sys_halt(void)
{
  shutdown_power_off();
}

static void
sys_exit(int status)
{
  struct thread *cur = thread_current();
  cur->process_status->exit_code = status;
  printf ("%s: exit(%d)\n",cur->name, status);
  sema_up(&cur->process_status->exit);
  thread_exit();
}

static int sys_write(int fd, void *buffer, unsigned size)
{
  if (fd == STDOUT_FILENO)
    {
      putbuf(buffer, size);
      return size;
    }
  else
    {
      printf("SYS_WRITE: write to file not implemented!\n");
    }
  return 0;
}


