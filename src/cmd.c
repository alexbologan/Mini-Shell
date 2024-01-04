// SPDX-License-Identifier: BSD-3-Clause

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cmd.h"
#include "utils.h"

#define READ		0
#define WRITE		1

char **convertToList(word_t *head)
{
	int count = 0;
	word_t *current = head;

	// Count the number of elements in the linked list
	while (current != NULL) {
		count++;
		current = current->next_word;
	}
	count++;
	// Allocate memory for an array of strings
	char **args = (char **)malloc((count + 1) * sizeof(char *));

	// Copy strings to the array
	current = head;
	for (int i = 1; i < count; i++) {
		args[i] = get_word(current->string);
		current = current->next_word;
	}

	// Set the last element to NULL
	args[count] = NULL;

	return args;
}

char *get_word(word_t *word)
{
	word_t *current = word;
	char *varValue = "";
	while (current != NULL) {
		if (current->expand) {
			// Expand the environment variable
			char *varValue = getenv(current->string);

			if (varValue != NULL) {
				current->string = varValue;
			} else {
				current->string = "";
			}
		}

		char *newPart = current->string;
		char *temp = malloc(strlen(varValue) + strlen(newPart) + 1);
		if (temp != NULL) {
			strcpy(temp, varValue);
			strcat(temp, newPart);
			varValue = temp;
		}
		current = current->next_part;
	}

	return varValue;
}

/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father)
{
	pid_t pid;
	int status = 0;
	char cwd[1024];

	if (strcmp(s->verb->string, "cd") == 0) {
		int fileDescriptor;

		if (s->out != NULL) {
			if (s->io_flags & IO_OUT_APPEND)
				// Append to the file
				fileDescriptor = open(get_word(s->out), O_WRONLY | O_CREAT | O_APPEND, 0644);
			else
				// Truncate the file
				fileDescriptor = open(get_word(s->out), O_WRONLY | O_CREAT | O_TRUNC, 0644);
			close(fileDescriptor);
		}
		// Change to the specified directory
		if (s->params != NULL && chdir(s->params->string) == -1) {
			perror("chdir");
			return -1;
		}
		return 0;
	}

	if (s->verb->next_part != NULL) {
		char *varName = s->verb->string;

		word_t *current = s->verb->next_part->next_part;
		
		char *varValue = get_word(current->next_part->next_part);

		varValue[strlen(varValue)] = '\0';
		if (setenv(varName, varValue, 1) == -1) {
			perror("setenv");

			return -1;
		}
		return 0;
    }

	pid = fork();
	if (pid == 0) {
		// Child process
		int fileDescriptor;

		if (s->out != NULL) {
			if (s->io_flags & IO_OUT_APPEND)
				// Append to the file
				fileDescriptor = open(get_word(s->out), O_WRONLY | O_CREAT | O_APPEND, 0644);
			else
				// Truncate the file
				fileDescriptor = open(get_word(s->out), O_WRONLY | O_CREAT | O_TRUNC, 0644);
				// Duplicate the file descriptor to STDOUT_FILENO
				dup2(fileDescriptor, STDOUT_FILENO);
		}

		if (s->in != NULL) {
			fileDescriptor = open(get_word(s->in), O_RDONLY);

			// Duplicate the file descriptor to STDIN_FILENO
			dup2(fileDescriptor, STDIN_FILENO);
		}

		if (s->err != NULL) {
			if (!(s->out != NULL && strcmp(get_word(s->out), get_word(s->err)) == 0)) {
				if (s->io_flags & IO_ERR_APPEND)
					// Append to the file
					fileDescriptor = open(get_word(s->err), O_WRONLY | O_CREAT | O_APPEND, 0644);
				else
					// Truncate the file
					fileDescriptor = open(get_word(s->err), O_WRONLY | O_CREAT | O_TRUNC, 0644);

				// Duplicate the file descriptor to STDERR_FILENO
				dup2(fileDescriptor, STDERR_FILENO);
			} else {
				// Duplicate the file descriptor to STDERR_FILENO
				dup2(STDOUT_FILENO, STDERR_FILENO);
			}
		}

		close(fileDescriptor);

		char **args = convertToList(s->params);

		args[0] = get_word(s->verb);

		if (strcmp(s->verb->string, "pwd") == 0) {
			// Print the current directory
			if (getcwd(cwd, sizeof(cwd)) != NULL) {
				printf("%s\n", cwd);
			} else {
				perror("getcwd");
				exit(EXIT_FAILURE);
			}
			exit(EXIT_SUCCESS); // Exit the child process after printing directory
		}

		if (s->params != NULL && s->params->expand) {
			// Expand the environment variable
			char *varValue = getenv(s->params->string);

			if (varValue != NULL) {
				args[1] = varValue;
				args[2] = NULL;
			} else {
				args[1] = NULL;
			}
		}
		
		execvp(get_word(s->verb), args);
		printf("Execution failed for '%s'\n", get_word(s->verb));
		exit(EXIT_FAILURE);
		free(args);
	} else if (pid < 0) {
		// Fork failed
		status = -1;
	} else {
		// Parent process. Wait for child to terminate.
		if (waitpid(pid, &status, 0) != pid)
			status = -1;
	}

	return status;
}

