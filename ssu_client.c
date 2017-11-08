#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>

struct my_msgbuf{
	long mtype;
	char mtext[4048];
};//syslog를 얻어올 메시지
struct ex_msgbuf{
	long mtype;
	struct p_info{
		uid_t u;
		char mtext[20];
	}info;
};//확장자를 기록할 메시지
int lockfile(int fd){
	struct flock fl;
	fl.l_type=F_WRLCK;
	fl.l_start=0;
	fl.l_whence=SEEK_SET;
	fl.l_len=0;
	return(fcntl(fd,F_SETLK,&fl));
}//파일을 lock하는 함수
int unlockfile(int fd){
	struct flock fl;
	fl.l_type=F_WRLCK;
	fl.l_start=0;
	fl.l_whence=SEEK_SET;
	fl.l_len=0;
	return(fcntl(fd,F_SETLK,&fl));
}//파일 unlock함수
char * getfile(char *name)
{
	int i;
	int len=strlen(name);
	name+=len;
	char *fe;
	for(i=0;i<len;i++)
	{
		if(*name=='.')
		{
			fe=name+1;
			break;
		}
		name--;
	}
	return fe;
}//파일 확장자를 돌려주는 함수
int main(int argc, char *argv[])
{
	int ch;
	int fd;
	int uflag=0;
	int rflag=0;
	char idnum[20];
	char format[20];
	struct passwd *spass;
	struct my_msgbuf mbuf;
	struct ex_msgbuf ebuf;
	while((ch=getopt(argc,argv,"u:r:"))!=-1){ //옵션 구분
		switch(ch){
			case 'u' :
				uflag=1;
				strcpy(idnum,optarg);
				break;
			case 'r' :
				rflag=1;
				strcpy(format,optarg);
				break;
		}
	}
	memset(&mbuf,0,sizeof(mbuf));
	uid_t u;
	char cmd[100]="cat ";
	char path[100];
	key_t key;
	int msqid;
	if(uflag==0&&rflag==0){ //옵션이 없을때
		u = getuid(); //유저 ID를 가져옴
		if(u==0){
			strcat(cmd,"/syslog");
			system(cmd);
		}
		else{
			spass=getpwuid(u);
			strcpy(path,spass->pw_dir);
			strcat(path,"/syslog");
			strcat(cmd,path);
			setuid(0);
			fd=open(path,O_RDONLY);
			lockfile(fd);
			system(cmd);
			unlockfile(fd);
			close(fd);
		}
	}
	else if(uflag==1&&rflag==0){ //u 옵션일때
		u=getuid();
		if(u!=0){//유저가 root가 아니라면
		if(atoi(idnum)!=0){ // target이 root가 아니라면
			key=ftok("/kirk.c",'A');
			msqid=msgget(key,0777|IPC_CREAT);//메시지를 만든다
			mbuf.mtype=1;
			char buf[1024];
			memset(&buf,0,1024);
			spass=getpwuid(atoi(idnum));
			strcpy(path,spass->pw_dir);
			strcat(path,"/syslog");
			strcpy(mbuf.mtext,path);
				msgsnd(msqid,&mbuf,strlen(mbuf.mtext)+1,0);
			memset(&mbuf.mtext,0,sizeof(mbuf.mtext));
			for(;;){
				if(msgrcv(msqid,&mbuf,sizeof(mbuf.mtext),0,0)>0)
					break;
			}
			printf("%s",mbuf.mtext);
			msgctl(msqid,IPC_RMID,NULL);
		}
		else{ //root이면 /syslog의 기록을 가져옴
			strcat(cmd,"/syslog");
			fd=open("/syslog",O_RDONLY);
			lockfile(fd);
			system(cmd);
			unlockfile(fd);
			close(fd);
		}
		}
		else{//유저가 root라면
		if(atoi(idnum)!=0){
			spass=getpwuid(atoi(idnum));
			strcpy(path,spass->pw_dir);
			strcat(path,"/syslog");
			strcat(cmd,path);
			system(cmd);
		}
		else{
			strcat(cmd,"/syslog");
			fd=open("/syslog",O_RDONLY);
			lockfile(fd);
			system(cmd);
			unlockfile(fd);
			close(fd);
		}
		}
	}
	else if(uflag==0&&rflag==1){//r 옵션 사용시 유저ID와 확장자 기록
		char all[30]="*";
		char *extention;
		strcat(all,format);
		key=ftok("/kirk.c",'B');
		msqid=msgget(key,0777|IPC_CREAT);
		ebuf.mtype=1;
		extention=getfile(all);
		ebuf.info.u=getuid();
		strcpy(ebuf.info.mtext,extention);
		msgsnd(msqid,&ebuf,sizeof(ebuf)-sizeof(long),0);
		msgctl(msqid,IPC_RMID,NULL);
		printf("registed \"%s\"\n",all);
	}
	exit(0);
}
