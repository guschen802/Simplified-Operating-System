#include "userprog/syscall.h"
#include "lib/user/syscall.h"
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
static bool copy_data(uint8_t *uaddr, uint8_t *dst, int size);
static struct opened_file* get_opened_file (int fd);
static void close_all_file ();
static void sys_halt(void);
static void sys_exit(int status);
static pid_t sys_exec (const char *cmd_line);
static int sys_wait (pid_t pid);
static bool sys_create (const char * file, unsigned initial_size);
static bool sys_remove (const char * file);
static int sys_open (const char * file);
static int sys_filesize (int fd);
static int sys_read (int fd, void * buffer, unsigned size);
static int sys_write (int fd, const void * buffer, unsigned size);
static void sys_seek (int fd, unsigned position);
static unsigned sys_tell (int fd);
static void sys_close (int fd);

static struct lock fys_lock;

struct opened_file
  {
    struct list_elem elem;	/* List element. */
    int fd;			/* File descripter. */
    struct file *file;		/* Associated file. */
  };

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&fys_lock);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int syscall_nr = -1;
  if (!is_user_vaddr(f->esp) || !copy_data(f->esp, &syscall_nr, sizeof(int)))
    {
      sys_exit(-1);
    }
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
      get_arg(arg,1, f->esp);
      f->eax = sys_exec((const char*)arg[0]);
      break;
    case SYS_WAIT:
      get_arg(arg,1, f->esp);
      f->eax = sys_wait(arg[0]);
      break;
    case SYS_CREATE:
      get_arg(arg,2, f->esp);
      f->eax = sys_create((const char *)arg[0], arg[1]);
      break;
    case SYS_REMOVE:
      get_arg(arg,1, f->esp);
      f->eax = sys_remove((const char *)arg[0]);
      break;
    case SYS_OPEN:
      get_arg(arg,1, f->esp);
      f->eax = sys_open((const char *)arg[0]);
      break;
    case SYS_FILESIZE:
      get_arg(arg,1, f->esp);
      f->eax = sys_filesize(arg[0]);
      break;
    case SYS_READ:
      get_arg(arg,3, f->esp);
      f->eax = sys_read(arg[0], arg[1],arg[2]);
      break;
    case SYS_WRITE:
      get_arg(arg,3, f->esp);
      f->eax = sys_write(arg[0], arg[1], arg[2]);
      break;
    case SYS_SEEK:
      get_arg(arg,2, f->esp);
      sys_seek(arg[0], arg[1]);
      break;
    case SYS_TELL:
      get_arg(arg,1, f->esp);
      f->eax = sys_tell(arg[0]);
      break;
    case SYS_CLOSE:
      get_arg(arg,1, f->esp);
      sys_close(arg[0]);
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
      if(!copy_data(ptr, (uint8_t *)&arg[i], sizeof arg[0]))
	sys_exit(-1);
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

static bool
copy_data(uint8_t *uaddr, uint8_t *dst, int size)
{
  int i;
  for (i = 0; i < size; i++, dst++, uaddr++)
    {
      if (!is_user_vaddr((const void *)uaddr))
	return false;
      int ret = get_user((uint8_t*)uaddr);

      if (ret == -1)
	return false;

      *dst = (uint8_t)ret;
    }
  return true;
}

/* Terminates Pintos by calling shutdown_power_off()
 * (declared in threads/init.h). This should be seldom used,
 * because you lose some information about possible deadlock
 * situations, etc. */
static void
sys_halt(void)
{
  shutdown_power_off();
}

/* Terminates the current user program, returning status to
 * the kernel. If the process’s parent waits for it (see below),
 * this is the status that will be returned. Conventionally,
 * a status of 0 indicates success and nonzero values indicate
 * errors. */
static void
sys_exit(int status)
{
  struct thread *cur = thread_current();
  cur->process_status->exit_code = status;
  printf ("%s: exit(%d)\n",cur->name, status);
  sema_up(&cur->process_status->exit);
  close_all_file ();
  thread_exit();
}

/* Runs the executable whose name is given in cmd_line,
 * passing any given arguments, and returns the new process's
 * program id (pid). Must return pid -1, which otherwise should
 * not be a valid pid, if the program cannot load or run for
 * any reason. Thus, the parent process cannot return from the
 * exec until it knows whether the child process successfully
 * loaded its executable. You must use appropriate
 * synchronization to ensure this. */
static pid_t
sys_exec (const char *cmd_line)
{
  /* Loading program also involve reading executable from
   * file system , so we need lock here. */
  lock_acquire(&fys_lock);
  pid_t pid = process_execute(cmd_line);
  lock_release(&fys_lock);
  return pid;
}