/**
 * Process two commands in parallel, by creating two children.
 */
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level,
														command_t *father)
{
	pid_t pid1, pid2;

	pid1 = fork();

	if (pid1 == 0) {
		// Child process
		parse_command(cmd1, level, father);
		exit(EXIT_SUCCESS);
	} else if (pid1 < 0) {
		// Fork failed
		exit(EXIT_FAILURE);
	}

	pid2 = fork();

	if (pid2 == 0) {
		// Child process
		parse_command(cmd2, level, father);
		exit(EXIT_SUCCESS);
	} else if (pid2 < 0) {
		// Fork failed
		exit(EXIT_FAILURE);
	}

	// Parent process waits for both children
	waitpid(pid1, NULL, 0);
	waitpid(pid2, NULL, 0);

	return true;
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level,
													command_t *father)
{
	pid_t pid1, pid2;
	int pipefd[2];

	pipe(pipefd);

	pid1 = fork();

	if (pid1 == 0) {
		// Child process
		close(pipefd[READ]);
		dup2(pipefd[WRITE], STDOUT_FILENO);
		close(pipefd[WRITE]);
	
		parse_command(cmd1, level, father);
		exit(EXIT_SUCCESS);
	} else if (pid1 < 0) {
		// Fork failed
		exit(EXIT_FAILURE);
	}

	pid2 = fork();

	if (pid2 == 0) {
		// Child process
		close(pipefd[WRITE]);
		dup2(pipefd[READ], STDIN_FILENO);
		close(pipefd[READ]);

		parse_command(cmd2, level, father);
		exit(EXIT_SUCCESS);
	} else if (pid2 < 0) {
		// Fork failed
		exit(EXIT_FAILURE);
	}

	close(pipefd[READ]);
	close(pipefd[WRITE]);

	// Parent process waits for both children
	waitpid(pid1, NULL, 0);
	waitpid(pid2, NULL, 0);

	return true;
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father)
{
	// Perform sanity checks before executing the command
	if (c == NULL)
		// Handle NULL command
		return SHELL_EXIT;

	if (c->op == OP_NONE && c->scmd == NULL)
		// Handle NULL simple command
		return SHELL_EXIT;

	switch (c->op) {
	case OP_NONE:
		// Execute a simple command.
		if (!strcmp(c->scmd->verb->string, "exit") || !strcmp(c->scmd->verb->string, "quit"))
			return SHELL_EXIT;
		else
			return parse_simple(c->scmd, level, father);
	case OP_SEQUENTIAL:
		// Execute the commands one after the other.
		parse_command(c->cmd1, level, father);
		return parse_command(c->cmd2, level, father);
	case OP_PARALLEL:
		// Execute the commands simultaneously.
		run_in_parallel(c->cmd1, c->cmd2, level, father);
		break;
	case OP_CONDITIONAL_NZERO:
		// Execute the second command only if the first one returns non zero.
		if (parse_command(c->cmd1, level, father) != 0)
			return parse_command(c->cmd2, level, father);
		break;
	case OP_CONDITIONAL_ZERO:
		// Execute the second command only if the first one returns zero.
		if (parse_command(c->cmd1, level, father) == 0)
			return parse_command(c->cmd2, level, father);
		break;
	case OP_PIPE:
		// Redirect the output of the first command to the input of the second.
		run_on_pipe(c->cmd1, c->cmd2, level, father);
		break;
	default:
		return SHELL_EXIT;
	}

	return 0;
}
