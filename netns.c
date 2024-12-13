#include <stdio.h>
#include <string.h>
#include <linux/rtnetlink.h>
#include <linux/veth.h>
#include <net/if.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include "netns.h"
//=======================================================================================================
static void addattr_l(struct nlmsghdr *n, int maxlen, __u16 type, const void *data, __u16 datalen)
    //Эта функция добавляет атрибут к сообщению netlink.
    //Она принимает указатель на сообщение netlink, максимальную длину сообщения, тип атрибута, данные атрибута и длину данных атрибута.
    //Если новая длина превышает максимальную, функция выводит сообщение об ошибке и завершает программу. 
    //Затем она добавляет атрибут к сообщению и обновляет длину сообщения.

{    
    __u16 attr_len = RTA_LENGTH(datalen); // Вычисление длины атрибута, используя макрос RTA_LENGTH

    __u32 newlen = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(attr_len); //Вычисление новой длины сообщения, выравнивая текущую длину сообщения и длину атрибута
    if (newlen > maxlen){ //Если новая длина превышает максимальную длину..
        printf("cannot add attribute. size (%d) exceeded maxlen (%d)\n", newlen, maxlen); // Вывод сообщения об ошибке
        exit(1); // Завершение программы с кодом ошибки 1
    }

    struct rtattr *rta; //Объявляние структуры rtattr
    rta = NLMSG_TAIL(n); // Получение указателя на конец сообщения
    rta->rta_type = type; //Установка типа атрибута
    rta->rta_len = attr_len; //Установка длины атрибута
    if (datalen){ //Если длина данных не равна нулю 
        memcpy(RTA_DATA(rta), data, datalen); //копирование данных в атрибут
    }

    n->nlmsg_len = newlen; //Обновление длины сообщения
}
//=======================================================================================================
static struct rtattr *addattr_nest(struct nlmsghdr *n, int maxlen, __u16 type)
    //Эта функция начинает вложенный атрибут. Она возвращает указатель на начало вложенного атрибута.
{
    struct rtattr *nest = NLMSG_TAIL(n); // Объявляние указателя на структуру rtattr и установка его на конец сообщения
    addattr_l(n, maxlen, type, NULL, 0); //Добавление вложенного атрибута к сообщению
    return nest; //Возвращения указателя на начало вложенного атрибута
}
//==========================================================================
static void addattr_nest_end(struct nlmsghdr *n, struct rtattr *nest)
    //Эта функция заканчивает вложенный атрибут, обновляя его длину.  
{   
    nest->rta_len = (void *)NLMSG_TAIL(n) - (void *)nest;  //Обновление длины вложенного атрибута
}
//=========================================================================
static ssize_t read_response( int fd, struct msghdr *msg, char **response)
{
    //Эта функция читает ответ от сокета netlink. Она возвращает длину прочитанных данных.

    struct iovec *iov = msg->msg_iov; //Получение указателя на вектор ввода/вывода
    iov->iov_base = *response; //Установка базового адреса вектора ввода/вывода
    iov->iov_len = MAX_PAYLOAD;  //Установка длины вектора ввода/вывода

    ssize_t resp_len = recvmsg(fd, msg, 0); //Чтение сообщения из сокета

    if (resp_len == 0){  // Если длина ответа равна нулю 
        printf("EOF on netlink\n");//Вывод сообщения об ошибке
        exit(1);// Завершение программы с кодом ошибки 1
    }

    if (resp_len < 0){ //Если длина ответа меньше нуля
        printf("netlink receive error: %m\n");// Вывод сообщения об ошибке
        exit(1); //Завершение программы с кодом ошибки 1
    }

    return resp_len; // Возвращение длины ответа
}
//============================================================
static void check_response(int sock_fd) 
//Эта функция проверяет ответ от сокета netlink.        
//Если сообщение содержит ошибку, функция выводит сообщение об ошибке и завершает программу.
{
    struct iovec iov; // Объявление структуры iovec
    struct msghdr msg = {  // Объявление и инициализация структуры msghdr
            .msg_name = NULL, // Имя не задано
            .msg_namelen = 0, // Длина имени равна нулю
            .msg_iov = &iov, // Указатель на вектор ввода/вывод
            .msg_iovlen = 1 //// Длина вектора ввода/вывода равна 1
    };
    char *resp = malloc(MAX_PAYLOAD); //Выделение памяти под ответ
    ssize_t resp_len = read_response(sock_fd, &msg, &resp); //Чтение ответа из сокета
    struct nlmsghdr *hdr = (struct nlmsghdr *) resp; // Преобразование ответа в структуру nlmsghdr
    int nlmsglen = hdr->nlmsg_len; //Получает длину сообщения
    int datalen = nlmsglen - sizeof(*hdr); // // Вычисление длины данных

    // Did we read all data?
    if (datalen < 0 || nlmsglen > resp_len) { // Если длина данных меньше нуля или длина сообщения больше длины ответа
        if (msg.msg_flags & MSG_TRUNC){ //Если сообщение обрезано
            printf("received truncated message\n"); // Вывод сообщения об ошибке
            exit(1); // Завершение программы с кодом ошибки 1
        }

       printf("malformed message: nlmsg_len=%d\n", nlmsglen); // Вывод сообщения об ошибке
       exit(1); // Завершение программы с кодом ошибки 1
    }
    // Проверка на наличие ошибки
    if (hdr->nlmsg_type == NLMSG_ERROR) { // Если тип сообщения - ошибка
        struct nlmsgerr *err = (struct nlmsgerr *) NLMSG_DATA(hdr);// Преобразование данных сообщения в структуру nlmsgerr

        if (datalen < sizeof(struct nlmsgerr))  //Если длина данных меньше размера структуры nlmsgerr
            fprintf(stderr, "ERROR truncated!\n"); // Вывод сообщения об ошибке

        if(err->error) { // Если есть ошибка
            errno = -err->error; // Установка кода ошибки
            printf("RTNETLINK: %m\n"); // Вывод сообщения об ошибке
            exit(1); // Завершение программы с кодом ошибки 1
        }
    }
    free(resp); // Освобождение памяти, выделенной под ответ
}
//=======================================================================================================
int create_socket(int domain, int type, int protocol)  //Эта функция создает сокет и возвращает его дескриптор.
{
    int sock_fd = socket(domain, type, protocol); // Создание сокета с заданными параметрами
    if (sock_fd < 0){ //Если создание сокета не удалосьу
       printf("cannot open socket: %m\n"); //вывод сообщения об ошибке
       exit(1); // Завершение программы с кодом ошибки 1
    }

    return sock_fd; //Возвращает дескриптор сокета
}
//=======================================================================================================


