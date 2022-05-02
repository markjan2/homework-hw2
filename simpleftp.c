
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <glob.h>

#define S_OK          true
#define S_FAIL        false
#define DEFAULT_PORT  9999
#define BUFSIZE       1024

typedef struct command_info
{
	char *cCommand;
	char *cFileName;
	size_t iNumbers;
} command_info;

bool receive_message(int iFd, char *cCommand, command_info *cmd_info)
{
	char cBuf[32];
	int iReceived = -1;

	bzero(cBuf, sizeof(cBuf));
	if ((iReceived = recv(iFd, cBuf, 32, 0)) < 0) 
	{
		printf("fail to receive message from client\n");
		close(iFd);
		return S_FAIL;
	}

	if(strcmp(cCommand, "command") == 0)
	{
		//printf("receive command : %s\n", cBuf);
		strncpy(cmd_info->cCommand, cBuf, sizeof(cBuf));
	}
	else if(strcmp(cCommand, "filename") == 0)
	{
		//printf("receive FileName : %s\n", cBuf);
		strncpy(cmd_info->cFileName, cBuf, sizeof(cBuf));
	}
	else if(strcmp(cCommand, "filenumbers") == 0)
	{
		cmd_info->iNumbers = atoi(cBuf);
        //printf("file numbers needs to handle : %d\n", cmd_info->iNumbers);
	}
	
	return S_OK;
}

bool recv_files(int iFd, command_info *cmd_info)
{
	char cBuf[BUFSIZE];
	int iWriteSize = 0, iReadSize = 0;
	FILE *fp;
	int iNum = 0;
	
	if(receive_message(iFd, "filenumbers", cmd_info) != S_OK)   // receive file numbers
	{
		printf("[recv_files] receive file numbers fail\n");
	}
	
	printf("[recv_files] %ld files needs to receive\n", cmd_info->iNumbers);
	
	for(iNum = 0;iNum < cmd_info->iNumbers;iNum++)
	{
        if(receive_message(iFd, "filename", cmd_info) != S_OK)   // receive real file name
        {
            printf("[recv_files] receive num %d files fail\n", iNum);
        }

        if(send(iFd, "Done", 4, 0) < 0)  // send file numbers 
        {
            printf("[recv_files] send file numbers to receiver fail\n");
            close(iFd);
            return S_FAIL;
        }
        
        printf("[recv_files] begin to receive %s \n", cmd_info->cFileName);
        
        if((fp = fopen(cmd_info->cFileName, "w")) < 0)
        {
            printf("[recv_files] open file fail\n");
            return S_FAIL;
        }

        iReadSize = read(iFd, cBuf, sizeof(cBuf));
        iWriteSize = fwrite(cBuf, sizeof(char), iReadSize, fp);
        
        printf("[recv_files] receive %s Done\n", cmd_info->cFileName);
        fclose(fp);

	}

	return S_OK;
}


