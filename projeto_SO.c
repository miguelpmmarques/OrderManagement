/*
* 	Welcome to Order and Delivery Management Project
*      			 Operating Systems
*
*	By: Paulo Cardoso 2017249716 and Miguel Marques 2017266263
*
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <semaphore.h>
#include <fcntl.h>
#include "drone_movement.h"

#define MAX_DRONES 100
#define MAX_PRODUCT 3
#define MAX_TIME 30
#define MAX_NAME 300
#define MAX_LOG_CHAR 600
#define CONFIGFILE "config.txt"
#define DEBUG
#define LOGFILE "project_output.log"
#define INPUT_PIPE "input_pipe"
#define MAX_ORDERS 200
#define STATS "ESTATISTICAS"
#define SHARED_MEM "MEMORIA_PARTILHADA"
#define WRITE_LOG "ESCRITA_LOG"
#define CTRL_C "CONTROL"

#define CASHIERS_NUM 2

/********* STRUCTS ********/

typedef struct Warehouse *Warehouse_ptr;
typedef struct Order *Order_list;
typedef struct Product *Product_list;

typedef struct Product
{
	char name[MAX_NAME];
	int quantity;
}Product;

typedef struct Warehouse
{
	char name[MAX_NAME];
	double x,y;
	int n_products;
	Product prod[MAX_PRODUCT];
}Warehouse;


typedef struct Order
{
	long order_no;
	char name[MAX_NAME];
	double x,y;
	Product prod;  //Each order has one and only product
	Order_list next; //SE for deturpar o enunciado crio mais uma estrutura ou um vector, no worries-- Paulo 
	time_t s, e;
}Order;

typedef struct Statistics
{
	int total_pack_indrones;
	int total_pack_inwarehouses;
	int total_pack_sent;
	int total_products_sent;
	double total_delivery_time; //in order to calculate average delivery time
}Statistics;


typedef struct Drone
{
	double xact,yact;
	double xdest,ydest;
	int state;
	int drone_id;
	Order encomenda;
}Drone;


typedef struct
{
	long destino;
	long drone_id;
	Product prod;
}Stock;
/********** GLOBAL VARIABLES ***********/

int block=0;
double limite_x;
double limite_y;
int nr_drones;
int freq_abstecimento;
int quanti;
double ut;
int id_ware;
int nr_p=0;
int number_warehouses;
Warehouse * ware;
int id_crl_c_triggered;
int * crl_c_triggered;
pthread_t drones[MAX_DRONES];
Product *prod;
int id_stat;
Statistics* statis;
pthread_cond_t cond=PTHREAD_COND_INITIALIZER;
pthread_mutex_t mut1=PTHREAD_MUTEX_INITIALIZER;
int nr_encomendas_pendentes;
pid_t simulation_manager;
pid_t central;
sem_t *mutex_log_file;
sem_t *mutex_statistic;
sem_t *mutex_ware_info;
sem_t *ctrl_c;
int fd_named_pipe;
int exit_thread = 0;
int numero_total_produtos = 0;
int mqid;
int call_tread_by_id = -1;
Drone drone_struct[MAX_DRONES];
Order_list queued_orders_list=NULL;
sigset_t block_sigs;
time_t t;
int ** pipes_wares;
fd_set read_ware_set;
//echo "ORDER Req_1 prod: Prod_A, 5 to: 300, 100" > input_pipe

/*********** Functions *************/
void change_drones_number(int n,Drone *drone_struct);
void movimenta_drones(int id);
void log_file_write(char* words);
void signal_sigint();
int read_config();
void *drone(void *n);
void processo_central();
void warehouse_process(int n_wo);
void project_output_log();
void creat_shm_statistics();
void write_shm_statistics_terminal();
void drone_delivery(int id);
void choose_closest_drone(double **warehouse_disponiveis,Drone *drone_struct,Order *encomenda,int* return_drone_and_ware);
void handl_sigs();
Order_list insere_encomenda_queued_list(Order * encomenda);
Order_list apaga_queued_order_list();
void ship_out(Order * ptr_enc, int ship_flag, char aux[],int i);
void movimenta_drones_para_base(int id);
void imprime_lista(Order_list lista);
void destroy_everything(int n);

