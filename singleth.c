#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h> 
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#define handle_error(msg) \
           do { perror(msg); exit(EXIT_FAILURE); } while (0)
#define ONE_TASK 4096

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

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

// 75分版本 fail e
void main_consumer(OPT **post,int num_tasks)
{
	char binary;

	if((*post)->rel_now > 0) //如果我要等的来了
	{
		if((*post)->wait_id == 0) //first one
		{
			//最后一位不打印
			write(STDOUT_FILENO, (*post)->encoded_now, (*post)->rel_now-1); 
		}
		else // not first one
		{
			if((*post)->encoded_now[0] == (*post)->encoded_former[(*post)->rel_former-2]) //合并
			{
				(*post)->cflag = 1;
				(*post)->count = (*post)->encoded_former[(*post)->rel_former-1] + (*post)->encoded_now[1];
				//先保留一下合并值，确认下一位不是相同字母，再打印
				if((*post)->rel_now > 2)
				{
					binary = (*post)->count;
					write(STDOUT_FILENO, &binary, 1);
					(*post)->count = 0;
					(*post)->cflag = 0;
					//打印除了前两位和最后一位的所有字符
					write(STDOUT_FILENO, &((*post)->encoded_now[2]), (*post)->rel_now-3);
				}
			}
			else //不合并
			{
				(*post)->cflag = 0;
				//打印上一个的最后一位
				write(STDOUT_FILENO, &((*post)->encoded_former[(*post)->rel_former-1]), 1);
				//打印当前task除最后一位的所有位
				write(STDOUT_FILENO,(*post)->encoded_now,(*post)->rel_now-1);
			}
		}
		(*post)->wait_id++;
	}
	else
		handle_error("post->rel_now");

	if((*post)->wait_id == num_tasks) // all tasks has done
	{
		if((*post)->rel_now == 2)
		{
			//判断是否合并
			if((*post)->encoded_now[0] == (*post)->encoded_former[(*post)->rel_former-2]) //合并
			{
				//打印最后一位合并值
				binary = (*post)->count;
				write(STDOUT_FILENO, &binary, 1);
				(*post)->count = 0;
			}
			else //不合并
			{
				//打印当前task最后一位
				write(STDOUT_FILENO,&((*post)->encoded_now[(*post)->rel_now - 1]),1); 
			}
		}
		else if((*post)->rel_now < 2)
		{
			handle_error("post->rel_now");
		}
		else if((*post)->rel_now > 2)
		{
		//打印当前task最后一位
			write(STDOUT_FILENO,&((*post)->encoded_now[(*post)->rel_now - 1]),1); 
		}
	}	
}

void main_director(int optind,TP **ptr,int argc,char *argv[])
{
	int num_tasks = 0;//所有文件加起来总任务数量
	//每mmap一次，立即切割发布任务
	// producer 开始
	int length = 0;
	for (int i = optind; i < argc; ++i)
	{
		// Open file
		int fd = open(argv[i], O_RDONLY);
		if (fd == -1)
		  handle_error("open");

		// Get file size
		struct stat sb;
		if (fstat(fd, &sb) == -1)
		  handle_error("fstat");
		
		// Map file into memory
		char *addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (addr == MAP_FAILED)
		  handle_error("mmap");
		close(fd);

		length = sb.st_size;
		// printf("length = %d\n",length);
		//切割task
		// int count = 0; // the number of tasks

		for (int i = 0; i < length; i = i + ONE_TASK)
		{
			pthread_mutex_lock(&mutex);

			(*ptr)->task_buffer[num_tasks].offset = i;
			if((*ptr)->task_buffer[num_tasks].offset + ONE_TASK > length)
				(*ptr)->task_buffer[num_tasks].size = length - (*ptr)->task_buffer[num_tasks].offset;
			else
				(*ptr)->task_buffer[num_tasks].size = ONE_TASK;

			(*ptr)->task_buffer[num_tasks].result_length = 0;
			(*ptr)->task_buffer[num_tasks].addr = addr;
			// printf("task %d's offset = %d, size = %d\n",num_tasks ,ptr->task_buffer[num_tasks].offset,ptr->task_buffer[num_tasks].size);
			fflush(stdout);
			num_tasks++;
			(*ptr)->num_tasks++;
			// pthread_cond_signal(&(ptr->taskcoming));  //发布任务
			pthread_cond_broadcast(&((*ptr)->taskcoming));
			// printf("main thread sends taskcoming signal, it's task %d\n",num_tasks -1 );
			
			pthread_mutex_unlock(&mutex);
		}
		// printf("length = %d\n",length); //1048576
	}

	// printf("num_tasks = %d\n",num_tasks); //256
	// producer 结束
	OPT *post = NULL;
	post = (OPT*)malloc(sizeof(OPT));
	post->wait_id = 0;
	post->count = 0;
	post->encoded_now = (char*)malloc(sizeof(char)*5000);
	post->encoded_former = (char*)malloc(sizeof(char)*5000);
	post->rel_former = 0;
	post->rel_now = 0;
	post->cflag = 0;

	// consumer 开始
	while(1)
	{
		pthread_mutex_lock(&mutex); //mutex 保护
		while((*ptr)->task_buffer[post->wait_id].result_length <= 0) //我要等的没来
		{
			pthread_cond_wait(&((*ptr)->complete),&mutex); 
		}
		// printf("main thread is waken by complete\n");

		//第一个
		if(post->wait_id == 0)
		{
			post->cflag = 0;
			//复制当前的task
			post->rel_now = (*ptr)->task_buffer[post->wait_id].result_length;
			memcpy(post->encoded_now, (*ptr)->task_buffer[post->wait_id].encoded,post->rel_now);
		}
		else //第二个及以后
		{
			//复制当前的task
			post->rel_now = (*ptr)->task_buffer[post->wait_id].result_length;
			memcpy(post->encoded_now, (*ptr)->task_buffer[post->wait_id].encoded,post->rel_now);

			if((*ptr)->task_buffer[post->wait_id-1].result_length <= 0)
				handle_error("post");

			//复制前一个task
			post->rel_former = (*ptr)->task_buffer[post->wait_id-1].result_length;
			memcpy(post->encoded_former, (*ptr)->task_buffer[post->wait_id-1].encoded,post->rel_former);

			//检查上一个和上上一个是否有合并
			if(post->cflag > 0)
			{
				char binary;
				binary = post->count;
				post->encoded_former[post->rel_former-1] = binary;
			}
		}

		(*ptr)->encode_complete_task--;
		pthread_mutex_unlock(&mutex);

		main_consumer(&post,num_tasks);
		if(post->wait_id == num_tasks)
			break; //编译任务全结束了
	}
}	






