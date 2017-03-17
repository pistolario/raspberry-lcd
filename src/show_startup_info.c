#include <stdio.h>  //include standard input output head file
#include <stdlib.h> //For rand
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Hardware related */
#include <wiringPi.h>  //include wiringpi
#include <mcp23017.h>  //include mcp23017 control head file
#include <lcd.h>       //include LCD control head file
#include <softPwm.h>   //include PWM control head file


/**
 * Globals
 */
static int running = 0;
static int sleepTime = 1;
static int counter = 0;
static char *conf_file_name = NULL;
static char *pid_file_name = NULL;
static int pid_fd = -1;
static char *app_name = NULL;
static FILE *log_stream;

/**
 *  Read configuration from config file
 *   */
int read_conf_file(int reload)
{
	FILE *conf_file = NULL;
	int ret = -1;
	if (conf_file_name == NULL) return 0;
	conf_file = fopen(conf_file_name, "r");
	if (conf_file == NULL)
	{
		syslog(LOG_ERR, "Can not open config file: %s, error: %s",
			conf_file_name, strerror(errno));
		return -1;
	}

	ret = fscanf(conf_file, "%d", &sleepTime);

	if (ret > 0)
	{
		if (reload == 1)
		{
			syslog(LOG_INFO, "Reloaded configuration file %s of %s",
				conf_file_name,
				app_name);
		}
		else
	       	{
			syslog(LOG_INFO, "Configuration of %s read from file %s",
				app_name,
				conf_file_name);
		}
	}

	fclose(conf_file);

	return ret;
}

/**
 *  This function tries to test config file
 *   */
int test_conf_file(char *_conf_file_name)
{
	FILE *conf_file = NULL;
	int ret = -1;

	conf_file = fopen(_conf_file_name, "r");

	if (conf_file == NULL)
	{
		fprintf(stderr, "Can't read config file %s\n",
			_conf_file_name);
		return EXIT_FAILURE;
	}

	ret = fscanf(conf_file, "%d", &sleepTime);

	if (ret <= 0)
	{
		fprintf(stderr, "Wrong config file %s\n",
			_conf_file_name);
	}

	fclose(conf_file);

	if (ret > 0)
		return EXIT_SUCCESS;
	else
		return EXIT_FAILURE;
}

void clean_to_stop()
{
	/* Unlock and close lockfile */
	if (pid_fd != -1)
	{
		lockf(pid_fd, F_ULOCK, 0);
		close(pid_fd);
	}
	/* Try to delete lockfile */
	if (pid_file_name != NULL)
	{
		unlink(pid_file_name);
	}
}
/**
 *  * Callback function for handling signals.
 *   * 	sig	identifier of signal
 *    */
void handle_signal(int sig)
{
	if (sig == SIGINT)
	{
		fprintf(log_stream, "Debug: stopping daemon ...\n");
		clean_to_stop();
		running = 0;
		/* Reset signal handling to default behavior */
		signal(SIGINT, SIG_DFL);
	}
	else if (sig == SIGHUP)
	{
		fprintf(log_stream, "Debug: reloading daemon config file ...\n");
			read_conf_file(1);
	}
	else if (sig == SIGCHLD)
	{
		fprintf(log_stream, "Debug: received SIGCHLD signal\n");
	}
}

/**
 *  * \brief This function will daemonize this app
 *   */