int main() /*SIMULATION MANAGER*/
{
	if(signal(SIGINT,SIG_IGN)==SIG_ERR)
	{
		printf("Error: Signal failed!\n");
		exit(EXIT_FAILURE);
	}
	if(signal(SIGUSR1,SIG_IGN)==SIG_ERR)
	{
		printf("Error: Signal failed!\n");
		exit(EXIT_FAILURE);
	}
	printf("[%d]IM THE SIMULATION MANAGER\n",getpid());
	int n_w = read_config();
	if (n_w == -1)
	{
		printf("Invalid file!\n");
		exit(0);
	}
	else if (n_w == -2)
	{
		printf("Invalid file!\n");
		if(shmdt(ware)==-1)
			destroy_everything(1);
		if(shmctl(id_ware,IPC_RMID,NULL)==-1)
			destroy_everything(1);
		exit(0);
	}
	handl_sigs();
	if(sem_unlink(CTRL_C) == EACCES)
		destroy_everything(5);
	if(sem_unlink(STATS) == EACCES)
		destroy_everything(5);
	if(sem_unlink(WRITE_LOG) == EACCES)
		destroy_everything(5);
	if(sem_unlink(SHARED_MEM) == EACCES)
		destroy_everything(5);
	if(unlink(INPUT_PIPE) == EACCES)
		destroy_everything(7);
	ctrl_c = sem_open(CTRL_C, O_CREAT | O_EXCL, 0777, 1);
	id_crl_c_triggered = shmget(IPC_PRIVATE,sizeof(int),IPC_CREAT|0777);
	if(id_crl_c_triggered <0)
		destroy_everything(1);
	crl_c_triggered = (int*)shmat(id_crl_c_triggered,NULL,0);
	if((crl_c_triggered == (int*)- 1))
		destroy_everything(1);
	crl_c_triggered[0] = 1;

	if ((mutex_statistic = sem_open(STATS, O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED) 
   		destroy_everything(5);
   	if ((mutex_log_file = sem_open(WRITE_LOG, O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED) 
   		destroy_everything(5);
   	if ((mutex_ware_info = sem_open(SHARED_MEM, O_CREAT | O_EXCL, 0777, 1)) == SEM_FAILED) 
		destroy_everything(5);
   	
	simulation_manager = getpid();
	project_output_log();
	int pid;
	log_file_write("Order and Delivery Management Project has started");
	#ifdef DEBUG
	printf("config file has been read\n");
	#endif

	pipes_wares = (int **)malloc(number_warehouses * sizeof(int*));
	for (int i = 0; i < number_warehouses; ++i)
	{
		pipes_wares[i] = (int *)malloc(2 * sizeof(int));
		pipe(pipes_wares[i]);
	}
	mqid = msgget(IPC_PRIVATE, IPC_CREAT|0777);
  	if (mqid < 0)
    	destroy_everything(3);
    #ifdef DEBUG
	printf("Message Queue created\n");
	#endif
	creat_shm_statistics();
	#ifdef DEBUG
	printf("Shared for statistics memory created\n");
	#endif
	for(int i=0;i<n_w;i++)
	{
		pid = fork();
		if (pid < 0)
			destroy_everything(2);
		else if(pid == 0)
		{
			warehouse_process(i);
			exit(0);
		}
	}
	pid = fork();
	if (pid < 0)
		destroy_everything(2);
	else if(pid == 0)
	{
		processo_central();
		exit(0);
	}
	if(signal(SIGUSR1,write_shm_statistics_terminal)==SIG_ERR)
		destroy_everything(4);
	if(signal(SIGINT,signal_sigint)==SIG_ERR)
		destroy_everything(4);
	 /*ESTA INSTRUCAO REDIRECIONA O SINAL SIGUSR1 PARA IMPRIMIR AS ESTATISTICAS*/
	//sleep(10);
	//destroy_everything(1);
	int i=0;
	char aux[MAX_LOG_CHAR];
	while(1)
	{
		/*O DRONE MANAGER GERE APENAS, A PARTIR DESTE MOMENTO O STOCK DOS ARMAZENS*/
		Stock ostock;
		/*SABENDO QUE OS ARMAZENS TAMBEM VAO RECEBER MENSAGENS DOS DRONES
		ESTE ostock.drone_id = -1 VAI FAZER COM QUE OS ARMAZENS CONSIGAM RECONHECER
		QUE A MENSAGEM VEM DO SIMULATION MANAGER*/
		ostock.drone_id = -1;
		for (int i = 1; i < number_warehouses+1; ++i)
		{
			/*FAZ O SLEEP INDICADO NO CONFIG E METE NA ESTRUTURA O QUE VAI ENVIAR PARA OS ARAMZENS
			AKA OS PRODUTOS E A QUANTIDADE, SENDO ESTA ULTIMA FIXA*/
			for (int i = 0; i < freq_abstecimento*100; ++i)
			{
				usleep(ut*10000);
			}
			ostock.destino = i;
			ostock.prod.quantity = quanti;
			strcpy(ostock.prod.name,prod[rand()%numero_total_produtos].name);
			/*ENVIA MENSAGEM PARA OS ARMAZEN*/
			if(msgsnd(mqid,&ostock,sizeof(ostock)-sizeof(long),0) == -1)
				destroy_everything(4);
		}
	}
	return 0;
}
void destroy_everything(int n)
{
	switch(n)
	{
		case 1:
			printf("Error in shared memory!\n");
			break;
		case 2:
			printf("Error creating processes\n");
			break;
		case 3:
			printf("Error in message queue\n");
			break;
		case 4:
			printf("Error in signal\n");
			break;
		case 5:
			printf("Error in semaphore\n");
			break;
		case 6:
			printf("Error in threads\n");
			break;
		case 7:
			printf("Error in Pipe\n");
			break;
		case 8:
			printf("Error in log file\n");
			break;
	}
	sem_unlink(CTRL_C);
	sem_unlink(STATS);
	sem_unlink(WRITE_LOG);
	sem_unlink(SHARED_MEM);
	unlink(INPUT_PIPE);
	sem_close(ctrl_c);
	sem_close(mutex_statistic);
	sem_close(mutex_ware_info);
	sem_close(mutex_log_file);
	close(fd_named_pipe);
	for (int i = 0; i < number_warehouses; ++i)
	{
		close(pipes_wares[i][0]);
		close(pipes_wares[i][1]);
	}
	shmdt(crl_c_triggered);
	shmctl(id_crl_c_triggered,IPC_RMID,NULL);
	shmdt(ware);
	shmctl(id_ware,IPC_RMID,NULL);
	shmdt(statis);
	shmctl(id_stat,IPC_RMID,NULL);
	msgctl(mqid,IPC_RMID,NULL);
	printf("Execute kill_ipcs.sh to clean all ipcs");
	system("killall -9 project_exe");
	exit(1);
}
void imprime_lista(Order_list orders_lista)
{
	Order_list lista = orders_lista;
	while(lista)
		lista=lista->next;
}
void ship_out(Order * ptr_enc, int ship_flag, char aux[], int i)
{
	imprime_lista(queued_orders_list);
	int return_drone_and_ware[2];
	/*UMA VEZ QUE VAMOS VERIFICAR SE HA PRODUTOS NOS ARMAZENS PRECISAMOS DE UM
	SEMAFORO PARA TAL*/

	/*CRIAMOS ESTA MATRIZ N_ARMAZENS_DISPOVEIS POR 2 EM QUE NOS ESPAÇOS VAI TER AS COORDENADAS
	DA SUA POSICAO, CASO NAO TENHA PRODUTO FICA A -1
	*/
	double **warehouse_disponiveis;
	warehouse_disponiveis = (double**)malloc(sizeof(double*)*number_warehouses);
	for (int i = 0; i < number_warehouses; ++i)
	{
		warehouse_disponiveis[i] = (double*)malloc(sizeof(double)*2);
		warehouse_disponiveis[i][0] = -1;
		warehouse_disponiveis[i][1] = -1;
	}

			
	/*	VEJAMOS O SEGUINTE EXEMPLO, TEMOS 3 ARMAZENS, O 1,2 E 3 COM COORDENADAS 100,200
	500,400 E 1000,100 RESPETIVAMENTE, IMAGINEMOS QUE VEM UMA ENCOMENDA COM UM PRODUTO 
	QUE SO HA NO ARMAZEM 1 E 3, NO FINAL DO FOR VAMOS TER A SEGINTE MATRIZ
			___       ____
			|		     |
			|  100   200 |
			|   -1   -1  |
			|  1000  100 |
			|__       ___|

	*/

	if(sem_wait(mutex_ware_info)==-1)
		destroy_everything(5);
	for (int i = 0; i < number_warehouses; ++i)
	{
		for (int j = 0; j < ware[i].n_products; ++j)
		{
			if (strcmp(ware[i].prod[j].name,ptr_enc->prod.name)==0)
			{
				/*DEPOIS DE VERIFICAR SE O ARMAZEM TEM ESSE PRODUT
				VERIFICA A SUA QUANTIDADE*/
				if (ware[i].prod[j].quantity >= ptr_enc->prod.quantity)
				{
					warehouse_disponiveis[i][0] = ware[i].x;
					warehouse_disponiveis[i][1] = ware[i].y;
					break;
				}
			}
		}
	}
	if(sem_post(mutex_ware_info)==-1)
		destroy_everything(5);

	/*ESTE ARRAY O QUE FAZER E, NO return_drone_and_ware[0] VAI INDICAR O DRONE MAIS PERTO
	E NO return_drone_and_ware[1] VAI INDICAR O ARMAZEM ESCOLHIDO, SE O VALOR DESTES FOR -1 
	SIGNIFICA QUE NAO HA DRONES DISPONIVEIS E QUE NAO HA ARMAZENS COM STOCK SUFICIENTE
	RESPETIVAMENTE

	POR EXEMPLO, SE O DRONE 15 ESTA LIVRE MAS NAO HA ARMAZENS COM STOCK SUFICIENTE OBTEMOS
	O SEGUINTE OUTOUT DA FUNCAO choose_closest_drone():
			___     ____
			|		   |
			| 15   -1  |
			|__     ___|
	
	Imaginemos que o armazem 2 tem stock sufiente mas os drones estao todos ocupados,
	obtemos este output da funcao choose_closest_drone():
			
			___     ____
			|		   |
			|  -1   -1 |
			|__     ___|
	


	*/
	return_drone_and_ware[0] = -1;
	return_drone_and_ware[1] = -1;
	choose_closest_drone(warehouse_disponiveis,drone_struct,ptr_enc,return_drone_and_ware);
	
	if (return_drone_and_ware[0] != -1 && return_drone_and_ware[1] != -1)
	{
		/*CASO HAJAM DRONES E ARMAZENS DISPONIVEIS, ENTRA NESTE IF E FAZ
		A ENCOMENDA EM SI*/
		sprintf(aux,"Encomenda %s-%ld enviada ao drone %d",ptr_enc->name,ptr_enc->order_no-number_warehouses,return_drone_and_ware[0]);
		log_file_write(aux);
		if(sem_wait(mutex_ware_info)==-1)
			destroy_everything(5);
		for (int i = 0; i < ware[return_drone_and_ware[1]-1].n_products; ++i)
		{
			/*A RESERVA DO STOCK E FEITA AQUI, AKA E RETIRADA DO ARMAZEM IMEDIATAMENTE*/
			if (strcmp(ware[return_drone_and_ware[1]-1].prod[i].name,ptr_enc->prod.name)==0)
			{
				ware[return_drone_and_ware[1]].prod[i].quantity -= ptr_enc->prod.quantity;
				break;
			}
		}
		if(sem_post(mutex_ware_info)==-1)
			destroy_everything(5);
		/*DE MANEIRA A SER POSSIVEL A COMUNICACAO ENTRE A CENTRAL E OS DRONES FOI
		CRIADA UM ARRAZ DE ENCOMENDAS DO TAMANHO DO NUMERO DE DRONES, EM QUE OS DRONES
		PODEM ACEDER, POIS A ENCOMENDA DE CADA UM DELES ESTÁ NO INDEX DO SEU ID*/
		drone_struct[return_drone_and_ware[0]].encomenda = *ptr_enc;
		/*AQUI MUDAMOS O ESTADO DO DRONE PARA OCUPADO E MUDA O SEU DESTINO PARA
		O ARMAZEM*/
		pthread_mutex_lock(&mut1);
		drone_struct[return_drone_and_ware[0]].state = 0;
		drone_struct[return_drone_and_ware[0]].xdest = ware[return_drone_and_ware[1]].x;
		drone_struct[return_drone_and_ware[0]].ydest = ware[return_drone_and_ware[1]].y;
		pthread_mutex_unlock(&mut1);
		/*MUDAMOS A VARIAVEL DE CONDICAO*/
		if(ship_flag==1){
			Order_list ant;
			ant = queued_orders_list;
			sprintf(aux,"Encomenda %s-%ld em queue a ser processada pela central",ptr_enc->name,ptr_enc->order_no-number_warehouses);
			log_file_write(aux);
			queued_orders_list=queued_orders_list->next;
			free(ant);
		}
		if(sem_wait(mutex_statistic)==-1) 
			destroy_everything(5);
		statis->total_pack_indrones++;
		if(sem_post(mutex_statistic)==-1) 
			destroy_everything(5);
		/*AVISAMOS OS DRONES TODOS PARA VERIFICAR A CONDICAO*/
		pthread_mutex_lock(&mut1);
		call_tread_by_id = return_drone_and_ware[0];
		if(pthread_cond_broadcast(&cond)!=0)
			destroy_everything(5);
		pthread_mutex_unlock(&mut1);
		usleep(100);
		#ifdef DEBUG
		printf("O DRONE ESCOLHIDO FOI O DRONE [%d] para o ware [%d]\n", return_drone_and_ware[0],return_drone_and_ware[1]+1);
		#endif
	}
	else
	{
		if (return_drone_and_ware[1] == -1)
		{
			if (ship_flag == 0)
			{
				sprintf(aux,"Encomenda %s-%ld em fila de espera por falta de stock",ptr_enc->name,ptr_enc->order_no-number_warehouses);
				log_file_write(aux);
			}
			
		}
		else if (return_drone_and_ware[0] == -1)
		{
			if (ship_flag == 0)
			{
				#ifdef DEBUG
				printf("Encomenda nao processada por falta de drones\n");	
				#endif
				block++;
			}
		}
		if(ship_flag==0)
		{
			queued_orders_list=insere_encomenda_queued_list(ptr_enc);
		}
	}
}
Order_list insere_encomenda_queued_list(Order_list pont_encomenda)
{
	Order_list ant,act,no;
	no = (Order_list)malloc(sizeof(Order));
	no->order_no= pont_encomenda->order_no;
	strcpy(no->name,pont_encomenda->name);
	no->x=pont_encomenda->x;
	no->y=pont_encomenda->y;
	no->prod=pont_encomenda->prod;
	no->s = pont_encomenda->s;
	act=queued_orders_list;
	if(act==NULL){
		act=no;
		act->next=NULL;
		#ifdef DEBUG
		printf("ORDER %s queued!\n", no->name);
		#endif
		return act;
	}
	while(act->next){
		ant=act;
		act=act->next;
	}
	ant=act;
	act=no;
	ant->next=act;
	act->next=NULL;
	return queued_orders_list;
}
Order_list apaga_queued_order_list()
{
	Order_list temp_ptr,act;
	act=queued_orders_list;
	while(act){
		temp_ptr=act;
		act=act->next;
		free(temp_ptr);
	}
	return NULL;
}
void warehouse_process(int n_wo)
{
	int ware_number = n_wo+1;
	char put_log[MAX_LOG_CHAR],c;
	sprintf(put_log,"[%d] Hi! Im warehouse number %d and im ready to work!",getpid(),ware_number);
	log_file_write(put_log);
	Stock s;
	while(1)
	{
		/*OS ARMAZENS VAO ESTAR CONSTANTEMTE A ESPERA DE RECEBER MENSAGENS POR PARTE TANTO 
		POR PARTE DO SIMULATION MANAGER ASSIM COMO POR PARTE DOS DRONES*/
		if(msgrcv(mqid,&s,sizeof(s)-sizeof(long),ware_number,0) == -1)
			destroy_everything(3);
		if(s.drone_id == -2)
		{
			signal_sigint();
		}
		else if(s.drone_id == -1)
		{
			/*SE A MENSAGEM VIER DO SIMULATION MANAGEr*/
			if(sem_wait(mutex_statistic)==-1) 
				destroy_everything(5);
			statis->total_pack_inwarehouses+=quanti;
			if(sem_post(mutex_statistic)==-1) 
				destroy_everything(5);
			if(sem_wait(mutex_ware_info)==-1)
				destroy_everything(5);
			sprintf(put_log,"%s recebeu novo stock",ware[n_wo].name);
			log_file_write(put_log);
			for (int i = 0; i < ware[n_wo].n_products; ++i)
			{
				if (strcmp(ware[n_wo].prod[i].name,s.prod.name)==0)
				{
					ware[n_wo].prod[i].quantity += s.prod.quantity;
					break;
				}
			}
			write(pipes_wares[n_wo][1],&n_wo,sizeof(int));
			if(sem_post(mutex_ware_info)==-1)
				destroy_everything(5);
		}
		else
		{
			/*SE A MENSAGEM VIER DOS DRONES*/
			#ifdef DEBUG
			printf("\t\t\tMENSAGEM RECEBIDA PELO ARMAZEM -> %d\n",getpid());
			#endif
			usleep(s.prod.quantity*ut*1000000);
			s.destino = s.drone_id;

			if(msgsnd(mqid,&s,sizeof(s)-sizeof(long),0) == -1)
				destroy_everything(3);
		}
	}
}
void signal_sigint()
{
	/*FALTA CORRIGIR ESTA FUNCAO*/
	int check;
	char choice;
	exit_thread = 1;
	int t;
	if (getpid() == simulation_manager)
	{
		if((fd_named_pipe = open(INPUT_PIPE,O_RDWR)) < 0)
			destroy_everything(7);
	    if(sem_wait(ctrl_c)==-1) 
			destroy_everything(5);
		crl_c_triggered[0] = 0;
		if(sem_post(ctrl_c)==-1) 
			destroy_everything(5);
		char buf[MAX_NAME];
		sprintf(buf,"DRONE_FREE ");
		write(fd_named_pipe,buf,sizeof(buf));
		while(wait(NULL) != -1);
		//write_shm_statistics_terminal();
		log_file_write("Order and Delivery Management Project has ended");
		if(sem_unlink(CTRL_C) == -1)
			destroy_everything(5);
		if(sem_unlink(STATS) == -1)
			destroy_everything(5);
		if(sem_unlink(WRITE_LOG) == -1)
			destroy_everything(5);
		if(sem_unlink(SHARED_MEM) == -1)
			destroy_everything(5);
		if(unlink(INPUT_PIPE) == -1)
			destroy_everything(7);
		if(sem_close(ctrl_c)==-1)
			destroy_everything(5);
		if(sem_close(mutex_statistic)==-1)
			destroy_everything(5);
		if(sem_close(mutex_ware_info)==-1)
			destroy_everything(5);
		if(sem_close(mutex_log_file)==-1)
			destroy_everything(5);
		if(close(fd_named_pipe)==-1)
			destroy_everything(7);
		for (int i = 0; i < number_warehouses; ++i)
		{
			close(pipes_wares[i][0]);
			close(pipes_wares[i][1]);
		}
		if(shmdt(crl_c_triggered)==-1)
			destroy_everything(1);
		if(shmctl(id_crl_c_triggered,IPC_RMID,NULL)==-1)
			destroy_everything(1);
		if(shmdt(ware)==-1)
			destroy_everything(1);
		if(shmctl(id_ware,IPC_RMID,NULL)==-1)
			destroy_everything(1);
		if(shmdt(statis)==-1)
			destroy_everything(1);
		if(shmctl(id_stat,IPC_RMID,NULL)==-1)
			destroy_everything(1);
		if(msgctl(mqid,IPC_RMID,NULL) == -1)
			destroy_everything(3);
		#ifdef DEBUG
		printf("Shared mem free from Warehouses\n");
		#endif
		printf("MANAGER[%d] Ended\n",getpid());
		exit(0);
	}
	else if(getpid() == central)
	{
		int c=0;
		exit_thread = 1;
		for (t = 0; t < MAX_DRONES; ++t)
		{
			if (drones[t] != -1)
			{
				if(pthread_mutex_lock(&mut1)!=0)
					destroy_everything(5);
				call_tread_by_id = t;
				if(pthread_cond_broadcast(&cond)!=0)
					destroy_everything(5);
				if(pthread_mutex_unlock(&mut1)!=0)
					destroy_everything(5);
				if(pthread_join(drones[t], NULL)!=0)
					destroy_everything(6);
				#ifdef DEBUG
				printf("\t\t[%d] Im a drone and Im leaving\n", t);
				#endif
				drones[t]=-1;
				usleep(20);
				nr_drones--;	
				c++;
			}
		}
		if(queued_orders_list)
			queued_orders_list = apaga_queued_order_list();
		if(pthread_mutex_destroy(&mut1)==-1)
			destroy_everything(5);
		if(pthread_cond_destroy(&cond)!=0)
			destroy_everything(5);
		#ifdef DEBUG
		printf("All %d threads Ended\n",c);
		printf("Central[%d] Ended\n",getpid());
		#endif
		exit(0);
	}
	else
	{
		char buff[MAX_NAME];
		sprintf(buff,"Warehouses[%d] Ended",getpid());
		log_file_write(buff);
		exit(0);
	}
	exit(0);
}
void creat_shm_statistics()
{
	id_stat = shmget(IPC_PRIVATE,sizeof(Statistics),IPC_CREAT|0777);
	if(id_stat <0)
		destroy_everything(1);
	statis = (Statistics*)shmat(id_stat,NULL,0);
	if(statis==(Statistics*)-1)
		destroy_everything(1);
	statis->total_pack_indrones=0;
	statis->total_pack_inwarehouses=0;
	statis->total_pack_sent=0;
	statis->total_products_sent=0;
	statis->total_delivery_time=0.0;
}
void write_shm_statistics_terminal()
{
	if(sem_wait(mutex_statistic)==-1) 
		destroy_everything(5);
	char buff[MAX_NAME];
	printf("\n\t\t\t[Statistics]\n\n");
	printf("\tNúmero total de encomendas atribuídas a drones -> %d\n",statis->total_pack_indrones);
	printf("\tNúmero total de produtos carregados de armazéns -> %d\n",statis->total_pack_inwarehouses);
	printf("\tNúmero total de encomendas entregues -> %d\n",statis->total_pack_sent);
	printf("\tNúmero total de produtos entregues -> %d\n",statis->total_products_sent);
	if(statis->total_delivery_time != 0)
		printf("\tTempo médio para conclusão de uma encomenda -> %.2lf\n\n",(double)(statis->total_delivery_time)/statis->total_pack_sent);
	else
		printf("\tAinda sem encomendas distribuidas, logo nao ha tempo medio\n\n");
	if(sem_post(mutex_statistic)==-1) 
		destroy_everything(5);
}
void movimenta_drones(int id)
{
	/*ESTA FUNÇAO FAZ A DESLOCAÇAO DOS DRONES PARA UM CERTO DESTINO*/
	int actx,result;
	while(result=move_towards(&(drone_struct[id].xact),&(drone_struct[id].yact),(drone_struct[id].xdest),(drone_struct[id].ydest))==1)
	{
		#ifdef DEBUG
		printf("IM DRONE %d AND IM HERE [%.2f-%.2f] HEADING [%.2f-%.2f]\n",id,(drone_struct[id].xact),(drone_struct[id].yact),(drone_struct[id].xdest),(drone_struct[id].ydest));
		#endif
		usleep(ut*1000000);
	}
	if(result==-1)
		printf("Already in warehouse.\n");
	if(result==-2)
		printf("Error: Couldn't move drone. Out of bounds!");
}
void *drone(void *n)
{
	Drone drone_car = *((Drone *)n);
	while(1)
	{
		if(pthread_mutex_lock(&mut1)!=0)
			destroy_everything(5);
		/*A CONDICAO VAI SER FEITA PELO ID DO DRONE DE MANEIRA A QUE A CENTAL CONSIGA
		CHAMAR APENAS UM DRONE, TER ISTO EM ATENCAO QUANDO FORMOS TRATAR DO SIGINT*/
		while(call_tread_by_id!=drone_car.drone_id )
		{
			if (pthread_cond_wait(&cond,&mut1)!=0)
		    	destroy_everything(5);
		}
		call_tread_by_id = -1;
		if(pthread_mutex_unlock(&mut1)!=0)
			destroy_everything(5);
		if (exit_thread == 0)
			drone_delivery(drone_car.drone_id);
		else
		{
			pthread_exit(NULL);
		}
	usleep(10);
	}
}
void processo_central()
{
	int rrx=0,rry=0,number_of_orders=number_warehouses;
	char read_pipe[MAX_NAME],aux[MAX_LOG_CHAR];
	central = getpid();
	#ifdef DEBUG
	printf("[%d] Central Process created\n",getpid());
	#endif
	nr_encomendas_pendentes = 0;
	for (int i = 0; i <  MAX_DRONES; ++i)
	{
		drones[i] = -1;
	}
	if((fd_named_pipe=(mkfifo(INPUT_PIPE,O_CREAT|0600)<0)) && errno!=EEXIST)
		destroy_everything(7);
	if((fd_named_pipe = open(INPUT_PIPE,O_RDWR)) < 0)
		destroy_everything(7);
	for (int i = 0; i < nr_drones; ++i)
	{
		drone_struct[i].drone_id=i;
		drone_struct[i].state = 1;
		drone_struct[i].xdest=-1;
		drone_struct[i].ydest=-1;
		drone_struct[i].xact=(rrx % 2)*limite_x;
		drone_struct[i].yact=(rry % 2)*limite_y;
		#ifdef DEBUG
		printf("\t\t\t[%d] -> {%.1f-%.1f}\n", i,drone_struct[i].xact,drone_struct[i].yact);
		#endif
		if(rrx% 2 == 1 && rry% 2 == 1)
			rrx++;
		else if(rrx% 2 == 1)
			rry++;
		else if(rry% 2 == 1)
			rry++;
		else 
			rrx++;
		if (pthread_create(&drones[i], NULL, drone,&drone_struct[i])!=0)
			destroy_everything(6);
		usleep(20);
	}
	#ifdef DEBUG
	printf("All drones created\n");
	#endif
	while (1)
	{
		/*AQUI A CENTRAL COMECA A FAZER O SEU TRABALHO EM SI, QUE E FAZER O MANAGEMENT
		DAS ENTREGAS, E MUDAR O NUMERO DE DRONES*/

		/*ESTE ARRAY O QUE FAZER E, NO return_drone_and_ware[0] VAI INDICAR O DRONE MAIS PERTO
		E NO return_drone_and_ware[1] VAI INDICAR O ARMAZEM ESCOLHIDO, SE O VALOR DESTES FOR -1 
		SIGNIFICA QUE NAO HA DRONES DISPONIVEIS E QUE NAO HA ARMAZENS COM STOCK SUFICIENTE
		RESPETIVAMENTE*/
		
		/*LEITURA DO NAMED PIPE*/

		int n,i,k,alerta_ware;
		Order_list ptr_order;
		
		FD_ZERO(&read_ware_set);
		for(i=0;i<number_warehouses;i++)
		{
			FD_SET(pipes_wares[i][0],&read_ware_set);
		}
		FD_SET(fd_named_pipe,&read_ware_set);
		if(select(fd_named_pipe +1, &read_ware_set, NULL, NULL, NULL) > 0)
		{
			for (int i = 0; i < number_warehouses; ++i)
			{
				if(FD_ISSET(pipes_wares[i][0],&read_ware_set))
				{
					read(pipes_wares[i][0],&alerta_ware,sizeof(int));
					int ship_flag= 1;
					int j = 0;//verificar este valor!!! - Paulo 
					ptr_order=queued_orders_list;
					while(ptr_order)
					{
						ship_out(ptr_order,ship_flag,aux,j);	
						ptr_order=ptr_order->next;
					}
					
				}
			}
			
			if(FD_ISSET(fd_named_pipe,&read_ware_set))
			{
				n =read(fd_named_pipe,&read_pipe,sizeof(read_pipe));
				read_pipe[n-1] = '\0';
				int flag = 1;
				#ifdef DEBUG
				printf("READ_PIPE: [ %s ]\n",read_pipe);
				#endif
				char* partition;
				char copy[MAX_LOG_CHAR];
				strcpy(copy,read_pipe);	
				partition = strtok(read_pipe, " ");
				if (strcmp(partition,"DRONE_FREE") == 0)
				{
					if(sem_wait(ctrl_c)==-1) 
						destroy_everything(5);
					if (crl_c_triggered[0] == 0)
					{
						if(sem_post(ctrl_c)==-1) 
							destroy_everything(5);
						int conta=0;
						for (t = 0; t < MAX_DRONES; ++t)
						{
							if (drones[t] != -1)
							{
								if(pthread_mutex_lock(&mut1)!=0)
									destroy_everything(5);
								if(drone_struct[t].state == 1 && ((drone_struct[t].xact == 0 || drone_struct[t].xact == limite_x) && (drone_struct[t].yact == 0 || drone_struct[t].yact == limite_y)))
									conta++;
								if(pthread_mutex_unlock(&mut1)!=0)
									destroy_everything(5);
							}
						}
						if (conta == nr_drones)
						{
							Stock ostock;
							ostock.drone_id = -2;
							for (int i = 1; i < number_warehouses+1; ++i)
							{
								ostock.destino = (long)i;
								if(msgsnd(mqid,&ostock,sizeof(ostock)-sizeof(long),0) == -1)
									destroy_everything(3);
							}
							signal_sigint();
						}
					}
					else
					{
						if(sem_post(ctrl_c)==-1) 
							destroy_everything(5);
						if(block>0)
						{
							int ship_flag=1;
							int j=0;
							ptr_order=queued_orders_list;
							ship_out(ptr_order,ship_flag,aux,0);	
							block--;
						}
					}
				}
				else if (strcmp(partition,"ORDER") == 0)
				{
					Order encomenda;
				
					/* FAZ A VERIFICACAO SE A ENCOMEMDA ESTA BEM ESCRITA, ASSIM COMO SE O PRODUTO ESPECIFICADO
					EXISTE NO SISTEMA*/
					if(sem_wait(ctrl_c)==-1) 
						destroy_everything(5);
					if (crl_c_triggered[0] == 0)
					{
						if(sem_post(ctrl_c)==-1) 
							destroy_everything(5);
						log_file_write(copy);
					}
					else
					{
						if(sem_post(ctrl_c)==-1) 
							destroy_everything(5);
						flag = 1;
						if(sscanf(copy,"ORDER %s prod: %[^,], %d to: %lf, %lf",encomenda.name,encomenda.prod.name,&encomenda.prod.quantity,&encomenda.x,&encomenda.y )!=5)
							flag = 0;
						int contador =0;
						for (int j = 0; j < numero_total_produtos; ++j)
						{
							if(strcmp(prod[j].name,encomenda.prod.name)==0)
								contador++;
						}
						if (contador == 0)
						{
							flag = 0;
						}
						if (encomenda.prod.quantity <1)
							flag = 0;
						if (encomenda.x<0 || encomenda.x>limite_x)
							flag = 0;
						if (encomenda.y<0 || encomenda.y>limite_y)
							flag = 0;
						if (flag == 1)
						{
							/*A ENCOMENDA GANHA EM PRIMEIRO LUGAR UM IDENTIFICADOR UNICO QUE MAIS TARDE
							VAI DAR JEITO PARA SERVIR DE IDENTIFICADOR NA MESSAGE QUEUE*/
							number_of_orders++;
							encomenda.order_no = number_of_orders;
							encomenda.s = time(0);
							encomenda.next=NULL;
							sprintf(aux,"Encomenda %s-%ld recebida pela central",encomenda.name,encomenda.order_no-number_warehouses);
							log_file_write(aux);
							int ship_flag=0;
							ship_out(&encomenda,ship_flag,aux,i);
						}
						else
						{
							strcat(copy," <- COMMAND ERROR");
							log_file_write(copy);
							printf("Comando PIPE invalido, siga a estrutura indicada!\n");
						}

					}
				}
				else if (strcmp(partition,"DRONE") == 0)
				{
					if(sem_wait(ctrl_c)==-1) 
						destroy_everything(5);
					if (crl_c_triggered[0] == 0)
					{
						if(sem_post(ctrl_c)==-1) 
							destroy_everything(5);
						log_file_write(copy);
					}
					else
					{
						if(sem_post(ctrl_c)==-1) 
							destroy_everything(5);
						char aux[MAX_LOG_CHAR];
						partition = strtok(NULL, " ");
						if (strcmp(partition,"SET") == 0)
						{
							int prev = nr_drones;
							partition = strtok(NULL, " ");
							int drone_n = atoi(partition);
							/* VERIFICA SE O NUEMRO DE DRONES ESTA DENTRO DOS LIMITES */
							if (drone_n>0 || drone_n < MAX_DRONES)
							{
								change_drones_number(drone_n,drone_struct);
								if (prev < drone_n)
								{
									if(block>0)
									{
										int ship_flag=1;
										int j=0;
										ptr_order=queued_orders_list;
										block--;
										ship_out(ptr_order,ship_flag,aux,0);	
									}
								}
							}
							else
								printf("Comando PIPE invalido, siga a estrutura indicada!\n");
						}
						else
							printf("Comando PIPE invalido, siga a estrutura indicada!\n");
					}
				}
				else
				{
					strcat(copy," <- COMMAND ERROR");
					log_file_write(copy);
					printf("Comando PIPE invalido, siga a estrutura indicada!\n");
				}
			}
		}
	}
}
void drone_delivery(int id)
{
	Order encomenda;
	long which_ware;
	Stock ostock;
	do{
		/* A PRIMEIRA COISA QUE O DRONE FAZ E IR BUSCAR A TUA ENCOMENDA*/
		encomenda = drone_struct[id].encomenda;
		if(sem_wait(mutex_ware_info)==-1)
			destroy_everything(5);
		for (int i = 0; i < number_warehouses; ++i)
		{
			if (ware[i].x == drone_struct[id].xdest && ware[i].y == drone_struct[id].ydest)
			{
				/* FAZ-SE A PROCURA DO ARMAZEM COM BASE NA POSICAO DESTINO DO DRONE
				POIS A CENTRAL ANTES DE O NOTIFICAR, ALTEROU A SUA ESTRUTURA NO QUE TOCA
				A POSICAO DESTINO PARA A DO ARMAZEM ESCOLHIDO*/
				which_ware = i+1;
				break;
			}
		}

		if(sem_post(mutex_ware_info)==-1)
			destroy_everything(5);
		#ifdef DEBUG
		printf("HI IM DRONE [%d] AND IM READY TO DO A DELIVERY\n",drone_struct[id].drone_id);
		#endif
		movimenta_drones(id);
		#ifdef DEBUG
		printf("IM ORDER -> %s that must be deliver to [%.2f-%.2f]\n",encomenda.name,encomenda.x,encomenda.y);
		printf("CHEGOU AO ARMAZEM\n");
		#endif

		/*CONSTRUCAO DA ESTRUTURA QUE VAI SER ENVIADA PELA MESSAGE QUEUE*/
		ostock.drone_id = encomenda.order_no;
		ostock.destino = (long)which_ware;
		ostock.prod.quantity = encomenda.prod.quantity;
		strcpy(ostock.prod.name,encomenda.prod.name);
		if(msgsnd(mqid,&ostock,sizeof(ostock)-sizeof(long),0) == -1)
			destroy_everything(3);
		if(msgrcv(mqid,&ostock,sizeof(ostock)-sizeof(long),ostock.drone_id,0) == -1)
			destroy_everything(3);
		printf("\t\t ORDER -> {%.2f-%.2f}\n",encomenda.x,encomenda.y );
		/*COMUNICACAO COM O ARMAZEM FEITA*/
		drone_struct[id].xdest = encomenda.x;
		drone_struct[id].ydest = encomenda.y;
		movimenta_drones(id);
		usleep(ut*1000000);
		encomenda.e = time(0);
		char buff[MAX_LOG_CHAR];
		sprintf(buff,"Encomenda %s-%ld entregue ao destino",encomenda.name,encomenda.order_no-number_warehouses );
		log_file_write(buff);
		if(sem_wait(mutex_statistic)==-1) 
			destroy_everything(5);
		statis->total_delivery_time += (double)(encomenda.e - encomenda.s);
		statis->total_products_sent += encomenda.prod.quantity;
		statis->total_pack_sent++;
		if(sem_post(mutex_statistic)==-1) 
			destroy_everything(5);
		double min_distance=-1;
		double act_distance;
		/*ESCOLHA DA BASE DE REPOUSO MAIS PROXIMA*/
		for (int i = 0; i < 2; ++i)
		{
			for (int j = 0; j < 2; ++j)
			{
				act_distance =  distance(drone_struct[id].xact,drone_struct[id].yact,limite_x*i,limite_y*j);
				if (min_distance == -1)
				{
					min_distance = act_distance;
					drone_struct[id].xdest = limite_x*i;
					drone_struct[id].ydest = limite_y*j;
				}
				else
				{
					if (min_distance > act_distance)
					{
						min_distance = act_distance;
						drone_struct[id].xdest = limite_x*i;
						drone_struct[id].ydest = limite_y*j;
					}
				}
			}
		}
		#ifdef DEBUG
		printf("\t\tA BASE ESCOLHIDA ESTA EM %.2f -- %.2f \n",drone_struct[id].xdest,drone_struct[id].ydest);
		#endif
		drone_struct[id].state = 1;
		char buf[MAX_NAME];
		sprintf(buf,"DRONE_FREE ");
		write(fd_named_pipe,buf,sizeof(buf));
		movimenta_drones_para_base(id);
		if(sem_wait(ctrl_c)==-1) 
			destroy_everything(5);
		if(crl_c_triggered[0] == 0)
			write(fd_named_pipe,buf,sizeof(buf));
		if(sem_post(ctrl_c)==-1) 
			destroy_everything(5);
	}while(drone_struct[id].state == 0);
	#ifdef DEBUG
	printf("CHEGOU À BASE!\n");
	#endif
}
void movimenta_drones_para_base(int id)
{
	/*ESTA FUNÇAO FAZ A DESLOCAÇAO DOS DRONES PARA UM CERTO DESTINO*/
	int actx,result=1,tempo=0;
	while(result==1)
	{
		if(pthread_mutex_lock(&mut1)!=0)
			destroy_everything(5);
		result = move_towards(&(drone_struct[id].xact),&(drone_struct[id].yact),(drone_struct[id].xdest),(drone_struct[id].ydest));
		if(drone_struct[id].state == 0)
		{
			call_tread_by_id = -1;
			if(pthread_mutex_unlock(&mut1)!=0)
				destroy_everything(5);
			break;
		}
		if(pthread_mutex_unlock(&mut1)!=0)
			destroy_everything(5);
		#ifdef DEBUG
		printf("IM DRONE %d AND IM HERE [%.2f-%.2f] HEADING [%.2f-%.2f]\n",id,(drone_struct[id].xact),(drone_struct[id].yact),(drone_struct[id].xdest),(drone_struct[id].ydest));
		#endif
		usleep(ut*1000000);
	}
	if(result==0)
	{
		drone_struct->xdest=-1;
		drone_struct->ydest=-1;
	}
	if(result==-1)
	{
		#ifdef DEBUG
		printf("Already in warehouse.\n");
		#endif
		drone_struct->xdest=-1;
		drone_struct->ydest=-1;
	}
	if(result==-2)
	{
		#ifdef DEBUG
		printf("Error: Couldn't move drone. Out of bounds!");
		#endif
		drone_struct->xdest=-1;
		drone_struct->ydest=-1;
	}
}
void choose_closest_drone(double **warehouse_disponiveis,Drone *drone_struct,Order *encomenda,int* return_drone_and_ware)
{
	double rtl,rtd,total;
	double dist_min = -1;
	int conta_w=0,conta_d=0;
	for (int i = 0; i < number_warehouses; ++i)
	{
		if (warehouse_disponiveis[i][0] != -1)
		{
			conta_w++;
			for (int j = 0; j < MAX_DRONES; ++j)
			{
				if (drones[j] != -1 && drone_struct[j].state == 1)
				{
					/*ESCOLHA DO DRONE E DO ARMAZEM, NOTA QUE PARA CHEGAR A ESTE
					IF JA FOI VERIFICADO QUE O DRONE NAO ESTA OCUPADO E QUE O
					ARMAZEM TEM STOCK SUFIENTE*/
					conta_d++;
					pthread_mutex_lock(&mut1);
					rtl = distance(warehouse_disponiveis[i][0],warehouse_disponiveis[i][1],drone_struct[j].xact,drone_struct[j].yact);
					pthread_mutex_unlock(&mut1);
					rtd = distance(warehouse_disponiveis[i][0],warehouse_disponiveis[i][1],encomenda->x,encomenda->y);
					total = rtl+rtd;
	
					if ( dist_min == -1)
					{
						dist_min = total;
						return_drone_and_ware[0] = j;
						return_drone_and_ware[1] = i;
					}
					if ( dist_min > total)
					{
						dist_min = total;
						return_drone_and_ware[0] = j;
						return_drone_and_ware[1] = i;
					}
				}
			}
		}
	}
	if (conta_w == 0)
	{
		/*QUANDO NAO HA ARMAZENS DISPOVEIS*/
		return_drone_and_ware[1] = -1;
	}
	else if (conta_d ==0)
	{
		/*QUANDO NAO HA DRONES DISPOVEIS*/
		return_drone_and_ware[1] = -2;
		return_drone_and_ware[0] = -1;
	}
}
void change_drones_number(int n,Drone *drone_struct)
{
	int rrx=0;
	int rry=0;
	if (n > nr_drones)
	{
		/*CRIACAO DE NOVOS DRONES*/
		for (int i = 0; i<MAX_DRONES && n != nr_drones; ++i)
		{
			if (drones[i]==-1)
			{
				drone_struct[i].drone_id=i;
				drone_struct[i].state=1;
				drone_struct[i].xdest=-1;
				drone_struct[i].ydest=-1;
				drone_struct[i].xact=(rrx% 2)*limite_x;
				drone_struct[i].yact=(rry% 2)*limite_y;
				if(rrx% 2 == 1 && rry% 2 == 1)
					rrx++;
				else if(rrx% 2 == 1)
					rry++;
				else if(rry% 2 == 1)
					rry++;
				else 
					rrx++;
				if (pthread_create(&drones[i], NULL, drone,&drone_struct[i])!=0)
					destroy_everything(6);
				#ifdef DEBUG
				printf("\t\t\t[%d] -> {%.1f-%.1f}\n", i,drone_struct[i].xact,drone_struct[i].yact);
				#endif
				usleep(20);
				nr_drones++;
			}
		}	
	}
	else if (n < nr_drones)
	{
		/*DESTRUICAO DE DRONES*/
		int i=0;
		exit_thread = 1;
		for (int i = 0; i<MAX_DRONES && n != nr_drones; ++i)	
		{

			if (drones[i] != -1)
			{
				if(drone_struct[i].state == 1 && ((drone_struct[i].xact == 0 || drone_struct[i].xact == limite_x) && (drone_struct[i].yact == 0 || drone_struct[i].yact == limite_y)))
				{
					if(pthread_mutex_lock(&mut1)!=0)
						destroy_everything(5);
					call_tread_by_id = i;
					if(pthread_cond_broadcast(&cond)!=0)
						destroy_everything(5);
					if(pthread_mutex_unlock(&mut1)!=0)
						destroy_everything(5);
					if(pthread_join(drones[i], NULL)!=0)
						destroy_everything(6);
					#ifdef DEBUG
					printf("\t\t[%d] Im a drone and Im leaving\n", i);
					#endif
					drones[i]=-1;
					usleep(20);
					nr_drones--;	
				}
			}
		}
		if (n != nr_drones)
		{
			#ifdef DEBUG
			printf("Nao foi possivel alterar o numero de drones na totalidade\n");
			#endif
		}
	exit_thread = 0;
	}
	char aux[MAX_LOG_CHAR];
	sprintf(aux,"Numero de Drones alterado para %d",nr_drones);
	log_file_write(aux);
}
void log_file_write(char* words)
{
    if(sem_wait(mutex_log_file)==-1)
		destroy_everything(5);
	FILE *log;
	char acttime[MAX_TIME];
	struct tm *get_time;
	time_t moment = time(0);
    get_time = gmtime (&moment);
	log = fopen(LOGFILE,"a");
	if(log==NULL)
		destroy_everything(8);
    strftime (acttime, sizeof(acttime), "%H:%M:%S ", get_time);
	fprintf(log, "%s%s\n",acttime,words);
	fclose(log);
	printf("%s\n", words);
	if(sem_post(mutex_log_file)==-1)
		destroy_everything(5);
}
void project_output_log()
{
	FILE *log;
	log = fopen(LOGFILE,"w");
	if(log==NULL)
		destroy_everything(8);
	fclose(log);
}
int read_config()
{
	FILE *fp;
	char aux[MAX_NAME];
	char c;
	int loops,conta=0;
	int beginIndex =0;
	int endIndex = -1; 
	int quant_aux;
	char checknewline;
	char *partition;
	Product_list new;
	Product_list fremem;
	fp = fopen(CONFIGFILE,"r");
	if(fp == NULL)
	{
		perror("Erro a ler ficheiro");
		exit(1);
	}
	if(fscanf(fp,"%lf,%lf%c",&limite_x,&limite_y,&c)!= 3)
	{
		return -1;
	}
	if(limite_x<0)
		return -1;
	if(limite_y<0)
		return -1;
	if (c != '\n')
		return -1;
	fscanf(fp,"%[^\n]",aux);
	// CRIA LISTA DE PRODUTOS
	partition = strtok(aux,", ");
	prod = (Product*)malloc(sizeof(Product));
	while(partition != NULL)
	{
		prod = (Product*)realloc(prod,sizeof(Product)*(nr_p+1));
		strcpy(prod[nr_p].name, partition);
		partition = strtok(NULL,", ");
		nr_p++;
	}
	numero_total_produtos = nr_p;
	if(fscanf(fp,"%d",&nr_drones)!=1)
		return -1;
	if (nr_drones>MAX_DRONES || nr_drones<0)
		return -1;
	if(fscanf(fp,"%d,%d,%lf",&freq_abstecimento,&quanti,&ut)!=3)
		return -1;
	fgetc(fp);
	if(fgetc(fp)!='\n')
	{
		return -1;
	}
	if (fscanf(fp,"%d%c",&loops,&c)!= 2)
		return -1;
	if(c != '\n')
		return -1;
	if(loops < 1)
		return -1;
	number_warehouses = loops;

	id_ware = shmget(IPC_PRIVATE,sizeof(Warehouse)*loops,IPC_CREAT|0777);
	if(id_ware <0)
		destroy_everything(1);
	ware = (Warehouse*)shmat(id_ware,NULL,0);
	if((ware == (Warehouse*)- 1))
		destroy_everything(1);

	int index;
	for (int i = 0; i < loops; ++i)
	{
		fscanf(fp,"%[^ ]s",ware[i].name);
		fscanf(fp,"%s:",aux);
		if (strcmp(aux,"x,y:")!=0)
			return -2;
		if (fscanf(fp,"%lf, %lf ",&ware[i].x,&ware[i].y)!= 2)
			return -2;
		if (ware[i].x < 0 || ware[i].x > limite_x || ware[i].y < 0 ||  ware[i].y > limite_y)
			return -2;
		fscanf(fp,"%s:",aux);
		if (strcmp(aux,"prod:")!=0)
			return -2;
		fscanf(fp,"%[^\n]",aux);
		fscanf(fp,"%c",&c);
		int check=0;

		partition = strtok(aux,", ");
	
		while(partition != NULL)
		{
			for (int j = 0; j < nr_p; ++j)
			{

				if(strcmp(prod[j].name,partition)==0)
				{
					strcpy(ware[i].prod[check].name,partition);
					partition = strtok(NULL,", ");
					ware[i].prod[check].quantity = atoi(partition);
					check++;
				}
			}
			if(check>3)
			{
				printf("[ERRO] O numero de produtos no armazens e superior que 3\n");
				return -2;
			}
			partition = strtok(NULL,", ");
		}
		ware[i].n_products = check;
	}
	fclose(fp);
	#ifdef DEBUG
	printf("Shared memory for Warehouses created\n");
	#endif
	return loops;
}
void handl_sigs()
{
	sigemptyset (&block_sigs);
	sigaddset (&block_sigs, SIGHUP);
	sigaddset (&block_sigs, SIGQUIT);
	sigaddset (&block_sigs, SIGILL);
	sigaddset (&block_sigs, SIGTRAP);
	sigaddset (&block_sigs, SIGABRT);
	sigaddset (&block_sigs, SIGFPE);
	sigaddset (&block_sigs, SIGBUS);
	sigaddset (&block_sigs, SIGSEGV);
	sigaddset (&block_sigs, SIGSYS);
	sigaddset (&block_sigs, SIGPIPE);
	sigaddset (&block_sigs, SIGALRM);
	sigaddset (&block_sigs, SIGURG);
	sigaddset (&block_sigs, SIGTSTP);
	sigaddset (&block_sigs, SIGCONT);
	sigaddset (&block_sigs, SIGCHLD);
	sigaddset (&block_sigs, SIGTTIN);
	sigaddset (&block_sigs, SIGTTOU);
	sigaddset (&block_sigs, SIGIO);
	sigaddset (&block_sigs, SIGXCPU);
	sigaddset (&block_sigs, SIGXFSZ);
	sigaddset (&block_sigs, SIGVTALRM);
	sigaddset (&block_sigs, SIGPROF);
	sigaddset (&block_sigs, SIGWINCH);
	sigaddset (&block_sigs, SIGUSR2);
	sigprocmask(SIG_BLOCK,&block_sigs, NULL);
}