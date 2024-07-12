#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAIN_BUFFER_SIZE 100
#define STRING_BUFFER_SIZE 300
#define SMALL_BUFFER_SIZE 10
#define MAX_INPUTS 5

void process_single_input(const char* input) {
    char small_buffer[SMALL_BUFFER_SIZE];
    strncpy(small_buffer, input, SMALL_BUFFER_SIZE * 2);  // Potential overflow
    printf("Small buffer content: %s\n", small_buffer);
}

void process_multiple_inputs(char* inputs[], int count) {
    char combined_buffer[STRING_BUFFER_SIZE];
    combined_buffer[0] = '\0';
    
    for (int i = 0; i < count; i++) {
        strcat(combined_buffer, inputs[i]);  // Potential overflow
    }
    
    printf("Combined inputs: %s\n", combined_buffer);
}

void process_numbers(const char* input) {
    int number_buffer[MAIN_BUFFER_SIZE];
    int input_numbers[MAIN_BUFFER_SIZE * 2];
    int input_count = 0;
    char* token = strtok((char*)input, ",");
    
    while (token != NULL && input_count < MAIN_BUFFER_SIZE * 2) {
        input_numbers[input_count++] = atoi(token);
        token = strtok(NULL, ",");
    }
    
    size_t copy_size = input_count * sizeof(int);
    memcpy(number_buffer, input_numbers, copy_size);  // Potential overflow
    
    printf("Processed numbers: ");
    for (int i = 0; i < MAIN_BUFFER_SIZE; i++) {
        printf("%d ", number_buffer[i]);
    }
    printf("\n");
}

void process_input(const char* input) {
    char string_buffer[STRING_BUFFER_SIZE];
    strcpy(string_buffer, input);  // Potential overflow
    
    printf("Input string: %s\n", string_buffer);
    
    process_numbers(input);
    process_single_input(input);
    
    char* inputs[MAX_INPUTS];
    int input_count = 0;
    char* token = strtok((char*)input, " ");
    
    while (token != NULL && input_count < MAX_INPUTS) {
        inputs[input_count] = token;
        input_count++;
        token = strtok(NULL, " ");
    }
    
    process_multiple_inputs(inputs, input_count);
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: %s <input_string>\n", argv[0]);
        return 1;
    }
    
    process_input(argv[1]);
    return 0;
}
