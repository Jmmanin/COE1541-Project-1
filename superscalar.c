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
    struct trace_item *output1;
    struct trace_item *output2;
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

    struct trace_item *lw_sw_pipeline[3];
    lw_sw_pipeline[0] = NULL;
    lw_sw_pipeline[1] = NULL;
    lw_sw_pipeline[2] = NULL;

    struct trace_item *alu_br_pipeline[3];
    alu_br_pipeline[0] = NULL;
    alu_br_pipeline[1] = NULL;
    alu_br_pipeline[2] = NULL;

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
        if(instruction_buffer[0] == NULL) instruction_buffer[0] = no_op_initializer();

        // Check for instructions of opposite types
        if (is_lwsw_type(instruction_buffer[0]->type) && is_alubr_type(instruction_buffer[1]->type)){
            // Check for data dependence between the two
            if (!data_dependency(instruction_buffer[0], instruction_buffer[1])){
                // No data dependency, check for if first in buffer is branch or jump
                if (!is_branch_jump(instruction_buffer[1]->type)){
                    // No branch or jump,
                    if ((REG[LW_LOC] != NULL) && (REG[LW_LOC]->type == ti_LOAD)){
                        if ((!load_dependency(REG[LW_LOC], instruction_buffer[0])) && (load_dependency(REG[LW_LOC], instruction_buffer[1]))){
                            // No load dependency for either
                            // Now we can issue both instructions
                            REG[LW_LOC]  = instruction_buffer[0];
                            REG[ALU_LOC] = instruction_buffer[1];

                            instruction_buffer[0] = NULL;
                            instruction_buffer[1] = NULL;
                        }
                    }
                }
            }
        // Check alternate combination of types
        } else if (is_lwsw_type(instruction_buffer[1]->type) && is_alubr_type(instruction_buffer[0]->type)){
            // Check for data dependence between the two
            if (!data_dependency(instruction_buffer[1], instruction_buffer[0])){
                // No data dependency, check for if first in buffer is branch or jump
                if (!is_branch_jump(instruction_buffer[1]->type)){
                    // No branch or jump,
                    if ((REG[LW_LOC] != NULL) && (REG[LW_LOC]->type == ti_LOAD)){
                        if ((!load_dependency(REG[LW_LOC], instruction_buffer[0])) && (load_dependency(REG[LW_LOC], instruction_buffer[1]))){
                            // No load dependency for either
                            // Now we can issue both instructions
                            REG[LW_LOC]  = instruction_buffer[0];
                            REG[ALU_LOC] = instruction_buffer[1];

                            instruction_buffer[0] = NULL;
                            instruction_buffer[1] = NULL;
                        }
                    }
                }
            }
        // The instructions are of the same type
        } else {
            // Check the first instuction for a load-use hazard with REG
            if(!load_dependency(REG[LW_LOC], instruction_buffer[1])){
                // Check type of first instruction
                if (is_lwsw_type(instruction_buffer[1]->type)){
                    REG[LW_LOC] = instruction_buffer[1];
                    instruction_buffer[1] = instruction_buffer[0];
                    instruction_buffer[0] = NULL;
                } else if (is_alubr_type(instruction_buffer[1]->type)){
                    REG[ALU_LOC] = instruction_buffer[1];
                    instruction_buffer[1] = instruction_buffer[0];
                    instruction_buffer[0] = NULL;
                }
            } else {
                // Fits no other step, insert two no-ops
                REG[LW_LOC]  = no_op_initializer();
                REG[ALU_LOC] = no_op_initializer(); 
            }
        }












        cycle_number++;

        output1 = instruction_buffer[1];
        // Propagate instruction through the queue
        instruction_buffer[1] = instruction_buffer[0];
        instruction_buffer[0] = NULL;


        if (trace_view_on) {
            // Print first completed instruction
            switch(output1->type) {
                case ti_NOP:
                free(output1);
                printf("[cycle %d] NOP:\n",cycle_number) ;
                break;
                case ti_RTYPE:
                printf("[cycle %d] RTYPE:",cycle_number) ;
                printf(" (PC: %x)(sReg_a: %d)(sReg_b: %d)(dReg: %d) \n", output1->PC, output1->sReg_a, output1->sReg_b, output1->dReg);
                break;
                case ti_ITYPE:
                printf("[cycle %d] ITYPE:",cycle_number) ;
                printf(" (PC: %x)(sReg_a: %d)(dReg: %d)(addr: %x)\n", output1->PC, output1->sReg_a, output1->dReg, output1->Addr);
                break;
                case ti_LOAD:
                printf("[cycle %d] LOAD:",cycle_number) ;
                printf(" (PC: %x)(sReg_a: %d)(dReg: %d)(addr: %x)\n", output1->PC, output1->sReg_a, output1->dReg, output1->Addr);
                break;
                case ti_STORE:
                printf("[cycle %d] STORE:",cycle_number) ;
                printf(" (PC: %x)(sReg_a: %d)(sReg_b: %d)(addr: %x)\n", output1->PC, output1->sReg_a, output1->sReg_b, output1->Addr);
                break;
                case ti_BRANCH:
                printf("[cycle %d] BRANCH:",cycle_number) ;
                printf(" (PC: %x)(sReg_a: %d)(sReg_b: %d)(addr: %x)\n", output1->PC, output1->sReg_a, output1->sReg_b, output1->Addr);
                break;
                case ti_JTYPE:
                printf("[cycle %d] JTYPE:",cycle_number) ;
                printf(" (PC: %x)(addr: %x)\n", output1->PC,output1->Addr);
                break;
                case ti_SPECIAL:
                printf("[cycle %d] SPECIAL:\n",cycle_number) ;
                break;
                case ti_JRTYPE:
                printf("[cycle %d] JRTYPE:",cycle_number) ;
                printf(" (PC: %x) (sReg_a: %d)(addr: %x)\n", output1->PC, output1->dReg, output1->Addr);
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
    if ((type == 3) || (type == 4))
        return 1;
    else
        return 0;
}

int is_alubr_type(char type){
    if ((type == 1) || (type == 2) || (type == 5) || (type == 6) || (type == 7))
        return 1;
    else
        return 0;
}

int data_dependency(struct trace_item *lwsw_instruction, struct trace_item *alubr_instruction){
    if (lwsw_instruction->type == ti_LOAD) {
       // Load Word Detected
       return load_dependency(lwsw_instruction, alubr_instruction);
   }
   // Reach here if we don't return 1
   return 0;

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

int is_branch_jump(char type){
    if((type == 5) || (type == 6) || (type == 8))
        return 1;
    else
        return 0;
}
