/**************************************************************/
/* CS/COE 1541
just compile with gcc -o pipeline pipeline.c
and execute using
./pipeline  /afs/cs.pitt.edu/courses/1541/short_traces/sample.tr	0
***************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include "CPU.h"

struct trace_item * no_op_initializer();
int is_lwsw_type(char);
int is_alubr_type(char);
int is_branch_jump(char);
int data_dependency(struct trace_item *, struct trace_item *);
int load_dependency(struct trace_item *, struct trace_item *);

int main(int argc, char **argv)
{
    const int LW_LOC  = 0;
    const int ALU_LOC = 1;

    struct trace_item *read;
    size_t size;
    char *trace_file_name;
    int trace_view_on = 0;
    int break_flag = 0;
    int buffer_full = 0;
    struct trace_item *REG[2];
    REG[LW_LOC]  = NULL; // lw, sw spot
    REG[ALU_LOC] = NULL; // alu, branch spot

    struct trace_item *instruction_buffer[2]; //FIFO buffer
    instruction_buffer[0] = NULL;
    instruction_buffer[1] = NULL;

    struct trace_item *lw_sw_pipeline[4];
    lw_sw_pipeline[0] = NULL; // REG/EX
    lw_sw_pipeline[1] = NULL; // EX/MEM
    lw_sw_pipeline[2] = NULL; // MEM/WB
    lw_sw_pipeline[3] = NULL; // WB/Output

    struct trace_item *alu_br_pipeline[4];
    alu_br_pipeline[0] = NULL; // REG/EX
    alu_br_pipeline[1] = NULL; // EX/MEM
    alu_br_pipeline[2] = NULL; // MEM/WB
    alu_br_pipeline[3] = NULL; // WB/Output

    unsigned int cycle_number = 0;

    if (argc == 1) {
        fprintf(stdout, "\nUSAGE: tv <trace_file> <switch - any character>\n");
        fprintf(stdout, "\n(switch) to turn on or off individual item view.\n\n");
        exit(0);
    }

    trace_file_name = argv[1];
    if (argc == 3) trace_view_on = atoi(argv[2]) ;

    fprintf(stdout, "\n ** opening file %s\n", trace_file_name);

    trace_fd = fopen(trace_file_name, "rb");

    if (!trace_fd) {
        fprintf(stdout, "\ntrace file %s not opened.\n\n", trace_file_name);
        exit(0);
    }

    trace_init();

    // Begin pipeline execution
    while(1) {
        if (break_flag){
            printf("+ Simulation terminates at cycle : %u\n", cycle_number);
            break;
        }

        // Get items for the instruction buffer, as long as it isn't full
        while((instruction_buffer[0] == NULL) || (instruction_buffer[1] == NULL)){
            size = trace_get_item(&read);
            // Terminate if the buffer is empty
            if (!size) {
                break_flag = 1;
                break;
            }
            // If first spot in queue is empty, insert there
            if(instruction_buffer[1] == NULL) {
                instruction_buffer[1] = read;
            } else {
                instruction_buffer[0] = read;
            }
        }

        // Avoid seg-faults by replacing NULLS with NO-OP (for the last cycle of the simulation)
        if(instruction_buffer[0] == NULL) {instruction_buffer[0] = no_op_initializer();}

        // Advance pipelines
        lw_sw_pipeline[3]  = lw_sw_pipeline[2];
        lw_sw_pipeline[2]  = lw_sw_pipeline[1];
        lw_sw_pipeline[1]  = lw_sw_pipeline[0];
        lw_sw_pipeline[0]  = REG[LW_LOC];

        alu_br_pipeline[3] = alu_br_pipeline[2];
        alu_br_pipeline[2] = alu_br_pipeline[1];
        alu_br_pipeline[1] = alu_br_pipeline[0];
        alu_br_pipeline[0] = REG[ALU_LOC];

        // Dynamic Scheduler Logic
        if (((is_lwsw_type(instruction_buffer[0]->type) && is_alubr_type(instruction_buffer[1]->type)) ||
          (is_lwsw_type(instruction_buffer[0]->type) && is_alubr_type(instruction_buffer[1]->type))) && // Check that the two in buffer are different AND
          (!is_branch_jump(instruction_buffer[1]->type)) &&   // Check that first in buffer is not a jump/branch AND
          (!data_dependency(instruction_buffer[0], instruction_buffer[1])) && // Check that there is no data dependence between the two instructions AND
          (!(data_dependency(instruction_buffer[0], REG[LW_LOC]) || data_dependency(instruction_buffer[1], REG[LW_LOC])))) // Check that there is no dependence between buffered instructions and REG instructions
          {
              // Issue both instructions, but first check which is the load word type
              if (is_lwsw_type(instruction_buffer[1]->type)){
                  // the first is the lw type
                  REG[LW_LOC]  = instruction_buffer[1];
                  REG[ALU_LOC] = instruction_buffer[0];
              } else {
                  // the second is the lw type
                  REG[LW_LOC]  = instruction_buffer[0];
                  REG[ALU_LOC] = instruction_buffer[1];
              }
              instruction_buffer[1] = NULL;
              instruction_buffer[0] = NULL;
          }
          else if (!data_dependency(instruction_buffer[1], REG[LW_LOC]))
          {
              //push only the first instruction, figure out which pipeline to send it
              if (is_lwsw_type(instruction_buffer[1]->type)){
                  // It's a load word type
                  REG[LW_LOC]  = instruction_buffer[1];
                  REG[ALU_LOC] = no_op_initializer();
              } else {
                  // It's an ALU/Brand/Jump type
                  REG[LW_LOC]  = no_op_initializer();
                  REG[ALU_LOC] = instruction_buffer[1];
              }
              // Push the instruction_buffer through by one
              instruction_buffer[1] = instruction_buffer[0];
              instruction_buffer[0] = NULL;
          }
          else
          {
              // push a no-op onto each pipeline
              REG[LW_LOC]  = no_op_initializer();
              REG[ALU_LOC] = no_op_initializer();
          }

        cycle_number++;

        if ((trace_view_on) && (lw_sw_pipeline[3] != NULL) && (alu_br_pipeline[3] != NULL)) {
            // Print first completed instruction
            switch(lw_sw_pipeline[3]->type) {
                case ti_NOP:
                free(lw_sw_pipeline[3]);
                printf("[cycle %d] NOP:\n",cycle_number) ;
                break;
                case ti_RTYPE:
                printf("[cycle %d] RTYPE:",cycle_number) ;
                printf(" (PC: %x)(sReg_a: %d)(sReg_b: %d)(dReg: %d) \n", lw_sw_pipeline[3]->PC, lw_sw_pipeline[3]->sReg_a, lw_sw_pipeline[3]->sReg_b, lw_sw_pipeline[3]->dReg);
                break;
                case ti_ITYPE:
                printf("[cycle %d] ITYPE:",cycle_number) ;
                printf(" (PC: %x)(sReg_a: %d)(dReg: %d)(addr: %x)\n", lw_sw_pipeline[3]->PC, lw_sw_pipeline[3]->sReg_a, lw_sw_pipeline[3]->dReg, lw_sw_pipeline[3]->Addr);
                break;
                case ti_LOAD:
                printf("[cycle %d] LOAD:",cycle_number) ;
                printf(" (PC: %x)(sReg_a: %d)(dReg: %d)(addr: %x)\n", lw_sw_pipeline[3]->PC, lw_sw_pipeline[3]->sReg_a, lw_sw_pipeline[3]->dReg, lw_sw_pipeline[3]->Addr);
                break;
                case ti_STORE:
                printf("[cycle %d] STORE:",cycle_number) ;
                printf(" (PC: %x)(sReg_a: %d)(sReg_b: %d)(addr: %x)\n", lw_sw_pipeline[3]->PC, lw_sw_pipeline[3]->sReg_a, lw_sw_pipeline[3]->sReg_b, lw_sw_pipeline[3]->Addr);
                break;
                case ti_BRANCH:
                printf("[cycle %d] BRANCH:",cycle_number) ;
                printf(" (PC: %x)(sReg_a: %d)(sReg_b: %d)(addr: %x)\n", lw_sw_pipeline[3]->PC, lw_sw_pipeline[3]->sReg_a, lw_sw_pipeline[3]->sReg_b, lw_sw_pipeline[3]->Addr);
                break;
                case ti_JTYPE:
                printf("[cycle %d] JTYPE:",cycle_number) ;
                printf(" (PC: %x)(addr: %x)\n", lw_sw_pipeline[3]->PC,lw_sw_pipeline[3]->Addr);
                break;
                case ti_SPECIAL:
                printf("[cycle %d] SPECIAL:\n",cycle_number) ;
                break;
                case ti_JRTYPE:
                printf("[cycle %d] JRTYPE:",cycle_number) ;
                printf(" (PC: %x) (sReg_a: %d)(addr: %x)\n", lw_sw_pipeline[3]->PC, lw_sw_pipeline[3]->dReg, lw_sw_pipeline[3]->Addr);
                break;
            }

            // Print first completed instruction
            switch(alu_br_pipeline[3]->type) {
                case ti_NOP:
                free(alu_br_pipeline[3]);
                printf("[cycle %d] NOP:\n",cycle_number) ;
                break;
                case ti_RTYPE:
                printf("[cycle %d] RTYPE:",cycle_number) ;
                printf(" (PC: %x)(sReg_a: %d)(sReg_b: %d)(dReg: %d) \n", alu_br_pipeline[3]->PC, alu_br_pipeline[3]->sReg_a, alu_br_pipeline[3]->sReg_b, alu_br_pipeline[3]->dReg);
                break;
                case ti_ITYPE:
                printf("[cycle %d] ITYPE:",cycle_number) ;
                printf(" (PC: %x)(sReg_a: %d)(dReg: %d)(addr: %x)\n", alu_br_pipeline[3]->PC, alu_br_pipeline[3]->sReg_a, alu_br_pipeline[3]->dReg, alu_br_pipeline[3]->Addr);
                break;
                case ti_LOAD:
                printf("[cycle %d] LOAD:",cycle_number) ;
                printf(" (PC: %x)(sReg_a: %d)(dReg: %d)(addr: %x)\n", alu_br_pipeline[3]->PC, alu_br_pipeline[3]->sReg_a, alu_br_pipeline[3]->dReg, alu_br_pipeline[3]->Addr);
                break;
                case ti_STORE:
                printf("[cycle %d] STORE:",cycle_number) ;
                printf(" (PC: %x)(sReg_a: %d)(sReg_b: %d)(addr: %x)\n", alu_br_pipeline[3]->PC, alu_br_pipeline[3]->sReg_a, alu_br_pipeline[3]->sReg_b, alu_br_pipeline[3]->Addr);
                break;
                case ti_BRANCH:
                printf("[cycle %d] BRANCH:",cycle_number) ;
                printf(" (PC: %x)(sReg_a: %d)(sReg_b: %d)(addr: %x)\n", alu_br_pipeline[3]->PC, alu_br_pipeline[3]->sReg_a, alu_br_pipeline[3]->sReg_b, alu_br_pipeline[3]->Addr);
                break;
                case ti_JTYPE:
                printf("[cycle %d] JTYPE:",cycle_number) ;
                printf(" (PC: %x)(addr: %x)\n", alu_br_pipeline[3]->PC,alu_br_pipeline[3]->Addr);
                break;
                case ti_SPECIAL:
                printf("[cycle %d] SPECIAL:\n",cycle_number) ;
                break;
                case ti_JRTYPE:
                printf("[cycle %d] JRTYPE:",cycle_number) ;
                printf(" (PC: %x) (sReg_a: %d)(addr: %x)\n", alu_br_pipeline[3]->PC, alu_br_pipeline[3]->dReg, alu_br_pipeline[3]->Addr);
                break;
            }
        }
    }

    trace_uninit();

    exit(0);
}

struct trace_item * no_op_initializer()
{
   struct trace_item *no_op;

   //allocates memory for no-op
   no_op = (struct trace_item *)malloc(sizeof(struct trace_item));

   //sets dummy values for no-op
   no_op->type   = 0;
   no_op->sReg_a = 255;
   no_op->sReg_b = 255;
   no_op->dReg   = 255;
   no_op->PC     = 0;
   no_op->Addr   = 0;

   return(no_op);
}

int is_lwsw_type(char type)
{
    if ((type == ti_LOAD) || (type == ti_STORE))
        return 1;
    else
        return 0;
}

int is_alubr_type(char type){
    if ((type == ti_RTYPE) || (type == ti_ITYPE) || (is_branch_jump(type)))
        return 1;
    else
        return 0;
}


int is_branch_jump(char type){
    if((type == ti_BRANCH) || (type == ti_JTYPE) || (type == ti_JRTYPE))
        return 1;
    else
        return 0;
}

int data_dependency(struct trace_item *instruct_1, struct trace_item *instruct_2){
    // If either are NULL. there cannot be data dependence
    if ((instruct_1 == NULL) || (instruct_2 == NULL)){
        return 0;
    }
    if (instruct_1->type == ti_LOAD) {
       // Load Word Detected
       return load_dependency(instruct_1, instruct_2);
   } else if (instruct_2->type == ti_LOAD){
       return load_dependency(instruct_2, instruct_1);
   } else {
       return 0;
   }

}

int load_dependency(struct trace_item *load, struct trace_item *instruction_to_compare){
    if ((instruction_to_compare->type == ti_RTYPE) || (instruction_to_compare->type == ti_STORE) || (instruction_to_compare->type == ti_BRANCH)){
        if ((load->dReg == instruction_to_compare->sReg_a) || (load->dReg == instruction_to_compare->sReg_b)){
            // Load-Use Hazard Detected
            return 1;
        }
    } else if ((instruction_to_compare->type == ti_ITYPE) || (instruction_to_compare->type == ti_LOAD) || (instruction_to_compare->type == ti_JRTYPE)){
        if(load->dReg == instruction_to_compare->sReg_a){
            // Load-Use Hazard Detected
            return 1;
        }
    }
    // Reach here if we don't return 1
    return 0;
}
