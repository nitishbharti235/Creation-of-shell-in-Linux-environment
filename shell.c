#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>

const static int ARG_MAX_COUNT  =  5000;
const static int HISTORY_MAXITEMS = 101;

// Structure of our command 
struct command 
{		  
	int no_of_args;                 
	char *name;          
	char *argv[5005]; 
	int fds[2]; 
};

struct commands 
{            
	int tot_cmd;           
	struct command *cmds[]; 
};

int exec_commands(struct commands *);
int exec_command(struct commands*, struct command*, int (*)[2]);
int history_handling(struct commands *, struct command *);
void clear_commands(struct commands *);
int clear_history(void);
int save_to_history(char *);
void cleanup(void);
int is_history_command(char *);
int is_blank(char *);
void close_pipes(int (*)[2], int);
void cleanup_and_exit(int);
char *input_reading();
struct commands *parse_multiple_commands(char*);

static char **history;             // history array
static int history_len;            // length
static char *input;

// for keeping track of parent's previous call for cleanup
static struct command *parent_cmd;
static struct commands *parent_cmds;
static char *temp_line;


// is history command?
int is_history_command(char *input)
{
	const char *ptr = "history";

	if (strlen(input) < strlen(ptr))
		return 0;
	int i;

	for (i = 0; i < (int) strlen(ptr); i++) {
		if (input[i] != ptr[i])
			return 0;
	}
	return 1;
}

// is blank command?
int is_blank(char *input)
{
	char *ptr=input;
	int len=strlen(input),i=0;
	while(ptr && i<len){
		if(!isspace(*ptr)){
			return 0;
		}
		i++;
		ptr++;
	}
	
	return 1;
}

// To parse the command
struct command *parse_single_command(char *input)
{
	int token_number = 0;
	char *token;
	struct command *cmd = calloc(sizeof(struct command) +
				     ARG_MAX_COUNT * sizeof(char *), 1);

	if (cmd == NULL) {
		fprintf(stderr, "error: memory alloc error\n");
		exit(EXIT_FAILURE);
	}
	token = strtok(input, " ");

	while (token != NULL && token_number < ARG_MAX_COUNT) {
		cmd->argv[token_number++] = token;
		token = strtok(NULL, " ");
	}
	cmd->name = cmd->argv[0];
	cmd->no_of_args = token_number;
	return cmd;
}
struct commands *parse_multiple_commands(char *input)
{
	int number_of_commands = 0;
	int i = 0;
	char *token;
	char *saveptr;
	char *c = input;
	struct commands *cmds;

	while (*c != '\0') {
		if (*c == '|')
			number_of_commands++;
		c++;
	}

	number_of_commands++;

	cmds = calloc(sizeof(struct commands) +
		      number_of_commands * sizeof(struct command *), 1);

	if (cmds == NULL) {
		fprintf(stderr, "error: memory alloc error\n");
		exit(EXIT_FAILURE);
	}

	token = strtok_r(input, "|", &saveptr);
	while (token != NULL && i < number_of_commands) {
		cmds->cmds[i++] = parse_single_command(token);
		token = strtok_r(NULL, "|", &saveptr);
	}

	cmds->tot_cmd = number_of_commands;
	return cmds;
}

