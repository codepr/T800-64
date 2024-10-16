#include "bytecode.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTION_START '.'
#define COMMENT_START ';'

#define da_init(da, capacity)                                                  \
    do {                                                                       \
        assert((capacity) > 0);                                                \
        (da)->length   = 0;                                                    \
        (da)->capacity = (capacity);                                           \
        (da)->data     = calloc((capacity), sizeof(*(da)->data));              \
    } while (0)

#define da_extend(da)                                                          \
    do {                                                                       \
        (da)->capacity *= 2;                                                   \
        (da)->data =                                                           \
            realloc((da)->data, (da)->capacity * sizeof(*(da)->data));         \
        if (!(da)->data) {                                                     \
            free((da));                                                        \
            fprintf(stderr, "DA realloc failed");                              \
            exit(EXIT_FAILURE);                                                \
        }                                                                      \
    } while (0)

#define da_push(da, item)                                                      \
    do {                                                                       \
        assert((da));                                                          \
        if ((da)->length + 1 == (da)->capacity)                                \
            da_extend((da));                                                   \
        (da)->data[(da)->length++] = (item);                                   \
    } while (0)

typedef enum { CODE_SEGMENT, DATA_SEGMENT } Segment_Type;

typedef enum { DATA_STRING, DATA_NUMBER } Data_Type;

// Generic quadword segment for storing the code
struct qword_segment {
    qword *data;
    size_t length;
    size_t capacity;
};

// Data segment using halfwords
struct hword_segment {
    hword *data;
    size_t length;
    size_t capacity;
};

static void *segment_extend(Segment_Type type, void *c)
{
    switch (type) {
    case CODE_SEGMENT:
        da_extend((struct qword_segment *)c);
        return c;
        break;
    case DATA_SEGMENT:
        da_extend((struct hword_segment *)c);
        return c;
        break;
    }

    return NULL;
}

static void *segment_create(Segment_Type type)
{
    void *ptr       = NULL;
    size_t capacity = 4;
    if (type == CODE_SEGMENT) {
        struct qword_segment *c = calloc(1, sizeof(*c));
        if (!c)
            return NULL;

        da_init(c, capacity);
        ptr = c;
    } else {
        struct hword_segment *c = calloc(1, sizeof(*c));
        if (!c)
            return NULL;

        da_init(c, capacity);
        ptr = c;
    }
    return ptr;
}

static void segment_free(Segment_Type type, void *c)
{
    if (type == CODE_SEGMENT)
        free(((struct qword_segment *)c)->data);
    else
        free(((struct hword_segment *)c)->data);
    free(c);
}

int bc_push_instr(Byte_Code *bc, qword instr)
{
    if (bc->code_segment->length + 1 == bc->code_segment->capacity)
        bc->code_segment = segment_extend(CODE_SEGMENT, bc->code_segment);

    bc->code_segment->data[bc->code_segment->length++] = instr;

    return 0;
}

static int bc_push_data(Byte_Code *bc, const char *data, size_t data_len,
                        Data_Type dtype)
{
    if (dtype == DATA_STRING) {
        // Skip delimiter
        if (*data == '"')
            data++;

        while (bc->data_segment->length + data_len + 1 >=
               bc->data_segment->capacity)
            bc->data_segment = segment_extend(DATA_SEGMENT, bc->data_segment);

        // TODO use memcpy or strncpy for nul inclusion
        for (size_t i = 0; i < data_len; ++i) {
            bc->data_segment->data[bc->data_segment->length++] = data[i];
        }

        bc->data_segment->data[bc->data_segment->length++] = '\0';
    } else {
        size_t bytes = (size_t)atoll(data);
        // Reserve bytes space in the data segment
        while (bc->data_segment->length + bytes >= bc->data_segment->capacity)
            bc->data_segment = segment_extend(DATA_SEGMENT, bc->data_segment);
        bc->data_segment->length += bytes;

        return bytes;
    }

    return data_len;
}

// TODO: Dispatch the correct encoding based on opcode
qword bc_encode_instruction(qword opcode, qword dst, qword src)
{
    qword quad_word = opcode << 56;
    quad_word |= (dst << 48);
    quad_word |= (src & ADDR_MASK);

    return quad_word;
}

