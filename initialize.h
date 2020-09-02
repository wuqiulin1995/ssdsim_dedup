#pragma once

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include "avlTree.h"

#define UNIQUE_PAGE_NB 1000000 // fing [1, UNIQUE_PAGE_NB]
#define FING_DELAY 32000 // fing compare delay, 32us
// #define MAX_OOB_ENTRY 1000000
#define MAX_OOB_SEG 102400 // 100M
#define OOB_ENTRY_PER_SEG 64 // 1KB seg
#define OOB_ENTRY_BYTES 16
#define NVRAM_READ_DELAY 50 // 50ns for PCM 64 byte
#define NVRAM_WRITE_DELAY 500 // 500ns for PCM 64 byte
#define INVALID_ENTRY_THRE 0.05

#define SECTOR 512
#define BUFSIZE 200
#define INDEX 10
#define PAGE_INDEX 1 

#define DYNAMIC_ALLOCATION 0
#define STATIC_ALLOCATION 1
#define HYBRID_ALLOCATION 2
#define SUPERBLOCK_ALLOCATION 3

#define SLC_MODE 0
#define TLC_MODE 1

#define SLC_TYPE 1
#define MLC_TYPE 2
#define TLC_TYPE 3
#define QLC_TYPE 4
#define	PAGE_TYPE SLC_TYPE

#define READ 1
#define WRITE 0

#define SIG_NORMAL 11
#define SIG_ERASE_WAIT 12
#define SIG_ERASE_SUSPEND 13
#define SIG_ERASE_RESUME 14

#define NORMAL_TYPE 0
#define SUSPEND_TYPE 1

#define WRITE_MORE 1      
#define READ_MORE 2

#define SSD_MOUNT 1
#define CHANNEL_MOUNT 2
/*********************************all states of each objects************************************************
*Defines the channel idle, command address transmission, data transmission, transmission, and other states
*And chip free, write busy, read busy, command address transfer, data transfer, erase busy, copyback busy, 
*other status , And read and write requests (sub) wait, read command address transfer, read, read data transfer, 
*write command address transfer, write data transfer, write transfer, completion and other status
************************************************************************************************************/

#define CHANNEL_IDLE 000
#define CHANNEL_C_A_TRANSFER 3
#define CHANNEL_GC 4           
#define CHANNEL_DATA_TRANSFER 7
#define CHANNEL_TRANSFER 8
#define CHANNEL_UNKNOWN 9

#define CHIP_IDLE 100
#define CHIP_WRITE_BUSY 101
#define CHIP_READ_BUSY 102
#define CHIP_C_A_TRANSFER 103
#define CHIP_DATA_TRANSFER 107
#define CHIP_WAIT 108
#define CHIP_ERASE_BUSY 109

#define SR_WAIT 200                 
#define SR_R_C_A_TRANSFER 201
#define SR_R_READ 202
#define SR_R_DATA_TRANSFER 203
#define SR_W_C_A_TRANSFER 204
#define SR_W_DATA_TRANSFER 205
#define SR_W_TRANSFER 206
#define SR_COMPLETE 299

#define REQUEST_IN 300         //Next request arrival time
#define OUTPUT 301             //The next time the data is output

#define GC_WAIT 400
#define GC_ERASE_C_A 401
#define GC_COPY_BACK 402
#define GC_COMPLETE 403
#define GC_INTERRUPT 0
#define GC_UNINTERRUPT 1

#define CHANNEL(lsn) (lsn&0x0000)>>16      
#define chip(lsn) (lsn&0x0000)>>16 
#define die(lsn) (lsn&0x0000)>>16 
#define PLANE(lsn) (lsn&0x0000)>>16 
#define BLOKC(lsn) (lsn&0x0000)>>16 
#define PAGE(lsn) (lsn&0x0000)>>16 
#define SUBPAGE(lsn) (lsn&0x0000)>>16  

#define PG_SUB 0xffffffff			
//define READ OPERATION
#define REQ_READ 0
#define UPDATE_READ 1
#define GC_READ 2

