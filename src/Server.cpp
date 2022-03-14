#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <string>
#include <queue>
using namespace std;

#define BUFFER_LEN 1024
#define NAME_LEN 20
#define MAX_CLIENT_NUM 32

struct Client
{
    int valid;               // to judge whether this user is online
    int fd_id;               // user ID number
    int socket;              // socket to this user
    char name[NAME_LEN + 1]; // name of the user
} client[MAX_CLIENT_NUM] = {0};

queue<string> message_q[MAX_CLIENT_NUM]; // message buffer

// the full number of clients exist in the chatroom
int current_client_num = 0;
// sync current_client_num
pthread_mutex_t num_mutex = PTHREAD_MUTEX_INITIALIZER;

// 2 kinds of threads
pthread_t chat_thread[MAX_CLIENT_NUM] = {0};
pthread_t send_thread[MAX_CLIENT_NUM] = {0};

// used to sync
pthread_mutex_t mutex[MAX_CLIENT_NUM] = {0};
pthread_cond_t cv[MAX_CLIENT_NUM] = {0};

// send message
void *handle_send(void *data)
{
    struct Client *pipe = (struct Client *)data;
    while (1)
    {
        pthread_mutex_lock(&mutex[pipe->fd_id]);
        // wait until new message receive
        while (message_q[pipe->fd_id].empty())
        {
            pthread_cond_wait(&cv[pipe->fd_id], &mutex[pipe->fd_id]);
        }
        // if message queue isn't full, send message
        while (!message_q[pipe->fd_id].empty())
        {
            // get the first message from the queue
            string message_buffer = message_q[pipe->fd_id].front();
            int n = message_buffer.length();
            // calculate one transfer length
            int trans_len = BUFFER_LEN > n ? n : BUFFER_LEN;
            // send the message
            while (n > 0)
            {
                int len = send(pipe->socket, message_buffer.c_str(), trans_len, 0);
                if (len < 0)
                {
                    perror("send");
                    return NULL;
                }
                n -= len;
                message_buffer.erase(0, len); // delete data that has been transported
                trans_len = BUFFER_LEN > n ? n : BUFFER_LEN;
            }
            // delete the message that has been sent
            message_buffer.clear();
            message_q[pipe->fd_id].pop();
        }
        pthread_mutex_unlock(&mutex[pipe->fd_id]);
    }
    return NULL;
}

// get client message and push into queue
void handle_recv(void *data)
{
    struct Client *pipe = (struct Client *)data;

    // message buffer
    string message_buffer;
    int message_len = 0;

    // one transfer buffer
    char buffer[BUFFER_LEN + 1];
    int buffer_len = 0;

    // receive
    while ((buffer_len = recv(pipe->socket, buffer, BUFFER_LEN, 0)) > 0)
    {
        // to find '\n' as the end of the message
        for (int i = 0; i < buffer_len; i++)
        {
            // the start of a new message
            if (message_len == 0)
            {
                char temp[100];
                sprintf(temp, "%s:", pipe->name);
                message_buffer = temp;
                message_len = message_buffer.length();
            }

            message_buffer += buffer[i];
            message_len++;

            if (buffer[i] == '\n')
            {
                // send to every client
                for (int j = 0; j < MAX_CLIENT_NUM; j++)
                {
                    if (client[j].valid && client[j].socket != pipe->socket)
                    {
                        pthread_mutex_lock(&mutex[j]);
                        message_q[j].push(message_buffer);
                        pthread_cond_signal(&cv[j]);
                        pthread_mutex_unlock(&mutex[j]);
                    }
                }
                // new message start
                message_len = 0;
                message_buffer.clear();
            }
        }
        // clear buffer
        buffer_len = 0;
        memset(buffer, 0, sizeof(buffer));
    }
    return;
}

