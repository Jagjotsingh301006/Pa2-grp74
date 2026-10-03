#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

struct procinfo procs[NPROC];
int nprocs;

int show_pid = 0;
int show_mem = 0;

// Print the process with the given pid, then its children one level deeper.
// depth 0 is the root (no prefix); depth 1 is "|- name";
// deeper levels are indented by 3 spaces per extra level.
void
print_proc(int pid, int depth)
{
  int idx;
  int k;

  for (idx = 0; idx < nprocs; idx++)
    if (procs[idx].pid == pid)
      break;
  if (idx == nprocs)
    return;

  if (depth > 0) {
    for (k = 1; k < depth; k++)
      printf("   ");
    printf("|- ");
  }

  printf("%s", procs[idx].name);
  if (show_pid)
    printf("(%d)", procs[idx].pid);
  if (show_mem)
    printf(" [%dB]", (int)procs[idx].sz);
  printf("\n");

  for (int i = 0; i < nprocs; i++)
    if (procs[i].ppid == pid && procs[i].pid != pid)
      print_proc(procs[i].pid, depth + 1);
}

int
main(int argc, char *argv[])
{
  // Accepts -p, -m, -p -m, -pm and -mp.
  for (int i = 1; i < argc; i++) {
    if (argv[i][0] != '-' || argv[i][1] == 0) {
      printf("Usage: pstree [-p] [-m]\n");
      exit(1);
    }
    for (int j = 1; argv[i][j]; j++) {
      if (argv[i][j] == 'p')
        show_pid = 1;
      else if (argv[i][j] == 'm')
        show_mem = 1;
      else {
        printf("Usage: pstree [-p] [-m]\n");
        exit(1);
      }
    }
  }

  nprocs = getprocs(procs, NPROC);
  if (nprocs < 0) {
    printf("pstree: getprocs failed\n");
    exit(1);
  }

  print_proc(1, 0);
  exit(0);
}
