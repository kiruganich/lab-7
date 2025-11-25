#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>


#ifdef _WIN32
#include <windows.h>
#endif

typedef enum {
    MAIN,                  // Основной код
    SLASH,                 // Символ /
    LINE_COMMENT,          // Однострочный комментарий (//)
    STRING_LITERAL,        // Строковый литерал
    CHAR_LITERAL,          // Символьный литерал
    MULTILINE_COMMENT,     // Многострочный комментарий
    STAR                   // Символ * внутри многострочного комментария
} ContextState;

typedef enum {
    NOT_IN_WORD,           // Не в слове
    IN_WORD_CYRILLIC,      // В кириллическом слове
    IN_WORD_NOT_CYRILLIC   // В не-кириллическом слове
} WordState;

typedef struct {
    ContextState context_state;
    WordState word_state;
    int cyrillic_word_count;
    FILE *input_file;
} AutomatonState;

int read_char(FILE *file) {
    int c = fgetc(file);
    if (c == EOF) return EOF;
    
    // Однобайтовый ASCII символ
    if ((c & 0x80) == 0) {
        return c;
    }
    
    // Двухбайтовый символ
    if ((c & 0xE0) == 0xC0) {
        int c2 = fgetc(file);
        if (c2 == EOF) return EOF;
        return ((c & 0x1F) << 6) | (c2 & 0x3F);
    }
    
    // Трехбайтовый символ
    if ((c & 0xF0) == 0xE0) {
        int c2 = fgetc(file);
        if (c2 == EOF) return EOF;
        int c3 = fgetc(file);
        if (c3 == EOF) {
            ungetc(c2, file);
            return EOF;
        }
        return ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
    }
    
    // Четырехбайтовый символ
    if ((c & 0xF8) == 0xF0) {
        int c2 = fgetc(file);
        if (c2 == EOF) return EOF;
        int c3 = fgetc(file);
        if (c3 == EOF) {
            ungetc(c2, file);
            return EOF;
        }
        int c4 = fgetc(file);
        if (c4 == EOF) {
            ungetc(c3, file);
            ungetc(c2, file);
            return EOF;
        }
        return ((c & 0x07) << 18) | ((c2 & 0x3F) << 12) | ((c3 & 0x3F) << 6) | (c4 & 0x3F);
    }

    return read_char(file);
}

bool is_cyrillic(int code_point) {

    if (code_point >= 0x0400 && code_point <= 0x04FF) {
        return true;
    }

    if (code_point >= 0x0500 && code_point <= 0x052F) {
        return true;
    }

    if (code_point >= 0x2DE0 && code_point <= 0x2DFF) {
        return true;
    }

    if (code_point >= 0xA640 && code_point <= 0xA69F) {
        return true;
    }
    return false;
}

bool is_word_separator(int code_point) {

    if (isspace(code_point)) {
        return true;
    }
    
    if (code_point == '.' || code_point == ',' || code_point == ';' || code_point == ':' ||
        code_point == '!' || code_point == '?' || code_point == '(' || code_point == ')' ||
        code_point == '[' || code_point == ']' || code_point == '{' || code_point == '}' ||
        code_point == '"' || code_point == '<' || code_point == '>' || 
        code_point == '=' || code_point == '+' || code_point == '*' ||
        code_point == '/' || code_point == '&' || code_point == '|' || code_point == '^' ||
        code_point == '%' || code_point == '$' || code_point == '#' || code_point == '@' ||
        code_point == '~' || code_point == '`') {
        return true;
    }
    
    return false;
}

void process_char(int c, AutomatonState *state) {
    switch (state->context_state) {
        case MAIN:
            if (c == '"') {
                state->context_state = STRING_LITERAL;
            } else if (c == '\'') {
                state->context_state = CHAR_LITERAL;
            } else if (c == '/') {
                state->context_state = SLASH;
            }
            break;
            
        case SLASH:
            if (c == '/') {
                state->context_state = LINE_COMMENT;
                state->word_state = NOT_IN_WORD;
            } else if (c == '*') {
                state->context_state = MULTILINE_COMMENT;
            } else {

                state->context_state = MAIN;

                process_char(c, state);
            }
            break;
            
        case LINE_COMMENT:
            if (c == '\n') {
                if (state->word_state == IN_WORD_CYRILLIC) {
                    state->cyrillic_word_count++;
                }
                state->context_state = MAIN;
                state->word_state = NOT_IN_WORD;
                break;
            }
            
            switch (state->word_state) {
                case NOT_IN_WORD:
                    if (!is_word_separator(c)) {
                        if (is_cyrillic(c)) {
                            state->word_state = IN_WORD_CYRILLIC;
                        } else {
                            state->word_state = IN_WORD_NOT_CYRILLIC;
                        }
                    }
                    break;
                    
                case IN_WORD_CYRILLIC:
                    if (is_word_separator(c)) {
                        state->cyrillic_word_count++;
                        state->word_state = NOT_IN_WORD;
                    } else if (!is_cyrillic(c) && c != '-' && c != '\'') {
                        state->word_state = IN_WORD_NOT_CYRILLIC;
                    }
                    break;
                    
                case IN_WORD_NOT_CYRILLIC:
                    if (is_word_separator(c)) {
                        state->word_state = NOT_IN_WORD;
                    }
                    break;
            }
            break;
            
        case STRING_LITERAL:
            if (c == '"') {
                state->context_state = MAIN;
            } else if (c == '\\') {
                int next_c = read_char(state->input_file);
                if (next_c == EOF) {
                    state->context_state = MAIN;
                }
            }
            break;
            
        case CHAR_LITERAL:
            if (c == '\'') {
                state->context_state = MAIN;
            } else if (c == '\\') {
                int next_c = read_char(state->input_file);
                if (next_c == EOF) {
                    state->context_state = MAIN;
                }
            }
            break;
            
        case MULTILINE_COMMENT:
            if (c == '*') {
                state->context_state = STAR;
            }
            break;
            
        case STAR:
            if (c == '/') {
                state->context_state = MAIN;
            } else if (c != '*') {
                state->context_state = MULTILINE_COMMENT;
            }
            break;
    }
}


void process_eof(AutomatonState *state) {
    if (state->context_state == LINE_COMMENT) {
        if (state->word_state == IN_WORD_CYRILLIC) {
            state->cyrillic_word_count++;
        }
    }
}

int main(int argc, char *argv[]) {
    
    FILE *file = fopen(argv[1], "rb");
    if (!file) {
        perror("Ошибка открытия файла");
        return 1;
    }
    
    AutomatonState state = {
        .context_state = MAIN,
        .word_state = NOT_IN_WORD,
        .cyrillic_word_count = 0,
        .input_file = file
    };
    
    int c;
    while ((c = read_char(file)) != EOF) {
        process_char(c, &state);
    }
    
    process_eof(&state);
    
    fclose(file);
    

    printf("%d", state.cyrillic_word_count);
    
    return 0;
}
