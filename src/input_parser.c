#include "my_shell.h"

char** parse_input(char* input)
{
    size_t buffer_size = MAX_INPUT;
    char** tokens = malloc(buffer_size * sizeof(char*));
    size_t position = 0;
    size_t i = 0;

    if(!tokens)
    {
        perror("Malloc");
        exit(1);
    }

    while (input[i])
    {
        // Skip leading whitespace characters ' ' \n \t \r \a
        while (input[i] == ' ' || input[i] == '\n' || input[i] == '\t' || input[i] == '\r' || input[i] == '\a')
        {
            i++;
        }
        if (input[i] == '\0') {
            break;
        }

        // Parse one token, honoring double quotes so arguments with spaces stay together.
        size_t token_cap = 64;
        char* token = malloc(token_cap);
        if (!token) {
            perror("Malloc");
            exit(1);
        }
        size_t token_length = 0;
        int in_quotes = 0;
        char quote_char = 0;

        while (input[i])
        {
            char c = input[i];
            if (c == '"' || c == '\'') {
                if (!in_quotes) {
                    in_quotes = 1;
                    quote_char = c;
                } else if (c == quote_char) {
                    in_quotes = 0;
                    quote_char = 0;
                } else {
                    // A different quote char inside a quoted span is literal.
                    if (token_length + 1 >= token_cap) {
                        token_cap *= 2;
                        char* grown = realloc(token, token_cap);
                        if (!grown) {
                            perror("Realloc");
                            exit(1);
                        }
                        token = grown;
                    }
                    token[token_length++] = c;
                }
                i++;
                continue;
            }
            if (!in_quotes && (c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\a')) {
                break;
            }
            if (token_length + 1 >= token_cap) {
                token_cap *= 2;
                char* grown = realloc(token, token_cap);
                if (!grown) {
                    perror("Realloc");
                    exit(1);
                }
                token = grown;
            }
            token[token_length++] = c;
            i++;
        }

        token[token_length] = '\0';
        tokens[position++] = token;

        if (position + 1 >= buffer_size) {
            buffer_size *= 2;
            char** grown = realloc(tokens, buffer_size * sizeof(char*));
            if (!grown) {
                perror("Realloc");
                exit(1);
            }
            tokens = grown;
        }
    }

    tokens[position] = NULL; // Terminate the array with NULL
    return tokens;
}

// Free allocated tokens
void free_tokens(char** tokens)
{
    if (!tokens)
        return;

    for (size_t i = 0; tokens[i]; i++) {
        free(tokens[i]); // Free each token
    }

    free(tokens); // Free the tokens array
}