// handling built in commands more specifically {exit,cd,history}
int check_built_in(struct command *cmd)
{
	int len=strlen(cmd->name);
	char ch1[10]="exit",ch2[10]="cd",ch3[10]="history";
	char *ptr=cmd->name;
	if(len==4)
    {
		int i=0;
		while(i<4){
			if(*ptr!=ch1[i]){
				return 0;
			}
			i++;
			ptr++;
		}
		
		return 1;
		
	}
    else if(len==2)
    {
		
		int i=0;
		while(i<2)
        {
			if(*ptr!=ch2[i])
            {
				return 0;
			}
			i++;
			ptr++;
		}
		
		return 1;
		
	}
    else if(len==7)
    {
		int i=0;
		while(i<7)
        {
			if(*ptr!=ch3[i])
            {
				return 0;
			}
			i++;
			ptr++;
		}
		
		return 1;
	}
	
	return 0;
		
}
// handle history
int history_handling(struct commands *cmds, struct command *cmd)
{
	if (cmd->no_of_args == 1) {
		int i;

		for (i = 0; i < history_len ; i++) {
			// write to a file descriptor - output_fd
			printf("%d %s\n", i, history[i]);
		}
		return 1;
	}
	if (cmd->no_of_args > 1) {
		if (strcmp(cmd->argv[1], "-c") == 0) {
			clear_history();
			return 0;
		}
		char *end;
		long left;
		int offset;

		left = strtol(cmd->argv[1], &end, 10);
		if (end == cmd->argv[1]) {
			fprintf(stderr, "error: cannot convert to number\n");
			return 1;
		}

		offset = (int) left;
		if (offset > history_len) {
			fprintf(stderr, "error: offset > number of items\n");
			return 1;
		}
		char *line = strdup(history[offset]);

		if (line == NULL)
			return 1;

		struct commands *new_commands = parse_multiple_commands(line);
		parent_cmd = cmd;
		temp_line = line;
		parent_cmds = cmds;

		exec_commands(new_commands);
		clear_commands(new_commands);
		free(line);

		/* reset */
		parent_cmd = NULL;
		temp_line = NULL;
		parent_cmds = NULL;

		return 0;
	}
	return 0;
}
int handle_built_in(struct commands *cmds, struct command *cmd)
{
	int ret;

	if (strcmp(cmd->name, "exit") == 0)
		return -1;

	if (strcmp(cmd->name, "cd") == 0) {
		ret = chdir(cmd->argv[1]);
		if (ret != 0) {
			fprintf(stderr, "error: unable to change dir\n");
			return 1;
		}
		return 0;
	}
	if (strcmp(cmd->name, "history") == 0)
		return history_handling(cmds, cmd);
	return 0;
}
int clear_history(void)
{
	int i;

	for (i = 0; i < history_len; i++)
		free(history[i]);
	history_len = 0;
	return 0;
}

