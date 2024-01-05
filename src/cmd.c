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
#define ERR			2


char *get_word_(word_t *word)
{
	word_t *current = word;
	char *varValue = "";

	while (current != NULL) {
		if (current->expand) {
			// Expand the environment variable
			char *varValue = getenv(current->string);

			if (varValue != NULL)
				current->string = varValue;
			else
				current->string = "";
		}

		char *newPart = (char *)current->string;

		// Calculate the total length for the new string
		size_t varValueLength = strlen(varValue);
		size_t newPartLength = strlen(newPart);
		size_t totalLength = varValueLength + newPartLength + 1;

		// Allocate memory for the new string
		char *temp = malloc(totalLength);

		// Check if memory allocation is successful
		if (temp != NULL) {
			// Copy varValue into temp
			memcpy(temp, varValue, varValueLength);

			// Copy newPart into temp after varValue
			memcpy(temp + varValueLength, newPart, newPartLength + 1);

			// Update varValue to point to the newly created string
			varValue = temp;

			// Free the memory allocated for temp
			free(temp);
		}

		// Move to the next part
		current = current->next_part;
	}

	return varValue;
}

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
		args[i] = get_word_(current);
		current = current->next_word;
	}

	// Set the last element to NULL
	args[count] = NULL;

	return args;
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

	if (strcmp(get_word(s->verb), "cd") == 0) {
		if (s->out != NULL) {
			int fileDescriptor;

			if (s->io_flags & IO_OUT_APPEND) {
				// Append to the file
				fileDescriptor = open(get_word_(s->out), O_WRONLY | O_CREAT | O_APPEND, 0644);
				if ((fileDescriptor) == -1) {
					close(fileDescriptor);
					return -1;
				}
			} else {
				// Truncate the file
				fileDescriptor = open(get_word_(s->out), O_WRONLY | O_CREAT | O_TRUNC, 0644);
				if ((fileDescriptor) == -1) {
					close(fileDescriptor);
					return -1;
				}
			}
		}
		// Change to the specified directory
		if (s->params != NULL && chdir(get_word_(s->params)) == -1) {
			perror("chdir");
			return -1;
		}
		return 0;
	}

	if (s->verb->next_part != NULL) {
		char *varName = (char *)s->verb->string;
		word_t *current = s->verb->next_part->next_part;
		char *varValue = get_word_(current);

		if (setenv(varName, varValue, 1) == -1) {
			perror("setenv");

			return -1;
		}
		return 0;
	}

	pid = fork();
	if (pid == 0) {
		// Child process
		int fileDescriptor = -1;
		char *inFile = get_word(s->in);

		if (inFile != NULL) {
			fileDescriptor = open(inFile, O_RDONLY);

			if ((fileDescriptor) == -1 || dup2((fileDescriptor), READ) == -1) {
				close(fileDescriptor);
				free(inFile);
				exit(-1);
			}
		}

		char *outFile = get_word(s->out);
		char *errFile = get_word(s->err);

		if (errFile != NULL && outFile != NULL && strcmp(errFile, outFile) == 0) {
			if (s->io_flags == IO_REGULAR) {
				// Truncate the file
				fileDescriptor = open(errFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);

				if ((fileDescriptor) == -1 || dup2((fileDescriptor), WRITE) == -1 || dup2((fileDescriptor), ERR) == -1) {
					close(fileDescriptor);
					free(inFile);
					free(outFile);
					free(errFile);
					exit(-1);
				}
			} else if (s->io_flags == IO_OUT_APPEND) {
				// Append to the file
				fileDescriptor = open(errFile, O_WRONLY | O_CREAT | O_APPEND, 0644);

				if ((fileDescriptor) == -1 || dup2((fileDescriptor), WRITE) == -1 || dup2((fileDescriptor), ERR) == -1) {
					close(fileDescriptor);
					free(inFile);
					free(outFile);
					free(errFile);
					exit(-1);
				}
			}
		} else {
			if (outFile != NULL) {
				if (s->io_flags == IO_OUT_APPEND) {
					// Append to the file
					fileDescriptor = open(outFile, O_WRONLY | O_CREAT | O_APPEND, 0644);
					if ((fileDescriptor) == -1 || dup2((fileDescriptor), WRITE) == -1) {
						close(fileDescriptor);
						free(inFile);
						free(outFile);
						free(errFile);
						exit(-1);
					}
				} else {
					// Truncate the file
					fileDescriptor = open(outFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
					if ((fileDescriptor) == -1 || dup2((fileDescriptor), WRITE) == -1) {
						close(fileDescriptor);
						free(inFile);
						free(outFile);
						free(errFile);
						exit(-1);
					}
				}
			}

			if (errFile != NULL) {
				if (s->io_flags == IO_ERR_APPEND) {
					// Append to the file
					fileDescriptor = open(errFile, O_WRONLY | O_CREAT | O_APPEND, 0644);
					if ((fileDescriptor) == -1 || dup2((fileDescriptor), ERR) == -1) {
						close(fileDescriptor);
						free(inFile);
						free(outFile);
						free(errFile);
						exit(-1);
					}
				} else {
					// Truncate the file
					fileDescriptor = open(errFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
					if ((fileDescriptor) == -1 || dup2((fileDescriptor), ERR) == -1) {
						close(fileDescriptor);
						free(inFile);
						free(outFile);
						free(errFile);
						exit(-1);
					}
				}
			}
		}

		free(inFile);
		free(outFile);
		free(errFile);

		char **args = convertToList(s->params);

		args[0] = get_word_(s->verb);

		if (strcmp(get_word_(s->verb), "pwd") == 0) {
			// Print the current directory
			if (getcwd(cwd, sizeof(cwd)) != NULL) {
				printf("%s\n", cwd);
			} else {
				perror("getcwd");
				exit(EXIT_FAILURE);
			}
			exit(EXIT_SUCCESS); // Exit the child process after printing directory
		}

		execvp(get_word_(s->verb), args);
		if (fileDescriptor != -1)
			close(fileDescriptor);
		fprintf(stderr, "%s '%s'\n", "Execution failed for", get_word_(s->verb));
		free(args);
		exit(EXIT_FAILURE);
	} else if (pid < 0) {
		// Fork failed
		perror("Fork failed\n");
		return -1;
	}
	// Parent process. Wait for child to terminate.
	waitpid(pid, &status, 0);
	if (WIFEXITED(status))
		return WEXITSTATUS(status);

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
		int retValue = parse_command(cmd1, level, father);

		exit(retValue);
	} else if (pid1 < 0) {
		// Fork failed
		exit(EXIT_FAILURE);
	}

	pid2 = fork();

	if (pid2 == 0) {
		// Child process
		int retValue = parse_command(cmd2, level, father);

		exit(retValue);
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
	int pipefd[2];
	pid_t pid1, pid2;
	int status1 = 0, status2 = 0;

	// Create the pipe
	if (pipe(pipefd) == -1) {
		perror("pipe");
		exit(EXIT_FAILURE);
	}

	pid1 = fork();

	if (pid1 == 0) {
		// Child process
		// Close the read end of the pipe
		close(pipefd[0]);

		if (dup2(pipefd[1], WRITE) == -1) {
			close(pipefd[1]);
			exit(-1);
		}

		// Afterwards we just run the first command, after it finished running we also close
		// the second end of the pipe and return the exit value
		int retValue = parse_command(cmd1, level, father);

		close(pipefd[1]);
		exit(retValue);
	} else if (pid1 < 0) {
		// Fork failed
		exit(EXIT_FAILURE);
	}

	pid2 = fork();

	if (pid2 < 0) {
		// Fork failed
		exit(EXIT_FAILURE);
	} else if (pid2 > 0) {
		close(pipefd[0]);
		close(pipefd[1]);

		waitpid(pid1, &status1, 0);
		waitpid(pid2, &status2, 0);

		if (WIFEXITED(status2))
			return WEXITSTATUS(status2);

		return 1;
	}

	close(pipefd[1]);

	if (dup2(pipefd[0], READ) == -1) {
		close(pipefd[0]);
		exit(-1);
	}

	int retValue = parse_command(cmd2, level, father);

	close(pipefd[0]);
	exit(retValue);

	return true;
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father)
{
	int retValue = 0;
	// Perform sanity checks before executing the command
	if (c == NULL || (c->op == OP_NONE && c->scmd == NULL))
		// Handle NULL simple command
		return SHELL_EXIT;

	switch (c->op) {
	case OP_NONE:
		// Execute a simple command.
		if (!strcmp(c->scmd->verb->string, "exit") || !strcmp(c->scmd->verb->string, "quit"))
			return SHELL_EXIT;
		retValue = parse_simple(c->scmd, level, c);
		break;
	case OP_SEQUENTIAL:
		// Execute the commands one after the other.
		retValue = parse_command(c->cmd1, level, c);
		retValue |= parse_command(c->cmd2, level, c);
		break;
	case OP_PARALLEL:
		// Execute the commands simultaneously.
		retValue = run_in_parallel(c->cmd1, c->cmd2, level, c);
		break;
	case OP_CONDITIONAL_NZERO:
		// Execute the second command only if the first one returns non zero.
		retValue = parse_command(c->cmd1, level, c);
		if (retValue != 0)
			retValue = parse_command(c->cmd2, level, c);
		break;
	case OP_CONDITIONAL_ZERO:
		// Execute the second command only if the first one returns zero.
		retValue = parse_command(c->cmd1, level, c);
		if (retValue == 0)
			retValue = parse_command(c->cmd2, level, c);
		break;
	case OP_PIPE:
		// Redirect the output of the first command to the input of the second.
		retValue = run_on_pipe(c->cmd1, c->cmd2, level, c);
		break;
	default:
		return SHELL_EXIT;
	}

	return retValue;
}