/* Waits for a child process pid and retrieves the child's exit status.
 * If pid is still alive, waits until it terminates. Then, returns the
 * status that pid passed to exit. If pid did not call exit(), but was
 * terminated by the kernel (e.g. killed due to an exception), wait(pid)
 * must return -1. It is perfectly legal for a parent process to wait
 * for child processes that have already terminated by the time the
 * parent calls wait, but the kernel must still allow the parent to
 * retrieve its child's exit status, or learn that the child was
 * terminated by the kernel.
 *
 * wait must fail and return -1 immediately if any of the following
 * conditions is true:
 *
 * * pid does not refer to a direct child of the calling process.
 *   pid is a direct child of the calling process if and only if
 *   the calling process received pid as a return value from a successful
 *   call to exec.
 *
 *   Note that children are not inherited: if A spawns child B and B
 *   spawns child process C, then A cannot wait for C, even if B is
 *   dead. A call to wait(C) by process A must fail. Similarly, orphaned
 *   processes are not assigned to a new parent if their parent process
 *   exits before they do.
 *
 * * The process that calls wait has already called wait on pid. That is,
 *   a process may wait for any given child at most once.
 *
 * Processes may spawn any number of children, wait for them in any
 * order, and may even exit without having waited for some or all of
 * their children. Your design should consider all the ways in which
 * waits can occur. All of a process's resources, including its struct
 * thread, must be freed whether its parent ever waits for it or not,
 * and regardless of whether the child exits before or after its parent.
 *
 * You must ensure that Pintos does not terminate until the initial
 * process exits. The supplied Pintos code tries to do this by calling
 * process_wait() (in userprog/process.c) from main() (in threads/init.c).
 * We suggest that you implement process_wait() according to the comment
 * at the top of the function and then implement the wait system call in
 * terms of process_wait(). */
static int
sys_wait (pid_t pid)
{
  return process_wait(pid);
}

/* Creates a new file called file initially initial_size bytes in size.
 * Returns true if successful, false otherwise. Creating a new file does
 * not open it: opening the new file is a separate operation which would
 * require a open system call. */
static bool
sys_create (const char * file, unsigned initial_size)
{
  if (file == NULL || *file == '\n')
    {
     sys_exit(-1);
    }
  lock_acquire(&fys_lock);
  bool ret = filesys_create(file, initial_size);
  lock_release(&fys_lock);
  return ret;
}

/* Deletes the file called file. Returns true if successful, false otherwise.
 * A file may be removed regardless of whether it is open or closed, and
 * removing an open file does not close it. See Removing an Open File,
 * for details. */
static bool
sys_remove (const char * file)
{
  lock_acquire(&fys_lock);
  bool ret = filesys_remove(file);
  lock_release(&fys_lock);
  return ret;
}

/* Check whether the given file descriptor has associated with an
 * open file. */
static struct opened_file*
get_opened_file (int fd)
{
  struct list_elem *e;
  struct thread *cur = thread_current();
  for (e = list_begin(&cur->opened_files); e!= list_end(&cur->opened_files); e = list_next(e))
    {
      struct opened_file * o_file = list_entry(e, struct opened_file, elem);
      if (fd == o_file->fd)
	return o_file;
    }
  return NULL;
}

/* Opens the file called file. Returns a nonnegative integer handle called
 * a "file descriptor" (fd), or -1 if the file could not be opened.
 *
 * File descriptors numbered 0 and 1 are reserved for the console:
 * fd 0 (STDIN_FILENO) is standard input, fd 1 (STDOUT_FILENO) is standard
 * output. The open system call will never return either of these file
 * descriptors, which are valid as system call arguments only as explicitly
 * described below.
 *
 * Each process has an independent set of file descriptors. File descriptors
 * are not inherited by child processes.
 *
 * When a single file is opened more than once, whether by a single process
 * or different processes, each open returns a new file descriptor. Different
 * file descriptors for a single file are closed independently in separate
 * calls to close and they do not share a file position. */
static int
sys_open (const char * file)
{
  if (file == NULL)
    sys_exit(-1);
  struct opened_file *o_file = malloc(sizeof (struct opened_file));

  if (o_file != NULL)
    {
      lock_acquire(&fys_lock);
      struct file *m_file = filesys_open(file);
      if (m_file == NULL)
	{
	  free(o_file);
	  lock_release(&fys_lock);
	  return -1;
	}
      o_file->file = m_file;
      struct thread *cur = thread_current();
      o_file->fd = cur->next_fd++;
      list_push_back(&cur->opened_files, &o_file->elem);
      int fd = o_file->fd;
      lock_release(&fys_lock);
      return fd;
    }
  return -1;
}