// TODO: Dispatch the correct decoding based on opcode
struct instruction bc_decode_instruction(qword e_instr)
{
    hword op  = (uint64_t)e_instr >> 56;
    qword dst = (e_instr & DEST_MASK) >> 48;
    qword src = e_instr & ADDR_MASK;

    return (struct instruction){.op = op, .src = src, .dst = dst};
}

qword *bc_code(const Byte_Code *const bc)
{
    if (!bc)
        return NULL;
    return bc->code_segment->data;
}

hword *bc_data(const Byte_Code *const bc)
{
    if (!bc)
        return NULL;
    return bc->data_segment->data;
}

qword bc_data_addr(const Byte_Code *const bc)
{
    if (!bc)
        return 0;
    return bc->data_addr;
}

Byte_Code *bc_create(void)
{
    Byte_Code *bc = calloc(1, sizeof(*bc));
    if (!bc)
        return NULL;

    bc->code_segment = segment_create(CODE_SEGMENT);
    if (!bc->code_segment)
        goto code_error;

    bc->data_segment = segment_create(DATA_SEGMENT);
    if (!bc->data_segment)
        goto data_error;

    bc->data_addr = DATA_OFFSET;

    return bc;

data_error:
    free(bc->code_segment);

code_error:
    free(bc);
    return NULL;
}

Byte_Code *bc_from_raw(const qword *bytecode, size_t length)
{
    Byte_Code *bc = bc_create();

    for (size_t i = 0; i < length; ++i)
        bc_push_instr(bc, bytecode[i]);

    return bc;
}

static inline void strip_spaces(char **str)
{
    if (!*str)
        return;
    while ((isspace(**str) && **str) || **str == ',')
        ++(*str);
}

static inline void read_token(char **str, char *dest)
{

    if (!str || !dest)
        return;

    if (**str == '"') {
        do {
            if (**str == '\\' && **(str + 1) == '"')
                *dest++ = *(*str)++;
            *dest++ = *(*str)++;
        } while (**str && **str != '"');
    }

    while (!isspace(**str) && **str && **str != ',')
        *dest++ = *(*str)++;
}

static Instruction_Set str_to_instruction(const char *str)
{
    if (strncasecmp(str, "NOP", 3) == 0)
        return NOP;
    if (strncasecmp(str, "HLT", 3) == 0)
        return HLT;
    if (strncasecmp(str, "MOV", 3) == 0)
        return MOV;
    if (strncasecmp(str, "MOD", 3) == 0)
        return MOD;
    if (strncasecmp(str, "MDI", 3) == 0)
        return MOD;
    if (strncasecmp(str, "CLF", 3) == 0)
        return CLF;
    if (strncasecmp(str, "CMP", 3) == 0)
        return CMP;
    if (strncasecmp(str, "CMI", 3) == 0)
        return CMI;
    if (strncasecmp(str, "LDI", 3) == 0)
        return LDI;
    if (strncasecmp(str, "LDR", 3) == 0)
        return LDR;
    if (strncasecmp(str, "STI", 3) == 0)
        return STI;
    if (strncasecmp(str, "STR", 3) == 0)
        return STR;
    if (strncasecmp(str, "PSR", 3) == 0)
        return PSR;
    if (strncasecmp(str, "PSM", 3) == 0)
        return PSM;
    if (strncasecmp(str, "PSI", 3) == 0)
        return PSI;
    if (strncasecmp(str, "POM", 3) == 0)
        return POM;
    if (strncasecmp(str, "POP", 3) == 0)
        return POP;
    if (strncasecmp(str, "ADD", 3) == 0)
        return ADD;
    if (strncasecmp(str, "ADI", 3) == 0)
        return ADI;
    if (strncasecmp(str, "SUB", 3) == 0)
        return SUB;
    if (strncasecmp(str, "SBI", 3) == 0)
        return SBI;
    if (strncasecmp(str, "MUL", 3) == 0)
        return MUL;
    if (strncasecmp(str, "MLI", 3) == 0)
        return MLI;
    if (strncasecmp(str, "DIV", 3) == 0)
        return DIV;
    if (strncasecmp(str, "DVI", 3) == 0)
        return DVI;
    if (strncasecmp(str, "INC", 3) == 0)
        return INC;
    if (strncasecmp(str, "DEC", 3) == 0)
        return DEC;
    if (strncasecmp(str, "CALL", 4) == 0)
        return CALL;
    if (strncasecmp(str, "SYSCALL", 7) == 0)
        return SYSCALL;
    if (strncasecmp(str, "RET", 3) == 0)
        return RET;
    if (strncasecmp(str, "JMP", 3) == 0)
        return JMP;
    if (strncasecmp(str, "JNE", 3) == 0)
        return JNE;
    if (strncasecmp(str, "JEQ", 3) == 0)
        return JEQ;
    if (strncasecmp(str, "JLT", 3) == 0)
        return JLT;
    if (strncasecmp(str, "JGE", 3) == 0)
        return JGE;
    if (strncasecmp(str, "AND", 3) == 0)
        return AND;
    if (strncasecmp(str, "BOR", 3) == 0)
        return BOR;
    if (strncasecmp(str, "BOR", 3) == 0)
        return BOR;
    if (strncasecmp(str, "XOR", 3) == 0)
        return XOR;
    if (strncasecmp(str, "NOT", 3) == 0)
        return NOT;
    if (strncasecmp(str, "SHL", 3) == 0)
        return SHL;
    if (strncasecmp(str, "SHR", 3) == 0)
        return SHR;
    if (strncasecmp(str, "NOT", 3) == 0)
        return NOT;

    return -1;
}