void encodeit(TP **ptr, int task_id)
{
	int count = 1;
	char binary;
	// printf("thread %ld : task %d, offset = %d, size = %d\n",pthread_self(),task_id,ptr->task_buffer[task_id].offset,ptr->task_buffer[task_id].size);
	// printf("task %d: size = %d\n",task_id,(*ptr)->task_buffer[task_id].size);
	for (int i = (*ptr)->task_buffer[task_id].offset; i < (*ptr)->task_buffer[task_id].size + (*ptr)->task_buffer[task_id].offset; ++i)
	{
		if(i < (*ptr)->task_buffer[task_id].size + (*ptr)->task_buffer[task_id].offset - 1)// not last one
		{	
			// printf("task_id = %d, result_length = %d\n",task_id,(*ptr)->task_buffer[task_id].result_length);

			if((*ptr)->task_buffer[task_id].addr[i] == (*ptr)->task_buffer[task_id].addr[i+1]) //same character
				count += 1;
			else //different character
			{
				memcpy((*ptr)->task_buffer[task_id].encoded + (*ptr)->task_buffer[task_id].result_length,&((*ptr)->task_buffer[task_id].addr[i]),1);
				// (*ptr)->task_buffer[task_id].encoded[(*ptr)->task_buffer[task_id].result_length] = (*ptr)->task_buffer[task_id].addr[i];
				(*ptr)->task_buffer[task_id].result_length += 1;
				// write(STDOUT_FILENO,&(*ptr)->encoded[mynid][i],1);
				binary = count;
				memcpy((*ptr)->task_buffer[task_id].encoded + (*ptr)->task_buffer[task_id].result_length,&binary,1);
				(*ptr)->task_buffer[task_id].result_length += 1;
				// write(STDOUT_FILENO,&binary,1);
				count = 1;
			}
		}
		else // last one
		{
			memcpy((*ptr)->task_buffer[task_id].encoded + (*ptr)->task_buffer[task_id].result_length,&((*ptr)->task_buffer[task_id].addr[i]),1);
			// (*ptr)->task_buffer[task_id].encoded[(*ptr)->task_buffer[task_id].result_length] = (*ptr)->task_buffer[task_id].addr[i];
			(*ptr)->task_buffer[task_id].result_length += 1;
			// write(STDOUT_FILENO,&(*ptr)->encoded[mynid][i],1);
			binary = count;
			memcpy((*ptr)->task_buffer[task_id].encoded + (*ptr)->task_buffer[task_id].result_length,&binary,1);
			(*ptr)->task_buffer[task_id].result_length += 1;
			// write(STDOUT_FILENO,&binary,1);
			count = 1;
		}	
	}
	// printf("thread %ld's task %d's result_length = %d\n",pthread_self(),task_id,(*ptr)->task_buffer[task_id].result_length);
}