// deal with each client
void *chat(void *data)
{
    struct Client *pipe = (struct Client *)data;

    // printf hello message
    char hello[100];
    sprintf(hello, "Hello %s, Welcome to join the chatroom. Online User Number: %d\n", pipe->name, current_client_num);
    pthread_mutex_lock(&mutex[pipe->fd_id]);
    message_q[pipe->fd_id].push(hello);
    pthread_cond_signal(&cv[pipe->fd_id]);
    pthread_mutex_unlock(&mutex[pipe->fd_id]);

    memset(hello, 0, sizeof(hello));
    sprintf(hello, "New User %s join in! Online User Number: %d\n", pipe->name, current_client_num);
    // send messages to other users
    for (int j = 0; j < MAX_CLIENT_NUM; j++)
    {
        if (client[j].valid && client[j].socket != pipe->socket)
        {
            pthread_mutex_lock(&mutex[j]);
            message_q[j].push(hello);
            pthread_cond_signal(&cv[j]);
            pthread_mutex_unlock(&mutex[j]);
        }
    }

    // create a new thread to handle send messages for this socket
    pthread_create(&send_thread[pipe->fd_id], NULL, handle_send, (void *)pipe);

    // receive message
    handle_recv(data);

    // because the recv() function is blocking, so when handle_recv() return, it means this user is offline
    pthread_mutex_lock(&num_mutex);
    pipe->valid = 0;
    current_client_num--;
    pthread_mutex_unlock(&num_mutex);
    // printf bye message
    printf("%s left the chatroom. Online Person Number: %d\n", pipe->name, current_client_num);
    char bye[100];
    sprintf(bye, "%s left the chatroom. Online Person Number: %d\n", pipe->name, current_client_num);
    // send offline message to other clients
    for (int j = 0; j < MAX_CLIENT_NUM; j++)
    {
        if (client[j].valid && client[j].socket != pipe->socket)
        {
            pthread_mutex_lock(&mutex[j]);
            message_q[j].push(bye);
            pthread_cond_signal(&cv[j]);
            pthread_mutex_unlock(&mutex[j]);
        }
    }

    pthread_mutex_destroy(&mutex[pipe->fd_id]);
    pthread_cond_destroy(&cv[pipe->fd_id]);
    pthread_cancel(send_thread[pipe->fd_id]);

    return NULL;
}

int main()
{
    // create server socket
    int server_sock;
    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket");
        return 1;
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;

    // get server port and bind
    int server_port = 0;
    while (1)
    {
        printf("Please enter the port number of the server: ");
        scanf("%d", &server_port);

        addr.sin_port = htons(server_port);
        if (bind(server_sock, (struct sockaddr *)&addr, sizeof(addr)))
        {
            perror("bind");
            continue;
        }
        break;
    }

    // no more than 32 clients
    if (listen(server_sock, MAX_CLIENT_NUM + 1))
    {
        perror("listen");
        return -1;
    }
    printf("Server start successfully!\n");
    printf("You can join the chatroom by connecting to 127.0.0.1:%d\n\n", server_port);

    // waiting for new client to join in
    while (1)
    {
        // create a new connect
        int client_sock = accept(server_sock, NULL, NULL);
        if (client_sock == -1)
        {
            perror("accept");
            return -1;
        }

        // check whether is full or not
        if (current_client_num >= MAX_CLIENT_NUM)
        {
            if (send(client_sock, "ERROR", strlen("ERROR"), 0) < 0)
                perror("send");
            shutdown(client_sock, 2);
            continue;
        }
        else
        {
            if (send(client_sock, "OK", strlen("OK"), 0) < 0)
                perror("send");
        }

        // get client's name
        char name[NAME_LEN + 1] = {0};
        ssize_t state = recv(client_sock, name, NAME_LEN, 0);
        if (state < 0)
        {
            perror("recv");
            shutdown(client_sock, 2);
            continue;
        }
        // new user do not input a name but leave directly
        else if (state == 0)
        {
            shutdown(client_sock, 2);
            continue;
        }

        // update client array, create new thread
        for (int i = 0; i < MAX_CLIENT_NUM; i++)
        {
            // find the first unused client
            if (!client[i].valid)
            {
                pthread_mutex_lock(&num_mutex);
                // set name
                memset(client[i].name, 0, sizeof(client[i].name));
                strcpy(client[i].name, name);
                // set other info
                client[i].valid = 1;
                client[i].fd_id = i;
                client[i].socket = client_sock;

                mutex[i] = PTHREAD_MUTEX_INITIALIZER;
                cv[i] = PTHREAD_COND_INITIALIZER;

                current_client_num++;
                pthread_mutex_unlock(&num_mutex);

                // create new receive thread for new client
                pthread_create(&chat_thread[i], NULL, chat, (void *)&client[i]);
                printf("%s join in the chatroom. Online User Number: %d\n", client[i].name, current_client_num);

                break;
            }
        }
    }

    // close socket
    for (int i = 0; i < MAX_CLIENT_NUM; i++)
        if (client[i].valid)
            shutdown(client[i].socket, 2);
    shutdown(server_sock, 2);
    return 0;
}