#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#define PORT 9000
#define BUFFER_SIZE 4096
#define FILE_NAME "/var/tmp/aesdsocketdata"

int server_fd, new_socket;
int active_connection = 0;

void perror_1(const char* c) {
  syslog(LOG_ERR, c, " %s (errno: %d)\n", strerror(errno), errno);
}

static void handle_signals(int signalnumber) {
  // syslog(LOG_WARNING, "Caught signal, exeting");
  // printf("active_connection %i \n", active_connection);
  if (active_connection == 0) {
    printf("Killing \n");
    if (server_fd > 0) {
      shutdown(server_fd, 0);
    }

    if (new_socket > 0) {
      shutdown(new_socket, 0);
    }

    remove(FILE_NAME);
    exit(0);
  }
}

void demonize() {
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

  if (setsid() < 0) {
    syslog(LOG_ERR, "Error in setsid");
    perror_1("setsid");
    exit(EXIT_FAILURE);
  }

  // if( chdir("/") < 0) {
  //    perror("setsid");
  // 	  perror_1("Error");
  // }

  int dev_null = open("/dev/null", O_RDWR);
  if (dev_null == -1) {
    perror_1("open /dev/null failed");
    return 1;
  }

  // Redirect to /dev/null
  dup2(dev_null, STDIN_FILENO);
  dup2(dev_null, STDOUT_FILENO);
  dup2(dev_null, STDERR_FILENO);
  close(dev_null);
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

  remove(FILE_NAME);
  signal(SIGTERM, handle_signals);
  signal(SIGINT, handle_signals);

  int demonize_var = 0;
  if (argc == 2) {
    char cmd[2] = "-d";
    int m = match(argv[1], cmd, 2);
    demonize_var = m;
  }

  struct sockaddr_in address;
  socklen_t addr_len = sizeof(address);
  char buffer[BUFFER_SIZE];

  // Create socket
  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror_1("Socket failed");
    exit(EXIT_FAILURE);
  }

  int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                 sizeof(opt)) < 0) {
    // syslog(LOG_ERR, "Got error setsockopt: %s (errno: %d)\n",
    // strerror(errno), errno);
    perror_1("set socket failed");
  }

  // Configure server address
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);

  // Bind socket
  if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
    perror_1("Bind failed");
    close(server_fd);
    exit(EXIT_FAILURE);
  }

  printf("daemonize %i \n", demonize_var);

  if (demonize_var == 1) {
    demonize();
  } else {
    int dev_null = open("/dev/null", O_RDWR);
    if (dev_null == -1) {
      perror_1("open /dev/null failed");
      return 1;
    }

    // Redirect to /dev/null
    dup2(dev_null, STDIN_FILENO);
    dup2(dev_null, STDOUT_FILENO);
    dup2(dev_null, STDERR_FILENO);
    close(dev_null);
  }

  // Listen for incoming connections
  if (listen(server_fd, 5) < 0) {
    perror_1("Listen failed");
    close(server_fd);
    exit(EXIT_FAILURE);
  }

  printf("Server listening on port %d...\n", PORT);

  while (1) {  // Infinite loop to keep server running
    printf("Waiting for a new connection...\n");

    // Accept client connection
    new_socket = accept(server_fd, (struct sockaddr*)&address, &addr_len);
    if (new_socket < 0) {
      perror_1("Accept failed");
      continue;  // Skip to next loop iteration, keep server running
    }
    active_connection = 1;

    printf("Client connected!\n");

    // Open file for writing received data
    FILE* file = fopen(FILE_NAME, "a");
    if (!file) {
      perror_1("File open failed");
      close(new_socket);
      continue;  // Skip this client but keep server running
    }

    int cont = 1;
    while (cont == 1) {
      // Receive data from netcat and write to file
      ssize_t bytes_received;
      while ((bytes_received = recv(new_socket, buffer, BUFFER_SIZE, 0)) > 0) {
        // printf("bytes_received %i \n", (int) bytes_received);
        int ret = fwrite(buffer, sizeof(char), bytes_received, file);
        cont = 0;
        if (buffer[bytes_received - 1] == '\n') {
          break;
        }
      }
    }
    fclose(file);

    printf("Data received and stored in %s\n", FILE_NAME);

    // Open file for reading
    file = fopen(FILE_NAME, "r");
    if (!file) {
      perror("File open failed");
      close(new_socket);
      continue;
      exit(1);
    }

    // compute the file length
    int result = fseek(file, 0L, SEEK_END);
    if (result != 0) {
      perror("Error calling fseek in read_file");
      exit(1);
    }
    const int file_size = ftell(file);

    result = fseek(file, 0L, SEEK_SET);
    if (result != 0) {
      perror("Error calling fseek in read_file");
      exit(1);
    }

    char* buffer_loc = (char*)malloc(sizeof(char) * file_size);

    while (1) {
      // int bytes_received = fread(buffer, sizeof(char), BUFFER_SIZE, file);
      int bytes_received = fread(buffer_loc, sizeof(char), file_size, file);
      printf("bytes_received %d \n", bytes_received);
      if (bytes_received < 0) {
        perror("reading failed");
      }
      int n = send(new_socket, buffer_loc, bytes_received, 0);
      // int n = send(new_socket, file, file_size, 0);
      if (n < 0) {
        perror("reading failed");
      }
      printf("n %d\n", n);
      break;
    }

    fclose(file);
    printf("File contents sent back to client.\n");

    // Close client connection but keep server running
    close(new_socket);
    free(buffer_loc);
    printf("Client disconnected. Waiting for new connections...\n");
    active_connection = 0;
  }

  // Close the server socket (this won't be reached in the infinite loop)
  close(server_fd);
  return 0;
}
