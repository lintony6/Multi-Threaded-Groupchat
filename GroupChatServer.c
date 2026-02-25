/*
 * Implementation of group-chat server in c
 */

/* include header files for socket related functions */
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Maximum message length */
#define MAXMESGLEN 1024
#define MAX_NUM_THREADS 8
// To handle ctrl + c to stop server
volatile sig_atomic_t keep_running = 1;

/*Linked list queue to process clients FIFO
Thread will be responsible for freeing mem of Client after
connection closes
*/
typedef struct Client {
  char client_name[20];
  int client_fd;
  struct sockaddr_in client_addr;
  struct Client *next;
} Client;

// ThreadPool to manage threads
typedef struct ThreadPool {
  pthread_mutex_t q_mutex;      // mutex for adding/removing clients from queue
  pthread_mutex_t active_mutex; // mutex for adding/removing clients from
                                // active, and broadcast
  pthread_cond_t q_cond;        // condition for q mutex
  pthread_cond_t active_cond;   // cond for active mutex
  pthread_t *threads;
  size_t num_threads;
  size_t active_threads;
  Client *q_head; // First client to be processed
  Client *q_tail; // Where to append new clients at back of queue
  // When threads pick up a client, move from q into active list
  Client *active_head; // Head of LL of active
  int stop;
} ThreadPool;

void *worker_function(void *arg);

/*Init ThreadPool
Returns ptr to ThreadPool object
*/
ThreadPool *init_thread_pool(size_t num_threads) {
  // alloc ThreadPool
  ThreadPool *pool = (ThreadPool *)malloc(sizeof(ThreadPool));
  if (!pool) {
    perror("Init ThreadPool");
    return NULL;
  }
  // Set all ptrs null first
  pool->threads = NULL;
  pool->q_head = NULL;
  pool->q_tail = NULL;
  pool->active_head = NULL;
  pool->num_threads = 0;
  pool->active_threads = 0;
  pool->stop = 0;

  // Init mutex and cond
  if (pthread_mutex_init(&pool->q_mutex, NULL) != 0) {
    perror("Init q_mutex");
    goto clean_pool;
  }
  if (pthread_mutex_init(&pool->active_mutex, NULL) != 0) {
    perror("Init active_mutex");
    goto clean_q_mutex;
  }
  if (pthread_cond_init(&pool->q_cond, NULL) != 0) {
    perror("Init q_cond");
    goto clean_active_mutex;
  }
  if (pthread_cond_init(&pool->active_cond, NULL) != 0) {
    perror("Init active_cond");
    goto clean_q_cond;
  }

  // Init threads
  pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * num_threads);
  if (!pool->threads) {
    perror("Init Threads");
    goto clean_active_cond;
  }
  for (size_t i = 0; i < num_threads; i++) {
    if (pthread_create(&pool->threads[i], NULL, worker_function,
                       (void *)pool) != 0) {
      perror("Init threads");
      goto clean_threads;
    }
    pool->num_threads++;
  }

  // If all valid
  return pool;

  // cleanups
clean_threads:
  // If fail, set stop signal, notify working threads
  pool->stop = 1;
  pthread_cond_broadcast(&pool->active_cond);
  for (size_t i = 0; i < pool->num_threads; i++) {
    pthread_join(pool->threads[i], NULL);
  }
  free(pool->threads);
clean_active_cond:
  pthread_cond_destroy(&pool->active_cond);
clean_q_cond:
  pthread_cond_destroy(&pool->q_cond);
clean_active_mutex:
  pthread_mutex_destroy(&pool->active_mutex);
clean_q_mutex:
  pthread_mutex_destroy(&pool->q_mutex);
clean_pool:
  free(pool);
  return NULL;
}

/*Destroy ThreadPool and free all mem
Clears all clients in q, closes all active fds
*/
void destroy_thread_pool(ThreadPool *pool) {
  if (!pool) {
    return;
  }

  // If destroy called, set stop = 1, free clients in q
  pthread_mutex_lock(&pool->q_mutex);
  // Set stop signal to 1
  pool->stop = 1;
  pthread_cond_broadcast(&pool->q_cond);
  // close all clients in q
  while (pool->q_head) {
    Client *temp = pool->q_head;
    pool->q_head = pool->q_head->next;
    shutdown(temp->client_fd, SHUT_RDWR);
    close(temp->client_fd);
    free(temp);
  }
  pool->q_head = pool->q_tail = NULL;
  pthread_mutex_unlock(&pool->q_mutex);

  // First gather all active client fd
  pthread_mutex_lock(&pool->active_mutex);

  size_t active_threads = pool->active_threads;
  if (active_threads == 0) {
    pthread_mutex_unlock(&pool->active_mutex);
  } else {
    int active_fds[MAX_NUM_THREADS];
    size_t fd_index = 0;
    Client *temp = pool->active_head;
    while (temp && fd_index < active_threads) {
      active_fds[fd_index++] = temp->client_fd;
      temp = temp->next;
    }
    pthread_mutex_unlock(&pool->active_mutex);

    // Shutdown fds
    for (size_t i = 0; i < fd_index; i++) {
      shutdown(active_fds[i], SHUT_RDWR);
    }
  }
  // Join threads together once work complete
  pthread_mutex_lock(&pool->active_mutex);
  while (pool->active_threads > 0) {
    pthread_cond_wait(&pool->active_cond, &pool->active_mutex);
  }
  pthread_mutex_unlock(&pool->active_mutex);

  // Join all threads together
  for (size_t i = 0; i < pool->num_threads; i++) {
    pthread_join(pool->threads[i], NULL);
  }

  // Threads terminated, clean up remaining alloc mem in thread pool
  pthread_mutex_destroy(&pool->q_mutex);
  pthread_mutex_destroy(&pool->active_mutex);
  pthread_cond_destroy(&pool->active_cond);
  pthread_cond_destroy(&pool->q_cond);
  free(pool->threads);
  free(pool);
}

