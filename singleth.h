#ifndef _SINGLETH_H_
#define _SINGLETH_H_


#include <pthread.h>
#include <sys/types.h> 

typedef struct
{
	int offset;
	int size;
	char *encoded;
	int result_length;
	char *addr;
} TASK;

typedef struct
{
	int wait_id;
	int count;
	char *encoded_former;
	char *encoded_now;
	int rel_former;
	int rel_now;
	int cflag;
} OPT;

typedef struct
{
	pthread_t *thread_id; //id of every thread
	int num_threads; //the number of threads

	TASK *task_buffer;
	int num_tasks; //total num of tasks
	int task_id; //the next num of task id
	int encode_complete_task; //num of tasks encoded by threads

	pthread_cond_t taskcoming; // if current_num = length, the taskcoming is false. if !=, taskcoming is true.
	pthread_cond_t complete; // if a worker has finished his job, complete is true.
} TP; //thread pool -- 包括所有threads

void main_consumer(OPT **post,int num_tasks);
void encodeit(TP *ptr,int task_id);
void sequential(int argc,char *argv[]);
int combile_files(int optind,int argc,char *argv[],char *concatenate);
// void parallel(int optind,char *concatenate,int length,int num_threads);
void *encode_worker(void *arg);
void create_tp(int optind, int argc, char *argv[],TP **ptr,TASK **task_buffer,int num_threads);
void main_director(int optind,TP **ptr,int argc,char *argv[]);



#endif




