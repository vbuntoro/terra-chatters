
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <netdb.h>
#include <unistd.h>
#include <pthread.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#define IP "127.0.0.1"
#define PORT 8010
#define BACKLOG 10
#define CLIENTS 10

#define BUFFSIZE 1024
#define ALIASLEN 32
#define PASSLEN 32
#define OPTLEN 16

struct PACKET
{
    char option[OPTLEN]; // instruction
    char alias[ALIASLEN]; // client's alias
    char buff[BUFFSIZE]; // payload
};

struct THREADINFO
{
    pthread_t thread_ID; // thread's pointer
    int sockfd; // socket file descriptor
    char alias[ALIASLEN]; // client's alias
};

struct LLNODE
{
    struct THREADINFO threadinfo;
    struct LLNODE *next;
};

struct LLIST
{
    struct LLNODE *head, *tail;
    int size;
};

int compare(struct THREADINFO *a, struct THREADINFO *b)
{
    return a->sockfd - b->sockfd;
}

void list_init(struct LLIST *ll)
{
    ll->head = ll->tail = NULL;
    ll->size = 0;
}

int list_insert(struct LLIST *ll, struct THREADINFO *thr_info)
{
    if(ll->size == CLIENTS) return -1;
    if(ll->head == NULL)
    {
        ll->head = (struct LLNODE *)malloc(sizeof(struct LLNODE));
        ll->head->threadinfo = *thr_info;
        ll->head->next = NULL;
        ll->tail = ll->head;
    }
    else
    {
        ll->tail->next = (struct LLNODE *)malloc(sizeof(struct LLNODE));
        ll->tail->next->threadinfo = *thr_info;
        ll->tail->next->next = NULL;
        ll->tail = ll->tail->next;
    }
    ll->size++;
    return 0;
}

int list_delete(struct LLIST *ll, struct THREADINFO *thr_info)
{
    struct LLNODE *curr, *temp;
    if(ll->head == NULL) return -1;
    if(compare(thr_info, &ll->head->threadinfo) == 0)
    {
        temp = ll->head;
        ll->head = ll->head->next;
        if(ll->head == NULL) ll->tail = ll->head;
        free(temp);
        ll->size--;
        return 0;
    }
    for(curr = ll->head; curr->next != NULL; curr = curr->next)
    {
        if(compare(thr_info, &curr->next->threadinfo) == 0)
        {
            temp = curr->next;
            if(temp == ll->tail) ll->tail = curr;
            curr->next = curr->next->next;
            free(temp);
            ll->size--;
            return 0;
        }
    }
    return -1;
}

void list_dump(struct LLIST *ll)
{
    struct LLNODE *curr;
    struct THREADINFO *thr_info;
    printf("Connection count: %d\n", ll->size);
    for(curr = ll->head; curr != NULL; curr = curr->next)
    {
        thr_info = &curr->threadinfo;
        printf("[%d] %s\n", thr_info->sockfd, thr_info->alias);
    }
}

struct tm GetDateTime()
{
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    tm.tm_year = tm.tm_year + 1900;
    tm.tm_mon = tm.tm_mon + 1;
    return tm;
}

void WriteToFile(char* message1,char* message2,char* filename,int option)
{
	FILE *f = fopen(filename, "a");
	if (f == NULL)
	{
	    printf("File doen't exist!\nCreating file...%s\n",filename);
	    f = fopen(filename, "wb");
            exit(1);
	}
        fprintf(f,"[%d-%d-%d %d:%d:%d]", GetDateTime().tm_year, GetDateTime().tm_mon,GetDateTime().tm_mday,GetDateTime().tm_hour, GetDateTime().tm_min, GetDateTime().tm_sec);
        if(option == 0) //Server logged in
        {
            fprintf(f,"Server starts\r\n");
        }
        else if(option == 1) //Someone logged in
        {
            fprintf(f,"%s logged in\r\n",message1);
        }
        else if(option == 2) //Someone disconnected from server
        {
            fprintf(f,"%s logged out\r\n",message1);
        }
        else if(option ==3) // Someone A messages B
        {
            fprintf(f,"%s messages %s\r\n",message1,message2);
        }
        else if(option ==4) //Someone created/join group
        {
            fprintf(f,"%s joined %s\r\n",message1,message2);
        }
        else if(option == 5) //Someone left the group
        {
            fprintf(f, "%s left %s\r\n",message1,message2);
        }
        fclose(f);
}