#define NAME_MAX_LEN 64

struct label {
    char name[NAME_MAX_LEN];
    uint64_t offset;
};

typedef struct {
    struct label names[NAME_MAX_LEN];
    size_t length;
} Labels;

#define is_label(token) ((token)[strlen(token) - 1] == ':')

static inline bool is_number(const char *str)
{
    while (*str) {
        if (isdigit(*str++) == 0)
            return false;
    }
    return true;
}

static int64_t parse_hex(const char *hex)
{
    int64_t val = -1;

    char str[16];
    char *pstr = &str[0];
    while (hex && isxdigit(*hex))
        *pstr++ = *hex++;

    if (*hex != ']') {
        fprintf(stderr, "Malformed memory address\n");
        exit(EXIT_FAILURE);
    }

    char *endptr;
    // To distinguish success/failure after call
    errno = 0;

    val   = strtol(str, &endptr, 16);

    // Check for various possible errors.
    if (errno != 0) {
        perror("strtol");
        exit(EXIT_FAILURE);
    }

    if (endptr == str) {
        fprintf(stderr, "No digits were found\n");
        exit(EXIT_FAILURE);
    }

    return val;
}

static int64_t parse_operand(const char *operand, const Labels *labels)
{
    int64_t op_value = -1;
    if (strncasecmp(operand, "AX", 2) == 0)
        op_value = AX;
    else if (strncasecmp(operand, "BX", 2) == 0)
        op_value = BX;
    else if (strncasecmp(operand, "CX", 2) == 0)
        op_value = CX;
    else if (strncasecmp(operand, "DX", 2) == 0)
        op_value = DX;
    else {
        if (is_number(operand)) {
            // Immediate value
            char *endptr;
            // To distinguish success/failure after call
            errno    = 0;

            op_value = strtol(operand, &endptr, 10);

            // Check for various possible errors.
            if (errno != 0) {
                perror("strtol");
                exit(EXIT_FAILURE);
            }

            if (endptr == operand) {
                fprintf(stderr, "No digits were found\n");
                exit(EXIT_FAILURE);
            }
        } else if (strncasecmp(operand + 1, "0x", 2) == 0) {
            // Immediate value hex format
            op_value = parse_hex(operand + 3);
        } else if (*operand == '[') {
            // Hex value
            if (strncasecmp(operand + 1, "0x", 2) == 0) {
                op_value = parse_hex(operand + 3);
            }
        } else {
            // label
            bool found = false;
            // Label case e.g. a JMP, check for the presence of the
            // label in the labels array
            for (size_t i = 0; i < labels->length && !found; ++i) {
                if (strncmp(operand, labels->names[i].name, strlen(operand)) ==
                    0) {
                    op_value = labels->names[i].offset;
                    found    = true;
                }
            }

            if (!found) {
                fprintf(stderr, "Label not found\n");
                exit(EXIT_FAILURE);
            }
        }
    }

    return op_value;
}

