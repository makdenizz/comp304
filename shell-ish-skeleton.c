#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <fcntl.h> //file I/O
#include <sys/stat.h> //mkdir and mkfifo
#include <dirent.h> //opendir ve readdir
#include <signal.h> //kill
const char *sysname = "shellish";

enum return_codes {
  SUCCESS = 0,
  EXIT = 1,
  UNKNOWN = 2,
};

struct command_t {
  char *name;
  bool background;
  bool auto_complete;
  int arg_count;
  char **args;
  char *redirects[3];     // in/out redirection
  struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
  int i = 0;
  printf("Command: <%s>\n", command->name);
  printf("\tIs Background: %s\n", command->background ? "yes" : "no");
  printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
  printf("\tRedirects:\n");
  for (i = 0; i < 3; i++)
    printf("\t\t%d: %s\n", i,
           command->redirects[i] ? command->redirects[i] : "N/A");
  printf("\tArguments (%d):\n", command->arg_count);
  for (i = 0; i < command->arg_count; ++i)
    printf("\t\tArg %d: %s\n", i, command->args[i]);
  if (command->next) {
    printf("\tPiped to:\n");
    print_command(command->next);
  }
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
  if (command->arg_count) {
    for (int i = 0; i < command->arg_count; ++i)
      free(command->args[i]);
    free(command->args);
  }
  for (int i = 0; i < 3; ++i)
    if (command->redirects[i])
      free(command->redirects[i]);
  if (command->next) {
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
int show_prompt() {
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
int parse_command(char *buf, struct command_t *command) {
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
  if (pch == NULL) {
    command->name = (char *)malloc(1);
    command->name[0] = 0;
  } else {
    command->name = (char *)malloc(strlen(pch) + 1);
    strcpy(command->name, pch);
  }

  command->args = (char **)malloc(sizeof(char *));

  int redirect_index;
  int arg_index = 0;
  char temp_buf[1024], *arg;
  while (1) {
    // tokenize input on splitters
    pch = strtok(NULL, splitters);
    if (!pch)
      break;
    arg = temp_buf;
    strcpy(arg, pch);
    len = strlen(arg);

    if (len == 0)
      continue; // empty arg, go for next
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
    if (strcmp(arg, "|") == 0) {
      struct command_t *c =
          (struct command_t *)malloc(sizeof(struct command_t));
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
    if (arg[0] == '>') {
      if (len > 1 && arg[1] == '>') {
        redirect_index = 2;
        arg++;
        len--;
      } else
        redirect_index = 1;
    }
    if (redirect_index != -1) {
      command->redirects[redirect_index] = (char *)malloc(len);
      strcpy(command->redirects[redirect_index], arg + 1);
      continue;
    }

    // normal arguments
    if (len > 2 &&
        ((arg[0] == '"' && arg[len - 1] == '"') ||
         (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
    {
      arg[--len] = 0;
      arg++;
    }
    command->args =
        (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
    command->args[arg_index] = (char *)malloc(len + 1);
    strcpy(command->args[arg_index++], arg);
  }
  command->arg_count = arg_index;

  // increase args size by 2
  command->args = (char **)realloc(command->args,
                                   sizeof(char *) * (command->arg_count += 2));

  // shift everything forward by 1
  for (int i = command->arg_count - 2; i > 0; --i)
    command->args[i] = command->args[i - 1];

  // set args[0] as a copy of name
  command->args[0] = strdup(command->name);
  // set args[arg_count-1] (last) to NULL
  command->args[command->arg_count - 1] = NULL;

  return 0;
}

void prompt_backspace() {
  putchar(8);   // go back 1
  putchar(' '); // write empty over
  putchar(8);   // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
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
  new_termios.c_lflag &=
      ~(ICANON |
        ECHO); // Also disable automatic echo. We manually echo each char.
  // Those new settings will be set to STDIN
  // TCSANOW tells tcsetattr to change attributes immediately.
  tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

  show_prompt();
  buf[0] = 0;
  while (1) {
    c = getchar();
    // printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

    if (c == 9) // handle tab
    {
      buf[index++] = '?'; // autocomplete
      break;
    }

    if (c == 127) // handle backspace
    {
      if (index > 0) {
        prompt_backspace();
        index--;
      }
      continue;
    }

    if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
      continue;
    }

    if (c == 65) // up arrow
    {
      while (index > 0) {
        prompt_backspace();
        index--;
      }

      char tmpbuf[4096];
      printf("%s", oldbuf);
      strcpy(tmpbuf, buf);
      strcpy(buf, oldbuf);
      strcpy(oldbuf, tmpbuf);
      index += strlen(buf);
      continue;
    }

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
  buf[index++] = '\0'; // null terminate string

  strcpy(oldbuf, buf);

  parse_command(buf, command);

  // print_command(command); // DEBUG: uncomment for debugging

  // restore the old settings
  tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  return SUCCESS;
}
//New Function For Part 1:
char* resolve_path(char* cmd) {
	if (strchr(cmd, '/')) {
		if (access(cmd, X_OK) == 0) return strdup(cmd);
		return NULL;
	}
	char* path_env = getenv("PATH");
	if (!path_env) return NULL;

	char* path_copy = strdup(path_env);

	char* dir = strtok(path_copy, ":");
	
	while (dir != NULL) {
	char* full_path = malloc(strlen(dir) + strlen(cmd) +2);
	sprintf(full_path, "%s/%s",dir,cmd);

	if (access(full_path, X_OK) == 0) {
		free(path_copy);
		return full_path;
	}
	free(full_path);
	dir = strtok(NULL, ":");
}
}
// Part 3: Implementing chatroom command as a builtin
void builtin_chatroom(struct command_t *command) {
 if (command->arg_count < 3) {
	fprintf(stderr, "chatroom <roomname> <username>\n");
	return;}
	char *roomname = command->args[1];
	char *username = command->args[2];

	char room_dir[512];
	char my_pipe[1024];

	snprintf(room_dir,sizeof(room_dir),"/tmp/chatroom-%s",roomname);
	snprintf(my_pipe, sizeof(my_pipe), "%s/%s", room_dir, username);

	mkdir(room_dir, 0777);
	mkfifo(my_pipe,0666);
	printf("roomname: %s \n",roomname);

	pid_t receiver_pid = fork();
	if (receiver_pid == 0) {
	int fd = open(my_pipe, O_RDWR);
	if (fd < 0) {
	perror("pipe cannot open");
	exit(1);
	}
	char buffer[4096];
	while (1) {
		int n = read(fd, buffer,sizeof(buffer)-1);
		if (n >0) {
		buffer[n] = '\0';
		printf("\r\033[K%s\n[%s] %s > ", buffer, roomname, username);
		fflush(stdout);
		}
	}
	exit(0);
	} else {
	char msg[2048];
	char full_msg[4096];

	while (1) {
		printf("[%s] %s > ", roomname,username);
		fflush(stdout);
		if (fgets(msg, sizeof(msg), stdin) == NULL) break;
		msg[strcspn(msg, "\n")] = 0;

		if (strcmp(msg, "exit") == 0) break;
		if(strlen(msg) == 0) continue;

		snprintf(full_msg, sizeof(full_msg), "[%s] %s: %s",roomname,username,msg);
	DIR *dir = opendir(room_dir);
	if (dir != NULL) {
		struct dirent  *entry;
		while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 ||
		strcmp(entry->d_name,"..") == 0 ||
		strcmp(entry->d_name,username) == 0) {
		continue;
		}

		pid_t writer_pid = fork();
		if (writer_pid == 0) {
		char target_pipe[1024];
		snprintf(target_pipe,sizeof(target_pipe),"%s/%s",room_dir,entry->d_name);

		int fd = open(target_pipe, O_WRONLY | O_NONBLOCK);
		if (fd >= 0) {
			write(fd,full_msg,strlen(full_msg));
			close(fd);
		}
		exit(0);
		} else{
			waitpid(writer_pid,NULL,0);
		}
		}
		closedir(dir);
	}
	}
	kill(receiver_pid,SIGTERM);
	waitpid(receiver_pid,NULL,0);
	unlink(my_pipe);
	}
	}

// Part 3: Implementing cut command as a builtin
void builtin_cut(struct command_t *command){
	char delim = '\t';
	char *fields_str = NULL;
	
	for (int i = 1; i < command->arg_count; i++){
	if(strcmp(command->args[i], "-d") == 0 && i + 1 < command->arg_count){
	delim = command->args[i+1][0];
	i++; }
	else if (strcmp(command->args[i], "-f") == 0 && i + 1 < command->arg_count) {
	fields_str = command->args[i+1];
	i++;}
	}
	if (!fields_str) {
	fprintf(stderr, "cut: -f mandotary (-f1,2)\n");
	return;}

	int print_field[100] = {0};
	char *f_copy = strdup(fields_str);
	char *token = strtok(f_copy, ",");
	while(token) {
		int f = atoi(token);
		if (f > 0 && f < 100) print_field[f] = 1;
		token = strtok(NULL, ",");
		}
	free(f_copy);

	char line[4096];
	while (fgets(line, sizeof(line), stdin)) {
		line[strcspn(line, "\n")] = 0;

		char *p = line;
		int current_field = 1;
		int first_printed = 1;

		while (*p) {
			char *next = strchr(p, delim);
			if (next) *next = '\0';

			if (print_field[current_field]) {
				if (!first_printed) printf("%c",delim);
				printf("%s",p);
				first_printed = 0;
	}

	if (!next) break;
	p = next + 1;
	current_field++;
	}
	printf("\n");
	}
}

int process_command(struct command_t *command) {
  int r;
  if (strcmp(command->name, "") == 0)
    return SUCCESS;

  if (strcmp(command->name, "exit") == 0)
    return EXIT;

  if (strcmp(command->name, "cd") == 0) {
    if (command->arg_count > 0) {
      r = chdir(command->args[1]);
      if (r == -1)
        printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
      return SUCCESS;
    }
  }

  if (command->next != NULL) {
	int pipefd[2];
	if (pipe(pipefd) < 0) {
	perror("No pipe");
	return SUCCESS;
	}
  pid_t pid1 = fork();
  if (pid1 == 0) {
	dup2(pipefd[1] , STDOUT_FILENO);
	close(pipefd[0]);
	close(pipefd[1]);
	command->next = NULL;
	exit(process_command(command));
	}

 pid_t pid2 = fork();
 if (pid2 == 0) {
	dup2(pipefd[0],STDIN_FILENO);
	close(pipefd[1]);
	close(pipefd[0]);
	exit(process_command(command->next));
}
 close(pipefd[0]);
 close(pipefd[1]);
 waitpid(pid1, NULL, 0);
 waitpid(pid2, NULL, 0);
 return SUCCESS;
}


  pid_t pid = fork();
  if (pid == 0) // child
  {
    /// This shows how to do exec with environ (but is not available on MacOs)
    // extern char** environ; // environment variables
    // execvpe(command->name, command->args, environ); // exec+args+path+environ

    /// This shows how to do exec with auto-path resolve
    // add a NULL argument to the end of args, and the name to the beginning
    // as required by exec

    // PART 2: handle redirections
    if (command->redirects[0] != NULL) {
      int fd_in = open(command-> redirects[0], O_RDONLY);
      if ( fd_in < 0) { perror("input file cannot open"); exit(1);}
      dup2(fd_in,STDIN_FILENO);
      close(fd_in);
    }

    if (command-> redirects[1] != NULL) {
      int fd_out = open(command->redirects[1], O_WRONLY | O_CREAT | O_TRUNC,0666);
      if (fd_out < 0 ) { perror("output file cannot open"); exit(1);}
      dup2(fd_out, STDOUT_FILENO);
      close(fd_out);
    }
    
    if (command -> redirects[2] != NULL) {
      int fd_app = open(command->redirects[2], O_WRONLY | O_CREAT | O_APPEND, 0666);
      if (fd_app < 0 ) { perror("file cannot open"); exit(1);}
      dup2(fd_app, STDOUT_FILENO);
      close(fd_app);
    }

    if (strcmp(command->name, "cut") == 0) {
	builtin_cut(command);
	exit(0);
 }
  if (strcmp(command->name, "chatroom") == 0){
	builtin_chatroom(command);
	exit(0);
	}

    // TODO: do your own exec with path resolving using execv()
    // PART 1: execv with path resolving
    char *full_path = resolve_path(command->name);
    if (full_path != NULL) {
	execv(full_path, command->args);
	free(full_path);
    } else{
	printf("-%s: %s: command not found\n",sysname, command->name);
    }
    exit(127);
  }  
  else if (pid > 0) {
    if (!command->background) {
	waitpid(pid, NULL,0);
    } else {
	printf("[%d] start with background\n", pid);
    }
	return SUCCESS;
  }
  return SUCCESS;
}

int main() {
  while (1) {
    struct command_t *command =
        (struct command_t *)malloc(sizeof(struct command_t));
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