/* Returns the size, in bytes, of the file open as fd. */
static int
sys_filesize (int fd)
{
  struct opened_file* o_file = get_opened_file(fd);
  if (o_file != NULL)
    {
      lock_acquire(&fys_lock);
      off_t ret = file_length(o_file->file);
      lock_release(&fys_lock);
      return ret;
    }
  return 0;
}

/* Reads size bytes from the file open as fd into buffer. Returns the number
 * of bytes actually read (0 at end of file), or -1 if the file could not
 * be read (due to a condition other than end of file). Fd 0 reads from
 * the keyboard using input_getc(). */
static int
sys_read (int fd, void * buffer, unsigned size)
{
  if (fd == STDIN_FILENO)
    {
      int count = size;
      uint8_t* buffer_ = (uint8_t*) buffer;
      while (count-->0)
	{
	  *buffer_ = input_getc();
	  buffer_++;
	}
      return size;
    }
  else
    {
      struct opened_file* o_file = get_opened_file(fd);
      if (o_file != NULL)
      {
	lock_acquire(&fys_lock);
	off_t ret = file_read(o_file->file, buffer, size);
	lock_release(&fys_lock);
	return ret;
      }
    }
  return 0;
}

/* Writes size bytes from buffer to the open file fd. Returns the number
 * of bytes actually written, which may be less than size if some bytes
 * could not be written.
 *
 * Writing past end-of-file would normally extend the file, but file
 * growth is not implemented by the basic file system. The expected
 * behavior is to write as many bytes as possible up to end-of-file
 * and return the actual number written, or 0 if no bytes could be
 * written at all.
 *
 * Fd 1 writes to the console. Your code to write to the console
 * should write all of buffer in one call to putbuf(), at least as
 * long as size is not bigger than a few hundred bytes. (It is
 * reasonable to break up larger buffers.) Otherwise, lines of text
 * output by different processes may end up interleaved on the console,
 * confusing both human readers and our grading scripts. */
static int
sys_write(int fd, const void *buffer, unsigned size)
{
  int c;
  char* buffer_ = (char*) buffer;

  do
    {
      c = get_user(buffer_);
      if (c == -1)
	sys_exit(-1);
      buffer_++;
    }
  while (c != '\0');

  if (fd == STDOUT_FILENO)
    {
      putbuf(buffer, size);
      return size;
    }
  else
    {
      struct opened_file* o_file = get_opened_file(fd);
      if (o_file != NULL)
	{
	  lock_acquire(&fys_lock);
	  off_t ret = file_write(o_file->file, buffer, size);
	  lock_release(&fys_lock);
	  return ret;
	}
    }
  return 0;
}

/* Changes the next byte to be read or written in open file fd
 * to position, expressed in bytes from the beginning of the
 * file. (Thus, a position of 0 is the file's start.)
 *
 * A seek past the current end of a file is not an error.
 * A later read obtains 0 bytes, indicating end of file. A later
 * write extends the file, filling any unwritten gap with zeros.
 * (However, in Pintos files have a fixed length until project 4 is
 * complete, so writes past end of file will return an error.) These
 * semantics are implemented in the file system and do not require
 * any special effort in system call implementation. */
static void
sys_seek (int fd, unsigned position)
{
  struct opened_file* o_file = get_opened_file(fd);
  if (o_file != NULL)
    {
      lock_acquire(&fys_lock);
      file_seek(o_file->file, position);
      lock_release(&fys_lock);
    }
}

/* Returns the position of the next byte to be read or written
 * in open file fd, expressed in bytes from the beginning of
 * the file. */
static unsigned
sys_tell (int fd)
{
  struct opened_file* o_file = get_opened_file(fd);
  if (o_file != NULL)
    {
      lock_acquire(&fys_lock);
      off_t ret = file_tell(o_file->file);
      lock_release(&fys_lock);
      return ret;
    }
  return 0;
}

/* Closes file descriptor fd. Exiting or terminating a
 * process implicitly closes all its open file descriptors,
 * as if by calling this function for each one. */
static void
sys_close (int fd)
{
  struct opened_file *o_file = get_opened_file(fd);
  if (o_file != NULL)
    {
      lock_acquire(&fys_lock);
      file_close(o_file->file);
      list_remove(&o_file->elem);
      free(o_file);
      lock_release(&fys_lock);
    }
}

/* Close all the file opened by current thread. */
static void
close_all_file ()
{
  struct list_elem *e;
  struct thread *cur = thread_current();
  lock_acquire(&fys_lock);
  for (e = list_begin(&cur->opened_files); e!= list_end(&cur->opened_files); e = list_next(e))
    {
      struct opened_file * o_file = list_entry(e, struct opened_file, elem);
      file_close(o_file->file);
      list_remove(&o_file->elem);
      free(o_file);
    }
  lock_release(&fys_lock);
}