int save_to_history(char *input)
{
	if (history == NULL) {
		history = calloc(sizeof(char *) * HISTORY_MAXITEMS, 1);
		if (history == NULL) {
			fprintf(stderr, "error: memory alloc error\n");
			return 0;
		}
	}
	char *line;

	line = strdup(input);
	if (line == NULL)
		return 0;

	if (history_len == HISTORY_MAXITEMS) {
		free(history[0]);
		int space_to_move = sizeof(char *) * (HISTORY_MAXITEMS - 1);

		memmove(history, history+1, space_to_move);
		if (history == NULL) {
			fprintf(stderr, "error: memory alloc error\n");
			return 0;
		}

		history_len--;
	}

	history[history_len++] = line;
	return 1;
}
int exec_command(struct commands *cmds, struct command *cmd, int (*pipes)[2])
{
	if (check_built_in(cmd) == 1)
		return handle_built_in(cmds, cmd);

	pid_t chp = fork();

	if (chp== -1) {
		fprintf(stderr, "error: fork error\n");
		return 0;
	}
	if (chp == 0) {

		int in_fd = cmd->fds[0];
		int ou_fd = cmd->fds[1];

		// change input/output file descriptors if they aren't standard
		if (in_fd != -1 && in_fd != STDIN_FILENO)
			dup2(in_fd, STDIN_FILENO);

		if (ou_fd != -1 && ou_fd != STDOUT_FILENO)
			dup2(ou_fd, STDOUT_FILENO);

		if (pipes != NULL) {
			int pipe_count = cmds->tot_cmd - 1;

			close_pipes(pipes, pipe_count);
		}

		execv(cmd->name, cmd->argv);

		fprintf(stderr, "error: %s\n", strerror(errno));

		clear_history();
		free(history);
		free(pipes);
		free(input);
		clear_commands(cmds);

		if (parent_cmd != NULL) {
			free(parent_cmd);
			free(temp_line);
			free(parent_cmds);
		}

		_exit(EXIT_FAILURE);
	}
	return chp;
}
void close_pipes(int (*pipes)[2], int pipe_count)
{
	int i;

	for (i = 0; i < pipe_count; i++) {
		close(pipes[i][0]);
		close(pipes[i][1]);
	}

}
int exec_commands(struct commands *cmds)
{
	int exec_ret;

	if (cmds->tot_cmd == 1) {
		cmds->cmds[0]->fds[STDIN_FILENO] = STDIN_FILENO;
		cmds->cmds[0]->fds[STDOUT_FILENO] = STDOUT_FILENO;
		exec_ret = exec_command(cmds, cmds->cmds[0], NULL);
		wait(NULL);
	}
             else {
		int pipe_count = cmds->tot_cmd - 1;
		int i;

		for (i = 0; i < cmds->tot_cmd; i++) {
			if (check_built_in(cmds->cmds[i])) {
				fprintf(stderr, "error: no builtins in pipe\n");
				return 0;
			}

		}
		int (*pipes)[2] = calloc(pipe_count * sizeof(int[2]), 1);

		if (pipes == NULL) {
			fprintf(stderr, "error: memory alloc error\n");
			return 0;
		}


		cmds->cmds[0]->fds[STDIN_FILENO] = STDIN_FILENO;
		for (i = 1; i < cmds->tot_cmd; i++) {
			pipe(pipes[i-1]);
			cmds->cmds[i-1]->fds[STDOUT_FILENO] = pipes[i-1][1];
			cmds->cmds[i]->fds[STDIN_FILENO] = pipes[i-1][0];
		}
		cmds->cmds[pipe_count]->fds[STDOUT_FILENO] = STDOUT_FILENO;

		for (i = 0; i < cmds->tot_cmd; i++)
			exec_ret = exec_command(cmds, cmds->cmds[i], pipes);

		close_pipes(pipes, pipe_count);
		for (i = 0; i < cmds->tot_cmd; ++i)
			wait(NULL);

		free(pipes);
	}

	return exec_ret;
}

void clear_commands(struct commands *cmds)
{
	int i;

	for (i = 0; i < cmds->tot_cmd; i++)
		free(cmds->cmds[i]);

	free(cmds);
}

void cleanup_and_exit(int status)
{
	clear_history();
	free(history);
	exit(status);
}

char* input_reading(void)
{

int buffer_size=201;
char* input=malloc(buffer_size * sizeof(char)); 
int i=0;
char ch;

if(input==NULL)
{
fprintf(stderr,"error:malloc failed\n");
cleanup_and_exit(EXIT_FAILURE);
}
while((ch=getchar())!='\n')
{

if(ch==EOF)
{
free(input);
return NULL;
}

if(i>=buffer_size)
{
buffer_size=2*buffer_size;
input=realloc(input,buffer_size);
}

input[i++]=ch;
}

input[i]='\0';

return input;
}
int main()
{

while(1)
{
int exec_ret=0;
fputs("Shell>>",stdout);

input=input_reading();

if(input==NULL)
{
cleanup_and_exit(EXIT_SUCCESS);
}

if(strlen(input)>0 && !is_blank(input) && input[0]!='|')
{

char* linecopy=strdup(input);

struct commands *commands=parse_multiple_commands(input);

if(commands->tot_cmd > 1 || !is_history_command(input))
{
save_to_history(linecopy);
}

free(linecopy);
exec_ret= exec_commands(commands);

clear_commands(commands);
}

free(input);

if(exec_ret==-1)
    break;
}
cleanup_and_exit(EXIT_SUCCESS);
return 0;
}