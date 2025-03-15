#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <sys/queue.h>  // Include sys/queue.h”

#define PORT 9000
#define BUFFER_SIZE 2048

#ifdef USE_AESD_CHAR_DEVICE
#define FILE_NAME "/dev/aesdchar"
#else
#define FILE_NAME "/var/tmp/aesdsocketdata"
#endif

#define MAX_NUM_TTHREADS 1000
#define C_NUM_CONNECTIONS 50

pthread_mutex_t file_lock;

int server_fd;
int* new_socket;
int active_connection = 0;

struct Threads {
  pthread_t thread;
  int status;
  int* new_socket;
  SLIST_ENTRY(Threads) entries;
};

pthread_t t_time_id = -1;

typedef SLIST_HEAD(thread_s, Threads) head_t;
head_t queue;

void perror_1(const char* c) {
  syslog(LOG_ERR, c, " %s (errno: %d)\n", strerror(errno), errno);
}

static void handle_signals(int signalnumber) {
  // syslog(LOG_WARNING, "Caught signal, exeting");
  while (!SLIST_EMPTY(&queue)) {
    struct Threads* node = SLIST_FIRST(&queue);
    // printf("here node status %i id %lu \n", node->status, node->thread);
    if (node->status == 1) {
      pthread_join(node->thread, NULL);
    }
    free(node->new_socket);
    SLIST_REMOVE_HEAD(&queue, entries);
    free(node);
  }

  pthread_cancel(t_time_id);
  pthread_join(t_time_id, NULL);

  if (server_fd > 0) {
    shutdown(server_fd, 0);
  }
  remove(FILE_NAME);
  exit(0);
  // }
}

void* time_thread(void* args) {
  time_t rawtime;
  struct tm* timeinfo;
  char buffer[80];

  while (1) {
    printf("timer sleep\n");
    sleep(10);
    printf("timer write\n");

    time(&rawtime);

    // Convert to local time format
    timeinfo = localtime(&rawtime);

    // Format the time as "YYYY-MM-DD HH:MM:SS"
    size_t length = strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S\n", timeinfo);

    pthread_mutex_lock(&file_lock);
    FILE* file = fopen(FILE_NAME, "a");
    if (!file) {
      perror_1("File open failed");
      return NULL;
    }

    char d[10] = {"timestamp:"};
    fwrite(d, sizeof(char), 10, file);
    fwrite(buffer, sizeof(char), length, file);
    fclose(file);
    pthread_mutex_unlock(&file_lock);
  }

  return NULL;
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
    exit(1);
  }

  // Redirect to /dev/null
  dup2(dev_null, STDIN_FILENO);
  dup2(dev_null, STDOUT_FILENO);
  dup2(dev_null, STDERR_FILENO);
  close(dev_null);
}

int match(const char* a, const char* b, int length) {
  for (int i = 0; i < length; i++) {
    if (a[i] != b[i]) {
      return 0;
    }
  }
  return 1;
}

void* send_receive(void* arg) {
  char buffer[BUFFER_SIZE];
  struct Threads* params = (struct Threads*)arg;

  params->status = 1;

  printf("Client connected! id %lu\n", params->thread);

  // Open file for writing received data
  FILE* file = fopen(FILE_NAME, "a");
  if (!file) {
    perror_1("File open failed");
    close(*params->new_socket);
    return NULL;  // Skip this client but keep server running
  }

  int cont = 1;
  while (cont == 1) {
    // Receive data from netcat and write to file
    ssize_t bytes_received;
    while ((bytes_received = recv(*params->new_socket, buffer, BUFFER_SIZE, 0)) > 0) {
      // printf("bytes_received %i \n", (int) bytes_received);
      pthread_mutex_lock(&file_lock);
      int ret = fwrite(buffer, sizeof(char), bytes_received, file);
      pthread_mutex_unlock(&file_lock);

      if(ret < 0) {
        perror_1("Error writing to file failed");
        fclose(file);
        return NULL;
      }

      cont = 0;
      if (buffer[bytes_received - 1] == '\n') {
        break;
      }
    }
  }
  fclose(file);

  // printf("Data received and stored in %s\n", FILE_NAME);

  pthread_mutex_lock(&file_lock);
  // Open file for reading
  file = fopen(FILE_NAME, "r");
  if (!file) {
    perror("File open failed");
    close(*params->new_socket);
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
    int bytes_received = fread(buffer_loc, sizeof(char), file_size, file);
    if (bytes_received < 0) {
      perror("reading failed");
    }
    int n = send(*params->new_socket, buffer_loc, bytes_received, 0);
    if (n < 0) {
      perror("reading failed");
    }
    break;
  }
  fclose(file);
  pthread_mutex_unlock(&file_lock);

  // printf("File contents sent back to client.\n");

  // Close client connection but keep server running
  close(*params->new_socket);
  free(buffer_loc);
  printf("Client disconnected. Waiting for new connections... thread id %lu \n", params->thread);
  params->status = 0;
  return NULL;
}

int main(int argc, const char* argv[]) {
  openlog(NULL, LOG_CONS, LOG_SYSLOG);

  remove(FILE_NAME);
  signal(SIGTERM, handle_signals);
  signal(SIGINT, handle_signals);

  pthread_mutex_init(&file_lock, NULL);

  int demonize_var = 0;
  if (argc == 2) {
    char cmd[2] = "-d";
    int m = match(argv[1], cmd, 2);
    demonize_var = m;
  }

  struct sockaddr_in address;
  socklen_t addr_len = sizeof(address);

  // Create socket
  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror_1("Socket failed");
    exit(EXIT_FAILURE);
  }

  int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
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
  if (listen(server_fd, C_NUM_CONNECTIONS) < 0) {
    perror_1("Listen failed");
    close(server_fd);
    exit(EXIT_FAILURE);
  }

  printf("Server listening on port %d...\n", PORT);

  int time_thread_started = 0;
  while (1) {  // Infinite loop to keep server running
    printf("Waiting for a new connection...\n");

    // Accept client connection
    new_socket = malloc(sizeof(int));
    *new_socket = accept(server_fd, (struct sockaddr*)&address, &addr_len);
    if (new_socket < 0) {
      perror_1("Accept failed");
      continue;  // Skip to next loop iteration, keep server running
    }

    // execute timer thread when the first connection has started
    if (time_thread_started == 0) {
      time_thread_started = 1;
      pthread_create(&t_time_id, NULL, time_thread, NULL);
    }

    struct Threads* t = (struct Threads*)malloc(sizeof(struct Threads));
    t->new_socket = new_socket;
    t->status = 1;
    SLIST_INSERT_HEAD(&queue, t, entries);

    pthread_create(&(t->thread), NULL, send_receive, t);
  }

  pthread_mutex_destroy(&file_lock);

  // Close the server socket (this won't be reached in the infinite loop)
  close(server_fd);
  return 0;
}
