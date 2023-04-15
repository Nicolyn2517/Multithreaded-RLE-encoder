#include <ctype.h>
#include "singleth.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h> 
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/mman.h>

#define handle_error(msg) \
           do { perror(msg); exit(EXIT_FAILURE); } while (0)


//references:
// https://www.gnu.org/software/libc/manual/html_node/Example-of-Getopt.html
// https://www.zhihu.com/question/47704079
// https://blog.csdn.net/ss49344/article/details/116564441
// https://blog.csdn.net/liangxanhai/article/details/7767430

int main(int argc, char *argv[])
{
  	int num_threads = 0;
  	int jflag = 0;
	char *jvalue = NULL;  
	int c;
	opterr = 0;

	while((c = getopt(argc, argv, "j:")) != -1)
		switch(c)
		{
			case 'j': // j specifies the number of worker threads. 
				jvalue = optarg; //argument of option j
				num_threads = jvalue[0] - '0';
				if(num_threads > 0)
					jflag = 1;
				else if(num_threads < 0)
					handle_error("j");
				break;
			case '?': // not including in options
				if(optopt == 'j') // j but have no argument
					fprintf(stderr, "Option -%c requires an argument.\n", optopt);
				else if(isprint (optopt)) // 如果optopt是可打印的字符
					fprintf(stderr, "Unknown option '-%c'.\n", optopt);
				else
					fprintf(stderr,"Unknown option character '\\x%x'.\n", optopt);
				return 1;
			default:
				abort();
		}

	if(jflag == 0)// no -j option -- sequential
	{
		// concatenate all files and encode them
		sequential(argc,argv);
	}
	else // have -j option -- parallel
	{	
		TP *ptr = NULL; //thread pool pointer
		TASK *task_buffer = NULL;
		create_tp(optind, argc, argv, &ptr,&task_buffer,num_threads);
	  	main_director(optind,&ptr,argc,argv);	
	}
	return 0;
}