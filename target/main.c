#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 100
#define STRING_BUFFER_SIZE 50
#define MAX_FILE_SIZE 1000

void process_input(const char* input) {
    int number_buffer[BUFFER_SIZE];
    char string_buffer[STRING_BUFFER_SIZE];
    int input_numbers[BUFFER_SIZE * 2];
    int input_count = 0;
    
    strcpy(string_buffer, input);
    
    char* token = strtok(string_buffer, ",");
    
    while (token != NULL && input_count < BUFFER_SIZE * 2) {
        input_numbers[input_count++] = atoi(token);
        token = strtok(NULL, ",");
    }
    
    size_t copy_size = input_count * sizeof(int);
    memcpy(number_buffer, input_numbers, copy_size);
    
    printf("Processed input: ");
    for (int i = 0; i < BUFFER_SIZE; i++) {
        printf("%d ", number_buffer[i]);
    }
    printf("\n");
    
    printf("Input string: %s\n", string_buffer);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <input_file>\n", argv[0]);
        return 1;
    }
    
    FILE *file = fopen(argv[1], "r");
    if (file == NULL) {
        printf("Error opening file: %s\n", argv[1]);
        return 1;
    }
    
    char input[MAX_FILE_SIZE];
    size_t bytesRead = fread(input, 1, MAX_FILE_SIZE - 1, file);
    fclose(file);
    
    if (bytesRead == 0) {
        printf("Error reading file or file is empty.\n");
        return 1;
    }
    
    input[bytesRead] = '\0';
    
    process_input(input);
    
    return 0;
}
