/* Publied under the GNU General Public License v3
   By David Hauweele <david@hauweele.net>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <time.h>

/* TODO:
 * Add /proc/stat
   Color
   Users number (like real uptime)
 */

#define PACKAGE "uptime-ng"
#define VERSION "0.1"

enum max  { STRLEN_MAX = 1024, TOKEN_MAX = 5 };
enum ldtk { AVG1_TK, AVG2_TK, AVG3_TK, PRCS_TK, LAST_TK };
 
static size_t tokenize(char *str, char **token, const char *sep, size_t size)
{
  unsigned int i;
  token[0] = strtok(str,sep);
  if(!token[0])
    return 0;
  for(i = 1 ; i < size ; i++) {
    token[i] = strtok(NULL,sep);
    if(!token[i])
      break;
  }
  return i;
}

static void cmdline(int argc, char *argv[], 
		    const char *progname)
{
  struct option opts[] =
  {
    {"version", no_argument, 0, 'V'},
    {"help", no_argument, 0, 'h' },
    {NULL,0,0,0}
  };
  const char *opts_help[] = {
    "Display version.",
    "Print this message."
  };
 
  struct option *opt;
  const char **hlp;
  int i,c,max,size;
  
  while(1) {
    c = getopt_long(argc,argv,"Vh",opts,NULL);
    if(c == -1)
      break;
    switch(c) {
      case 'V':
	printf(PACKAGE " version " VERSION "\n");
	exit(EXIT_SUCCESS);
	break;
      case 'h':
      default:
	fprintf(stderr,"Usage: %s [Vh]\n", progname);
	max = 0;
	for(opt = opts ; opt->name ; opt++) {
	  size = strlen(opt->name);
	  if(size > max)
	    max = size;
	}
	for(opt = opts, hlp = opts_help ;
	    opt->name ; 
	    opt++, hlp++) {
	  fprintf(stderr,"  -%c, --%s",
		  opt->val, opt->name);
	  size = strlen(opt->name);
	  for(; size < max ; size++)
	    fprintf(stderr," ");
	  fprintf(stderr," %s\n",*hlp);
	}
	exit(EXIT_FAILURE);
	break;
    }
  }
}

static void error()
{
  perror(NULL);
  exit(EXIT_FAILURE);
}

static void dhmstime(char *buf, unsigned long time)
{
  unsigned long day,hour,min,sec;

  day = time / 86400;
  hour = (time % 86400) / 3600;
  min = (time % 3600) / 60;
  sec = (time % 60);
  
  if(day)
    sprintf(buf,"%d days %02d:%02d:%02d",day,hour,min,sec);
  else if(hour)
    sprintf(buf,"%02d:%02d:%02d",hour,min,sec);
  else if(min)
    sprintf(buf,"%02d min %02d sec",min,sec);
  else
    sprintf(buf,"%02d sec",sec);
}

int main(int argc, char *argv[]) 
{
  FILE *fp;
  const char *progname;
  char buf[STRLEN_MAX];
  char output[STRLEN_MAX];
  char *token[TOKEN_MAX];
  
  progname = (const char *)strrchr(argv[0],'/');
  progname = progname ? (progname + 1) : argv[0];
  cmdline(argc,argv,progname);
  
  /* up time and idle time */
  unsigned long uptime, idletime;
  double pct;
  fp = fopen("/proc/uptime","r");
  if(!fp)
    error();
  fgets(buf,sizeof(buf),fp);
  fclose(fp);
  
  char *up_tk = strtok(buf," ");
  char *idle_tk = strtok(NULL," ");

  uptime   = strtoul(up_tk,NULL,10);
  idletime = strtoul(idle_tk,NULL,10);
  
  /* load average, running and total processes */
  unsigned long running,total;
  double avg_1, avg_2, avg_3;
  fp = fopen("/proc/loadavg","r");
  if(!fp)
    error();
  fgets(buf,sizeof(buf),fp);
  fclose(fp);

  char *runn_tk;
  char *totl_tk;
  
  tokenize(buf,token," ",5);

  runn_tk = strtok(token[PRCS_TK],"/");
  totl_tk = strtok(NULL,"/");

  avg_1 = atof(token[AVG1_TK]);
  avg_2 = atof(token[AVG2_TK]);
  avg_3 = atof(token[AVG3_TK]);
  running = strtoul(runn_tk,NULL,10);
  total = strtoul(totl_tk,NULL,10);

  /* format */
  strcpy(output," ");

  time_t raw;
  const struct tm *tm;
  time(&raw);
  tm = localtime(&raw);
  strftime(buf,sizeof(buf),"%H:%M:%S",tm);
  
  strcat(output,buf);
  strcat(output," up ");

  dhmstime(buf,uptime);
  strcat(output,buf);

  if(idletime) {
    strcat(output,", idle ");
    dhmstime(buf,idletime);
    strcat(output,buf);
    pct = (float)idletime/uptime * 100;
    if(pct >= 0.01) {
      strcat(output," ");
      sprintf(buf,"(%0.2f%%)",pct);
    }
    strcat(output,buf);
  }

  strcat(output,", load average: ");
  sprintf(buf,"%0.2f, %0.2f, %0.2f",
	  avg_1,avg_2,avg_3);
  strcat(output,buf);

  strcat(output,", running: ");
  sprintf(buf,"%d",running);
  strcat(output,buf);

  strcat(output,", total: ");
  sprintf(buf,"%d",total);
  strcat(output,buf);
  
  printf("%s\n",output);

  return EXIT_SUCCESS;
}