static void daemonize()
{
	pid_t pid = 0;
	int fd;

	/* Fork off the parent process */
	pid = fork();

	/* An error occurred */
	if (pid < 0)
	{
		exit(EXIT_FAILURE);
	}

	/* Success: Let the parent terminate */
	if (pid > 0)
	{
		exit(EXIT_SUCCESS);
	}

	/* On success: The child process becomes session leader */
	if (setsid() < 0)
	{
		exit(EXIT_FAILURE);
	}

	/* Ignore signal sent from child to parent process */
	signal(SIGCHLD, SIG_IGN);

	/* Fork off for the second time*/
	pid = fork();

	/* An error occurred */
	if (pid < 0)
	{
		exit(EXIT_FAILURE);
	}

	/* Success: Let the parent terminate */
	if (pid > 0)
	{
		exit(EXIT_SUCCESS);
	}

	/* Set new file permissions */
	umask(0);

	/* Change the working directory to the root directory */
	/* or another appropriated directory */
	chdir("/");

	/* Close all open file descriptors */
	for (fd = sysconf(_SC_OPEN_MAX); fd > 0; fd--)
	{
		close(fd);
	}

	/* Reopen stdin (fd = 0), stdout (fd = 1), stderr (fd = 2) */
	stdin = fopen("/dev/null", "r");
	stdout = fopen("/dev/null", "w+");
	stderr = fopen("/dev/null", "w+");

	/* Try to write PID of daemon to lockfile */
	if (pid_file_name != NULL)
	{
		char str[256];
		pid_fd = open(pid_file_name, O_RDWR|O_CREAT, 0640);
		if (pid_fd < 0)
		{
			/* Can't open lockfile */
			exit(EXIT_FAILURE);
		}
		if (lockf(pid_fd, F_TLOCK, 0) < 0)
		{
			/* Can't lock file */
			exit(EXIT_FAILURE);
		}
		/* Get current PID */
		sprintf(str, "%d\n", getpid());
		/* Write PID to lockfile */
		write(pid_fd, str, strlen(str));
	}
}

/**
 *  * \brief Print help for this application
 *   */
void print_help(void)
{
	printf("\n Usage: %s [OPTIONS]\n\n", app_name);
	printf("  Options:\n");
	printf("   -h --help                 Print this help\n");
	printf("   -c --conf_file filename   Read configuration from the file\n");
	printf("   -t --test_conf filename   Test configuration file\n");
	printf("   -l --log_file  filename   Write logs to the file\n");
	printf("   -d --daemon               Daemonize this application\n");
	printf("   -p --pid_file  filename   PID file used by daemonized app\n");
	printf("\n");
}



int detect_anything()
{
	char *cmdDetect="i2cdetect -y 1 |grep \"20:\" | sed -s \"s/^[^ ]* //\" | sed -s \"s/-//g\"";
	FILE * cmd_p = popen(cmdDetect, "r");
	if(!cmd_p)
	{
		return -2;
	}
	char buffer[1024];
	char *line_p = fgets(buffer, sizeof(buffer), cmd_p);
	pclose(cmd_p);
	// Careful because the line has more than the "20"
	if(strncmp(buffer, "20", 2)==0)
		return 0;
	else
		return -1;
}

void stringStripRight(char *orig)
{
	int i=strlen(orig);
	i=i-1;
	int encontrado=0;
	while(i>=0&&!encontrado)
	{
		if(((int)orig[i])<=32)
		{
			orig[i]='\0';
			i--;
		}
		else
			encontrado=1;
	}
}

int wlan_ready()
{
	char * cmdWlan="iw dev wlan0 link |grep SSID";
	FILE * cmd_p = popen(cmdWlan, "r");
	if(!cmd_p)
	{
		return -1;
	}
	char buffer[1024];
	char *line_p = fgets(buffer, sizeof(buffer), cmd_p);
	pclose(cmd_p);
	stringStripRight(buffer);
	if(strstr(buffer, "SSID"))
		return 1;
	else
		return 0;
}

