# 基于C++的多人聊天室

**该代码为`Linux`版本，已在`WSL Ubuntu20.04`上测试通过.**

## Get Started

在`src/`文件夹下，输入`make`命令，即可完成编译并生成可执行文件`Server`和`Client`，分别是服务器程序和客户端程序。

在终端中输入`./Server`即可运行服务器，可以看到终端打印出如下信息:

```
Please enter the port number of the server: 
```

提示用户输入服务器端口号，输入`8888`后，可以看到如下信息:

```
Server start successfully!
You can join the chatroom by connecting to 127.0.0.1:8888
```

表明服务器正常开启，在`127.0.0.1:8888`监听`TCP`连接。

接下来可以新建终端，输入`./Client`即可运行服务器，此时程序会依次提示输入服务器IP地址以及端口号:

```
Please enter IP address of the server: 127.0.0.1
Please enter port number of the server: 8888
Connect Successfully!
Welcome to Use Multi-Person Chatroom!
```

握手成功后会提示输入用户名:

```
Please enter your name: Komorebi
Hello Komorebi, Welcome to join the chatroom. Online User Number: 1
```

确认后服务器会打印如下信息:

```
Komorebi join in the chatroom. Online User Number: 1
```

表明有新用户进入。用户想要断开连接可以按下`ctrl+c`或直接关闭终端，此时服务器会打印如下信息:

```
Komorebi left the chatroom. Online Person Number: 0
```

表明该用户已离开。

### Advanced

- 在`Server.cpp`中，可以通过修改宏定义`MAX_CLIENT_NUM`来调节最大客户连接数。
- 在`Server.cpp`和`Client.cpp`中可以通过修改宏定义`BUFFER_LEN`来调节缓冲区大小，修改宏定义`NAME_LEN`来调节用户姓名的最大长度。


## 代码简介

采用**服务器-客户端**模式，服务器负责通过`TCP socket`接受客户的连接请求，然后监听其发送到消息，然后由服务器转发消息至其余客户端。

### Server.cpp

服务器源代码文件，主要数据结构为：

- **用户结构体**

```cpp
struct Client
{
    int valid;               //to judge whether this user is online
    int fd_id;               //user ID number
    int socket;              //socket to this user
    char name[NAME_LEN + 1]; //name of the user
}client[MAX_CLIENT_NUM];
```

- **用户消息队列**

```cpp
queue<string> message_q[MAX_CLIENT_NUM];
```

`Server`采用多线程设计，并利用**生产者-消费者**模型解决临界资源问题，代码主要包含四个部分：

#### `int main(void)`

main函数负责建立`TCP`连接，默认端口为`127.0.0.1:8888`，然后监听来自其它用户的连接，允许最大客户连接量为`32`.

通过修改源文件的宏定义：

```cpp
#define MAX_CLIENT_NUM 32
#define SERVER_PORT 8888
```

即可调整端口和最大客户数量。

#### `void *chat(void *data)`

这个函数负责处理每个**用户的连接**与**消息的接收转发**，当新客户连接后，会首先广播欢迎信息；然后开启新线程负责从这个客户的消息队列中取消息送往`socket`发送；接着调用`handle_recv()`函数，用于接收来自该用户的消息，这个函数是一个`while(1)`无限循环，当这个函数返回时，表明这个用户退出，这时广播该客户的离开消息，最后清理线程及各种`buffer`.

这个函数其实就是对`handle_send()`和`handle_recv()`函数的封装。

#### `void handle_recv(void *data)`

这个函数负责接收用户发送的消息，并将它们压入其它用户的消息队列中。本部分的主要难点在于对接收离散的数据包的整合。

我们规定每条消息以换行符`'\n'`作为消息的分割，大致的代码框架如下：

```cpp
while ((buffer_len = recv(pipe->socket, buffer, BUFFER_LEN, 0)) > 0)
{
    //to find '\n' as the end of the message
    for (int i = 0; i < buffer_len; i++)
    {
        message_buffer += buffer[i];
        message_len++;

        if (buffer[i] == '\n')
        {
            for (int j = 0; j < MAX_CLIENT_NUM; j++)
            {
                if (client[j].valid && client[j].socket != pipe->socket)
                {
                    //send to every client
                }
            }  
        }
    }
}
```

#### `void *handle_send(void *data)`

这个函数负责从用户的消息队列中取出消息，然后交由`socket`发送。

为了防止发送大文件时出现丢包的情况，将文件**拆分**为`BUFFER_LEN`大小的的块分块发送(`BUFFER_LEN`的大小通过宏定义更改)。

在分块传送中要注意`send()`函数里`len`参数的设置，需要比较当前剩余消息的长度和`BUFFER_LEN`的大小，选择**较小**的作为发送的长度，否则可能会发生无用数据。

**容易看出，消息队列作为全局变量，将由多个线程共享，属于临界资源，下面介绍利用生产者-消费者模型处理消息队列的使用问题。**

基本处理框架为：

```cpp
void producer()
{
    /* 获取消息 */
    ......
    pthread_mutex_lock(&mutex);  //加锁
    //插入缓冲队列
    pthread_cond_signal(&cv);    //信号量，通知消费者
    pthread_mutex_unlock(&mutex);//解锁
}

void consumer()
{
    pthread_mutex_lock(&mutex); //加锁
    //等待直到生产者生产出消息并加入队列中
    while (empty())
    {
        pthread_cond_wait(&cv, &mutex);
    }
    /* 从消息队列中取出数据，处理 */
    ......
    pthread_mutex_unlock(&mutex);//解锁
}
```

`handle_recv()`函数作为生产者首先需要获取临界资源--消息队列的使用权，也即`mutex`，然后将获取的消息加入消息队列中，通知消费者`handle_send()`函数取走消息并发送；

`handle_send()`作为消费者不断检查消息队列是否为空，若是空则释放`mutex`，并等待，直到生产者将新消息放入队列中，它才重新获得`mutex`，并进行处理，此时，其它线程包括生产者都不能访问这个队列。

这种方法能有效解决临界资源一致性问题。

### Client.cpp

客户端代码文件，主要功能有：

- **接收消息并显示在屏幕上**
 
这个功能可以通过开启新线程调用`void *handle_recv(void *data)`函数来实现，这个函数的主要内容与`Server.cpp`一致。

- **从`stdin`读取数据，然后发送给服务器**

这个功能包含在`main()`函数中，主要难点为对读入数据的处理。从`stdin`读入要求：

1. 忽略`ctrl+D`;
2. 允许`'\n'`作为一条消息;
3. 读入数据过长时能够丢弃并清空读入缓冲区，以便重新输入；
4. 消息以`'\n'`作为结束标志，读入数据要包含`'\n'`.

实现代码如下：

```cpp
char message[BUFFER_LEN + 1];
cin.get(message, BUFFER_LEN);
int n = strlen(message);
if (cin.eof())
{
    //reset
    cin.clear();
    clearerr(stdin);
    continue;
}
//single enter
else if (n == 0)
{
    //reset
    cin.clear();
    clearerr(stdin);
}
//overflow
if (n > BUFFER_LEN - 2)
{
    //reset
    cin.clear();
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
    printf("Reached the upper limit of the words!\n");
    continue;
}
cin.get();         //remove '\n' in stdin
message[n] = '\n'; //add '\n'
message[n + 1] = '\0';
n++;    //the length of message now is n+1
```