//=======================================================================================================
static void send_nlmsg(int sock_fd, struct nlmsghdr *n) 
//Эта функция отправляет сообщение netlink через сокет и проверяет ответ.
//Если отправка сообщения не удалась, функция выводит сообщение об ошибке и завершает программу.
{
    struct iovec iov = { // Объявление и инициализация структуры iovec
            .iov_base = n, // Базовый адрес равен адресу сообщения
            .iov_len = n->nlmsg_len // Длина равна длине сообщения
    };

    struct msghdr msg = { // Объявление и инициализация структуры msghdr
            .msg_name = NULL, // Имя не задано
            .msg_namelen = 0, // Длина имени равна нулю
            .msg_iov = &iov,  // Указатель на вектор ввода/вывода
            .msg_iovlen = 1 // Длина вектора ввода/вывода равна 1
    };

    n->nlmsg_seq++;  //Увеличение счетчика последовательности сообщения

    ssize_t status = sendmsg(sock_fd, &msg, 0); //Отправляет сообщение через сокет
    if (status < 0) { //Если отправка сообщения не удалась, выводит сообщение об ошибке и завершает программу
       printf("cannot talk to rtnetlink: %m\n");
       exit(1);
    }

    check_response(sock_fd); //Отправляет сообщение через сокет
}
//===================================================
int get_netns_fd(int pid)
//Эта функция возвращает файловый дескриптор для файла пространства имен сети процесса с заданным PID
//Этот файловый дескриптор затем может быть использован для выполнения операций в этом пространстве имен.
{
    char path[256]; //Объявление массива символов для хранения пути к файлу.
    sprintf(path, "/proc/%d/ns/net", pid); //Формирование строки, указывающей на файл пространства имен сети процесса с заданным PID

    int fd = open(path, O_RDONLY);  // Открытие файла только для чтения и возвращения файлового дескриптора

    if (fd < 0){  // Если открытие файла не удалось
        printf("cannot read netns file %s: %m\n", path);  // Вывод сообщения об ошибке
        exit(1); // Завершение программы с кодом ошибки 1
    }

    return fd; // Возвращение файлового дескриптора
}
//===================================================
void if_up(char *ifname, char *ip, char *netmask)
//Эта функция включает сетевой интерфейс с заданным именем, устанавливает его IP-адрес и маску подсети.
//Она использует сокеты и ioctl для взаимодействия с ядром Linux и изменения состояния интерфейса. 
{
    int sock_fd = create_socket(PF_INET, SOCK_DGRAM, IPPROTO_IP); // Создание сокета

    struct ifreq ifr; //Объявление структуры для хранения информации об интерфейсе
    memset(&ifr, 0, sizeof(struct ifreq)); // Инициализация структуры ifreq нулями
    strncpy(ifr.ifr_name, ifname, strlen(ifname));  // Копирование имени интерфейса в структуру ifreq

    struct sockaddr_in saddr;  // Объявление структуры sockaddr_in для хранения IP-адреса
    memset(&saddr, 0, sizeof(struct sockaddr_in)); //Инициализация структуры sockaddr_in нулями
    saddr.sin_family = AF_INET; // Установка семейства адресов на AF_INET (IPv4)
    saddr.sin_port = 0; // Установка порта на 0

    char *p = (char *) &saddr; // Получение указателя на структуру sockaddr_in

    saddr.sin_addr.s_addr = inet_addr(ip);  // Преобразование IP-адреса из текстового формата в двоичный
    memcpy(((char *)&(ifr.ifr_addr)), p, sizeof(struct sockaddr)); // Копирование IP-адреса в структуру ifreq
    if (ioctl(sock_fd, SIOCSIFADDR, &ifr)) {  // Установка IP-адреса интерфейса. Если операция не удалась, выводит сообщение об ошибке и завершает программу
        printf("cannot set ip addr %s, %s: %m\n", ifname, ip); 
       exit(1);  // Завершение программы с кодом ошибки 1
    }

    saddr.sin_addr.s_addr = inet_addr(netmask); // Преобразование маски подсети из текстового формата в двоичный
    memcpy(((char *)&(ifr.ifr_addr)), p, sizeof(struct sockaddr)); // Копирование маски подсети в структуру интерфейса
    if (ioctl(sock_fd, SIOCSIFNETMASK, &ifr)) { //Установка маски подсети интерфейса. Если операция не удалась, выводит сообщение об ошибке и завершает программу
       printf("cannot set netmask for addr %s, %s: %m\n", ifname, netmask); 
       exit(1); // Завершение программы с кодом ошибки 1
    }

    ifr.ifr_flags |= IFF_UP | IFF_BROADCAST | IFF_RUNNING | IFF_MULTICAST; //Установка флагов интерфейса
    if (ioctl(sock_fd, SIOCSIFFLAGS, &ifr)){ //Применение флаги к интерфейсу. Если операция не удалась, выводит сообщение об ошибке и завершает программу
       printf("cannot set flags for addr %s, %s: %m\n", ifname, ip);  
       exit(1);// Завершение программы с кодом ошибки 1
    }

    close(sock_fd); //Закрывает сокет
}
//=======================================================
void create_veth(int sock_fd, char *ifname, char *peername)
//Эта функция создает пару виртуальных Ethernet-интерфейсов (veth) с заданными именами. 
//Она использует API nectlink для отправки сообщений в ядро Linux и создания интерфейсов
{
    __u16 flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK; //Установка флагов для сообщения netlink
    struct nl_req req = { // Объявление и инициализация структуры nl_req для сообщения netlink
            .n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg)), //длина сообщения
            .n.nlmsg_flags = flags, //флаги для определения типа сообщения
            .n.nlmsg_type = RTM_NEWLINK, //тип сообщения netlink
            .i.ifi_family = PF_NETLINK, //Семейство протоколов, к которому относится интерфейс.
    };
    struct nlmsghdr *n = &req.n;  // Получение указателя на заголовок сообщения netlink
    int maxlen = sizeof(req); // Установка максимальной длины сообщения
    
    addattr_l(n, maxlen, IFLA_IFNAME, ifname, strlen(ifname) + 1); // // Добавление атрибута с именем интерфейса к сообщению

    struct rtattr *linfo = addattr_nest(n, maxlen, IFLA_LINKINFO); //// Начало вложенного атрибута IFLA_LINKINFO с информацией о ссылке
    addattr_l(&req.n, sizeof(req), IFLA_INFO_KIND, "veth", 5); //Добавление атрибута с типом интерфейса к сообщению

    struct rtattr *linfodata = addattr_nest(n, maxlen, IFLA_INFO_DATA); // Начало вложенного атрибута IFLA_INFO_DATA с данными о ссылке

    struct rtattr *peerinfo = addattr_nest(n, maxlen, VETH_INFO_PEER);  // Начало вложенного атрибута VETH_INFO_PEER с информацией о пире
    n->nlmsg_len += sizeof(struct ifinfomsg);  // Увеличение длины сообщения на размер структуры ifinfomsg
    addattr_l(n, maxlen, IFLA_IFNAME, peername, strlen(peername) + 1); // Добавление атрибута с именем пира(партнёра) к сообщению
    addattr_nest_end(n, peerinfo); // Окончание вложенного атрибута VETH_INFO_PEER

    addattr_nest_end(n, linfodata); // Окончание вложенного атрибута IFLA_INFO_DATA
    addattr_nest_end(n, linfo); // Окончание вложенного атрибута IFLA_LINKINFO

    send_nlmsg(sock_fd, n); //Отправка сообщения через сокет netlink
}
//==============================================================
void move_if_to_pid_netns(int sock_fd, char *ifname, int netns)
// Эта функция перемещает сетевой интерфейс в пространство имен сети другого процесса.
//Она использует API netlink для отправки сообщений в ядро Linux и перемещения интерфейса.
{
    struct nl_req req = {  // Объявление и инициализация структуры nl_req для сообщения netlink
            .n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg)),//длина сообщения
            .n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK,//флаги для определения типа сообщения
            .n.nlmsg_type = RTM_NEWLINK,//тип сообщения netlink
            .i.ifi_family = PF_NETLINK,//Семейство протоколов, к которому относится интерфейс.
    };

    addattr_l(&req.n, sizeof(req), IFLA_NET_NS_FD, &netns, 4); //- Добавление атрибута с файловым дескриптором пространства имен сети к сообщению
    addattr_l(&req.n, sizeof(req), IFLA_IFNAME, ifname, strlen(ifname) + 1);  //Добавление атрибута с именем интерфейса к сообщению
    send_nlmsg(sock_fd, &req.n); // Отправка сообщения netlink
}