/*char* tostring(char* str, int num)
{
    int i, rem, len = 0, n;
    n = num;
    while (n != 0)
    {
        len++;

        n /= 10;
    }
    for (i = 0; i < len; i++)
    {
        rem = num % 10;

        num = num / 10;

        str[len - (i + 1)] = rem + '0';
    }
    str[len] = '\0';
    return str;

}

int toint(char str[])
{
    int len = strlen(str);
    int i, num = 0;
    for (i = 0; i < len; i++)
    {

        num = num + ((str[len - (i + 1)] - '0') * pow(10, i));

    }
   return num;

}*/

int sockfd, newfd;
struct THREADINFO thread_info[CLIENTS];
struct LLIST client_list;
pthread_mutex_t clientlist_mutex;
FILE * userlist;

void *io_handler(void *param);
void *client_handler(void *fd);

int main(int argc, char **argv)
{
    int err_ret, sin_size;
    struct sockaddr_in serv_addr, client_addr;
    pthread_t interrupt;



    /* initialize linked list */
    list_init(&client_list);

    /* initiate mutex */
    pthread_mutex_init(&clientlist_mutex, NULL);

    /* open a socket */
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        err_ret = errno;
        fprintf(stderr, "socket() failed...\n");
        return err_ret;
    }

    /* set initial values */
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    serv_addr.sin_addr.s_addr = inet_addr(IP);
    memset(&(serv_addr.sin_zero), 0, 8);

    /* bind address with socket */
    if(bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr)) == -1)
    {
        err_ret = errno;
        fprintf(stderr, "bind() failed...\n");
        return err_ret;
    }

    /* start listening for connection */
    if(listen(sockfd, BACKLOG) == -1)
    {
        err_ret = errno;
        fprintf(stderr, "listen() failed...\n");
        return err_ret;
    }

    printf("Server start\n");
    WriteToFile("","","server_log.txt",0);
    if(pthread_create(&interrupt, NULL, io_handler, NULL) != 0)
    {
        err_ret = errno;
        fprintf(stderr, "pthread_create() failed...\n");
        return err_ret;
    }

    /* keep accepting connections */
    printf("Starting socket listener...\n");
    while(1)
    {
        sin_size = sizeof(struct sockaddr_in);
        if((newfd = accept(sockfd, (struct sockaddr *)&client_addr, (socklen_t*)&sin_size)) == -1)
        {
            err_ret = errno;
            fprintf(stderr, "accept() failed...\n");
            return err_ret;
        }
        else
        {
            if(client_list.size == CLIENTS)
            {
                fprintf(stderr, "Connection full, request rejected...\n");
                continue;
            }
            printf("Connection requested received...\n");
            struct THREADINFO threadinfo;
            threadinfo.sockfd = newfd;
            strcpy(threadinfo.alias, "Anonymous");
            pthread_mutex_lock(&clientlist_mutex);
            list_insert(&client_list, &threadinfo);
            pthread_mutex_unlock(&clientlist_mutex);
            pthread_create(&threadinfo.thread_ID, NULL, client_handler, (void *)&threadinfo);
        }
    }

    return 0;
}

void *io_handler(void *param)
{
    char option[OPTLEN];
    while(scanf("%s", option)==1)
    {
        if(!strcmp(option, "exit"))
        {
            /* clean up */
            printf("Terminating server...\n");
            pthread_mutex_destroy(&clientlist_mutex);
            close(sockfd);
            exit(0);
        }
        else if(!strcmp(option, "list"))
        {
            pthread_mutex_lock(&clientlist_mutex);
            list_dump(&client_list);
            pthread_mutex_unlock(&clientlist_mutex);
        }
        else
        {
            fprintf(stderr, "Unknown command: %s...\n", option);

        }
    }
    return NULL;
}