#define MAX_IPS 10
char **getAllLocalIPs(int ipv6)
{
	struct ifaddrs * ifAddrStruct=NULL;
	struct ifaddrs * ifa=NULL;
	void * tmpAddrPtr=NULL;
	char **ips;

	
	ips=(char**)malloc(MAX_IPS*sizeof(char*));
	int i;
	for(i=0;i<MAX_IPS;i++)
		ips[i]=NULL;
	getifaddrs(&ifAddrStruct);
	i=0;
	for (ifa = ifAddrStruct; ifa != NULL && i<MAX_IPS; ifa = ifa->ifa_next)
       	{
		if (!ifa->ifa_addr)
	       	{
			continue;
		}
		if (ifa->ifa_addr->sa_family == AF_INET && !ipv6)
	       	{ // check it is IP4
		// is a valid IP4 Address
			tmpAddrPtr=&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
			char *addressBuffer=(char *)malloc(INET_ADDRSTRLEN*sizeof(char));
			inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
			//fprintf(ofp("%s IP Address %s\n", ifa->ifa_name, addressBuffer); 
			if(strcmp("127.0.0.1", addressBuffer)!=0)
				ips[i++]=addressBuffer;
		} else if (ifa->ifa_addr->sa_family == AF_INET6 && ipv6)
	       	{ // check it is IP6
		// is a valid IP6 Address
			tmpAddrPtr=&((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
			char *addressBuffer=(char *)malloc(INET6_ADDRSTRLEN*sizeof(char));
			inet_ntop(AF_INET6, tmpAddrPtr, addressBuffer, INET6_ADDRSTRLEN);
			//fprintf(ofp("%s IP Address %s\n", ifa->ifa_name, addressBuffer); 
			ips[i++]=addressBuffer;
		} 
	}
	if (ifAddrStruct!=NULL)
	       	freeifaddrs(ifAddrStruct);
	return ips;
}

int initializeEnvironment()
{
	int i;
	int display;
	wiringPiSetup(); //init wiringPi
	mcp23017Setup (100, 0x20); //init mcp23017 chip I2C address: 0x20,the first pin number: 100                                          
	for(i=0;i<16;i++) 
		pinMode(100+i,OUTPUT); //set pin 100 - 115 as output 
	digitalWrite(101,0);             //set pin 101 low voltage  
	display=lcdInit(2,16,4,100,102,103,104,105,106,0,0,0,0);      //lcd init 2*16,4 bit control,use 100,101,102,103,104 pin as control pin         
	lcdHome(display);  //reset cursor  
	lcdClear(display); //clear screen   
	return display;
}

int main(int argc, char *argv[])
{
	static struct option long_options[] = {
		{"conf_file", required_argument, 0, 'c'},
		{"test_conf", required_argument, 0, 't'},
		{"log_file", required_argument, 0, 'l'},
		{"help", no_argument, 0, 'h'},
		{"daemon", no_argument, 0, 'd'},
		{"pid_file", required_argument, 0, 'p'},
		{NULL, 0, 0, 0}
	};


	int value, option_index = 0, ret;
	char *log_file_name = NULL;
	int start_daemonized = 0;

	/* Application name / location */
	app_name = argv[0];

	/* Try to process all command line arguments */
	while ((value = getopt_long(argc, argv, "c:l:t:p:dh", long_options, &option_index)) != -1)
	{
		switch (value)
		{
			case 'c':
				conf_file_name = strdup(optarg);
				break;
			case 'l':
				log_file_name = strdup(optarg);
				break;
			case 'p':
				pid_file_name = strdup(optarg);
				break;
			case 't':
				return test_conf_file(optarg);
			case 'd':
				start_daemonized = 1;
				break;
			case 'h':
				print_help();
				return EXIT_SUCCESS;
			case '?':
				print_help();
				return EXIT_FAILURE;
			default:
				break;
		}
	}


	/* When daemonizing is requested at command line. */
	if (start_daemonized == 1)
	{
		/* It is also possible to use glibc function deamon()
  		 * at this point, but it is useful to customize your daemon. */
		daemonize();
	}

	/* Open system log and write message to it */
	openlog(argv[0], LOG_PID|LOG_CONS, LOG_DAEMON);
	syslog(LOG_INFO, "Started %s", app_name);

	/* Daemon will handle two signals */
	signal(SIGINT, handle_signal);
	signal(SIGHUP, handle_signal);

	/* Try to open log file to this daemon */
	if (log_file_name != NULL)
	{
		log_stream = fopen(log_file_name, "a+");
		if (log_stream == NULL)
		{
			syslog(LOG_ERR, "Can not open log file: %s, error: %s",
				log_file_name, strerror(errno));
			log_stream = stdout;
		}
	}
	else
	{
		log_stream = stdout;
	}

	/* Read configuration from config file */
	read_conf_file(0);

	/* This global variable can be changed in function handling signal */
	running = 1;

	/*
	 * Business variables
	 */
	int rand_num;	 
	int value_blue;  //the blue backlight brightness
	int value_red;   //the red backlight brightness
	int value_green; //the green backlight brightness
	int display,i,count;                                                 
	/* 
	 * Test hardware available
	 */
	fprintf(log_stream, "Starting work\n");
	if(detect_anything()<0)
	{
		fprintf(log_stream, "Error, no I2C detected in 0x20");
		running = -1;
	}
	else
	{
		fprintf(log_stream, "LCD screen detected\n");
	}

	display=initializeEnvironment();
	fprintf(log_stream, "Environment initialized\n");
	pinMode(0, OUTPUT); //set Raspberry pi pin 0 as output   
	pinMode(2, OUTPUT); //set Raspberry Pi pin 2 as output  
	pinMode(3, OUTPUT); //set Raspberry Pi pin 3 as output
	softPwmCreate (3, 50, 100);  //set soft PWM pin 3 PWM scale (0-100) original 50
	softPwmCreate (2, 50, 100);  //set soft PWM pin 2 PWM scale (0-100) original 50
	softPwmCreate (0, 50, 100);  //set soft PWM pin 0 PWM scale (0-100) original 50
	value_red=50;
	value_blue=50;
	value_green=50;
	softPwmWrite (3,value_red); //soft PWM control red backlight
	softPwmWrite (2,value_green); //soft PWM control green backlight
	softPwmWrite (0,value_blue);  //soft PWM control blue backlight
	/* Never ending loop of server */
	int loop = 10;
	while (running == 1)
	{
		/* Debug print */
		ret = fprintf(log_stream, "Debug: %d\n", counter++);
		if (ret < 0)
		{
			syslog(LOG_ERR, "Can not write to log stream: %s, error: %s",
				(log_stream == stdout) ? "stdout" : log_file_name, strerror(errno));
			break;
		}
		ret = fflush(log_stream);
		if (ret != 0)
		{
			syslog(LOG_ERR, "Can not fflush() log stream: %s, error: %s",
				(log_stream == stdout) ? "stdout" : log_file_name, strerror(errno));
			break;
		}

		/**
		 * Real work
		 */
		char **ips=getAllLocalIPs(0);
		if(ips[0]!=NULL)
		{
			lcdPosition(display,0,0); //set display location (0,0)   
			lcdPuts(display, ips[0]);
			free(ips[0]);
			ips[0]=NULL;
			lcdPosition(display,0,1);        //set display location(0,1)  
			if(ips[1]!=NULL)
			{
				lcdPuts(display,ips[1]);
				free(ips[1]);
				ips[1]=NULL;
			}
			else
			{
				lcdPuts(display,"");
			}
		}
		else
		{
			/* No IPs, no text*/
			lcdPosition(display,0,0);
			lcdPuts(display, "");
			lcdPosition(display,0,1);
			lcdPuts(display, "");
		}
		for(i=0;i<10;i++)
		{
			if(ips[i]!=NULL)
				free(ips[i]);
		}
		free(ips);
		loop--;
		if(loop<=0)
		{
			fprintf(log_stream, "Debug: stop after several loops\n");
			clean_to_stop();
			running=0;
		}

		/**
		 * Wait just in case there are news
		 */
		sleep(sleepTime);
	}

	/* Close log file, when it is used. */
	if (log_stream != stdout)
	{
		fclose(log_stream);
	}

	/* Write system log and close it. */
	syslog(LOG_INFO, "Stopped %s", app_name);
	closelog();

	/* Free allocated memory */
	if (conf_file_name != NULL) free(conf_file_name);
	if (log_file_name != NULL) free(log_file_name);
	if (pid_file_name != NULL) free(pid_file_name);

	if(running<0)
		return EXIT_FAILURE;
	else
		return EXIT_SUCCESS;
}