void *encode_worker(void *arg)
{
	TP *ptr = NULL;
	ptr = (TP *)arg;

	while(1)
	{
		pthread_mutex_lock(&mutex); 
		while(ptr->num_tasks == 0) //任务池里没任务
		{
			// printf("thread %ld is blocked by taskcoming, num_tasks = %d\n",pthread_self(),ptr->num_tasks);
			pthread_cond_wait(&(ptr->taskcoming),&mutex); 
		}

		int task_id = ptr->task_id;

		if(ptr->task_buffer[task_id].size <= 0)
			handle_error("size");

		ptr->task_id++;
		ptr->num_tasks--;
		// printf("task_id = %d, num_tasks = %d\n",task_id,ptr->num_tasks);
		// printf("thread %ld is waken by taskcoming, going to take task %d,the num_tasks = %d\n",pthread_self(),task_id,ptr->num_tasks);
		pthread_mutex_unlock(&mutex);

		//开始encoding
		pthread_mutex_lock(&mutex); 
		encodeit(&ptr,task_id);
		// printf("thread %ld's task %d done\n",pthread_self(),task_id);
		pthread_mutex_unlock(&mutex);

		//encode 完成
		pthread_mutex_lock(&mutex);
		ptr->encode_complete_task++;
		pthread_cond_signal(&(ptr->complete));
		// printf("thread %ld's task %d has submitted, sending the complete signal\n",pthread_self(),task_id);
		pthread_mutex_unlock(&mutex);
	}
}

void create_tp(int optind, int argc, char *argv[], TP **ptr,TASK **task_buffer,int num_threads)
{
	int num = 0;
	for (int i = optind; i < argc; ++i)
	{
		// Open file
		int fd = open(argv[i], O_RDONLY);
		if (fd == -1)
		  handle_error("open");

		// Get file size
		struct stat sb;
		if (fstat(fd, &sb) == -1)
		  handle_error("fstat");
		
		// Map file into memory
		char *addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (addr == MAP_FAILED)
		  handle_error("mmap");
		close(fd);

		if(sb.st_size <= 4096)
			num++;
		else
		{
			if(sb.st_size % 4096 == 0) //是4096的整数
				num = num + sb.st_size/4096;
			else
				num = num + sb.st_size/4096 + 1;
		}
	}

	// malloc thread_pool 
	(*ptr) = (TP *)malloc(sizeof(TP)*100);
	(*ptr)->thread_id = (pthread_t*)malloc(sizeof(pthread_t)*num_threads); //id
	(*ptr)->num_threads = num_threads; //线程数量

	// printf("num = %d\n",num); //num 不会data race
	// malloc task_buffer
	(*task_buffer) = (TASK *)malloc(sizeof(TASK)*num);
	for (int i = 0; i < num; ++i)
	{
		(*task_buffer)[i].encoded = (char*)malloc(sizeof(char)*4500);
		(*task_buffer)[i].addr = NULL;
		(*task_buffer)[i].offset = 0;
		(*task_buffer)[i].size = 0;
		(*task_buffer)[i].result_length = 0;
	}
	(*ptr)->task_buffer = (*task_buffer);

	(*ptr)->num_tasks = 0;
	(*ptr)->task_id = 0;
	(*ptr)->encode_complete_task = 0;

	pthread_cond_init(&((*ptr)->taskcoming),NULL);
	pthread_cond_init(&((*ptr)->complete),NULL);

	// printf("num_threads = %d\n",num_threads);
	for (int i = 0; i < num_threads; i++) 
	{
		// printf("thread %d has created\n", i);
	    int result_code = pthread_create(&((*ptr)->thread_id[i]), NULL, encode_worker, (void *)(*ptr));
	    assert(!result_code);
  	}
}

int combile_files(int optind,int argc,char *argv[],char *concatenate) //concatenate all files, store it in concatenate and return total length
{
	int length = 0;
	for (int i = optind; i < argc; ++i)
	{
		// Open file
		int fd = open(argv[i], O_RDONLY);
		if (fd == -1)
		  handle_error("open");

		// Get file size
		struct stat sb;
		if (fstat(fd, &sb) == -1)
		  handle_error("fstat");
		
		// Map file into memory
		char *addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (addr == MAP_FAILED)
		  handle_error("mmap");
		close(fd);

		memcpy(concatenate + length,addr,sb.st_size);
		length += sb.st_size;
	}
	return length;
}

void sequential(int argc,char *argv[])
{
	int count = 1;
	unsigned char binary;
	char *concatenate = (char*)malloc(sizeof(char)*1000000000);
	memset(concatenate,'\0',1000000000);

	//concatenate all files
	int length = combile_files(1,argc,argv,concatenate);

	for (int i = 0; i < length; ++i)
	{
		if(i < length - 1)// not last one
		{	
			if(concatenate[i] == concatenate[i+1]) //same character
				count += 1;
			else //different character
			{
				write(STDOUT_FILENO,&concatenate[i],1);
				binary = count;
				write(STDOUT_FILENO,&binary,1);
				count = 1;
			}
		}
		else // last one
		{
			write(STDOUT_FILENO,&concatenate[i],1);
			// write(STDOUT_FILENO,&concatenate[i],1);
			binary = count;
			write(STDOUT_FILENO,&binary,1);
			count = 1;
		}	
	}
	free(concatenate);
}