static struct instruction parse_instruction(const char *op, const char *dst,
                                            const char *src,
                                            const Labels *labels)
{
    struct instruction instr;

    int opcode = str_to_instruction(op);
    if (opcode < 0) {
        fprintf(stderr, "unrcognized instruction %s\n", op);
        exit(EXIT_FAILURE);
    }

    instr.op = opcode;

    // Maybe parse dest operand
    if (!dst || *dst == '\0')
        goto exit;

    instr.dst = parse_operand(dst, labels);

    if (!src || *src == '\0')
        goto exit;

    instr.src = parse_operand(src, labels);

exit:
    return instr;
}

typedef enum { OP_NONE, OP_REG, OP_IMM, OP_ADDR } OP_Type;

typedef struct {
    const char *name;
    OP_Type dst, src;
} Instruction_Def;

static const char *reg_to_str[NUM_REGISTERS] = {"AX", "BX", "CX", "DX"};

static const Instruction_Def instr_defs[NUM_INSTRUCTIONS] = {
    {.name = "NOP", .dst = OP_NONE, .src = OP_NONE},
    {.name = "CLF", .dst = OP_NONE, .src = OP_NONE},
    {.name = "CMP", .dst = OP_REG, .src = OP_REG},
    {.name = "CMI", .dst = OP_REG, .src = OP_IMM},
    {.name = "MOV", .dst = OP_REG, .src = OP_REG},
    {.name = "LDI", .dst = OP_REG, .src = OP_IMM},
    {.name = "LDR", .dst = OP_REG, .src = OP_ADDR},
    {.name = "STI", .dst = OP_ADDR, .src = OP_IMM},
    {.name = "STR", .dst = OP_ADDR, .src = OP_REG},
    {.name = "PSR", .dst = OP_REG, .src = OP_NONE},
    {.name = "PSM", .dst = OP_ADDR, .src = OP_NONE},
    {.name = "PSI", .dst = OP_IMM, .src = OP_NONE},
    {.name = "POP", .dst = OP_REG, .src = OP_NONE},
    {.name = "POM", .dst = OP_ADDR, .src = OP_NONE},
    {.name = "ADD", .dst = OP_REG, .src = OP_REG},
    {.name = "ADI", .dst = OP_REG, .src = OP_IMM},
    {.name = "SUB", .dst = OP_REG, .src = OP_REG},
    {.name = "SBI", .dst = OP_REG, .src = OP_IMM},
    {.name = "MUL", .dst = OP_REG, .src = OP_REG},
    {.name = "MLI", .dst = OP_REG, .src = OP_IMM},
    {.name = "DIV", .dst = OP_REG, .src = OP_REG},
    {.name = "DVI", .dst = OP_REG, .src = OP_IMM},
    {.name = "MOD", .dst = OP_REG, .src = OP_REG},
    {.name = "MDI", .dst = OP_REG, .src = OP_IMM},
    {.name = "INC", .dst = OP_REG, .src = OP_NONE},
    {.name = "DEC", .dst = OP_REG, .src = OP_NONE},
    {.name = "AND", .dst = OP_REG, .src = OP_REG},
    {.name = "BOR", .dst = OP_REG, .src = OP_REG},
    {.name = "XOR", .dst = OP_REG, .src = OP_REG},
    {.name = "NOT", .dst = OP_REG, .src = OP_NONE},
    {.name = "SHL", .dst = OP_REG, .src = OP_IMM},
    {.name = "SHR", .dst = OP_REG, .src = OP_IMM},
    {.name = "JMP", .dst = OP_ADDR, .src = OP_NONE},
    {.name = "JEQ", .dst = OP_ADDR, .src = OP_NONE},
    {.name = "JNE", .dst = OP_ADDR, .src = OP_NONE},
    {.name = "JLE", .dst = OP_ADDR, .src = OP_NONE},
    {.name = "JLT", .dst = OP_ADDR, .src = OP_NONE},
    {.name = "JGE", .dst = OP_ADDR, .src = OP_NONE},
    {.name = "JGT", .dst = OP_ADDR, .src = OP_NONE},
    {.name = "CALL", .dst = OP_ADDR, .src = OP_NONE},
    {.name = "RET", .dst = OP_NONE, .src = OP_NONE},
    {.name = "SYSCALL", .dst = OP_NONE, .src = OP_NONE},
    {.name = "HLT", .dst = OP_NONE, .src = OP_NONE}

};