bool send_files(int iFd, command_info *cmd_info)
{
	char cBuf[BUFSIZE];
	int iWriteSize = 0, iReadSize = 0;
	int iInputFd = 0;
	struct stat file_status;
	int i = 0;
	
	FILE *fp;
	glob_t globbuf;
	
	if (glob(cmd_info->cFileName, GLOB_PERIOD, NULL, &globbuf) == GLOB_NOSPACE 
			|| glob(cmd_info->cFileName, GLOB_PERIOD, NULL, &globbuf) == GLOB_NOMATCH)
	{
		printf("[send_files] not match\n");
		return S_FAIL; 
	}
	
	printf("[send_files] %ld files want to send\n", globbuf.gl_pathc);
	
	char cNumbers[32];
	memset(cNumbers, 0, 32);
	
	sprintf(cNumbers, "%ld", globbuf.gl_pathc);
	
	if(send(iFd, cNumbers, sizeof(cNumbers), 0) < 0)  // send file numbers 
	{
		printf("[send_files] send file numbers to receiver fail\n");
		close(iFd);
		return S_FAIL;
	}
	
	for(i=0;i<globbuf.gl_pathc;i++)
	{
        printf("[send_files] %s is sending\n", globbuf.gl_pathv[i]);
        if(send(iFd, globbuf.gl_pathv[i], sizeof(globbuf.gl_pathv[i]), 0) < 0)  // send real file name
        {
            printf("[send_files] send file name to receiver fail\n");
            close(iFd);
            return S_FAIL;
        }

        if(receive_message(iFd, "Done", cmd_info) != S_OK)
        {
            printf("[send_files] send file name to receiver fail\n");
            close(iFd);
            return S_FAIL;
        }

        if((iInputFd = open(globbuf.gl_pathv[i], O_RDWR)) < 0)
        {
            printf("[send_files] open %s file fail\n", globbuf.gl_pathv[i]);
            return S_FAIL;
        }

        if(fstat(iInputFd, &file_status) < 0)
        {
            printf("[send_files] file state is wrong\n");
            return S_FAIL;
        }

		if(file_status.st_size > 1000)
		{
			printf("file size too big\n");
		}

        if((fp = fopen(globbuf.gl_pathv[i], "r")) < 0)
        {
            printf("[send_files] open file fail\n");
            return S_FAIL;
        }

        while(!feof(fp))
        {
            iReadSize = fread(cBuf, sizeof(char), sizeof(cBuf), fp);
            iWriteSize = write(iFd, cBuf, iReadSize);
        }

        printf("[send_files] send %s to receiver done\n", globbuf.gl_pathv[i]);
        
        fclose(fp);
	}

	return S_OK;
}

bool connect_server(char *cServerIP, command_info *cmd_info)
{
	int iSockfd = 0;
	struct sockaddr_in sClientInfo;
	
	if ((iSockfd = socket(AF_INET , SOCK_STREAM , 0)) < 0)
	{
		printf("create socket fail\n");
		return S_FAIL;
    }
	
	sClientInfo.sin_family = AF_INET;
	sClientInfo.sin_port = htons(DEFAULT_PORT);
	sClientInfo.sin_addr.s_addr = inet_addr(cServerIP);
	
	if(connect(iSockfd, (struct sockaddr *)&sClientInfo, sizeof(sClientInfo)) < 0)
	{
		printf("connect to server fail\n");
		return S_FAIL;
	}
	
	while(true)
	{
		memset(cmd_info->cCommand, 0, 32);
		memset(cmd_info->cFileName, 0, 32);
        printf("\n");
		printf("*************************************\n");
        printf("* Usage:                            *\n");
        printf("*    please input command:   STOR   *\n");
        printf("*    please input file name: *txt   *\n");
		printf("*    (enter 'q' to close the socket)*\n");
        printf("*************************************\n");
        printf("\n");
        
		printf("enter 'q' to close the socket\n");
		printf("please input command:");
		scanf("%s", cmd_info->cCommand);
		
		if(send(iSockfd, cmd_info->cCommand, sizeof(cmd_info->cCommand), 0) < 0)
		{
			printf("send command to server fail\n");
			close(iSockfd);
			return S_FAIL;
		}
		
		if (!strcmp(cmd_info->cCommand, "q")) // quit the program
		{
			break;
		}
		
		printf("please input file name:");
		scanf("%s", cmd_info->cFileName);
			
		if(send(iSockfd, cmd_info->cFileName, sizeof(cmd_info->cFileName), 0) < 0)
		{
			printf("send file name to server fail\n");
			close(iSockfd);
		    return S_FAIL;
		}
		
		if (strcmp(cmd_info->cCommand, "RETR") == 0)
		{
			if(!recv_files(iSockfd, cmd_info))
			{
				printf("[client]upload file fail\n");
				close(iSockfd);
				return S_FAIL;
			}
		}
		else if(strcmp(cmd_info->cCommand, "STOR") == 0)
		{
			if(!send_files(iSockfd, cmd_info))
			{
				printf("[client]upload file fail\n");
				close(iSockfd);
				return S_FAIL;
			}
		}
		else
		{
			printf("[client]command is not support\n");
			return S_FAIL;
		}
	}
	
	close(iSockfd);
	
	return S_OK;
}

