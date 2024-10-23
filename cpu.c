#include "cpu.h"
#include "syscall.h"
#include <stdio.h>
#include <string.h>

static void reset(Cpu *cpu, qword *code_segment, hword *data_segment,
                  size_t data_len)
{
    memset(cpu->r, 0x00, NUM_REGISTERS * sizeof(*cpu->r));
    memset(cpu->stack, 0x00, STACK_SIZE * sizeof(*cpu->stack));
    cpu->bcode = code_segment;

    cpu->pc    = 0;
    cpu->sp    = cpu->stack;
    cpu->run   = false;

    if (!data_segment)
        return;

    memset(cpu->memory, 0x00, 32768);
    // Addressing
    for (size_t i = DATA_OFFSET; i < DATA_OFFSET * 2; ++i)
        cpu->memory[i] = i + DATA_OFFSET;
    // Storing
    for (size_t i = DATA_OFFSET * 2, j = 0; j < data_len - DATA_OFFSET;
         ++i, ++j)
        cpu->memory[i] = data_segment[j];
}

static qword fetch(Cpu *cpu) { return cpu->bcode[cpu->pc++]; }

static struct instruction_line decode(qword einstr)
{
    return bc_decode_instruction(einstr);
}

static void clear_flags(Cpu *cpu)
{
    cpu->flags[0] = 0;
    cpu->flags[1] = 0;
    cpu->flags[2] = 0;
}

static void set_flags(Cpu *cpu, qword a, qword b)
{
    qword result  = a - b;
    cpu->flags[0] = (result == 0);
    cpu->flags[1] = (result < 0);
    cpu->flags[2] = (result > 0);
}

static qword *set_operands(Cpu *cpu, const struct instruction_line *i,
                           qword *src)
{
    *src = (i->sem & IS_SRC_REG)   ? cpu->r[i->src]
           : (i->sem & IS_SRC_MEM) ? cpu->memory[i->src]
                                   : i->src;

    return (i->sem & IS_DST_REG) ? &cpu->r[i->dst] : &cpu->memory[i->dst];
}