static const char *instruction_to_str(const struct instruction *instr,
                                      char *dst)
{
    if (instr->op < 0 || instr->op >= NUM_INSTRUCTIONS) {
        fprintf(stderr, "unrecognized assembly instruction %d\n", instr->op);
        return NULL;
    }

    Instruction_Def idef = instr_defs[instr->op];
    size_t nbytes        = 0;

    switch (idef.dst) {
    case OP_IMM: {
        nbytes = snprintf(dst, NAME_MAX_LEN, "%s %lli", idef.name, instr->dst);
        break;
    }
    case OP_REG: {
        nbytes = snprintf(dst, NAME_MAX_LEN, "%s %s", idef.name,
                          reg_to_str[instr->dst]);
        break;
    }
    case OP_ADDR: {
        nbytes =
            snprintf(dst, NAME_MAX_LEN, "%s [0x%lli]", idef.name, instr->dst);
        break;
    }
    default:
        nbytes = snprintf(dst, NAME_MAX_LEN, "%s", idef.name);
        break;
    }

    switch (idef.src) {
    case OP_IMM: {
        nbytes = snprintf(dst + nbytes, NAME_MAX_LEN, " %lli", instr->src);
        break;
    }
    case OP_REG: {
        nbytes =
            snprintf(dst + nbytes, NAME_MAX_LEN, " %s", reg_to_str[instr->src]);
        break;
    }
    case OP_ADDR: {
        nbytes = snprintf(dst + nbytes, NAME_MAX_LEN, " [0x%llx]", instr->src);
        break;
    }
    default:
        break;
    }

    return dst;
}

void bc_disassemble(const Byte_Code *const bc)
{

    size_t i    = 0;

    qword *code = bc_code(bc);
    char instr_str[NAME_MAX_LEN];

    printf("\nOffset  Instructions    Hex words\n");
    printf("-------------------------------------------------------\n\n");

    while (i < bc->code_segment->length) {
        memset(instr_str, 0x00, sizeof(instr_str));
        const struct instruction ins = bc_decode_instruction(code[i]);
        printf("0x%04lX\t%-16s%02X %02X %02X %02X %02X %02X %02X %02X  "
               "0x%04lX",
               i, instruction_to_str(&ins, instr_str),
               (unsigned short)(code[i] >> 56 & 0xFF),
               (unsigned short)(code[i] >> 48 & 0xFF),
               (unsigned short)(code[i] >> 40 & 0xFF),
               (unsigned short)(code[i] >> 32 & 0xFF),
               (unsigned short)(code[i] >> 24 & 0xFF),
               (unsigned short)(code[i] >> 16 & 0xFF),
               (unsigned short)(code[i] >> 8 & 0xFF),
               (unsigned short)(code[i] >> 0 & 0xFF),
               (unsigned long)i * sizeof(qword));

        printf("\n");
        i++;
    }
}

static Labels scan_labels(FILE *fp)
{
    if (!fp) {
        fprintf(stderr, "Invalid file handler\n");
        exit(EXIT_FAILURE);
    }

    Labels labels = {0};

    char line[0xFFF], label[NAME_MAX_LEN];
    char *line_ptr;
    size_t line_nr         = 0;
    bool skip_data_section = false;

    while (fgets(line, 0xFFF, fp)) {
        line_ptr = line;

        strip_spaces(&line_ptr);

        memset(label, 0x00, NAME_MAX_LEN);

        if (strncmp(line_ptr, ".data", 5) == 0)
            skip_data_section = true;
        else if (*line_ptr == '.')
            skip_data_section = false;

        if (skip_data_section)
            continue;

        if (*line_ptr == '\0' || *line_ptr == ';' || *line_ptr == '.')
            continue;

        read_token(&line_ptr, label);

        size_t token_length = strlen(label);

        if (label[token_length - 1] == ':') {
            if (token_length > NAME_MAX_LEN) {
                fprintf(stderr, "Label too long: %s\n", label);
                exit(EXIT_FAILURE);
            }
            labels.names[labels.length].offset = line_nr;
            strncpy(labels.names[labels.length].name, label, token_length);

            labels.length++;
        }

        line_nr++;
    }

    return labels;
}

enum { TOKEN_OP, TOKEN_DST, TOKEN_SRC, NUM_TOKENS };