/*
 * Read a message from the socket
 * Put the message in the given buffer
 * Return NULL if nothing to read
 * Otherwise, return the message
 */
char *recvMesg(int sd, char *mesg, size_t maxLen) {
  /* Keep reading one char at a time */
  char *ptr = mesg;

  // Add bounds checking to prevent segfault
  size_t charsRead = 0;
  while (charsRead < maxLen - 1) {
    /* Read one char into the buffer */
    int nread = read(sd, ptr, sizeof(char));

    /* If errors, report and exit */
    if (nread == -1) {
      perror("socket");
      exit(1);
    }

    /* If no byte read, i.e., EOF, return NULL */
    if (nread == 0)
      return (NULL);

    /* If newline character, thats the end of message */
    if (*ptr == '\n') {
      *ptr++ = '\n';
      /* string should end with a null character */
      break;
    }

    /* Advance the pointer to place the next char */
    ptr++;
    charsRead++;
  }

  *ptr = '\0';
  /* Return the message */
  return (mesg);
}

/* Function to serve client
 */
void serve_client(Client *client, ThreadPool *pool) {
  if (!client) {
    return;
  }
  char buffer[MAXMESGLEN];
  inet_ntop(AF_INET, &client->client_addr.sin_addr, buffer, sizeof(buffer));
  printf("Connected to a client at ('%s', '%hu')\n", buffer,
         ntohs(client->client_addr.sin_port));

  /* Keep connection open with client */
  while (1) {
    /* Wait to receive a message */
    char *message = recvMesg(client->client_fd, buffer, MAXMESGLEN);

    /* if message is NULL ==> client closed connection */
    if (message == NULL) {
      printf("Client closed connection\n");
      return;
    }

    /* Display the message */
    printf("%s: %s", client->client_name, buffer);

    // Need to broadcast to all other clients
    pthread_mutex_lock(&pool->active_mutex);
    int active_fds[MAX_NUM_THREADS];
    size_t fd_index = 0;
    Client *temp = pool->active_head;
    // Gather fds of all other active clients
    while (temp && fd_index < pool->active_threads) {
      // Skip current client
      if (temp->client_fd == client->client_fd) {
        temp = temp->next;
        continue;
      }
      active_fds[fd_index++] = temp->client_fd;
      temp = temp->next;
    }
    pthread_mutex_unlock(&pool->active_mutex);
    // Loop through other clients
    for (size_t i = 0; i < fd_index; i++) {
      char client_broadcast[MAXMESGLEN];
      // Append sending client to beginning of message
      int n = snprintf(client_broadcast, sizeof(client_broadcast), " %s: %s",
                       client->client_name, message);
      if (n < 0) {
        perror("snprintf");
      }
      // Check message length valid
      if ((size_t)n >= sizeof(client_broadcast)) {
        fprintf(stderr, "Message too long, truncated");
        n = sizeof(client_broadcast) - 1;
      }
      // Send client message to all other clients
      if (write(active_fds[i], client_broadcast, n) < 0) {
        printf("Unable to write message to %s", client->client_name);
      }
    }
  }
}

/* Function passed to pthread to maintain connection with clients
 */
void *worker_function(void *arg) {
  if (!arg) {
    return NULL;
  }
  // Get access to pool
  ThreadPool *pool = (ThreadPool *)arg;

  // Enter loop of doing work or sleep waiting for work
  while (1) {

    // Lock mutex to check queue
    pthread_mutex_lock(&pool->q_mutex);

    // If no work and no stop signal, sleep
    while (!pool->q_head && pool->stop == 0) {
      // Unlock and wait for notify signal
      pthread_cond_wait(&pool->q_cond, &pool->q_mutex);
    }

    // Thread has woken up, if stop then done.
    if (pool->stop == 1) {
      pthread_mutex_unlock(&pool->q_mutex);
      return NULL;
    }

    // If work to do, retrieve client
    Client *client = pool->q_head;
    // Update list
    pool->q_head = pool->q_head->next;
    // Empty head
    if (!pool->q_head) {
      pool->q_tail = NULL;
    }
    // Client retrieved, unlock
    pthread_mutex_unlock(&pool->q_mutex);

    // Need to add client to activeq
    pthread_mutex_lock(&pool->active_mutex);
    client->next = pool->active_head;
    pool->active_head = client;
    // Work to do, increment active threads
    pool->active_threads++;
    pthread_mutex_unlock(&pool->active_mutex);

    // serve client
    serve_client(client, pool);

    // Done serving client, decrement pool->active_threads
    pthread_mutex_lock(&pool->active_mutex);

    // Free client
    Client *prev = NULL;
    Client *curr = pool->active_head;
    // Remove client from active q
    while (curr && curr != client) {
      prev = curr;
      curr = curr->next;
    }
    if (curr == client) {
      // Curr is not on head
      if (prev) {
        prev->next = curr->next;
      } else {
        // Curr is on head
        pool->active_head = curr->next;
      }
    }

    close(client->client_fd);
    free(client);

    pool->active_threads--;
    // Notify pool complete
    pthread_cond_signal(&pool->active_cond);
    pthread_mutex_unlock(&pool->active_mutex);
  }
}