static Exec_Result execute(Cpu *cpu, struct instruction_line *instr)
{
    switch (instr->op) {
    case NOP: {
        break;
    }
    case MOV: {
        if (instr->sem & IS_SRC_REG && instr->sem & IS_DST_REG)
            cpu->r[instr->dst] = cpu->r[instr->src];
        else if (instr->sem & IS_SRC_REG && instr->sem & IS_DST_MEM)
            cpu->memory[instr->dst] = cpu->r[instr->src];
        else if (instr->sem & IS_SRC_MEM && instr->sem & IS_DST_REG)
            cpu->r[instr->dst] = cpu->memory[instr->src];
        else if (instr->sem & IS_SRC_IMM && instr->sem & IS_DST_REG)
            cpu->r[instr->dst] = instr->src;
        else if (instr->sem & IS_SRC_IMM && instr->sem & IS_DST_MEM)
            cpu->memory[instr->dst] = instr->src;

        break;
    }
    case PSH: {
        if (instr->sem & IS_DST_REG)
            *cpu->sp++ = cpu->r[instr->dst];
        else if (instr->sem & IS_DST_MEM)
            *cpu->sp++ = cpu->memory[instr->dst];
        else if (instr->sem & IS_SRC_IMM)
            *cpu->sp++ = instr->src;

        break;
    }
    case POP: {
        cpu->r[instr->dst] = *--cpu->sp;
        break;
    }
    case ADD: {
        qword src  = 0;
        qword *dst = set_operands(cpu, instr, &src);

        *dst += src;
        break;
    }
    case SUB: {
        qword src  = 0;
        qword *dst = set_operands(cpu, instr, &src);

        *dst -= src;
        break;
    }
    case MUL: {
        qword src  = 0;
        qword *dst = set_operands(cpu, instr, &src);

        *dst *= src;

        break;
    }
    case DIV: {
        qword src  = 0;
        qword *dst = set_operands(cpu, instr, &src);

        if (src == 0)
            return E_DIV_BY_ZERO;

        *dst /= src;
        break;
    }
    case MOD: {
        qword src  = 0;
        qword *dst = set_operands(cpu, instr, &src);

        *dst %= src;
        break;
    }
    case INC: {
        if (instr->sem & IS_SRC_MEM)
            cpu->memory[instr->dst]++;
        else
            cpu->r[instr->dst]++;
        break;
    }
    case DEC: {
        if (instr->sem & IS_SRC_MEM)
            cpu->memory[instr->dst]++;
        else
            cpu->r[instr->dst]--;
        break;
    }
    case CLF: {
        clear_flags(cpu);
        break;
    }
    case CMP: {
        qword src  = 0;
        qword *dst = set_operands(cpu, instr, &src);

        set_flags(cpu, *dst, src);
        break;
    }
    case JMP: {
        cpu->pc = instr->dst;
        break;
    }
    case JEQ: {
        if (cpu->flags[0])
            cpu->pc = instr->dst;
        break;
    }
    case JNE: {
        if (!cpu->flags[0])
            cpu->pc = instr->dst;
        break;
    }
    case JLE: {
        if (cpu->flags[0] || cpu->flags[1])
            cpu->pc = instr->dst;
        break;
    }
    case JLT: {
        if (!cpu->flags[0] && cpu->flags[1])
            cpu->pc = instr->dst;
        break;
    }
    case JGE: {
        if (cpu->flags[0] || cpu->flags[2])
            cpu->pc = instr->dst;
        break;
    }
    case JGT: {
        if (!cpu->flags[0] && cpu->flags[2])
            cpu->pc = instr->dst;
        break;
    }
    case AND: {
        cpu->r[instr->dst] &= cpu->r[instr->src];
        break;
    }
    case BOR: {
        cpu->r[instr->dst] |= cpu->r[instr->src];
        break;
    }
    case XOR: {
        cpu->r[instr->dst] ^= cpu->r[instr->src];
        break;
    }
    case NOT: {
        cpu->r[instr->dst] = -cpu->r[instr->src];
        break;
    }
    case SHR: {
        cpu->r[instr->dst] >>= cpu->r[instr->src];
        break;
    }
    case SHL: {
        cpu->r[instr->dst] <<= cpu->r[instr->src];
        break;
    }
    case CALL: {
        *cpu->sp++ = (cpu->pc - 1);
        cpu->pc    = instr->dst;
        break;
    }
    case RET: {
        cpu->pc = *--cpu->sp;
        break;
    }
    case SYSCALL: {
        switch (cpu->r[BX]) {
        // STDIN
        case 0:
            syscall_read(cpu->r[BX], &cpu->memory[cpu->r[CX]],
                         cpu->r[DX] * sizeof(qword));
            break;
        // STDOUT
        case 1:
            syscall_write(cpu->r[BX], &cpu->memory[cpu->r[CX]],
                          cpu->r[DX] * sizeof(qword));
            fflush(stdout);
            break;
        case 64:
            cpu->r[AX] = syscall_atoi(&cpu->memory[cpu->r[CX]]);
            break;
        }
        break;
    }
    case HLT:
        cpu->run = false;
        break;
    default:
        return E_UNKNOWN_INSTRUCTION;
    }

    return SUCCESS;
}

Cpu *cpu_create(const Byte_Code *bc, size_t memory_size)
{
    Cpu *cpu = malloc(sizeof(*cpu));
    if (!cpu)
        return NULL;

    cpu->memory = malloc(memory_size * sizeof(qword));
    if (!cpu->memory) {
        free(cpu);
        return NULL;
    }

    reset(cpu, bc_code(bc), bc_data(bc), bc_data_addr(bc));

    return cpu;
}

void cpu_free(Cpu *cpu)
{
    free(cpu->memory);
    free(cpu);
}

void cpu_reset(Cpu *cpu, qword *code, hword *data, size_t data_len)
{
    reset(cpu, code, data, data_len);
}

Exec_Result cpu_run(Cpu *cpu)
{
    Exec_Result r = SUCCESS;
    cpu->run      = true;
    while (cpu->run && r == SUCCESS) {
        qword encoded_instr           = fetch(cpu);
        struct instruction_line instr = decode(encoded_instr);
        r                             = execute(cpu, &instr);
    }

    return r;
}

void cpu_print_registers(const Cpu *const cpu)
{
    printf("AX: %llu BX: %llu CX: %llu DX: %llu\n", cpu->r[AX], cpu->r[BX],
           cpu->r[CX], cpu->r[DX]);
}