bool establish_connection(command_info *cmd_info)
{
	int iSockfd = 0;
	fd_set fdset, master;
	int iClientInfoLen = 0, iClientFd = 0;
	
	struct sockaddr_in sServerInfo, sClientInfo;
	
	if ((iSockfd = socket(PF_INET,SOCK_STREAM,IPPROTO_TCP)) < 0) //socket 類型 
    {
        printf("Failed to create socket\n");
    }
	
	memset(&sServerInfo, 0, sizeof(sServerInfo));
	
	sServerInfo.sin_family = AF_INET;
	sServerInfo.sin_port = htons(DEFAULT_PORT);
	sServerInfo.sin_addr.s_addr = INADDR_ANY;
	
	if(bind(iSockfd, (struct sockaddr *)&sServerInfo, sizeof(sServerInfo)) == -1)
	{
		printf("bind fail\n");
		return S_FAIL;
	}
	
	if(listen(iSockfd, 10) == -1)
	{
		printf("listen fail\n");
		return S_FAIL;
	}

	FD_ZERO(&fdset);
	FD_ZERO(&master);
	FD_SET(iSockfd, &master);
	
	iClientInfoLen = sizeof(sClientInfo);

    while(true)
    {
        if((iClientFd = accept(iSockfd, (struct sockaddr *)&sClientInfo, &iClientInfoLen)) < 0)
        {
            printf("can't accept client connection\n");
            return S_FAIL;
        }

        while(true)
        {
            memset(cmd_info->cCommand, 0, 32);
            memset(cmd_info->cFileName, 0, 32);
            
            receive_message(iClientFd, "command", cmd_info);
            receive_message(iClientFd, "filename", cmd_info);

            if(strcmp(cmd_info->cCommand, "RETR") == 0)
            {
                if(send_files(iClientFd, cmd_info) != S_OK)
                {
                    printf("[server]download file fail\n");
                    close(iClientFd);
                    FD_CLR(iClientFd, &master);
                    return S_FAIL;
                }
            }
            else if(strcmp(cmd_info->cCommand, "STOR") == 0)
            {
                if(recv_files(iClientFd, cmd_info) != S_OK)
                {
                    printf("[server]upload file fail\n");
                    close(iClientFd);
                    FD_CLR(iClientFd, &master);
                    return S_FAIL;
                }
            }
            else if(strcmp(cmd_info->cCommand, "q") == 0)
            {
                printf("[server]client is stop connecting\n");
                close(iClientFd);
                FD_CLR(iClientFd, &master);
                return S_OK;
            }
            else 
            {
                printf("[server]command not support\n");
                close(iClientFd);
                FD_CLR(iClientFd, &master);
                return S_FAIL;
            }
        }

        close(iSockfd);
    }

	return S_OK;
}

int main(int argc, char *argv[])
{
	char cServerIP[32];
	memset(cServerIP, 0, 32);
	
	command_info *cmd_info = (command_info *)malloc(sizeof(command_info));
	cmd_info->cCommand = (char *)malloc(sizeof(char)*32);
	cmd_info->cFileName = (char *)malloc(sizeof(char)*32);
	
	bool bClientFlag = false;
	
	int opt;
	
	while((opt = getopt(argc, argv, "C:c:")) != -1) 
	{
        switch(opt)
		{
			case 'C':
				strncpy(cmd_info->cCommand, optarg, sizeof(cmd_info->cCommand));
				break;
			case 'c':
				strncpy(cServerIP, optarg, sizeof(cServerIP));
				bClientFlag = true;
				break;
			default: 
				break;
		}
	}	
	
	argc -= optind;
	argv += optind;
	
	if(argc > 0) 
    {
        printf("simpleftp [-c <serverIP>]\n");
		return S_FAIL;
    }

    if(bClientFlag && *cServerIP == '\0')
	{
	    printf("server IP incorrect\n");
	    return S_FAIL;
	}
	
	if(bClientFlag)
	{
		if(!connect_server(cServerIP, cmd_info))
		{
			return S_FAIL;
		}
		
		return S_OK;
	}

	if(!establish_connection(cmd_info))
	{
		return S_FAIL;
	}
	
	return S_OK;
}