void *client_handler(void *fd)
{
    struct THREADINFO threadinfo = *(struct THREADINFO *)fd;
    struct PACKET packet;
    struct LLNODE *curr;
    int bytes, sent;
    while(1)
    {
        bytes = recv(threadinfo.sockfd, (void *)&packet, sizeof(struct PACKET), 0);
        if(!bytes)
        {
            fprintf(stderr, "Connection lost from [%d] %s...\n", threadinfo.sockfd, threadinfo.alias);
            pthread_mutex_lock(&clientlist_mutex);
            list_delete(&client_list, &threadinfo);
            pthread_mutex_unlock(&clientlist_mutex);
            break;
        }
        printf("[%d] %s %s %s\n", threadinfo.sockfd, packet.option, packet.alias, packet.buff);
        if(!strcmp(packet.option, "alias"))
        {
            printf("%s has logged-in", packet.alias);
            WriteToFile(packet.alias,"","server_log.txt",1);
            pthread_mutex_lock(&clientlist_mutex);
            for(curr = client_list.head; curr != NULL; curr = curr->next)
            {
                if(compare(&curr->threadinfo, &threadinfo) == 0)
                {
                    strcpy(curr->threadinfo.alias, packet.alias);
                    strcpy(threadinfo.alias, packet.alias);
                    break;
                }
            }
            pthread_mutex_unlock(&clientlist_mutex);
        }
        else if(!strcmp(packet.option, "message"))
        {
            int i;
            char target[ALIASLEN];
            for(i = 0; packet.buff[i] != ' '; i++);
            packet.buff[i++] = 0;
            strcpy(target, packet.buff);
            pthread_mutex_lock(&clientlist_mutex);
            for(curr = client_list.head; curr != NULL; curr = curr->next)
            {
                if(strcmp(target, curr->threadinfo.alias) == 0)
                {
                    struct PACKET spacket;
                    memset(&spacket, 0, sizeof(struct PACKET));
                    if(!compare(&curr->threadinfo, &threadinfo)) continue;
                    strcpy(spacket.option, "msg");
                    strcpy(spacket.alias, packet.alias);
                    strcpy(spacket.buff, &packet.buff[i]);
                    sent = send(curr->threadinfo.sockfd, (void *)&spacket, sizeof(struct PACKET), 0);
                }
            }
            pthread_mutex_unlock(&clientlist_mutex);
        }
        else if(!strcmp(packet.option, "send"))
        {
            pthread_mutex_lock(&clientlist_mutex);
            for(curr = client_list.head; curr != NULL; curr = curr->next)
            {
                struct PACKET spacket;
                memset(&spacket, 0, sizeof(struct PACKET));
                if(!compare(&curr->threadinfo, &threadinfo)) continue;
                strcpy(spacket.option, "msg");
                strcpy(spacket.alias, packet.alias);
                strcpy(spacket.buff, packet.buff);
                sent = send(curr->threadinfo.sockfd, (void *)&spacket, sizeof(struct PACKET), 0);
            }
            pthread_mutex_unlock(&clientlist_mutex);
        }
        else if(!strcmp(packet.option, "exit"))
        {
            printf("%s has logged out\n",packet.alias);
            WriteToFile(threadinfo.alias,"","server_log.txt",2);
            pthread_mutex_lock(&clientlist_mutex);
            list_delete(&client_list, &threadinfo);
            pthread_mutex_unlock(&clientlist_mutex);
            break;
        }
        else if(!strcmp(packet.option, "signup"))
        {
            pthread_mutex_lock(&clientlist_mutex); //make new mutex if deadlocked
            //signup
            printf("creating new user..\n");
            int userexist = 0;
            userlist = fopen("userlist.txt","a+");
            char username[ALIASLEN];
            while(fscanf(userlist,"%s",username)>0)
            {
                if(!strcmp(packet.alias,username))
                {
                    //username already exists
                    userexist = 1;
                    break;
                }
            }
            if(!userexist)
            {
                fprintf(userlist,"%s %s\n",packet.alias,packet.buff);
                printf("created new user: %s\n",packet.alias);
            }
            else
            {
                printf("username %s already exist. Disconnecting..\n", packet.alias);
                list_delete(&client_list, &threadinfo);
                fclose(userlist);
                pthread_mutex_unlock(&clientlist_mutex);
                break;
            }
            fclose(userlist);
            pthread_mutex_unlock(&clientlist_mutex);
        }
        else if(!strcmp(packet.option,"auth"))
        {
            char temp[32];
            int authed = 0;
            pthread_mutex_lock(&clientlist_mutex);
            userlist = fopen("userlist.txt","r");
            printf("authenticating..\n");
            while(fscanf(userlist,"%s",&temp)>0)
            {
                if(!strcmp(temp,packet.alias))
                {
                    printf("user found!\n");
                    fscanf(userlist,"%s",&temp);
                    if(!strcmp(temp,packet.buff))
                    {
                        printf("%s password match!\n", packet.alias);
                        authed = 1;
                        break;
                    }
                    else
                    {
                        printf("%s password incorrect\n", packet.alias);
                    }
                }
                else printf("user %s not found\n", packet.alias);
            }
            fclose(userlist);
            if(!authed)
            {
                list_delete(&client_list, &threadinfo);
                pthread_mutex_unlock(&clientlist_mutex);
                break;
            }
            pthread_mutex_unlock(&clientlist_mutex);
        }
        else
        {
            fprintf(stderr, "Garbage data from [%d] %s...\n", threadinfo.sockfd, threadinfo.alias);
        }
    }

    /* clean up */
    close(threadinfo.sockfd);

    return NULL;
}