#define SET_VALID(s,i) ((1<<i)|s)
#define GET_BIT(s,i)    ((s>>i)&1)

//define superblock infomation 
#define PLANE_LEVEL 1
#define DIE_LEVEL 2
#define CHIP_LEVEL 3
#define CHANNEL_LEVEL 4

#define MIN_SB_RATE 0.05
#define INVALID_PPN -1

#define FULL_FLAG 1024

//FTL
#define SECTOR4KB 1   //at the granalarity of 4KB
//lsm_tree FTL
#define MAX_PARALALISM 64  //max paralelism

/* 1:plane-level superblock
2:die-level superblock
3:chip-level superblock
4:channel-level superblock
*/

//data type 
// #define USER_DATA 0
// #define GC_USER_DATA 4

/*
    16 GB SSD  = 4 M flash pages
*/

//unit 
#define B 1

//#define CACHE_SIZE 1024 
#define MAX_CACHE_SIZE  (4*1024*1024)
// #define MAX_LUN_PER_PAGE 1
#define MAP_PER_PAGE 4096

/*************************************************************************
*Function result status code
*Status is the function type ,the value is the function result status code
**************************************************************************/
#define TRUE		1
#define FALSE		0
#define SUCCESS		1
#define FAILURE		0
#define ERROR		-1
#define INFEASIBLE	-2
#define OVERFLOW	-3
typedef int Status;

struct ac_time_characteristics{
	int tPROG;     //program time
	int tDBSY;     //bummy busy time for two-plane program
	int tBERS;     //block erase time
	int tPROGO;    //one shot program time
	int tERSL;	   //the trans time of suspend / resume operation
	int tCLS;      //CLE setup time
	int tCLH;      //CLE hold time
	int tCS;       //CE setup time
	int tCH;       //CE hold time
	int tWP;       //WE pulse width
	int tALS;      //ALE setup time
	int tALH;      //ALE hold time
	int tDS;       //data setup time
	int tDH;       //data hold time
	int tWC;       //write cycle time
	int tWH;       //WE high hold time
	int tADL;      //address to data loading time
	int tR;        //data transfer from cell to register
	int tAR;       //ALE to RE delay
	int tCLR;      //CLE to RE delay
	int tRR;       //ready to RE low
	int tRP;       //RE pulse width
	int tWB;       //WE high to busy
	int tRC;       //read cycle time
	int tREA;      //RE access time
	int tCEA;      //CE access time
	int tRHZ;      //RE high to output hi-z
	int tCHZ;      //CE high to output hi-z
	int tRHOH;     //RE high to output hold
	int tRLOH;     //RE low to output hold
	int tCOH;      //CE high to output hold
	int tREH;      //RE high to output time
	int tIR;       //output hi-z to RE low
	int tRHW;      //RE high to WE low
	int tWHR;      //WE high to RE low
	int tRST;      //device resetting time
}ac_timing;


struct ssd_info{ 
	//Global variable
	int buffer_full_flag;				 //buffer blocking flag:0--unblocking , 1-- blocking
	int trace_over_flag;				 //the end of trace flag:0-- not ending ,1--ending
	unsigned long request_lz_count;			 //trace request count
	int warm_flash_cmplt;
	
	//superblock info
	int sb_cnt;
	int free_sb_cnt;
	struct super_block_info *sb_pool;
	struct super_block_info *open_sb; //0 is for user data ; 1 is for mapping data

	__int64 current_time;                //Record system time
	__int64 next_request_time;

	int flag;
	unsigned int request_queue_length;
	
	char parameterfilename[50];
	char tracefilename[50];
	char outputfilename[50];
	char statisticfilename[50];
	char stat_file_name[50];

	FILE *outputfile;
	FILE *tracefile;
	FILE *statisticfile;

	FILE *stat_file;
	struct NVRAM_OOB_SEG *nvram_seg;

