#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <pwd.h>
#include <time.h>
#include <sys/inotify.h>
#include <dirent.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#define LOCKFILE "/var/run/daemon.pid"
#define LOCKMODE (S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)
sigset_t mask;

int lockfile(int fd)
{
	struct flock fl;
	fl.l_type=F_WRLCK;
	fl.l_start=0;
	fl.l_whence=SEEK_SET;
	fl.l_len=0;
	return(fcntl(fd, F_SETLK, &fl));
}//파일을 lock한다
int unlockfile(int fd)
{
	struct flock fl;
	fl.l_type=F_UNLCK;
	fl.l_start=0;
	fl.l_whence=SEEK_SET;
	fl.l_len=0;
	return(fcntl(fd, F_SETLK, &fl));
}lock된 파일을 unlock한다
void daemonize(const char *cmd)//데몬을 만든다
{
	int i, fd0, fd1, fd2;
	pid_t pid;
	struct rlimit rl;
	struct sigaction sa;

	umask(0);
	if(access("/ttmp",F_OK)<0){
		mkdir("/ttmp",0777);
	}
	if(access("/kirk.c",F_OK)<0){
		int fd = creat("/kirk.c",0666);
		close(fd);
	}
	if(getrlimit(RLIMIT_NOFILE, &rl)<0){
		printf("%s: cannot get file limit",cmd);
		exit(1);
	}
	if((pid=fork())<0){
		printf("%s: cannot fork",cmd);
		exit(1);
	}
	else if(pid!=0)
		exit(0);
	setsid();

	sa.sa_handler=SIG_IGN;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags=0;
	if(sigaction(SIGHUP, &sa, NULL)<0){
		printf("cannot ignore SIGHUP");
		exit(1);
	}
	if(chdir("/")<0){
		printf("cannot change directory to /");
		exit(1);
	}

	if(rl.rlim_max==RLIM_INFINITY)
		rl.rlim_max=1024;
	for(i=0;i<rl.rlim_max;i++)
		close(i);

	fd0 = open("/dev/null", O_RDWR);
	fd1 = dup(0);
	fd2 = dup(0);

	openlog(cmd, LOG_CONS, LOG_DAEMON);
	if(fd0!=0||fd1!=1||fd2!=2){
		syslog(LOG_ERR, "Unexpected File Descriptors %d %d %d", fd0, fd1, fd2);
		exit(1);
	}
}
struct my_msgbuf{ //ssu_client와 –u로 통신할 때
	long mtype;
	char mtext[4048];
};
struct ex_msgbuf{//ssu_client와 –r로 통신할 때
	long mtype;
	struct p_info{
		uid_t u;
		char mtext[20];
	}info;
};
struct ex_msgbuf ebuf[100];
int count=0;
char *getfile(char *name)
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
}
char dirpath[1024][1024]; //ttmp를 포함한 하위 디렉토리 저장배열
int dircount=0;//저장되는 디렉토리의 개수
void regit_dir(char *path){//디렉토리를 탐색하여 저장하는 함수
		DIR *dp;
		struct dirent *dirp;
		struct stat sbuf;
		char dpath[100];
		memset(&dpath,0,sizeof(dpath));
		dp=opendir(path);
		chdir(path);
		getcwd(dpath,sizeof(dpath));
		strcpy(dirpath[dircount++],dpath);
		while((dirp=readdir(dp))!=NULL){
				if(strcmp(dirp->d_name,".")==0||strcmp(dirp->d_name,"..")==0)
						continue;
				stat(dirp->d_name,&sbuf);
				if(S_ISDIR(sbuf.st_mode)){
						regit_dir(dirp->d_name);
				}
		}
		chdir("..");
		closedir(dp);
}
void start_ino(char *filepath){//inotify를 이용하여 디렉토리를 감시
	int length;
	int fd;
	int wd;
	int suid;
	struct stat stat_buf;
	struct passwd *spass;
	char buffer[1024];
	FILE *fp;
	int sd;
	int fpno;
	time_t current;
	int j=0;
	fd=inotify_init();//inotify 등록
	wd=inotify_add_watch(fd,filepath,IN_CREATE|IN_DELETE|IN_OPEN);//감시할 디렉토리 등록
		int i=0;
		memset(&buffer,0,sizeof(buffer));
		length=read(fd,buffer,1024);
		while(i<length){
		struct inotify_event *event = (struct inotify_event*)&buffer[i]; //이벤트 버퍼등록
		if(event->len){
				time(&current);
				chdir(filepath); //이벤트 발생시 위치 이동
					stat(event->name,&stat_buf);
					suid=stat_buf.st_uid;
					spass=getpwuid(suid);
			if(event->mask&IN_CREATE){//추가 이벤트
				if(event->mask&IN_ISDIR){
					if(spass->pw_uid==0){
						if((fp=fopen("/syslog","a"))==NULL){
							sd=creat("/syslog",0666);
							close(sd);
							fp=fopen("/syslog","a");
						}
						fpno=fileno(fp);
						lockfile(fpno);
						fprintf(fp,"The %s user of '%s' directory is added on %s",spass->pw_name,event->name,ctime(&current));
						unlockfile(fpno);
						fclose(fp);
						break;
					}
					else{
						char path[100];
						strcpy(path,spass->pw_dir);
						strcat(path,"/syslog");
						if((fp=fopen(path,"a"))==NULL){
							sd=creat(path,0666);
							close(sd);
							fp=fopen(path,"a");
						}
						fpno=fileno(fp);
						lockfile(fpno);
						fprintf(fp,"The %s user of '%s' directory is added on %s",spass->pw_name,event->name,ctime(&current));
						unlockfile(fpno);
						fclose(fp);
						break;
					}
				}else{
					if(spass->pw_uid==0){
						if((fp=fopen("/syslog","a"))==NULL){
							sd=creat("/syslog",0666);
							close(sd);
							fp=fopen("/syslog","a");
						}
						fpno=fileno(fp);
						lockfile(fpno);
						fprintf(fp,"The %s user of '%s' file is added on %s",spass->pw_name,event->name,ctime(&current));
						unlockfile(fpno);
						fclose(fp);
						char fuser[10];
						memset(&fuser,0,sizeof(fuser));
						strcpy(fuser,spass->pw_name);
						char *extention=getfile(event->name);
						for(j=0;j<count;j++){//-r로 등록된 확장자 있으면 기록
							if(strcmp(ebuf[j].info.mtext,extention)==0){
								struct passwd *spass2;
								char path[100];
								spass2=getpwuid(ebuf[j].info.u);
								if(ebuf[j].info.u==0){
									fp=fopen(path,"a");
									fpno=fileno(fp);
									lockfile(fpno);
									fprintf(fp,"The %s user of '%s' file is added on %s",fuser,event->name,ctime(&current));
									unlockfile(fpno);
									fclose(fp);
								}
								else{
									strcpy(path,spass2->pw_dir);
									strcat(path,"/syslog");
									fp=fopen(path,"a");
									fpno=fileno(fp);
									lockfile(fpno);
									fprintf(fp,"The %s user of '%s' file is added on %s",fuser,event->name,ctime(&current));
									unlockfile(fpno);
									fclose(fp);
								}
							}
						}
						break;
					}
					else{
						char path[100];
						strcpy(path,spass->pw_dir);
						strcat(path,"/syslog");
						if((fp=fopen(path,"a"))==NULL){
							sd=creat(path,0666);
							close(sd);
							fp=fopen(path,"a");
						}
						fpno=fileno(fp);
						lockfile(fpno);
						fprintf(fp,"The %s user of '%s' file is added on %s",spass->pw_name,event->name,ctime(&current));
						unlockfile(fpno);
						fclose(fp);
						char fuser[10];
						memset(&fuser,0,sizeof(fuser));
						strcpy(fuser,spass->pw_name);
						char *extention=getfile(event->name);
						for(j=0;j<count;j++){
							if(strcmp(ebuf[j].info.mtext,extention)==0){
								struct passwd *spass2;
								char path[100];
								spass2=getpwuid(ebuf[j].info.u);
								if(ebuf[j].info.u==0){
									fp=fopen(path,"a");
									fpno=fileno(fp);
									lockfile(fpno);
									fprintf(fp,"The %s user of '%s' file is added on %s",fuser,event->name,ctime(&current));
									unlockfile(fpno);
									fclose(fp);
								}
								else{
									strcpy(path,spass2->pw_dir);
									strcat(path,"/syslog");
									fp=fopen(path,"a");
									fpno=fileno(fp);
									lockfile(fpno);
									fprintf(fp,"The %s user of '%s' file is added on %s",fuser,event->name,ctime(&current));
									unlockfile(fpno);
									fclose(fp);
								}
							}
						}
						break;
					}
				}
			}
			else if(event->mask&IN_DELETE){//삭제 이벤트
				if(event->mask&IN_ISDIR){
					if(spass->pw_uid==0){
						if((fp=fopen("/syslog","a"))==NULL){
							sd=creat("/syslog",0666);
							close(sd);
							fp=fopen("/syslog","a");
						}
						fpno=fileno(fp);
						lockfile(fpno);
						fprintf(fp,"The %s user of '%s' directory is deleted on %s",spass->pw_name,event->name,ctime(&current));
						unlockfile(fpno);
						fclose(fp);
					}
					else{
						char path[100];
						strcpy(path,spass->pw_dir);
						strcat(path,"/syslog");
						if((fp=fopen(path,"a"))==NULL){
							sd=creat(path,0666);
							close(sd);
							fp=fopen(path,"a");
						}
						fpno=fileno(fp);
						lockfile(fpno);
						fprintf(fp,"The %s user of '%s' directory is deleted on %s",spass->pw_name,event->name,ctime(&current));
						unlockfile(fpno);
						fclose(fp);
					}
				}else{
					if(spass->pw_uid==0){
						if((fp=fopen("/syslog","a"))==NULL){
							sd=creat("/syslog",0666);
							close(sd);
							fp=fopen("/syslog","a");
						}
						fpno=fileno(fp);
						lockfile(fpno);
						fprintf(fp,"The %s user of '%s' file is deleted on %s",spass->pw_name,event->name,ctime(&current));
						unlockfile(fpno);
						fclose(fp);
						char fuser[10];
						memset(&fuser,0,sizeof(fuser));
						strcpy(fuser,spass->pw_name);
						char *extention=getfile(event->name);
						for(j=0;j<count;j++){
							if(strcmp(ebuf[j].info.mtext,extention)==0){
								struct passwd *spass2;
								char path[100];
								spass2=getpwuid(ebuf[j].info.u);
								if(ebuf[j].info.u==0){
									fp=fopen(path,"a");
									fpno=fileno(fp);
									lockfile(fpno);
									fprintf(fp,"The %s user of '%s' file is deleted on %s",fuser,event->name,ctime(&current));
									unlockfile(fpno);
									fclose(fp);
								}
								else{
									strcpy(path,spass2->pw_dir);
									strcat(path,"/syslog");
									fp=fopen(path,"a");
									fpno=fileno(fp);
									lockfile(fpno);
									fprintf(fp,"The %s user of '%s' file is deleted on %s",fuser,event->name,ctime(&current));
									unlockfile(fpno);
									fclose(fp);
								}
							}
						}
					}
					else{
						char path[100];
						strcpy(path,spass->pw_dir);
						strcat(path,"/syslog");
						if((fp=fopen(path,"a"))==NULL){
							sd=creat(path,0666);
							close(sd);
							fp=fopen(path,"a");
						}
						fpno=fileno(fp);
						lockfile(fpno);
						fprintf(fp,"The %s user of '%s' file is deleted on %s",spass->pw_name,event->name,ctime(&current));
						unlockfile(fpno);
						fclose(fp);
						char fuser[10];
						memset(&fuser,0,sizeof(fuser));
						strcpy(fuser,spass->pw_name);
						char *extention=getfile(event->name);
						for(j=0;j<count;j++){
							if(strcmp(ebuf[j].info.mtext,extention)==0){
								struct passwd *spass2;
								char path[100];
								spass2=getpwuid(ebuf[j].info.u);
								if(ebuf[j].info.u==0){
									fp=fopen(path,"a");
									fpno=fileno(fp);
									lockfile(fpno);
									fprintf(fp,"The %s user of '%s' file is deleted on %s",fuser,event->name,ctime(&current));
									unlockfile(fpno);
									fclose(fp);
								}
								else{
									strcpy(path,spass2->pw_dir);
									strcat(path,"/syslog");
									fp=fopen(path,"a");
									fpno=fileno(fp);
									lockfile(fpno);
									fprintf(fp,"The %s user of '%s' file is deleted on %s",fuser,event->name,ctime(&current));
									unlockfile(fpno);
									fclose(fp);
								}
							}
						}
					}
				}
			}
			else if(event->mask&IN_OPEN){//수정 함수
				if(event->mask&IN_ISDIR){
					if(spass->pw_uid==0){
						if((fp=fopen("/syslog","a"))==NULL){
							sd=creat("/syslog",0666);
							close(sd);
							fp=fopen("/syslog","a");
						}
						fpno=fileno(fp);
						lockfile(fpno);
						fprintf(fp,"The %s user of '%s' directory is modified on %s",spass->pw_name,event->name,ctime(&current));
						unlockfile(fpno);
						fclose(fp);
					}
					else{
						char path[100];
						strcpy(path,spass->pw_dir);
						strcat(path,"/syslog");
						if((fp=fopen(path,"a"))==NULL){
							sd=creat(path,0666);
							close(sd);
							fp=fopen(path,"a");
						}
						fpno=fileno(fp);
						lockfile(fpno);
						fprintf(fp,"The %s user of '%s' directory is modified on %s",spass->pw_name,event->name,ctime(&current));
						unlockfile(fpno);
						fclose(fp);
					}
				}else{
					if(spass->pw_uid==0){
						if((fp=fopen("/syslog","a"))==NULL){
							sd=creat("/syslog",0666);
							close(sd);
							fp=fopen("/syslog","a");
						}
						fpno=fileno(fp);
						lockfile(fpno);
						fprintf(fp,"The %s user of '%s' file is modified on %s",spass->pw_name,event->name,ctime(&current));
						unlockfile(fpno);
						fclose(fp);
						char fuser[10];
						memset(&fuser,0,sizeof(fuser));
						strcpy(fuser,spass->pw_name);
						char *extention=getfile(event->name);
						for(j=0;j<count;j++){
							if(strcmp(ebuf[j].info.mtext,extention)==0){
								struct passwd *spass2;
								char path[100];
								spass2=getpwuid(ebuf[j].info.u);
								if(ebuf[j].info.u==0){
									fp=fopen(path,"a");
									fpno=fileno(fp);
									lockfile(fpno);
									fprintf(fp,"The %s user of '%s' file is modified on %s",fuser,event->name,ctime(&current));
									unlockfile(fpno);
									fclose(fp);
								}
								else{
									strcpy(path,spass2->pw_dir);
									strcat(path,"/syslog");
									fp=fopen(path,"a");
									fpno=fileno(fp);
									lockfile(fpno);
									fprintf(fp,"The %s user of '%s' file is modified on %s",fuser,event->name,ctime(&current));
									unlockfile(fpno);
									fclose(fp);
								}
							}
						}
					}
					else{
						char path[100];
						strcpy(path,spass->pw_dir);
						strcat(path,"/syslog");
						if((fp=fopen(path,"a"))==NULL){
							sd=creat(path,0666);
							close(sd);
							fp=fopen(path,"a");
						}
						fpno=fileno(fp);
						lockfile(fpno);
						fprintf(fp,"The %s user of '%s' file is modified on %s",spass->pw_name,event->name,ctime(&current));
						unlockfile(fpno);
						fclose(fp);
						char fuser[10];
						memset(&fuser,0,sizeof(fuser));
						strcpy(fuser,spass->pw_name);
						char *extention=getfile(event->name);
						for(j=0;j<count;j++){
							if(strcmp(ebuf[j].info.mtext,extention)==0){
								struct passwd *spass2;
								char path[100];
								spass2=getpwuid(ebuf[j].info.u);
								if(ebuf[j].info.u==0){
									fp=fopen(path,"a");
									fpno=fileno(fp);
									lockfile(fpno);
									fprintf(fp,"The %s user of '%s' file is modified on %s",fuser,event->name,ctime(&current));
									unlockfile(fpno);
									fclose(fp);
								}
								else{
									strcpy(path,spass2->pw_dir);
									strcat(path,"/syslog");
									fp=fopen(path,"a");
									fpno=fileno(fp);
									lockfile(fpno);
									fprintf(fp,"The %s user of '%s' file is modified on %s",fuser,event->name,ctime(&current));
									unlockfile(fpno);
									fclose(fp);
								}
							}
						}
					}
				}
			}
		}
		i+=sizeof(struct inotify_event)+event->len;
		}
	inotify_rm_watch(fd,wd);
	close(fd);
}
int thr_count=0;//저장된 디렉토리만큼 스레드를 생성할 개수
pthread_t td[1024];
int check=0;
void *thr_fn3(void *arg)//inotify를 실행할 스레드
{
	int k;
	start_ino(dirpath[thr_count++]);
	check=1;	
}
void *thr_fn2(void *arg)//확장자와 uid를 가져올 스레드
{
	int msqid;
	key_t key;
	while(1){
		key=ftok("/kirk.c",'B');
		msqid=msgget(key,0777|IPC_CREAT);
		for(;;){
			if(msgrcv(msqid,&ebuf[count],sizeof(ebuf[count])-sizeof(long),0,0)>0){
				count++;
				break;
			}
		}
	}
	return (0);
}
void *thr_fn(void *arg)//syslog를 보내주는 스레드
{
	struct my_msgbuf buf;
	int msqid;
	int fd;
	char path[100];
	char body[4048];
	key_t key;
	while(1){
	key=ftok("/kirk.c",'A');
	msqid=msgget(key,0777|IPC_CREAT);
	for(;;){
		if(msgrcv(msqid,&buf,sizeof(buf.mtext),0,0)>0)
			break;
	}
	memset(&path,0,sizeof(path));
	strcpy(path,buf.mtext);
	fd=open(path,O_RDONLY);
	memset(&buf.mtext,0,sizeof(buf.mtext));
	memset(&body,0,sizeof(body));
	read(fd,body,sizeof(body));
	close(fd);
	strcpy(buf.mtext,body);
	msgsnd(msqid,&buf,strlen(buf.mtext)+1,0);
	memset(&buf,0,sizeof(buf));
	}
	return (0);
}
int main(int argc, char *argv[])
{
	int err;
	
	pthread_t tid,tid2;
	char *cmd;
	struct sigaction sa;
	if((cmd=strrchr(argv[0], '/'))==NULL)
		cmd=argv[0];
	else
		cmd++;

	daemonize(cmd);

	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags=0;
	if(sigaction(SIGHUP, &sa, NULL)<0){
		printf("cannot restore SIGHUP default");
		exit(1);
	}
	sigfillset(&mask);
	if((err=pthread_sigmask(SIG_BLOCK, &mask, NULL))!=0){
		printf("SIG_BLOCK error");
		exit(1);
	}
	err=pthread_create(&tid, NULL, thr_fn, 0);
	if(err!=0){
		printf("cannot create thread");
		exit(1);
	}
	err=pthread_create(&tid2,NULL,thr_fn2,0);
	if(err!=0){
		printf("cannot create thread");
		exit(1);
	}
	while(1){
		check=0;
		int k;
		thr_count=0;
		memset(&td,0,sizeof(td));
		memset(&dirpath,0,sizeof(dirpath));
		dircount=0;
		regit_dir("/ttmp"); //디렉토리 탐색하며 등록
		for(k=0;k<dircount;k++){ //디렉토리만큼 스레드를 생성하여 감시
			err=pthread_create(&td[k],NULL,thr_fn3,0);
		}
		while(1){
			if(check==1){//스레드중 1개가 끝남
			for(k=0;k<dircount;k++){//스레드 모두 강제종료
				if(pthread_cancel(td[k])!=0)
					continue;
			}
			int errnum=0;
			for(k=0;k<dircount;k++){//종료된 스레드 회수
				if(pthread_join(td[k],NULL)==0)
					errnum=1;
				else 
					continue;
			}
			if(errnum==1)
				break;
			}
		}
	}
	exit(1);
}