/* Function that takes address to store new socket fd and takes port
Returns 0 on successful creation of socket
Returns -1 on Fail
*/
int init_server_sock(int *sock_out, int server_port) {
  if (!sock_out) {
    return -1;
  }
  /* Get the port on which server should listen */

  /* Create the server socket */
  int server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (server_sock == -1) {
    perror("socket");
    exit(1);
  }

  /* Bind the socket to the given port */
  struct sockaddr_in serverAddr;
  bzero((char *)&serverAddr, sizeof(serverAddr));
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  serverAddr.sin_port = htons(server_port);
  if (bind(server_sock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) ==
      -1) {
    perror("bind");
    return (-1);
  }

  /* Set the server for listening */
  if (listen(server_sock, 5) != 0) {
    perror("listen");
    return -1;
  }
  *sock_out = server_sock;

  return 0;
}

/* Takes pool, client fd, client sockaddr_in,
creates Client instance and adds to queue
Returns 0 on success
Returns 1 on Fail
*/
int add_client(ThreadPool *pool, int client_fd, struct sockaddr_in client_addr,
               char *client_name) {
  if (!pool) {
    return -1;
  }

  Client *client = (Client *)malloc(sizeof(Client));
  if (!client) {
    perror("Client malloc");
    return -1;
  }
  client->next = NULL;
  client->client_fd = client_fd;
  client->client_addr = client_addr;
  size_t name_len = strlen(client_name);
  if (name_len > 19) {
    perror("Client Name too long");
    return -1;
  }
  memcpy(client->client_name, client_name, name_len);
  client->client_name[name_len] = '\0';
  // Lock mutex to add client
  // If unable to secure lock, do not attempt to add
  if (pthread_mutex_lock(&pool->q_mutex) != 0) {
    perror("Mutex Lock");
    free(client);
    return -1;
  }
  // Check if queue empty
  if (!pool->q_head) {
    pool->q_head = client;
  }
  // If queue not empty, add to end,
  else if (pool->q_head) {
    pool->q_tail->next = client;
  }
  // update tail
  pool->q_tail = client;

  // Unlock mutex
  pthread_mutex_unlock(&pool->q_mutex);

  // New client, signal to thread
  pthread_cond_signal(&pool->q_cond);

  return 0;
}

void handle_sigint(int sig) {
  (void)sig;
  keep_running = 0; // Just flip the switch and exit
}

/*
 * The server program starts from here
 */
int main(int argc, char *argv[]) {
  /* Server needs the port number to listen on */
  if (argc != 2) {
    fprintf(stderr, "usage : %s <port>\n", argv[0]);
    exit(1);
  }

  int server_port = atoi(argv[1]);

  // Handle SIGINT
  struct sigaction sa;
  sa.sa_handler = handle_sigint;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, NULL);

  // Init server socket
  int server_sock;
  if (init_server_sock(&server_sock, server_port) != 0) {
    perror("Init Server Sock");
    return -1;
  }

  // Create ThreadPool
  ThreadPool *pool = init_thread_pool(MAX_NUM_THREADS);
  if (!pool) {
    close(server_sock);
    return -1;
  }
  char client_name[20];

  // Begin listening for new client
  while (keep_running) {
    printf("Waiting for a client ...\n");
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int client_sock =
        accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len);
    if (client_sock == -1) {
      if (errno == EINTR) {
        break; // Ctrl+C was pressed, exit the loop gracefully
      }
      perror("accept");
      exit(0);
    }
    // Listen for client name
    if (recvMesg(client_sock, client_name, 20) == NULL) {
      fprintf(stderr, "Invalid client name, skipping this client");
      close(client_sock);
      continue;
    }
    client_name[strcspn(client_name, "\n")] = '\0';
    write(client_sock, "Welcome ", 9);
    write(client_sock, client_name, strlen(client_name));
    write(client_sock, "\n", 1);
    // Add Client to queue
    add_client(pool, client_sock, client_addr, client_name);
  }

  // Received SIGINT, need to shutdown
  printf("\nSIGINT received. Cleaning up...\n");
  destroy_thread_pool(pool);
  close(server_sock);
  return 0;
}