    struct parameter_value *parameter;   //SSD parameter
	struct dram_info *dram;
	struct request *request_queue;       //dynamic request queue
	struct request *request_head;		 // the head of the request queue
	struct request *request_tail;	     // the tail of the request queue
	struct request *request_work;		 // the work point of the request queue
	struct sub_request *subs_w_head;     //When using the full dynamic allocation, the first hanging on the ssd, etc. into the process function is linked to the corresponding channel read request queue
	struct sub_request *subs_w_tail;
	struct channel_info *channel_head;   //Points to the first address of the channel structure array

	unsigned int min_lsn;
	unsigned int max_lsn;

	unsigned long write_request_count;    //Record the number of write operations
	unsigned long read_request_count;     //Record the number of read operations

	unsigned long erase_count;
	unsigned long gc_count;
	unsigned long total_gc_count;

	unsigned long data_read_cnt;
	unsigned long data_program_cnt;
	unsigned long gc_program_cnt;

	__int64 write_avg;                   //Record the time to calculate the average response time for the write request
	__int64 read_avg;                    //Record the time to calculate the average response time for the read request
	
	float ave_read_size;
	float ave_write_size;

	unsigned int reduced_writes;
	unsigned int use_remap_fail;
	unsigned int total_oob_entry;
	unsigned int invalid_oob_entry;
	unsigned int total_alloc_seg;
	unsigned int max_alloc_seg;
	unsigned int min_alloc_seg;
	// unsigned int max_ref;

	long long avg_write_delay_print; // write_request_count % 10000 = 1
	long long max_write_delay_print;
	long long last_write_avg;

	unsigned int nvram_gc_print;
	long long nvram_gc_delay_print;
	long long avg_nvram_gc_delay;

	unsigned int gcr_nvram_print;
	long long gcr_nvram_delay_print;
	long long avg_gcr_nvram_delay;
};


struct channel_info{
	int chip;                            //Indicates how many particles are on the bus
	int current_state;                   //channel has serveral states, including idle, command/address transfer,data transfer
	int next_state;
	__int64 current_time;                //Record the current time of the channel
	__int64 next_state_predict_time;     //the predict time of next state, used to decide the sate at the moment

	struct sub_request *subs_r_head;     //The read request on the channel queue header, the first service in the queue header request
	struct sub_request *subs_r_tail;     //Channel on the read request queue tail, the new sub-request added to the tail
	struct sub_request *subs_w_head;     //The write request on the channel queue header, the first service in the queue header request
	struct sub_request *subs_w_tail;     //The write request queue on the channel, the new incoming request is added to the end of the queue
	
	unsigned int channel_busy_flag;
	unsigned long channel_read_count;	 //Record the number of read and write wipes within the channel
	unsigned long channel_program_count;
	unsigned long channel_erase_count;

	struct chip_info *chip_head;        
};


struct chip_info{
	int current_state;                  //chip has serveral states, including idle, command/address transfer,data transfer,unknown
	int next_state;            
	__int64 current_time;               //Record the current time of the chip
	__int64 next_state_predict_time;    //the predict time of next state, used to decide the sate at the moment

	struct die_info *die_head;
};


struct die_info{
	struct plane_info *plane_head;
};

struct plane_info{
	struct blk_info *blk_head;
};


struct blk_info{
	unsigned int invalid_page_num;     //Record the number of invaild pages in the block
	int last_write_page;               //Records the number of pages executed by the last write operation, and -1 indicates that no page has been written
	struct page_info *page_head; 
};

struct page_info{
	unsigned int fing;
	struct LPN_ENTRY *lpn_entry;
};

struct super_block_info{
	int ec; //min ec
	int blk_cnt; //superblock size
	struct local *pos;
	int next_wr_page; // next available page
	int pg_off; //used page offset in the superpage
	unsigned int gcing;   //GC ing
};


struct dram_info{
	unsigned int dram_capacity;
	__int64 current_time;

	struct dram_parameter *dram_paramters;     

	struct map_info *map;   //mapping for user data
	unsigned int data_buffer_capacity;
	unsigned int read_data_buffer_capacity;

	struct buffer_info *data_buffer;  //data buffer 
	struct buffer_info *read_data_buffer; // read data buffer
	struct buffer_info *data_command_buffer; //data commond buffer
};

