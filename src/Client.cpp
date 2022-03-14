#include <iostream>
#include <stdio.h>
#include <string.h>
#include <limits>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <string>
using namespace std;

#define BUFFER_LEN 1024
#define NAME_LEN 20

char name[NAME_LEN + 1]; // client's name

// receive message and print out
void *handle_recv(void *data)
{
    int pipe = *(int *)data;

    // message buffer
    string message_buffer;
    int message_len = 0;

    // one transfer buffer
    char buffer[BUFFER_LEN + 1];
    int buffer_len = 0;

    // receive
    while ((buffer_len = recv(pipe, buffer, BUFFER_LEN, 0)) > 0)
    {
        // to find '\n' as the end of the message
        for (int i = 0; i < buffer_len; i++)
        {
            if (message_len == 0)
                message_buffer = buffer[i];
            else
                message_buffer += buffer[i];

            message_len++;

            if (buffer[i] == '\n')
            {
                // print out the message
                cout << message_buffer << endl;

                // new message start
                message_len = 0;
                message_buffer.clear();
            }
        }
        memset(buffer, 0, sizeof(buffer));
    }
    // because the recv() function is blocking, so when the while() loop break, it means the server is offline
    printf("The Server has been shutdown!\n");
    return NULL;
}

int main()
{
    // create a socket to connect with the server
    int client_sock;
    if ((client_sock = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket");
        return -1;
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;

    // get IP address and port of the server and connect
    int server_port = 0;
    char server_ip[16] = {0};
    while (1)
    {
        printf("Please enter IP address of the server: ");
        scanf("%s", server_ip);
        printf("Please enter port number of the server: ");
        scanf("%d", &server_port);
        getchar(); // read useless '\n'

        addr.sin_port = htons(server_port);
        addr.sin_addr.s_addr = inet_addr(server_ip);
        // connect the server
        if (connect(client_sock, (struct sockaddr *)&addr, sizeof(addr)))
        {
            perror("connect");
            continue;
        }
        break;
    }

    // check state
    printf("Connecting......");
    fflush(stdout);
    char state[10] = {0};
    if (recv(client_sock, state, sizeof(state), 0) < 0)
    {
        perror("recv");
        return -1;
    }
    if (strcmp(state, "OK"))
    {
        printf("\rThe chatroom is already full!\n");
        return 0;
    }
    else
    {
        printf("\rConnect Successfully!\n");
    }

    //////////////// get client name ////////////////
    printf("Welcome to Use Multi-Person Chat room!\n");
    while (1)
    {
        printf("Please enter your name: ");
        cin.get(name, NAME_LEN);
        int name_len = strlen(name);
        // no input
        if (cin.eof())
        {
            // reset
            cin.clear();
            clearerr(stdin);
            printf("\nYou need to input at least one word!\n");
            continue;
        }
        // sigle enter
        else if (name_len == 0)
        {
            // reset
            cin.clear();
            clearerr(stdin);
            cin.get();
            continue;
            printf("\nYou need to input at least one word!\n");
        }
        // overflow
        if (name_len > NAME_LEN - 2)
        {
            // reset
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            printf("\nReached the upper limit of the words!\n");
            continue;
        }
        cin.get(); // remove '\n' in stdin
        name[name_len] = '\0';
        break;
    }
    if (send(client_sock, name, strlen(name), 0) < 0)
    {
        perror("send");
        return -1;
    }
    //////////////// get client name ////////////////

    // create a new thread to handle receive message
    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, handle_recv, (void *)&client_sock);

    // get message and send
    while (1)
    {
        char message[BUFFER_LEN + 1];
        cin.get(message, BUFFER_LEN);
        int n = strlen(message);
        if (cin.eof())
        {
            // reset
            cin.clear();
            clearerr(stdin);
            continue;
        }
        // single enter
        else if (n == 0)
        {
            // reset
            cin.clear();
            clearerr(stdin);
        }
        // overflow
        if (n > BUFFER_LEN - 2)
        {
            // reset
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            printf("Reached the upper limit of the words!\n");
            continue;
        }
        cin.get();         // remove '\n' in stdin
        message[n] = '\n'; // add '\n'
        message[n + 1] = '\0';
        // the length of message now is n+1
        n++;
        printf("\n");
        // the length of message that has been sent
        int sent_len = 0;
        // calculate one transfer length
        int trans_len = BUFFER_LEN > n ? n : BUFFER_LEN;

        // send message
        while (n > 0)
        {
            int len = send(client_sock, message + sent_len, trans_len, 0);
            if (len < 0)
            {
                perror("send");
                return -1;
            }
            // if one transfer has not sent the full message, then send the remain message
            n -= len;
            sent_len += len;
            trans_len = BUFFER_LEN > n ? n : BUFFER_LEN;
        }
        // clean the buffer
        memset(message, 0, sizeof(message));
    }

    pthread_cancel(recv_thread);
    shutdown(client_sock, 2);
    return 0;
}