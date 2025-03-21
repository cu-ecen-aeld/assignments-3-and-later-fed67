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
#include <math.h>

#include <regex.h>

#define USE_AESD_CHAR_DEVICE 1

#include <sys/queue.h>  // Include sys/queue.h”
#include "aesd_ioctl.h"

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

struct pair {
  int x;
  int y;
};

int string_match(char* a, size_t size_a, char* b, size_t size_b) {
  size_t size_min = fmin(size_a, size_b);
  for(size_t i = 0; i < size_min; ++i) {
    if(a[i] != b[i]) {
      return 0;
    }
  }
  return 1;
}


struct pair extract_numbers(char* a, size_t size) {
  struct pair p;
  p.x = -1;
  p.y = -1;
  const int offset = 20;

  regex_t regex;
  regmatch_t match;
  const char *pattern = "[0-9]+";  // Regular expression to find numbers

  regex_t regex_sub;
  regmatch_t match_sub;
  const char *pattern_sub = "[0-9]+,";  // Regular expression to find numbers

  if (regcomp(&regex, pattern, REG_EXTENDED)) {
    return p;
  }
  if (regcomp(&regex_sub, pattern_sub, REG_EXTENDED)) {
    return p;
  }

  if (regexec(&regex, a, 1, &match, 0) == 0) {
    // Extract matched number from the string
    regoff_t size = match.rm_eo - match.rm_so;
    char number[size];
    for(regoff_t  i = 0; i < size; ++i ) {
      number[i] = a[i+match.rm_so];
    }
    number[match.rm_eo - match.rm_so] = '\0';
    p.x = atoi(number);
    // printf("Extracted number: %s p.x %i rm_eo %lu rm_so %lu number %s \n", number, p.x, match.rm_eo, match.rm_so, number);

    regoff_t offset = match.rm_eo + 1;
    if(regexec(&regex, a+ offset, 1, &match, 0) == 0) {
      regoff_t size = match.rm_eo - match.rm_so;


      char numbery[size];
      // char* c = malloc(sizeof(char)*);

      for(regoff_t  i = 0; i < size; ++i ) {
        numbery[i] = a[i+offset+match.rm_so];
      }
      number[match.rm_eo - match.rm_so] = '\0';
      p.y = atoi(numbery);
    } else {
    }

  }
  return p;
}


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
    // printf("timer sleep\n");
    sleep(10);
    // printf("timer write\n");

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
  #ifdef USE_AESD_CHAR_DEVICE
     int file = open(FILE_NAME, O_RDWR);

     if (file < 0) {
      printf("error %d\n", file);
      perror_1("File open failed");
      close(*params->new_socket);
      return NULL;  // Skip this client but keep server running
    }
  #else
    FILE* file = fopen(FILE_NAME, "a");

    if (!file) {
      perror_1("File open failed");
      close(*params->new_socket);
      return NULL;  // Skip this client but keep server running
    }
  #endif

  printf("file open return %d \n", file);

  int cont = 1;
  while (cont == 1) {

    // Receive data from netcat and write to file
    ssize_t bytes_received;
    while ((bytes_received = recv(*params->new_socket, buffer, BUFFER_SIZE, 0)) > 0) {
      // printf("bytes_received %i \n", (int) bytes_received);
      pthread_mutex_lock(&file_lock);

      #ifdef USE_AESD_CHAR_DEVICE
      if(string_match(buffer, bytes_received, "AESDCHAR_IOCSEEKTO:", 19)) {
        syslog(LOG_DEBUG, "if ioctl \n");
        struct pair p = extract_numbers(buffer, BUFFER_SIZE);

        struct  aesd_seekto* messsage = (struct  aesd_seekto*) malloc(sizeof(struct aesd_seekto));
        messsage->write_cmd = p.x;
        messsage->write_cmd_offset = p.y;

        // printf("px %i py %i \n", p.x, p.y);
        if(p.x < 0  || p.y < 0 ) {
          pthread_mutex_unlock(&file_lock);
          perror("p.x < 0  || p.y < 0");
          close(*params->new_socket);
          return NULL;
        }


        if (file < 0) {
          printf("error opeing file ictl %d\n", file);
          // perror_1("File open failed");
          close(*params->new_socket);
          return NULL;  // Skip this client but keep server running
        }
        
        if (ioctl(file, AESDCHAR_IOCSEEKTO, messsage) == -1) {
          perror("ioctl failed");
          pthread_mutex_unlock(&file_lock);
          close(file);
          close(*params->new_socket);
          return NULL;
        }

        printf("closing");
        close(file);

        free(messsage);
        pthread_mutex_unlock(&file_lock);

        goto break_loop;
  
      } else {
        printf("else write \n");
      #endif
      // printf("bytes recievec %s   end", buffer);
      // printf("bytes_received %lu   end", bytes_received);

      #ifdef USE_AESD_CHAR_DEVICE
        int ret = write(file, buffer, bytes_received);
      #else
        int ret = fwrite(buffer, sizeof(char), bytes_received, file);
      #endif

        pthread_mutex_unlock(&file_lock);

        if(ret < 0) {
          printf("write ret y 0 %d \n", ret);
          perror_1("Error writing to file failed");
          #ifdef USE_AESD_CHAR_DEVICE
            close(file);
          #else
            fclose(file);
          #endif 
          return NULL;
        }
      }
      cont = 0;

      if (buffer[bytes_received - 1] == '\n') {
        break;
      }
    }
  }

  break_loop:

  printf("Data received and stored in %s\n", FILE_NAME);

  pthread_mutex_lock(&file_lock);
  // Open file for reading
  
  #ifdef USE_AESD_CHAR_DEVICE
    // file = open(FILE_NAME, O_RDONLY);
    file = open(FILE_NAME, O_RDWR);
  #else
    file = fopen(FILE_NAME, "r");
  #endif

  char* buffer_loc = (char*)malloc(sizeof(char) * BUFFER_SIZE);
  // char buffer_loc[BUFFER_SIZE];

  while (1) {
    printf("while loop read\n");
    #ifdef USE_AESD_CHAR_DEVICE
      int bytes_received = read(file, buffer_loc, sizeof(char)*BUFFER_SIZE);
    #else
      int bytes_received = fread(buffer_loc, sizeof(char), BUFFER_SIZE, file);
    #endif
    printf("while loop read %d \n", bytes_received);
    
    if (bytes_received < 0) {
      perror("reading failed");
    } else if(bytes_received == 0) {
      break;
    }


    int n = send(*params->new_socket, buffer_loc, bytes_received, 0);
    if (n < 0) {
      // printf("sending failed");
      perror("sending failed");
    }
    
  }

  #ifdef USE_AESD_CHAR_DEVICE
    close(file);
  #else
    fclose(file);
  #endif

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

  #ifndef USE_AESD_CHAR_DEVICE
   remove(FILE_NAME);
  #endif
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

    // // Redirect to /dev/null
    // dup2(dev_null, STDIN_FILENO);
    // dup2(dev_null, STDOUT_FILENO);
    // dup2(dev_null, STDERR_FILENO);
    // close(dev_null);
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