/*****************************************************************************************************************************************
*Buff strategy:Blocking buff strategy
*1--first check the buffer is full, if dissatisfied, check whether the current request to put down the data, if so, put the current request, 
*if not, then block the buffer;
*
*2--If buffer is blocked, select the replacement of the two ends of the page. If the two full page, then issued together to lift the buffer 
*block; if a partial page 1 full page or 2 partial page, then issued a pre-read request, waiting for the completion of full page and then issued 
*And then release the buffer block.
********************************************************************************************************************************************/
typedef struct buffer_group{
	TREE_NODE node;                     //The structure of the tree node must be placed at the top of the user-defined structure
	struct buffer_group *LRU_link_next;	// next node in LRU list
	struct buffer_group *LRU_link_pre;	// previous node in LRU list

	unsigned int group;                 //the first data logic sector number of a group stored in buffer 
	unsigned int stored;

	unsigned int dirty; // indicate whether the mapping node (page) is dirty or not 
}data_buf_node;


struct dram_parameter{
	float active_current;
	float sleep_current;
	float voltage;
	int clock_time;
};

struct map_info{
	struct LPN2PPN *L2P_entry;
	char *in_nvram;
	struct FING2PPN *F2P_entry;
};

struct controller_info{
	unsigned int frequency;             //Indicates the operating frequency of the controller
	__int64 clock_time;                 //Indicates the time of a clock cycle
	float power;                        //Indicates the energy consumption per unit time of the controller
};

struct request{
	__int64 time;                      //Request to reach the time(us)
	unsigned int lsn;                  //The starting address of the request, the logical address
	unsigned int size;                 //The size of the request, the number of sectors
	unsigned int operation;            //The type of request, 1 for the read, 0 for the write
	unsigned int cmplt_flag;		   //Whether the request is executed, 0 means no execution, 1 means it has been executed
	unsigned int fing;                 //fing

	__int64 begin_time;
	__int64 response_time;

	struct sub_request *subs;          //Record all sub-requests belonging to the request
	struct request *next_node;        
};


struct sub_request{
	unsigned int lpn;                  //The logical page number of the sub request for read reqeusts
	unsigned int ppn;                  //The physical page number of the request
	unsigned int operation;            //Indicates the type of the sub request, except that read 1 write 0, there are erase, two plane and other operations
	unsigned int state;              //The requested sector status bit
	int size;

	unsigned int current_state;        //Indicates the status of the subquery
	unsigned int next_state;
	__int64 next_state_predict_time;

	__int64 current_time;
	__int64 begin_time;               //Sub request start time
	__int64 complete_time;            //Record the processing time of the sub-request, the time that the data is actually written or read out

	struct local *location;           //In the static allocation and mixed allocation mode, it is known that lpn knows that the lpn is assigned to that channel, chip, die, plane, which is used to store the calculated address
	struct sub_request *next_subs;    //Points to the child request that belongs to the same request
	struct sub_request *next_node;    //Points to the next sub-request structure in the same channel

	struct request *total_request;
	unsigned int mutliplane_flag;
	unsigned int oneshot_flag;
	unsigned int oneshot_mutliplane_flag;

	//suspend
	unsigned int suspend_req_flag;
	struct sub_request *next_suspend_sub;
};


struct parameter_value{
	unsigned int chip_num;          //the number of chip in ssd
	unsigned int data_dram_capacity;  //Record the DRAM capacity for data buffer in SSD
	unsigned int read_dram_capacity;  //Record the DRAM capacity for read buffer in SSD
	unsigned int mapping_dram_capacity; //Record the DRAM capacity for mapping cache in SSD
	unsigned int cpu_sdram;         //sdram capacity in cpu

	unsigned int channel_number;    //Record the number of channels in the SSD, each channel is a separate bus
	unsigned int chip_channel[100]; //Set the number of channels in the SSD and the number of particles on each channel

	unsigned int die_chip;    
	unsigned int plane_die;
	unsigned int block_plane;
	unsigned int page_block;
	unsigned int subpage_page;