static void read_data(Byte_Code *bc, Labels *labels, char **line, FILE *fp)
{
    if (!*line)
        return;

    char tokens[32][NAME_MAX_LEN] = {0};
    memset(tokens, 0x00, NUM_TOKENS * sizeof(tokens[0]));
    size_t i = 0, data_len = 0;

    // Parse till another section starts or the line is empty
    do {
        // skip empty lines
        if (**line == '\n')
            continue;

        // Parsing a well-formed data segment line:
        // e.g. message: db "Hello", 5
        while (**line != '\0' && **line != ';') {
            read_token(line, tokens[i++]);
            strip_spaces(line);
        }

        for (size_t j = 0; j < i; j += 4) {
            // label expected first
            if (!is_label(tokens[j]))
                goto parse_error;

            // Only support define bytes directive for now
            if (strncmp(tokens[j + 1], "db", 2) != 0)
                goto parse_error;

            if (!is_number(tokens[j + 3]))
                goto parse_error;

            // Length of the defined data
            data_len = atoi(tokens[j + 3]);

            // Copy the label name so it can be referenced later when building
            // the instruction set
            strncpy(labels->names[labels->length].name, tokens[j], data_len);

            // Reserving bytes space
            if (is_number(tokens[j + 2])) {
                labels->names[labels->length++].offset = bc->data_addr;
                char number[0xF];
                snprintf(number, sizeof(number), "%s", tokens[j + 2]);
                bc->data_addr +=
                    bc_push_data(bc, number, data_len, DATA_NUMBER);
            } else {
                // String content, store pointer address
                labels->names[labels->length++].offset = bc->data_addr;
                bc->data_addr += data_len;
                bc_push_data(bc, tokens[j + 2], data_len, DATA_STRING);
            }
        }
    } while (fgets(*line, 0xFFF, fp) && **line != '\0' && **line != '.');

    return;

parse_error:
    fprintf(stderr, "parse error\n");
}

Byte_Code *bc_load(const char *path)
{
    if (!path)
        return NULL;

    FILE *fp = fopen(path, "r");
    if (!fp)
        return NULL;

    Byte_Code *bc = bc_create();
    if (!bc)
        goto exit;

    char line[0xFFF];
    char tokens[NUM_TOKENS][NAME_MAX_LEN];
    char *line_ptr;
    int ntokens   = 0;

    Labels labels = scan_labels(fp);

    rewind(fp);

    while (fgets(line, 0xFFF, fp)) {
        ntokens  = 0;
        line_ptr = line;
        strip_spaces(&line_ptr);
        memset(tokens, 0x00, NUM_TOKENS * sizeof(tokens[0]));

        while (*line_ptr != '\0') {
            switch (*line_ptr) {
            case SECTION_START:
                if (strncasecmp(line_ptr, ".main", 5) == 0) {
                    *line_ptr = '\0';
                } else if (strncasecmp(line_ptr, ".data", 5) == 0) {
                    if (!fgets(line, 0xFFF, fp)) {
                        *line_ptr = '\0';
                    } else {
                        line_ptr = line;
                        read_data(bc, &labels, &line_ptr, fp);
                        strip_spaces(&line_ptr);
                    }
                }
                break;
            case COMMENT_START:
                *line_ptr = '\0';
                break;
            default:
                read_token(&line_ptr, tokens[ntokens++]);
                strip_spaces(&line_ptr);
                break;
            }
        }

        switch (ntokens) {
        case 1: {
            if (is_label(tokens[TOKEN_OP]))
                break;
            const struct instruction i =
                parse_instruction(tokens[TOKEN_OP], NULL, NULL, &labels);
            bc_push_instr(bc, bc_encode_instruction(i.op, i.dst, i.src));
            break;
        }
        case 2: {
            const struct instruction i = parse_instruction(
                tokens[TOKEN_OP], tokens[TOKEN_DST], NULL, &labels);
            bc_push_instr(bc, bc_encode_instruction(i.op, i.dst, i.src));
            break;
        }
        case 3: {
            const struct instruction i =
                parse_instruction(tokens[TOKEN_OP], tokens[TOKEN_DST],
                                  tokens[TOKEN_SRC], &labels);
            bc_push_instr(bc, bc_encode_instruction(i.op, i.dst, i.src));
            break;
        }
        default:
            break;
        }
    }

    fclose(fp);

    return bc;

exit:
    fclose(fp);
    return NULL;
}

void bc_free(Byte_Code *bc)
{
    segment_free(CODE_SEGMENT, bc->code_segment);
    segment_free(DATA_SEGMENT, bc->data_segment);
    free(bc);
}
