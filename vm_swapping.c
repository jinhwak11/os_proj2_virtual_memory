#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <time.h>

#define CHILDNUM 3
#define PAGETNUM 1000
#define INDEXNUM 1000
#define FRAMENUM 32

int count = 0;
int i = 0;
int total_count = 0;
pid_t pid[CHILDNUM];
int front, rear = 0;
int run_queue[20];
int flag = 0;

int child_execution_time[CHILDNUM] ={3,6,5};
int child_execution_ctime[CHILDNUM];

struct msgbuf{
        long int  mtype;
        int pid_index;
        unsigned int  virt_mem[10];
};

typedef struct{
	int valid;
	int pfn;
	int counter;
	int disk;
}TABLE;

typedef struct{
	int valid;
	TABLE*  pt;
}DIR_TABLE;

DIR_TABLE dir_table[CHILDNUM][PAGETNUM];

int phy_mem [FRAMENUM];

int fpl[32] ; //free page list
int fpl_rear,fpl_front = 0;
//int fpn=0;

int msgq;
int ret;
int key = 0x12345;
struct msgbuf msg;

FILE* fptr;

void initialize_first_table()
{
	for( int l = 0; l < CHILDNUM ; l++){
		for(int j =0; j< PAGETNUM ; j++)
		{	
			dir_table[l][j].valid =0;
			dir_table[l][j].pt = NULL;
	
		}
	}
}

void child_signal_handler(int signum)  // sig child handler
{
	printf("pid:%d ,remaining cpu-burst%d\n",getpid(),child_execution_time[i]);
	child_execution_time[i] -- ;
	if(child_execution_time[i] <= 0)
		child_execution_time[i] = child_execution_ctime[i];
	printf("pid: %d get signal\n",getpid());
	memset(&msg,0,sizeof(msg));
	msg.mtype = IPC_NOWAIT;
	msg.pid_index = i;
	unsigned int addr;
	for (int k=0; k< 10 ; k++){
		addr = (rand() %5)<<22;
		addr |= (rand()%3)<<12;
		addr |= (rand()%0xfff);
		msg.virt_mem[k] = addr ;
	}
	ret = msgsnd(msgq, &msg, sizeof(msg),IPC_NOWAIT);
	if(ret == -1)
		perror("msgsnd error");


}
void clean_memory(DIR_TABLE* dtpt)
{
	DIR_TABLE* dir_ptf = dtpt;
	for(int j=0; j< PAGETNUM ; j++)
	{
		if(dir_ptf[j].valid == 1){
			dir_ptf[j].valid=0;
			for(int k=0 ; k < INDEXNUM ; k++)
				if(((dir_ptf[j].pt)[k].valid == 1)){
					if((dir_ptf[j].pt)[k].disk == 0){
						printf("add %d to fpl\n",(dir_ptf[j].pt)[k].pfn);
						fpl[(fpl_rear++)%FRAMENUM]=(dir_ptf[j].pt)[k].pfn;
					}
					else{
						//file 접근해서 free 시키기 
					} 
				}
			dir_ptf[j].pt =NULL;
			free(dir_ptf[j].pt);
		}
	}



}

void parent_signal_handler(int signum)  // sig parent handler
{
	if(flag == 1){
		clean_memory(dir_table[run_queue[(front-1)%20]]);
		flag = 0;
	}

        total_count ++;
        count ++;
        if(total_count >= 13){

		for(int k = 0; k < CHILDNUM ; k ++)
		{
			kill(pid[k],SIGKILL);
		}
		msgctl(msgq, IPC_RMID, NULL);
		fclose(fptr);
		exit(0);
	}
	if((front%20) != (rear%20)){
		child_execution_time[run_queue[front%20]] --;
		printf("time %d:================================================\n",total_count);
		kill(pid[run_queue[front % 20]],SIGINT);
		if((count == 3)|(child_execution_time[run_queue[front%20]]==0)){
	                count  = 0;
			if(child_execution_time[run_queue[front%20]] != 0)
				 run_queue[(rear++)%20] = run_queue[front%20];
			if(child_execution_time[run_queue[front%20]] == 0 ){
				child_execution_time[run_queue[front%20]] = child_execution_ctime[run_queue[front%20]];
				run_queue[(rear++)%20] = run_queue[front%20];
				flag = 1;
				//clean_memory(dir_table[run_queue[front%20]]);
			}
			front ++;
		}
	}
}

int find_victim(TABLE* tp)
{
	int lru= 0;
	int curr = 0;
	for( int l =0 ; l < INDEXNUM ; l++)
	{		
		if(tp[curr].valid==0|tp[curr].disk == 1)
			continue;
		if(tp[curr].counter < tp[lru].counter)
			lru= curr;
		curr++;
	}
	return lru;

}
int swapping (TABLE* tp,int pid_index){
	int vict_pn;
	vict_pn = find_victim(tp);
	printf("vict_pm is %d",vict_pn);
	int fpl = tp[vict_pn].pfn;
	tp[vict_pn].disk = 1;
	printf("vict_pm %d goes to disk ",vict_pn);
	fprintf(fptr,"pid_index: %d , page_num : %d\n",pid_index,vict_pn);  
	return fpl;
}