	unsigned int page_capacity;
	unsigned int subpage_capacity;
	unsigned int mapping_entry_size;


	unsigned int ers_limit;         //Record the number of erasable blocks per block
	int address_mapping;            //Record the type of mapping,1��page��2��block��3��fast
	int wear_leveling;              //WL algorithm 
	int gc;                         //Record gc strategy
	float overprovide;

	double operating_current;       //NAND FLASH operating current(uA)
	double supply_voltage;	
	double dram_active_current;     //cpu sdram work current   uA
	double dram_standby_current;    //cpu sdram work current   uA
	double dram_refresh_current;    //cpu sdram work current   uA
	double dram_voltage;            //cpu sdram work voltage  V

	int buffer_management;          //indicates that there are buffer management or not
	int scheduling_algorithm;       //Record which scheduling algorithm to use��1:FCFS
	float quick_radio;
	int related_mapping;

	unsigned int time_step;
	unsigned int small_large_write; //the threshould of large write, large write do not occupt buffer, which is written back to flash directly

	int striping;                   //Indicates whether or not striping is used, 0--unused, 1--used
	int interleaving;
	int pipelining;
	int threshold_fixed_adjust;
	int threshold_value;
	int active_write;               //Indicates whether an active write operation is performed,1,yes;0,no
	float gc_hard_threshold;        //Hard trigger gc threshold size
	float gc_soft_threshold;        //Soft trigger gc threshold size
	int allocation_scheme;          //Record the choice of allocation mode, 0 for dynamic allocation, 1 for static allocation
	int static_allocation;          //The record is the kind of static allocation
	int dynamic_allocation;			 //The priority of the dynamic allocation 0--channel>chip>die>plane,1--plane>channel>chip>die
	int advanced_commands;  
	int ad_priority;                //record the priority between two plane operation and interleave operation
	int greed_MPW_ad;               //0 don't use multi-plane write advanced commands greedily; 1 use multi-plane write advanced commands greedily
	int aged;                       //1 indicates that the SSD needs to be aged, 0 means that the SSD needs to be kept non-aged
	float aged_ratio; 
	int queue_length;               //Request the length of the queue
	int warm_flash;
	int flash_mode;                 //0--slc mode,1--tlc mode

	struct ac_time_characteristics time_characteristics;
};

/********************************************************
*mapping information,The highest bit of state indicates 
*whether there is an additional mapping relationship
*********************************************************/
struct LPN2PPN{
	unsigned int pn;                //Physical number, either a physical page number, a physical subpage number, or a physical block number
};

struct FING2PPN{
	unsigned int pn;
};

struct LPN_ENTRY{
	unsigned int lpn;
	struct LPN_ENTRY *next;
};

struct NVRAM_OOB_SEG{
	__int64 next_avail_time;
	int alloc_seg;
	int free_entry;
	int invalid_entry;
};

struct local{          
	unsigned int channel;
	unsigned int chip;
	unsigned int die;
	unsigned int plane;
	unsigned int block;
	unsigned int page;
};

struct ssd_info *initiation(struct ssd_info *);
struct parameter_value *load_parameters(char parameter_file[30]);
struct page_info * initialize_page(struct page_info * p_page);
struct blk_info * initialize_block(struct blk_info * p_block,struct parameter_value *parameter);
struct plane_info * initialize_plane(struct plane_info * p_plane,struct parameter_value *parameter );
struct die_info * initialize_die(struct die_info * p_die,struct parameter_value *parameter,long long current_time );
struct chip_info * initialize_chip(struct chip_info * p_chip,struct parameter_value *parameter,long long current_time );
struct ssd_info * initialize_channels(struct ssd_info * ssd );
struct dram_info * initialize_dram(struct ssd_info * ssd);
void initialize_statistic(struct ssd_info * ssd);
void intialize_sb(struct ssd_info * ssd);
int Get_Channel(struct ssd_info * ssd, int i);
int Get_Chip(struct ssd_info * ssd, int i);
int Get_Die(struct ssd_info * ssd, int i);
int Get_Plane(struct ssd_info * ssd, int i);