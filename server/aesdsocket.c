#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

#define PORT 9000
#define BUFFER_SIZE 256

int socketfd = 0;
int new_socket = 0;
char buffer[BUFFER_SIZE];
int opt = 1;

struct sockaddr_in address;
const socklen_t addrlen = sizeof(struct sockaddr_in);

int active_connection = 0;

FILE* file = NULL;
const char* filename = "/var/tmp/aesdsocketdata";

static void handle_signals(int signalnumber) {
  syslog(LOG_WARNING, "Caught signal, exeting");
  if (active_connection == 0) {
    if (socketfd > 0) {
      shutdown(socketfd, 0);
    }

    if (socketfd > 0) {
      shutdown(socketfd, 0);
    }

    remove(filename);

    exit(1);
  }
}

int write_file(const char* filename, const char* buffer, int size) {
  file = fopen(filename, "a");
  if (file == NULL) {
    perror("Opening file failed");
    closelog();
    exit(EXIT_FAILURE);
  }
  int result = fwrite(buffer, sizeof(char), size, file);

  syslog(LOG_DEBUG, "writing %d bytes to file\n", size);
  // for(int i = 0; i < size; ++i) {
  //     syslog(LOG_DEBUG, "i %d char %c \n", i, buffer[i]);
  // }
  fclose(file);
  return result;
}

long int read_file(const char* filename, char** buffer) {
  FILE* file;
  file = fopen(filename, "r");

  if (file == NULL) {
    perror("Opening file failed");
    closelog();
    exit(EXIT_FAILURE);
  }

  // compute the file length
  int result = fseek(file, 0L, SEEK_END);
  if (result != 0) {
    perror("Error calling fseek in read_file");
    exit(1);
  }

  const long file_size = ftell(file);

  result = fseek(file, 0L, SEEK_SET);
  if (result != 0) {
    perror("Error calling fseek in read_file");
    exit(1);
  }

  char* buffer_loc = (char*)malloc(sizeof(char) * file_size);
  result = fread(buffer_loc, sizeof(char), file_size, file);

  if (result == 0) {
    return 0;
  } else if (result < 0) {
    perror("Error calling fread in read_file");
    exit(1);
  }

  fclose(file);
  *buffer = buffer_loc;

  return file_size;
}

void receive_connections() {
  struct sockaddr_in client_address;

  if (listen(socketfd, 1) < 0) {
    perror("listen failed");
    closelog();
    exit(EXIT_FAILURE);
  }

  int n = 0;
  while (1) {
    syslog(LOG_NOTICE, "while\n");
    socklen_t clilen = sizeof(client_address);
    new_socket = accept(socketfd, (struct sockaddr*)&client_address, &clilen);
    active_connection = 1;
    if (new_socket < 0) {
      syslog(LOG_ERR, "Got error socket: %s (errno: %d)\n", strerror(errno), errno);
      perror("accept failed");
      closelog();
      exit(EXIT_FAILURE);
    } else {
      syslog(LOG_WARNING, "ACCEPTED connection from %d", client_address.sin_addr.s_addr);
    }

    int new_line_recieved = 0;
    while (1) {
      memset(buffer, BUFFER_SIZE, 0);
      n = recv(new_socket, buffer, BUFFER_SIZE, 0);
      syslog(LOG_DEBUG, "Write to file %d characters \n", n);
      if (n < 0) {
        // Use errno to get the error reason
        syslog(LOG_DEBUG, "recv failed: %s (errno: %d)\n", strerror(errno), errno);
        break;
      }

      if (n > 0) {
        
        write_file(filename, buffer, n);
        if (buffer[n - 1] == '\n') {
          syslog(LOG_DEBUG, "receive zero");
          break;
        }
      } else if (n < 0) {
        syslog(LOG_DEBUG, "Got error: %s (errno: %d)\n", strerror(errno), errno);
        exit(1);
      }
    }
    syslog(LOG_DEBUG, "sending response");
    char* read_buffer;
    const int read_buffer_size = read_file(filename, &read_buffer);

    n = write(new_socket, read_buffer, read_buffer_size);
    free(read_buffer);

    if (n < 0) {
      active_connection = 0;
      // Use errno to get the error reason
      syslog(LOG_DEBUG, "send failed: %s (errno: %d)\n", strerror(errno), errno);
      break;
    }
    active_connection = 0;
  }
}

void server_thread(const char* filename, int is_daemon) {
  syslog(LOG_NOTICE, "Running as deamon %d\n", is_daemon);
  socketfd = socket(AF_INET, SOCK_STREAM, 0);
  if (socketfd < 0) {
    syslog(LOG_ERR, "Got error socket: %s (errno: %d)\n", strerror(errno), errno);
    perror("socket failed");
    closelog();
    exit(EXIT_FAILURE);
  }

  if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
    syslog(LOG_ERR, "Got error setsockopt: %s (errno: %d)\n", strerror(errno), errno);
    perror("set socket failed");
  }

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);

  if (bind(socketfd, (struct sockaddr*)&address, addrlen) < 0) {
    syslog(LOG_ERR, "Got error bind socket: %s (errno: %d)\n", strerror(errno), errno);
    perror("bind failed");
    closelog();
  }

  if (is_daemon == 1) {
    switch (fork()) {
      case -1:
        syslog(LOG_INFO, "Error during fork");
        exit(EXIT_FAILURE);
        break;
      case 0:
        break;
      default:
        syslog(LOG_INFO, "Main thread Exiting");
        exit(EXIT_SUCCESS);
        break;
    }

    signal(SIGTERM, handle_signals);
    signal(SIGINT, handle_signals);
    receive_connections();
  } else {
    signal(SIGTERM, handle_signals);
    signal(SIGINT, handle_signals);
    receive_connections();
  }
}

int match(const char* a, const char* b, int length) {
  // if a is shorter than b then \0 != <character>
  for (int i = 0; i < length; i++) {
    if (a[i] != b[i]) {
      return 0;
    }
  }
  return 1;
}

int main(int argc, const char* argv[]) {
  openlog(NULL, LOG_CONS, LOG_SYSLOG);

  remove(filename);
  signal(SIGTERM, handle_signals);
  signal(SIGINT, handle_signals);


  if (argc == 2) {
    char cmd[2] = "-d";
    int m = match(argv[1], cmd, 2);

    server_thread(filename, m);
  } else {
    server_thread(filename, 0);
  }

  return 0;
}