#ifndef ISOLATE_NETNS_H
#define ISOLATE_NETNS_H

#include <stdio.h>
#include <string.h>
#include <linux/rtnetlink.h>
#include <linux/veth.h>
#include <net/if.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>

#define MAX_PAYLOAD 1024 //Это макрос, который определяет максимальный размер полезной нагрузки как 1024.

struct nl_req { //Определение структуры nl_req, которая содержит заголовок сообщения netlink, информацию об интерфейсе и буфер для полезной нагрузки.
    struct nlmsghdr n;
    struct ifinfomsg i;
    char buf[MAX_PAYLOAD];
};

#define NLMSG_TAIL(nmsg) ((struct rtattr *) (((void *) (nmsg)) + NLMSG_ALIGN((nmsg)->nlmsg_len))) //Это макрос, который вычисляет указатель на конец сообщения netlink.
//Прототипы функций, которые реализованы в netns.h
int create_socket(int domain, int type, int protocol); //
void if_up(char *ifname, char *ip, char *netmask); //Включает сетевой интерфейс и назначает ему IP-адрес и маску подсети.
void create_veth(int sock_fd, char *ifname, char *peername);//Создает пару виртуальных Ethernet-интерфейсов veth
void move_if_to_pid_netns(int sock_fd, char *ifname, int netns); //Перемещает сетевой интерфейс в пространство имен сети другого процесса
int get_netns_fd(int pid); //Получает файловый дескриптор пространства имен сети для данного PID.

#endif //ISOLATE_NETNS_H