int main(int argc, char *argv[])
{
        //pid_t pid;
	unsigned int virt_mem[10];
        unsigned int offset[10];
	unsigned int pageTIndex[10];
        unsigned int pageIndex[10];
	int pid_index;
	fptr = fopen("disk","w");
	for(int l=0; l<CHILDNUM;l++)
		child_execution_ctime[l]=child_execution_time[l]; 
	
	for(int l=0 ; l < FRAMENUM; l++){
		fpl[l] = l ;
		fpl_rear++ ;
	}
	msgq = msgget( key, IPC_CREAT | 0666);

	while(i< CHILDNUM) {
		initialize_first_table();
		pid[i] = fork();
		run_queue[(rear++)%20] = i ;
		if (pid[i]== -1) {
			perror("fork error");
			return 0;
		}
		else if (pid[i]== 0) {
			//child
			struct sigaction old_sa;
			struct sigaction new_sa;
			memset(&new_sa, 0, sizeof(new_sa));
			new_sa.sa_handler = &child_signal_handler;
			sigaction(SIGINT, &new_sa, &old_sa);
			while(1);
			return 0;
		}
		else {
			//parent
			//printf("my pid is %d\n", getpid());
			// iterative signal , timer --> alarm
			struct sigaction old_sa;
			struct sigaction new_sa;
			memset(&new_sa, 0, sizeof(new_sa));

			new_sa.sa_handler = &parent_signal_handler;
			sigaction(SIGALRM, &new_sa, &old_sa);

			struct itimerval new_itimer, old_itimer;
			new_itimer.it_interval.tv_sec = 1;
			new_itimer.it_interval.tv_usec = 0;
			new_itimer.it_value.tv_sec = 1;
			new_itimer.it_value.tv_usec = 0;
			setitimer(ITIMER_REAL, &new_itimer, &old_itimer);
		}
		i++;
	}
	while(1){
		ret = msgrcv(msgq,&msg,sizeof(msg),IPC_NOWAIT,IPC_NOWAIT); //to receive message
		if(ret != -1){
			printf("get message\n");
			pid_index = msg.pid_index;
			for(int k=0 ; k < 10 ; k ++ ){
	
				
				virt_mem[k]=msg.virt_mem[k]; 
				offset[k] = virt_mem[k] & 0xfff;
				pageTIndex[k]=(virt_mem[k] & 0xFFC00000)>>22;
				pageIndex[k] = (virt_mem[k] & 0x3FF000)>>12;

	//			      printf("message virtual memory: 0x%08x\n",msg.virt_mem[k]);
	//			    printf("Offset: 0x%04x\n", offset[k]);
	//			      printf("Page Index: %d\n", pageIndex[k]);
	//			      printf("paget index : %d\n",pageTIndex[k]);

				//When page fault happened in first level 
				if( dir_table[pid_index][pageTIndex[k]].valid == 0 )
				{	
					printf("pagefault in first page \n");
					TABLE* table = (TABLE*) calloc(INDEXNUM, sizeof(TABLE));
					dir_table[pid_index][pageTIndex[k]].pt = table;
					dir_table[pid_index][pageTIndex[k]].valid = 1; 
				}

				TABLE* imm_tp = dir_table[pid_index][pageTIndex[k]].pt;

				if(imm_tp[pageIndex[k]].valid== 0)
				{
					printf("page fault in second page\n");
					if(fpl_front != fpl_rear){
						imm_tp[pageIndex[k]].pfn = fpl[(fpl_front%FRAMENUM)];
						imm_tp[pageIndex[k]].valid = 1;
						fpl_front++;
					}
					else{
						printf("full");
						int sfn = swapping(imm_tp,pid_index); // swapping frame number
						imm_tp[pageIndex[k]].pfn = sfn ;
                                                imm_tp[pageIndex[k]].valid = 1; 
					}
				}
				
				imm_tp[pageIndex[k]].counter ++;
				printf("VM : 0x%08x , PFN : %d, counter:%d\n", virt_mem[k], imm_tp[pageIndex[k]].pfn,imm_tp[pageIndex[k]].counter);
				
			//	printf("message virtual memory: 0x%08x\n",msg.virt_mem[k]);
			//	printf("Offset: 0x%04x\n", offset[k]);
			//	printf("Page Index: %d\n", pageIndex[k]);
			//	printf("paget index : %d\n",pageTIndex[k]);			
			}
			memset(&msg, 0, sizeof(msg));
		}

		
	}
	return 0;

}
