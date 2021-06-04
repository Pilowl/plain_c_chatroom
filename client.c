#include <arpa/inet.h>

#include "common.h"

int sockfd;

static volatile int server_is_responding = 0;

pthread_mutex_t stdout_lock = PTHREAD_MUTEX_INITIALIZER;

void overwrite_stdout() {
    printf("%s ", ">");
    fflush(stdout);
}

// Thread-safe printf'ing message
void display_message(char *msg)
{
    pthread_mutex_lock(&stdout_lock);
    printf("%s\n", msg);
    overwrite_stdout();
    pthread_mutex_unlock(&stdout_lock);
}

// Thread for sending messages.
void send_handler()
{
    char buffer[MSG_LENGTH + 2] = {};
    char name[32] = {};
    for(;;)
    {
        bzero(buffer, strlen(buffer));
        overwrite_stdout();
        fgets(buffer, MSG_LENGTH + 2, stdin);
        // Message length validating is client-sided.
        if (strlen(buffer) > MSG_LENGTH)
        {
            pthread_mutex_lock(&stdout_lock);
            printf("ERROR: Max message length should be %d.\n", MSG_LENGTH);
            pthread_mutex_unlock(&stdout_lock);

            while ((getchar()) != '\n');
            continue;
        }
        send(sockfd, buffer, strlen(buffer), 0);
    }
}

// Basic thread for receiving messages.
void receive_handler()
{
    // Message with length a bit more than message itself for metadata to fit in.
    char message[MSG_LENGTH + 50] = {};
    // Basic loop with receiving/displaying server messages.
    for (;;)
    {
        int receive = recv(sockfd, message, MSG_LENGTH + 50, 0);
        if (receive > 0) {
            display_message(message);
        }
        else
        {
            server_is_responding = 0;
            break;
        }
        memset(message, 0, sizeof(message));
        sleep(1);
    }
    pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("Usage: ./client <server_port>\n");
        exit(EINVAL);
    }

    char *ip = "127.0.0.1";
    int port = atoi(argv[1]);
    
    struct sockaddr_in server_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    int err = connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (err != 0)
    {
        printf("Failed to initialize socket connection...\n");
        return 1;
    }

    server_is_responding = 1;

    pthread_t send_msg_thread;
    pthread_create(&send_msg_thread, NULL, (void *) send_handler, NULL);

    pthread_t recv_msg_thread;
    pthread_create(&recv_msg_thread, NULL, (void *) receive_handler, NULL);
    
    // Loop while server is responding. Otherwise could exit only through SIGINT.
    while (server_is_responding) {}

    // Canceling client input receiving thread.
    pthread_cancel(send_msg_thread);
    printf("Connection to server is lost.\n");
    close(sockfd);
}
