#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  bool debug = getenv("DEBUG") != NULL;
  if (argc < 3) {
    fprintf(stderr, "Usage: %s SOCKET COMMAND [ARGS...]\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  const char *socket_path = argv[1];
  int socket_fd = -1;
  struct sockaddr_un addr = {0};
  if ((socket_fd = socket(PF_LOCAL, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }
  addr.sun_family = PF_LOCAL;
  if (strlen(socket_path) + 1 > sizeof(addr.sun_path)) {
    fprintf(stderr, "The socket path \"%s\" is too long\n", socket_path);
    exit(EXIT_FAILURE);
  }
  strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
  if (connect(socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    fprintf(stderr, "Failed to connect to \"%s\": %s\n", socket_path, strerror(errno));
    exit(EXIT_FAILURE);
  }
  char **child_argv = argv + 2;
  if (strcmp(child_argv[0], "--") == 0)
    child_argv++;
  if (debug)
    for (int i = 0; child_argv[i] != NULL; i++)
      fprintf(stderr, "child_argv[%d]: \"%s\"\n", i, child_argv[i]);
  if (execvp(child_argv[0], child_argv) < 0) {
    fprintf(stderr, "Failed to exec \"%s\": %s\n", child_argv[0], strerror(errno));
    exit(EXIT_FAILURE);
  }
  return 0;
}
