#include <iostream>

#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

using namespace std;

static const int PORT = 8080;
static const int MAX_EVENTS = 1024;

// 设置 fd 为非阻塞
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main()
{
    // 1.创建监听socket
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("socket");
        return 1;
    }

    // 端口复用
    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 绑定地址
    sockaddr_in addr{};
    addr.sin_family =  AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(listenfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    // 监听
    if (listen(listenfd, SOMAXCONN) < 0) {
        perror("listen");
        return 1;
    }

    set_nonblocking(listenfd);

    // 2.创建epoll实例
    int epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1");
        return 1;
    }

    // 3.把listenfd加入epoll
    epoll_event ev{};
    ev.events = EPOLLIN; // 可读事件
    ev.data.fd = listenfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);

    epoll_event events[MAX_EVENTS];

    cout << "Echo server running on port " << PORT << endl;

    // 4.主循环
    while(true) {
        // 一直等
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (n < 0) {
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t e = events[i].events;

            // 4.1 新连接事件
            if (fd == listenfd) {
                while(true) {
                    sockaddr_in cliaddr{};
                    socklen_t len = sizeof(cliaddr);
                    int connfd = accept(listenfd, (sockaddr*)&cliaddr, &len);
                    if (connfd < 0) {
                        break; // 没有更多连接
                    }

                    // 设置fd非堵塞
                    set_nonblocking(connfd);

                    epoll_event cev{};
                    cev.events = EPOLLIN | EPOLLRDHUP;  // 可读 + 对端关闭
                    cev.data.fd = connfd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &cev);

                    std::cout << "New client: " << connfd << std::endl;
                }
            }
            // 4.2 客户端关闭
            else if (e & EPOLLRDHUP) {
                std::cout << "Client closed: " << fd << std::endl;
                close(fd);
            }
            // 4.3 客户端发送数据
            else if (e & EPOLLIN) {
                char buf[4096];
                while (true)
                {
                    ssize_t cnt = read(fd, buf, sizeof(buf));
                    if (cnt > 0) {
                        // echo
                        write(fd, buf, cnt);
                        cout << "server get message: " << buf << endl;
                    } else if (cnt == 0) {
                        // 客户端主动关闭
                        std::cout << "Client disconnected: " << fd << std::endl;
                        close(fd);
                        break;
                    } else {
                       if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;  // 数据已经读完，稍后再读
                        }
                        perror("read");
                        close(fd);
                        break;
                    }
                }
            }
        }
    }


    close(listenfd);
    close(epfd);

    return 0;
}