#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h> //termios, TCSANOW, ECHO, ICANON
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#define PATH_MAX 4096

/* THIS PROJECT IS DONE BY THE TEAM ibb & obb, whose members are TOLGAY DÜLGER, 68881 and METİN ARDA ORAL, 69205. */

const char *sysname = "shellfyre";

enum return_codes
{
	SUCCESS = 0,
	EXIT = 1,
	UNKNOWN = 2,
};

struct command_t
{
	char *name;
	bool background;
	bool auto_complete;
	int arg_count;
	char **args;
	char *redirects[3];		// in/out redirection
	struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command)
{
	int i = 0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background ? "yes" : "no");
	printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
	printf("\tRedirects:\n");
	for (i = 0; i < 3; i++)
		printf("\t\t%d: %s\n", i, command->redirects[i] ? command->redirects[i] : "N/A");
	printf("\tArguments (%d):\n", command->arg_count);
	for (i = 0; i < command->arg_count; ++i)
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	if (command->next)
	{
		printf("\tPiped to:\n");
		print_command(command->next);
	}
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command)
{
	if (command->arg_count)
	{
		for (int i = 0; i < command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}
	for (int i = 0; i < 3; ++i)
		if (command->redirects[i])
			free(command->redirects[i]);
	if (command->next)
	{
		free_command(command->next);
		command->next = NULL;
	}
	free(command->name);
	free(command);
	return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt()
{
	char cwd[1024], hostname[1024];
	gethostname(hostname, sizeof(hostname));
	getcwd(cwd, sizeof(cwd));
	printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
	return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command)
{
	const char *splitters = " \t"; // split at whitespace
	int index, len;
	len = strlen(buf);
	while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
	{
		buf++;
		len--;
	}
	while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
		buf[--len] = 0; // trim right whitespace

	if (len > 0 && buf[len - 1] == '?') // auto-complete
		command->auto_complete = true;
	if (len > 0 && buf[len - 1] == '&') // background
		command->background = true;

	char *pch = strtok(buf, splitters);
	command->name = (char *)malloc(strlen(pch) + 1);
	if (pch == NULL)
		command->name[0] = 0;
	else
		strcpy(command->name, pch);

	command->args = (char **)malloc(sizeof(char *));

	int redirect_index;
	int arg_index = 0;
	char temp_buf[1024], *arg;

	while (1)
	{
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch)
			break;
		arg = temp_buf;
		strcpy(arg, pch);
		len = strlen(arg);

		if (len == 0)
			continue;										 // empty arg, go for next
		while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
		{
			arg++;
			len--;
		}
		while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
			arg[--len] = 0; // trim right whitespace
		if (len == 0)
			continue; // empty arg, go for next

		// piping to another command
		if (strcmp(arg, "|") == 0)
		{
			struct command_t *c = malloc(sizeof(struct command_t));
			int l = strlen(pch);
			pch[l] = splitters[0]; // restore strtok termination
			index = 1;
			while (pch[index] == ' ' || pch[index] == '\t')
				index++; // skip whitespaces

			parse_command(pch + index, c);
			pch[l] = 0; // put back strtok termination
			command->next = c;
			continue;
		}

		// background process
		if (strcmp(arg, "&") == 0)
			continue; // handled before

		// handle input redirection
		redirect_index = -1;
		if (arg[0] == '<')
			redirect_index = 0;
		if (arg[0] == '>')
		{
			if (len > 1 && arg[1] == '>')
			{
				redirect_index = 2;
				arg++;
				len--;
			}
			else
				redirect_index = 1;
		}
		if (redirect_index != -1)
		{
			command->redirects[redirect_index] = malloc(len);
			strcpy(command->redirects[redirect_index], arg + 1);
			continue;
		}

		// normal arguments
		if (len > 2 && ((arg[0] == '"' && arg[len - 1] == '"') || (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
		{
			arg[--len] = 0;
			arg++;
		}
		command->args = (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
		command->args[arg_index] = (char *)malloc(len + 1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count = arg_index;
	return 0;
}

void prompt_backspace()
{
	putchar(8);	  // go back 1
	putchar(' '); // write empty over
	putchar(8);	  // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command)
{
	int index = 0;
	char c;
	char buf[4096];
	static char oldbuf[4096];

	// tcgetattr gets the parameters of the current terminal
	// STDIN_FILENO will tell tcgetattr that it should write the settings
	// of stdin to oldt
	static struct termios backup_termios, new_termios;
	tcgetattr(STDIN_FILENO, &backup_termios);
	new_termios = backup_termios;
	// ICANON normally takes care that one line at a time will be processed
	// that means it will return if it sees a "\n" or an EOF or an EOL
	new_termios.c_lflag &= ~(ICANON | ECHO); // Also disable automatic echo. We manually echo each char.
	// Those new settings will be set to STDIN
	// TCSANOW tells tcsetattr to change attributes immediately.
	tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

	// FIXME: backspace is applied before printing chars
	show_prompt();
	int multicode_state = 0;
	buf[0] = 0;

	while (1)
	{
		c = getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		if (c == 9) // handle tab
		{
			buf[index++] = '?'; // autocomplete
			break;
		}

		if (c == 127) // handle backspace
		{
			if (index > 0)
			{
				prompt_backspace();
				index--;
			}
			continue;
		}
		if (c == 27 && multicode_state == 0) // handle multi-code keys
		{
			multicode_state = 1;
			continue;
		}
		if (c == 91 && multicode_state == 1)
		{
			multicode_state = 2;
			continue;
		}
		if (c == 65 && multicode_state == 2) // up arrow
		{
			int i;
			while (index > 0)
			{
				prompt_backspace();
				index--;
			}
			for (i = 0; oldbuf[i]; ++i)
			{
				putchar(oldbuf[i]);
				buf[i] = oldbuf[i];
			}
			index = i;
			continue;
		}
		else
			multicode_state = 0;

		putchar(c); // echo the character
		buf[index++] = c;
		if (index >= sizeof(buf) - 1)
			break;
		if (c == '\n') // enter key
			break;
		if (c == 4) // Ctrl+D
			return EXIT;
	}
	if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
		index--;
	buf[index++] = 0; // null terminate string

	strcpy(oldbuf, buf);

	parse_command(buf, command);

	// print_command(command); // DEBUG: uncomment for debugging

	// restore the old settings
	tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
	return SUCCESS;
}

int process_command(struct command_t *command);

void filesearch(char* dir, char* search,char* rpath) {
	char* newrpath = malloc(sizeof(char)*PATH_MAX);
	strcpy(newrpath,rpath);
	FILE *fp;
	fp = popen("ls", "r");
	char* path = malloc(sizeof(char)*PATH_MAX);
	char* direc = malloc(sizeof(char)*PATH_MAX);
	strcpy(path,dir);
		while (fgets(direc, PATH_MAX, fp) != NULL) {
			direc = strtok(direc,"\n");
			if (strstr(direc,search)) {
			!strlen(newrpath) ? printf("./%s\n",direc) : printf("./%s/%s\n",newrpath,direc);
			}
			strcpy(path,dir);
			strcat(path,"/");
			strcat(path,direc);
			struct stat statbuf;
   			stat(path, &statbuf);
   				if(S_ISDIR(statbuf.st_mode)) {
   					chdir(path);
   					strcat(rpath,direc);
   					filesearch(path,search,rpath);
   				}

		}
	free(path);
	free(direc);	
}

int main()
{
	while (1)
	{
		struct command_t *command = malloc(sizeof(struct command_t));
		memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

		int code;
		code = prompt(command);
		if (code == EXIT)
			break;

		code = process_command(command);
		if (code == EXIT)
			break;

		free_command(command);
	}

	printf("\n");
	return 0;
}

int process_command(struct command_t *command)
{
	int r;
	//to reach path of the history file
	char historyPath[PATH_MAX];
	if(strlen(historyPath) == 0) realpath("history.txt", historyPath); 
        		
	if (strcmp(command->name, "") == 0)
		return SUCCESS;

	if (strcmp(command->name, "exit") == 0)
		return EXIT;

	if (strcmp(command->name, "cd") == 0)
	{
		if (command->arg_count > 0)
		{
			r = chdir(command->args[0]);
			if (r == -1)
				printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
			
			//modify for cdh command, push the directories into the dirs stack
			char cwd[PATH_MAX];
   			if (getcwd(cwd, sizeof(cwd)) != NULL) {
   				strcat(cwd, "\n");
       			FILE *fptr;
				fptr = fopen(historyPath,"a");
				fprintf(fptr,"%s", cwd);
   				fclose(fptr);
   			}
   			
			/*system("/bin/bash -c 'pushd . > /dev/null'"); push, popd and dirs commands does not work*/
			return SUCCESS;
		}
	}
	
	if (strcmp(command->name, "filesearch") == 0) 
	{
		char path[4096];
		char pwd[4096];
		char backto[4096];
		char *rpath = malloc(sizeof(char)*PATH_MAX);
		char *p;
		FILE *fp;
		char* search = command->args[(command->arg_count)-1];
		fp = popen("ls", "r");
	
		if (strcmp(command->args[0],"-r") == 0) {
			FILE *fp2;
			fp2 = popen("pwd","r");
			char *dir = fgets(pwd,PATH_MAX,fp2);
			dir = strtok(dir,"\n");
			strcpy(backto,dir);
			filesearch(dir, search,rpath);
			chdir(backto);
		}
	
		if (strcmp(command->args[0],"-o") == 0) {
			char xdg[PATH_MAX] = "xdg-open ";
			
			while (fgets(path, PATH_MAX, fp) != NULL) {
				if (strstr(path,search)) {
					strcat(xdg, path);
					system(xdg);
				}
			}
		}
		else {
			while (fgets(path, PATH_MAX, fp) != NULL) {
				if (strstr(path,search)) printf("./%s",path);
			}
		}
	
	}
	
	if (strcmp(command->name, "joker") == 0) 
	{
		char **args = malloc(sizeof(char) * 400);
		FILE *joketab;
		joketab = fopen("joketab.txt", "w");
		fprintf(joketab, "%s", "*/15 * * * * XDG_RUNTIME_DIR=/run/user/$(id -u) notify-send \"HEY A JOKE!\" \"$(curl --silent https://icanhazdadjoke.com/ | cat)\"\n");
		fclose(joketab);
		args[0] = "crontab";
		args[1] = "joketab.txt";
		execvp("crontab", args);
	}
	
	if (strcmp(command->name, "take") == 0)
	{
		char* take = command->args[0];
		char* dir = strtok(take, "/");
		while( dir != NULL ) {
      			mkdir(dir, 0755); 
      			chdir(dir);
      			dir = strtok(NULL, "/");
   		}
	}
	
	if (strcmp(command->name, "cdh") == 0)
	{
		int dirCount = 1;
		char **prevDirs = malloc(sizeof(char *) * dirCount);
		char dir[PATH_MAX];
		char letter = 'a';
		char selectedDir;
		int selectedDirIndex;
    		FILE* fp = fopen(historyPath, "r");
    		
		//Get previous directories
    		while (fgets(dir, sizeof(dir), fp)) {
    			dir[strcspn(dir, "\n")] = 0;
        		prevDirs[dirCount - 1] = strdup(dir);
			dirCount += 1;
			prevDirs = (char **)realloc(prevDirs, sizeof(char *) * dirCount);
    		}
   		fclose(fp);
   		
   		if(dirCount == 1) {
    			printf("There are no previous directories to select from.\n");
    			return SUCCESS;
   		}
   		//print previous directories
   		int iterate = dirCount < 10 ? dirCount - 2 : 10;
		for(int i = iterate; 0 <= i; i--) {
			printf("\n%c   %d)\t%s\n", letter, iterate - i, prevDirs[i]);
			letter += 1;		
   		}
		
		//select and change directory
		printf("\nSelect directory by letter or number: ");
		scanf("%c", &selectedDir);
		selectedDirIndex = (int)selectedDir;
		
		//select by letter
		if(selectedDirIndex < 107 && selectedDirIndex > 96) {
			selectedDirIndex -= 97;
		} else if (selectedDirIndex < 58 && selectedDirIndex > 47) {
		//select by number
			selectedDirIndex -= 48;
		}	
		
		selectedDirIndex = iterate - selectedDirIndex;
		chdir(prevDirs[selectedDirIndex]);
		free(prevDirs);
		return SUCCESS;
		
	}
	
	if (strcmp(command->name, "storyteller") == 0) {
		printf("\n---------------------------------------------\nThis is a short, semi-interactive sci-fi story, written by me(arda) and inspired from the movie Oxygen. Enjoy!\n---------------------------------------------\n");
		sleep(3);
		printf("\nSelect an option when you are provided with the notation (N). \n Example: (1) How are you?\n");
		char line[1024];
		strcpy(line,"You woke up in a small, cryogenic unit.");
		printf("%s",line);
		sleep(3);
		strcpy(line, "You see that the oxygen levels are critical, below %30. It looks like thats why you are awake.\n");
		printf("%s",line);
		sleep(3);
		strcpy(line, "You don’t know who you are, you don’t know where you are, you don’t know your name, you don’t remember anything.\n");
		printf("%s",line);
		sleep(3);
		strcpy(line, "You call MILO, the AI of the cryogenic unit, to find out what is going on.\n");
		printf("%s",line);
		sleep(3);
		strcpy(line, "You: MILO!\n");
		printf("%s",line);
		sleep(3);
		strcpy(line, "MILO: MILO is listening.\n");
		printf("%s",line);
		sleep(3);
		strcpy(line, "7. You:\n(1). Who am I? (2). Where am I? (3). Why my oxygen levels are low? (4). Can you disable the locks, so I can leave the chamber?\n");
		printf("%s",line);
		char buf[256];
		while(scanf("%s", buf)) {
			int input = atoi(buf);
			if (input == 1) {
			printf("MILO: You are USER-237.\n");
			}
			else if (input == 2) {
			printf("MILO: You are in a cryogenic unit.\n");
			}
			
			else if (input == 3) {
			printf("MILO: Your cryogenic unit is damaged. Unit’s oxygen generator is not working.\n");
			}
			
			else if (input == 4) {
			printf("MILO: No. You need root access to unlock the unit’s door.\n");
			break;
			}
			
			else {
			printf("Choose a valid option.\n");
			}
		}
		
		strcpy(line, "You: Then give me the access.\n");
		printf("%s",line);
		sleep(3);
		strcpy(line, "MILO: You need the root password.\n");
		printf("%s",line);
		sleep(3);
		strcpy(line, "You: MILO, Search for USER-237 on the internet...\n");
		printf("%s",line);
		sleep(3);
		strcpy(line, "---For the next 15 minutes, you tried to understand what is going on, and tried to get some knowledge. You could not find anything.---\n");
		printf("%s",line);
		sleep(3);
		strcpy(line, "Then, finally, you find a number, related to the MILO project.\n");
		printf("%s",line);
		sleep(3);
		strcpy(line, "Out of nowhere, an alarm starts to rang. Your oxygen levels are below %10\n");
		printf("%s",line);
		sleep(3);
		strcpy(line, "You call the number.\n");
		printf("%s",line);
		sleep(3);
		strcpy(line, "Elizabeth opens the phone. After a long talk, you learn that you are a guinea pig for an experiment.\n");
		printf("%s",line);
		sleep(3);
		strcpy(line, "Even though she strongly insists and advices you to not open the chamber, you beg her for the root password, since you are running out of oxygen, anyway.\n");
		printf("%s",line);
		sleep(3);
		strcpy(line, "She finally gives the root password, just before the line disconnects.\n");
		printf("%s",line);
		sleep(3);
		strcpy(line, "You have %4 percent oxygen left. With the root access, (1) you can now open the chamber, (2) look at the outside, or (3) try to maybe somehow fix the oxygen generator.\n");
		printf("%s",line);
		char buf2[256];
		while(scanf("%s", buf)) {
			int input = atoi(buf);
			if (input == 1) {
			printf("Elizabeth strongly suggested that you should not open the chamber. Are you sure? (1) Yes, (2) No\n");
			while(scanf("%s", buf2)) {
			int input2 = atoi(buf2);
			if (input2 == 1) {
				printf("You opened the chamber. As soon as you open it, the air inside the pot is sucked out. You feel like your lungs are collapsing. Just after, for a brief second, you find yourself in the middle of the space, and die a second after.\n");
				sleep(3);
				printf("THANKS FOR PLAYING!!\n GAME OVER!----PRESS CTRL+C to ABORT");
				sleep(1000);
			}
			else if (input2 == 2) {
			printf("You have %%4 percent oxygen left. With the root access, (1) you can now open the chamber, (2) look at the outside, or (3) try to maybe somehow fix the oxygen generator.\n");
				break;
			}
			else {
			printf("Choose a valid option.\n");
			}
			}
			}
			else if (input == 2) {
			printf("MILO opens the windows, and you see that you are middle of the space, alone. You feel shocked, and go into a seizure. Until you can get yourself back together, oxygen levels are diminished. You start to not be able to breate. Slowly, you feel the sleep overwhelm you.\n");
			sleep(3);
			printf("You fell asleep, to never wake up again...\n");
			sleep(3);
			printf("THANKS FOR PLAYING!!\n");
			printf("GAME OVER!----PRESS CTRL+C to ABORT\n");
			}
			
			else if (input == 3) {
			sleep(2);
			printf("After some digging through MILO's documentations, you find out that you can put yourself back to a deep-sleep state. With only %%1 oxygen left, you successfully started the procedure. Without knowing anything, without knowing where and until when you are going, you go back to a deep sleep, to hopefully wake up alive.\n");
			sleep(5);
			printf("THANKS FOR PLAYING!!\n");
			printf("GAME OVER!----PRESS CTRL+C to ABORT\n");
			}
			
			else {
			printf("Choose a valid option.\n");
			}
		}
		
		
		
		
		
	}

	// TODO: Implement your custom commands here
	pid_t pid = fork();

	if (pid == 0) // child
	{
		// increase args size by 2
		command->args = (char **)realloc(
			command->args, sizeof(char *) * (command->arg_count += 2));

		// shift everything forward by 1
		for (int i = command->arg_count - 2; i > 0; --i)
			command->args[i] = command->args[i - 1];

		// set args[0] as a copy of name
		command->args[0] = strdup(command->name);
		// set args[arg_count-1] (last) to NULL
		command->args[command->arg_count - 1] = NULL;

		/// TODO: do your own exec with path resolving using execv()
		char *paths = getenv("PATH");		
		char *path = malloc(sizeof(char)*PATH_MAX);
		char *tok = strtok(paths, ":");
		while(tok != NULL) {
		strcpy(path,tok);
		strcat(path,"/");
		strcat(path,command->args[0]);
		execv(path, command->args);
		path = (char *) realloc(path, sizeof(char)*PATH_MAX);
		tok = strtok(NULL, ":");
		}
		free(path);
		
		exit(0);
	}
	else
	{
		/// TODO: Wait for child to finish if command is not running in background
		if(!command->background) wait(NULL);
		return SUCCESS;
	}

	printf("-%s: %s: command not found\n", sysname, command->name);
	return UNKNOWN;
}
